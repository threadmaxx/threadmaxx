// threadmaxx_navmesh_bake — minimal CLI driver around `bakeNavMesh`.
//
// Reads a simple text-format triangle soup from stdin (or `--in PATH`)
// and writes a v2 baked navmesh blob to stdout (or `--out PATH`).
//
// Input format — one directive per line:
//   v X Y Z            — append vertex (X, Y, Z)
//   t A B C [TAG]      — append triangle (A, B, C) with optional area
//                        tag (default 0). Vertex ids are 0-based and
//                        index the vertex pool above.
//   name STRING        — set the asset name in the blob header
//   tile ID            — set the single-tile id stamped on the output
//   # comment          — blank lines and '#' lines are ignored
//
// On bake error the diagnostic + error code go to stderr and the
// process exits non-zero. On success the blob bytes are written
// untouched (binary-safe; the caller is expected to redirect stdout to
// a file or pass `--out`).

#include "threadmaxx_navmesh/bake.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void printUsage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s [--in PATH] [--out PATH] [--name NAME] [--tile ID]\n"
        "  reads a triangle-soup text format from stdin (or --in PATH)\n"
        "  writes a v2 navmesh blob to stdout (or --out PATH)\n"
        "  input format:\n"
        "    v X Y Z          # vertex\n"
        "    t A B C [TAG]    # triangle (vertex ids, optional area tag)\n"
        "    name STRING      # asset name\n"
        "    tile ID          # tile id stamped on the output\n"
        "    # comment        # ignored\n",
        argv0);
}

bool parseInput(std::istream& is,
                threadmaxx::navmesh::BakeInput& out,
                std::vector<threadmaxx::Vec3>& verts,
                std::vector<threadmaxx::navmesh::BakeInputTriangle>& tris,
                std::string& err) {
    std::string line;
    std::size_t lineNo = 0;
    while (std::getline(is, line)) {
        ++lineNo;
        std::istringstream iss(line);
        std::string tok;
        if (!(iss >> tok)) continue;        // blank
        if (tok.empty() || tok[0] == '#') continue;
        if (tok == "v") {
            float x = 0, y = 0, z = 0;
            if (!(iss >> x >> y >> z)) {
                err = "line " + std::to_string(lineNo) +
                      ": malformed 'v' directive";
                return false;
            }
            verts.push_back(threadmaxx::Vec3{x, y, z});
        } else if (tok == "t") {
            std::uint32_t a = 0, b = 0, c = 0;
            std::uint32_t tag = 0;
            if (!(iss >> a >> b >> c)) {
                err = "line " + std::to_string(lineNo) +
                      ": malformed 't' directive";
                return false;
            }
            iss >> tag;  // optional
            tris.push_back(threadmaxx::navmesh::BakeInputTriangle{
                a, b, c, static_cast<std::uint16_t>(tag)});
        } else if (tok == "name") {
            std::string rest;
            std::getline(iss, rest);
            // strip leading whitespace
            std::size_t pos = 0;
            while (pos < rest.size() &&
                   (rest[pos] == ' ' || rest[pos] == '\t')) ++pos;
            out.name = rest.substr(pos);
        } else if (tok == "tile") {
            std::uint32_t id = 0;
            if (!(iss >> id)) {
                err = "line " + std::to_string(lineNo) +
                      ": malformed 'tile' directive";
                return false;
            }
            out.tileId = id;
        } else {
            err = "line " + std::to_string(lineNo) +
                  ": unknown directive '" + tok + "'";
            return false;
        }
    }
    out.vertices = verts;
    out.triangles = tris;
    return true;
}

const char* bakeErrorName(threadmaxx::navmesh::BakeError e) noexcept {
    using threadmaxx::navmesh::BakeError;
    switch (e) {
        case BakeError::None: return "None";
        case BakeError::EmptyInput: return "EmptyInput";
        case BakeError::InvalidIndex: return "InvalidIndex";
        case BakeError::DegenerateTriangle: return "DegenerateTriangle";
        case BakeError::NonManifoldEdge: return "NonManifoldEdge";
        case BakeError::TooManyPolygons: return "TooManyPolygons";
    }
    return "?";
}

} // namespace

int main(int argc, char** argv) {
    std::string inPath;
    std::string outPath;
    std::string nameOverride;
    bool haveNameOverride = false;
    std::uint32_t tileOverride = 0;
    bool haveTileOverride = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto needArg = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing argument for %s\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--in") inPath = needArg("--in");
        else if (a == "--out") outPath = needArg("--out");
        else if (a == "--name") {
            nameOverride = needArg("--name");
            haveNameOverride = true;
        } else if (a == "--tile") {
            tileOverride =
                static_cast<std::uint32_t>(std::strtoul(needArg("--tile"),
                                                       nullptr, 10));
            haveTileOverride = true;
        } else if (a == "-h" || a == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown flag: %s\n", a.c_str());
            printUsage(argv[0]);
            return 2;
        }
    }

    threadmaxx::navmesh::BakeInput input;
    std::vector<threadmaxx::Vec3> verts;
    std::vector<threadmaxx::navmesh::BakeInputTriangle> tris;
    std::string parseErr;

    if (inPath.empty()) {
        if (!parseInput(std::cin, input, verts, tris, parseErr)) {
            std::fprintf(stderr, "parse error: %s\n", parseErr.c_str());
            return 2;
        }
    } else {
        std::ifstream f(inPath);
        if (!f) {
            std::fprintf(stderr, "cannot open --in %s: %s\n",
                         inPath.c_str(), std::strerror(errno));
            return 2;
        }
        if (!parseInput(f, input, verts, tris, parseErr)) {
            std::fprintf(stderr, "parse error in %s: %s\n",
                         inPath.c_str(), parseErr.c_str());
            return 2;
        }
    }
    if (haveNameOverride) input.name = nameOverride;
    if (haveTileOverride) input.tileId = tileOverride;

    auto baked = threadmaxx::navmesh::bakeNavMesh(input);
    if (baked.error != threadmaxx::navmesh::BakeError::None) {
        std::fprintf(stderr, "bake failed: %s — %s\n",
                     bakeErrorName(baked.error),
                     baked.diagnostic.c_str());
        return 1;
    }

    if (outPath.empty()) {
        std::cout.write(reinterpret_cast<const char*>(baked.blob.data()),
                        static_cast<std::streamsize>(baked.blob.size()));
        if (!std::cout) {
            std::fprintf(stderr, "stdout write failed\n");
            return 1;
        }
    } else {
        std::ofstream f(outPath, std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "cannot open --out %s: %s\n",
                         outPath.c_str(), std::strerror(errno));
            return 1;
        }
        f.write(reinterpret_cast<const char*>(baked.blob.data()),
                static_cast<std::streamsize>(baked.blob.size()));
        if (!f) {
            std::fprintf(stderr, "write to %s failed\n", outPath.c_str());
            return 1;
        }
    }

    std::fprintf(stderr,
                 "baked %zu vertices, %zu polygons → %zu bytes\n",
                 verts.size(), tris.size(), baked.blob.size());
    return 0;
}
