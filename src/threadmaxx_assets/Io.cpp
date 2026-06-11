#include "threadmaxx_assets/detail/io.hpp"

#include <fstream>
#include <ios>
#include <string>

namespace threadmaxx::assets::detail {

AssetResult<std::vector<std::byte>> readFile(std::string_view path) {
    std::ifstream in(std::string(path), std::ios::binary | std::ios::ate);
    if (!in) {
        return AssetResult<std::vector<std::byte>>::failure(
            ErrorCode::FileNotFound, std::string(path));
    }

    const std::streamsize size = in.tellg();
    if (size < 0) {
        return AssetResult<std::vector<std::byte>>::failure(
            ErrorCode::IoError, std::string(path));
    }

    in.seekg(0, std::ios::beg);

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(bytes.data()), size);
        if (in.gcount() != size) {
            return AssetResult<std::vector<std::byte>>::failure(
                ErrorCode::Truncated, std::string(path));
        }
    }

    return AssetResult<std::vector<std::byte>>::success(std::move(bytes));
}

ErrorCode readFileInto(std::string_view path, std::vector<std::byte>& out) {
    std::ifstream in(std::string(path), std::ios::binary | std::ios::ate);
    if (!in) {
        out.clear();
        return ErrorCode::FileNotFound;
    }

    const std::streamsize size = in.tellg();
    if (size < 0) {
        out.clear();
        return ErrorCode::IoError;
    }

    in.seekg(0, std::ios::beg);

    out.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(out.data()), size);
        if (in.gcount() != size) {
            out.clear();
            return ErrorCode::Truncated;
        }
    }

    return ErrorCode::Ok;
}

} // namespace threadmaxx::assets::detail
