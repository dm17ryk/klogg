if(NOT DEFINED PATCH_ROOT)
  set(PATCH_ROOT "${CMAKE_SOURCE_DIR}")
endif()

set(patch_file "${PATCH_ROOT}/CMakeLists.txt")
if(NOT EXISTS "${patch_file}")
  message(FATAL_ERROR "vectorscan patch: missing ${patch_file}")
endif()

file(READ "${patch_file}" content)
set(updated "${content}")

string(REPLACE "find_package(PkgConfig REQUIRED)" "find_package(PkgConfig QUIET)" updated "${updated}")

set(_klogg_vs_arch_flags [=[if (NOT FAT_RUNTIME)
    if (GNUCC_TUNE)
        set(ARCH_C_FLAGS "-${ARCH_FLAG}=${GNUCC_ARCH} -${TUNE_FLAG}=${GNUCC_TUNE}")
        set(ARCH_CXX_FLAGS "-${ARCH_FLAG}=${GNUCC_ARCH} -${TUNE_FLAG}=${GNUCC_TUNE}")
    else()
        set(ARCH_C_FLAGS "-${ARCH_FLAG}=${GNUCC_ARCH} -mtune=${TUNE_FLAG} ${ARCH_C_FLAGS}")
        set(ARCH_CXX_FLAGS "-${ARCH_FLAG}=${GNUCC_ARCH} -mtune=${TUNE_FLAG} ${ARCH_CXX_FLAGS}")
    endif()
endif()]=])
set(_klogg_vs_arch_flags_replacement [=[if (WIN32 AND MSVC AND ARCH_AARCH64)
    set(ARCH_C_FLAGS "")
    set(ARCH_CXX_FLAGS "")
elseif (NOT FAT_RUNTIME)
    if (GNUCC_TUNE)
        set(ARCH_C_FLAGS "-${ARCH_FLAG}=${GNUCC_ARCH} -${TUNE_FLAG}=${GNUCC_TUNE}")
        set(ARCH_CXX_FLAGS "-${ARCH_FLAG}=${GNUCC_ARCH} -${TUNE_FLAG}=${GNUCC_TUNE}")
    else()
        set(ARCH_C_FLAGS "-${ARCH_FLAG}=${GNUCC_ARCH} -mtune=${TUNE_FLAG} ${ARCH_C_FLAGS}")
        set(ARCH_CXX_FLAGS "-${ARCH_FLAG}=${GNUCC_ARCH} -mtune=${TUNE_FLAG} ${ARCH_CXX_FLAGS}")
    endif()
endif()]=])
string(REPLACE "${_klogg_vs_arch_flags}" "${_klogg_vs_arch_flags_replacement}" updated "${updated}")

set(_klogg_vs_opt_flags [=[if(RELEASE_BUILD)
    if (NOT CMAKE_BUILD_TYPE MATCHES MINSIZEREL)
        set(OPT_C_FLAG "-O3")
        set(OPT_CXX_FLAG "-O3")
    else ()
        set(OPT_C_FLAG "-Os")
        set(OPT_CXX_FLAG "-Os")
    endif ()
else()
    set(OPT_C_FLAG "-O0")
    set(OPT_CXX_FLAG "-O0")
endif(RELEASE_BUILD)]=])
set(_klogg_vs_opt_flags_replacement [=[if(WIN32 AND MSVC AND ARCH_AARCH64)
    set(OPT_C_FLAG "")
    set(OPT_CXX_FLAG "")
elseif(RELEASE_BUILD)
    if (NOT CMAKE_BUILD_TYPE MATCHES MINSIZEREL)
        set(OPT_C_FLAG "-O3")
        set(OPT_CXX_FLAG "-O3")
    else ()
        set(OPT_C_FLAG "-Os")
        set(OPT_CXX_FLAG "-Os")
    endif ()
else()
    set(OPT_C_FLAG "-O0")
    set(OPT_CXX_FLAG "-O0")
endif(RELEASE_BUILD)]=])
string(REPLACE "${_klogg_vs_opt_flags}" "${_klogg_vs_opt_flags_replacement}" updated "${updated}")

string(REPLACE [=[include (${CMAKE_MODULE_PATH}/cflags-generic.cmake)]=]
               [=[if(WIN32 AND MSVC AND ARCH_AARCH64)
  set(EXTRA_C_FLAGS "")
  set(EXTRA_CXX_FLAGS "")
else()
  include (${CMAKE_MODULE_PATH}/cflags-generic.cmake)
endif()]=]
               updated
               "${updated}")

if(NOT updated STREQUAL content)
  file(WRITE "${patch_file}" "${updated}")
endif()

set(ue2common_file "${PATCH_ROOT}/src/ue2common.h")
if(EXISTS "${ue2common_file}")
  file(READ "${ue2common_file}" ue2common_content)
  set(ue2common_updated "${ue2common_content}")

  string(REPLACE [=[#define ALIGN_ATTR(x) __attribute__((aligned((x))))

#define ALIGN_DIRECTIVE ALIGN_ATTR(16)
#define ALIGN_AVX_DIRECTIVE ALIGN_ATTR(32)
#define ALIGN_CL_DIRECTIVE ALIGN_ATTR(64)
]=]
                 [=[#if defined(_MSC_VER)
#define ALIGN_ATTR(x) __declspec(align(x))
#else
#define ALIGN_ATTR(x) __attribute__((aligned((x))))
#endif

#define ALIGN_DIRECTIVE ALIGN_ATTR(16)
#define ALIGN_AVX_DIRECTIVE ALIGN_ATTR(32)
#define ALIGN_CL_DIRECTIVE ALIGN_ATTR(64)
]=]
                 ue2common_updated
                 "${ue2common_updated}")

  string(REPLACE [=[typedef unsigned long long ALIGN_ATTR(8) u64a;
typedef signed long long ALIGN_ATTR(8) s64a;
]=]
                 [=[#if defined(_MSC_VER)
typedef __declspec(align(8)) unsigned long long u64a;
typedef __declspec(align(8)) signed long long s64a;
#else
typedef unsigned long long ALIGN_ATTR(8) u64a;
typedef signed long long ALIGN_ATTR(8) s64a;
#endif
]=]
                 ue2common_updated
                 "${ue2common_updated}")

  string(REPLACE [=[#ifndef HS_PUBLIC_API
#define HS_PUBLIC_API     __attribute__((visibility("default")))
#endif

#define ARRAY_LENGTH(a) (sizeof(a)/sizeof((a)[0]))

/** \brief Shorthand for the attribute to shut gcc about unused parameters */
#define UNUSED __attribute__ ((unused))

/* really_inline forces inlining always */
#if defined(HS_OPTIMIZE)
#define really_inline inline __attribute__ ((always_inline, unused))
#else
#define really_inline __attribute__ ((unused))
#endif

/** no, seriously, inline it, even if building in debug mode */
#define really_really_inline inline __attribute__ ((always_inline, unused))
#define never_inline __attribute__ ((noinline))
#define alignof __alignof
#define HAVE_TYPEOF 1
]=]
                 [=[#ifndef HS_PUBLIC_API
#if defined(_MSC_VER)
#define HS_PUBLIC_API
#else
#define HS_PUBLIC_API     __attribute__((visibility("default")))
#endif
#endif

#define ARRAY_LENGTH(a) (sizeof(a)/sizeof((a)[0]))

/** \brief Shorthand for the attribute to shut gcc about unused parameters */
#if defined(_MSC_VER)
#define UNUSED
#else
#define UNUSED __attribute__ ((unused))
#endif

/* really_inline forces inlining always */
#if defined(_MSC_VER)
#if defined(HS_OPTIMIZE)
#define really_inline __forceinline
#else
#define really_inline __inline
#endif
#define really_really_inline __forceinline
#define never_inline __declspec(noinline)
#if !defined(__cplusplus)
#define alignof __alignof
#endif
#if defined(__clang__)
#define HAVE_TYPEOF 1
#endif
#else
#if defined(HS_OPTIMIZE)
#define really_inline inline __attribute__ ((always_inline, unused))
#else
#define really_inline __attribute__ ((unused))
#endif

/** no, seriously, inline it, even if building in debug mode */
#define really_really_inline inline __attribute__ ((always_inline, unused))
#define never_inline __attribute__ ((noinline))
#define alignof __alignof
#define HAVE_TYPEOF 1
#endif
]=]
                 ue2common_updated
                 "${ue2common_updated}")

  if(NOT ue2common_updated STREQUAL ue2common_content)
    file(WRITE "${ue2common_file}" "${ue2common_updated}")
  endif()
endif()

set(unaligned_file "${PATCH_ROOT}/src/util/unaligned.h")
if(EXISTS "${unaligned_file}")
  file(READ "${unaligned_file}" unaligned_content)
  set(unaligned_updated "${unaligned_content}")

  string(REPLACE [=[#define PACKED__MAY_ALIAS __attribute__((packed, may_alias))

/// Perform an unaligned 16-bit load
static really_inline
u16 unaligned_load_u16(const void *ptr) {
    struct unaligned { u16 u; } PACKED__MAY_ALIAS;
    const struct unaligned *uptr = (const struct unaligned *)ptr;
    return uptr->u;
}

/// Perform an unaligned 32-bit load
static really_inline
u32 unaligned_load_u32(const void *ptr) {
    struct unaligned { u32 u; } PACKED__MAY_ALIAS;
    const struct unaligned *uptr = (const struct unaligned *)ptr;
    return uptr->u;
}

/// Perform an unaligned 64-bit load
static really_inline
u64a unaligned_load_u64a(const void *ptr) {
    struct unaligned { u64a u; } PACKED__MAY_ALIAS;
    const struct unaligned *uptr = (const struct unaligned *)ptr;
    return uptr->u;
}

/// Perform an unaligned 16-bit store
static really_inline
void unaligned_store_u16(void *ptr, u16 val) {
    struct unaligned { u16 u; } PACKED__MAY_ALIAS;
    struct unaligned *uptr = (struct unaligned *)ptr;
    uptr->u = val;
}

/// Perform an unaligned 32-bit store
static really_inline
void unaligned_store_u32(void *ptr, u32 val) {
    struct unaligned { u32 u; } PACKED__MAY_ALIAS;
    struct unaligned *uptr = (struct unaligned *)ptr;
    uptr->u = val;
}

/// Perform an unaligned 64-bit store
static really_inline
void unaligned_store_u64a(void *ptr, u64a val) {
    struct unaligned { u64a u; } PACKED__MAY_ALIAS;
    struct unaligned *uptr = (struct unaligned *)ptr;
    uptr->u = val;
}

#undef PACKED__MAY_ALIAS
]=]
                 [=[#if defined(_MSC_VER)
#include <string.h>

/// Perform an unaligned 16-bit load
static really_inline
u16 unaligned_load_u16(const void *ptr) {
    u16 value;
    memcpy(&value, ptr, sizeof(value));
    return value;
}

/// Perform an unaligned 32-bit load
static really_inline
u32 unaligned_load_u32(const void *ptr) {
    u32 value;
    memcpy(&value, ptr, sizeof(value));
    return value;
}

/// Perform an unaligned 64-bit load
static really_inline
u64a unaligned_load_u64a(const void *ptr) {
    u64a value;
    memcpy(&value, ptr, sizeof(value));
    return value;
}

/// Perform an unaligned 16-bit store
static really_inline
void unaligned_store_u16(void *ptr, u16 val) {
    memcpy(ptr, &val, sizeof(val));
}

/// Perform an unaligned 32-bit store
static really_inline
void unaligned_store_u32(void *ptr, u32 val) {
    memcpy(ptr, &val, sizeof(val));
}

/// Perform an unaligned 64-bit store
static really_inline
void unaligned_store_u64a(void *ptr, u64a val) {
    memcpy(ptr, &val, sizeof(val));
}
#else
#define PACKED__MAY_ALIAS __attribute__((packed, may_alias))

/// Perform an unaligned 16-bit load
static really_inline
u16 unaligned_load_u16(const void *ptr) {
    struct unaligned { u16 u; } PACKED__MAY_ALIAS;
    const struct unaligned *uptr = (const struct unaligned *)ptr;
    return uptr->u;
}

/// Perform an unaligned 32-bit load
static really_inline
u32 unaligned_load_u32(const void *ptr) {
    struct unaligned { u32 u; } PACKED__MAY_ALIAS;
    const struct unaligned *uptr = (const struct unaligned *)ptr;
    return uptr->u;
}

/// Perform an unaligned 64-bit load
static really_inline
u64a unaligned_load_u64a(const void *ptr) {
    struct unaligned { u64a u; } PACKED__MAY_ALIAS;
    const struct unaligned *uptr = (const struct unaligned *)ptr;
    return uptr->u;
}

/// Perform an unaligned 16-bit store
static really_inline
void unaligned_store_u16(void *ptr, u16 val) {
    struct unaligned { u16 u; } PACKED__MAY_ALIAS;
    struct unaligned *uptr = (struct unaligned *)ptr;
    uptr->u = val;
}

/// Perform an unaligned 32-bit store
static really_inline
void unaligned_store_u32(void *ptr, u32 val) {
    struct unaligned { u32 u; } PACKED__MAY_ALIAS;
    struct unaligned *uptr = (struct unaligned *)ptr;
    uptr->u = val;
}

/// Perform an unaligned 64-bit store
static really_inline
void unaligned_store_u64a(void *ptr, u64a val) {
    struct unaligned { u64a u; } PACKED__MAY_ALIAS;
    struct unaligned *uptr = (struct unaligned *)ptr;
    uptr->u = val;
}

#undef PACKED__MAY_ALIAS
#endif
]=]
                 unaligned_updated
                 "${unaligned_updated}")

  if(NOT unaligned_updated STREQUAL unaligned_content)
    file(WRITE "${unaligned_file}" "${unaligned_updated}")
  endif()
endif()
