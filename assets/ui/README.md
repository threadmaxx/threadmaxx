# tou2d UI assets

## `font.ttf` — default UI font

DejaVu Sans Mono, bundled as the v1 default per M6.0. Permissive
licence (Bitstream Vera derivative); see `LICENSE.dejavu` in this
directory for the full text. **You must include `LICENSE.dejavu` if
you redistribute the font file as-is.**

## Drop-in replacement

The TTF path is loaded at runtime by `tou2d::ui::bakeFont()`. To swap
the default font:

1. Replace `font.ttf` with any TTF/OTF that covers the ASCII range
   `0x20..0x7E`.
2. Rebuild (the asset is copied into the build tree by CMake).
3. Re-run the binary. No source changes required.

For non-default codepoint ranges (non-Latin scripts, extended Latin,
HUD glyph extras), pass the desired ranges via
`FontConfig::codepoints` when calling `bakeFont()`. The font file you
ship must include those glyphs.

## Add a new licence file if you swap the font

If you replace `font.ttf` with a different font, ship the matching
licence file (e.g. `LICENSE.jetbrains` for JetBrains Mono) and remove
or keep `LICENSE.dejavu` as appropriate. The build does not validate
this for you — licence hygiene is a release-engineering checklist
item.
