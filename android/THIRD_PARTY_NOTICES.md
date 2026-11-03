# Image filter notices

`xbrz/xbrz.cpp`, `xbrz/xbrz.h`, `xbrz/xbrz_tools.h`, and `xbrz/xbrz_config.h` are xBRZ by Zenju, copied with their original notices intact. The license text and special linking exceptions are in `xbrz/COPYING`. Original project: https://sourceforge.net/projects/xbrz/ . These sources were reused from the local Babel Recomp FlipPad port; no changes were made to those four files. The adapter selects xBRZ’s buffered color-distance path.

`filter_engine.cpp`, `filter_engine.h`, `filters_jni.cpp`, and `src/local/sfc/tall/VideoEffects.java` are the image filter integration and are provided under GPL-3.0-or-later. See `xbrz/COPYING` for the license text. The filter is built as a separate generic bitmap library, `libtallfilters.so`, without links to libretro or emulator state APIs.

The Snes9x core and other dependencies retain all their existing license notices. This notice does not relicense them. Full modified source, filter sources and build scripts are included in the source archive.

# Full-map presentation references

The read-only full-map compositor reuses the locally authored FlipPad presentation code from [Babel Recomp](https://github.com/kingo132/babel-recomp), with its PolyForm Noncommercial license and required notices retained in the APK notices. The game-specific WRAM layout and sprite formats were independently checked against [RustyBlazer](https://github.com/hellow554/RustyBlazer) (GPL-3.0) and [Crysta](https://github.com/lambadalambda/crysta) (MIT, © 2026 Lain Soykaf and Terranigma project contributors). No ROM, game graphics, translated text, disassembled game code or save file is included in the source repository. Crysta's MIT notice is retained as a precaution for the source-derived format descriptions.

The generic Tall renderer and Android host originated in the user's separate SFC Tall prototype, based on libretro/snes9x `fae2fea08f74180759ef540ee94259213f503480`. Original notices remain intact. The standalone applications add their own authenticated game profiles, UI, icons and build variants.
