# Host-native DXSO corpus harness: batch-compiles real D3D9 shader bytecode
# through the in-tree DXSO -> SPIR-V compiler with no GPU or Vulkan loader.
# Built-in fixtures run everywhere (`dxso-corpus --selftest`); real corpora
# (.sdp packages / DXVK_SHADER_DUMP_PATH dumps) are local-only (game data).
# Links libdxvk_d3d9.dylib like the d3d9-clear example (the DXSO compiler,
# fixed-function emitters, and Logger all live inside the dylib) — run it
# with the build's src/d3d9 on DYLD_LIBRARY_PATH (CI smoke paths already do).
# See tests/dxso/dxso_corpus.cpp and docs/MACOS_TESTING.md.
executable('dxso-corpus',
  'dxso/dxso_corpus.cpp',
  dependencies        : [ d3d9_dep ],
  include_directories : [ dxvk_include_path ],
  install             : true,
)
