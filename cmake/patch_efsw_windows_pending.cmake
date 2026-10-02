if(NOT DEFINED PATCH_ROOT)
  message(FATAL_ERROR "efsw patch: PATCH_ROOT is required")
endif()

# efsw 1.7.2 retires cancelled IOCP requests. A failed synchronous rearm has
# no completion to drain, so only outstanding requests may enter that set.
# Keep this patch tied to the pinned version and fail on source drift.
file(STRINGS "${PATCH_ROOT}/CMakeLists.txt" version_line REGEX "^project\\(efsw VERSION 1\\.7\\.2\\)$")
if(NOT version_line)
  message(FATAL_ERROR "efsw pending-completion patch requires the pinned 1.7.2 source")
endif()
find_package(Git REQUIRED)
set(patch_file "${CMAKE_CURRENT_LIST_DIR}/patches/efsw-windows-pending.patch")
execute_process(
  COMMAND "${GIT_EXECUTABLE}" apply --check "${patch_file}"
  WORKING_DIRECTORY "${PATCH_ROOT}"
  RESULT_VARIABLE check_result
  ERROR_VARIABLE check_error
)
if(check_result EQUAL 0)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply "${patch_file}"
    WORKING_DIRECTORY "${PATCH_ROOT}"
    RESULT_VARIABLE apply_result
    ERROR_VARIABLE apply_error
  )
  if(NOT apply_result EQUAL 0)
    message(FATAL_ERROR "efsw pending-completion patch failed: ${apply_error}")
  endif()
  message(STATUS "efsw: applied Windows pending-completion lifetime patch")
else()
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${patch_file}"
    WORKING_DIRECTORY "${PATCH_ROOT}"
    RESULT_VARIABLE reverse_result
    ERROR_QUIET
  )
  if(NOT reverse_result EQUAL 0)
    message(FATAL_ERROR "efsw pending-completion patch does not match pinned source: ${check_error}")
  endif()
  message(STATUS "efsw: Windows pending-completion lifetime patch already applied")
endif()
