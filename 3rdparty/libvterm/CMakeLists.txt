# Offline, source-pinned dependency; local bounds patches are already applied.
add_library(cilogg_vterm STATIC
  src/cilogg_limits.c src/encoding.c src/keyboard.c src/mouse.c src/parser.c
  src/pen.c src/screen.c src/state.c src/unicode.c src/vterm.c)
target_include_directories(cilogg_vterm PUBLIC include PRIVATE src)
set_target_properties(cilogg_vterm PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES
  POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_definitions(cilogg_vterm PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
