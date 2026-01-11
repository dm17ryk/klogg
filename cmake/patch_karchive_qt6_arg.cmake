if(NOT DEFINED PATCH_ROOT)
  set(PATCH_ROOT "${CMAKE_SOURCE_DIR}")
endif()

function(patch_arg_mode path)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "karchive patch: missing ${path}")
  endif()

  file(READ "${path}" content)
  set(updated "${content}")
  string(REPLACE "arg(mode))" "arg(static_cast<int>(mode)))" updated "${updated}")
  string(REPLACE "arg(d->mode))" "arg(static_cast<int>(d->mode)))" updated "${updated}")

  if(NOT updated STREQUAL content)
    file(WRITE "${path}" "${updated}")
  endif()
endfunction()

patch_arg_mode("${PATCH_ROOT}/karchive/src/karchive.cpp")
patch_arg_mode("${PATCH_ROOT}/karchive/src/kar.cpp")
patch_arg_mode("${PATCH_ROOT}/karchive/src/krcc.cpp")
