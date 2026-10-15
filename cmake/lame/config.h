/* LAME's config.h for Segno's build of the MP3 encoder (cmake/Dependencies.cmake; export.md §17):
   what LAME's configure would find on any C99 compiler, on Linux, macOS and Windows. The encoder
   only: no decoder (mpglib), no front end, no assembly. */
#ifndef FD_LAME_CONFIG_H
#define FD_LAME_CONFIG_H

#define STDC_HEADERS 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1

#define HAVE_INT8_T 1
#define HAVE_INT16_T 1
#define HAVE_INT32_T 1
#define HAVE_INT64_T 1
#define HAVE_UINT8_T 1
#define HAVE_UINT16_T 1
#define HAVE_UINT32_T 1
#define HAVE_UINT64_T 1
#include <stdint.h>

typedef float ieee754_float32_t;
typedef double ieee754_float64_t;
typedef long double ieee854_float80_t;
#define HAVE_IEEE754_FLOAT32_T 1
#define HAVE_IEEE754_FLOAT64_T 1
#define HAVE_IEEE854_FLOAT80_T 1

#define LAME_LIBRARY_BUILD 1
#define PACKAGE "lame"
#define PACKAGE_NAME "lame"
#define PACKAGE_VERSION "3.100"
#define VERSION "3.100"

/* A faster logarithm, precise enough (LAME's default for release builds). */
#define USE_FAST_LOG 1

#endif
