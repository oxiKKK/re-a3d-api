/*
 * config.h - minimal build configuration for the vendored liba52.
 *
 * NOT PART OF liba52's distribution.  The autoconf-generated config.h is not
 * used here; this supplies only what the source files read.  HAVE_MEMALIGN,
 * LIBA52_DJBFFT and LIBA52_DOUBLE are deliberately left undefined, which selects
 * malloc alignment, the C IMDCT (no djbfft dependency) and float samples.
 */

#ifndef A52_CONFIG_H
#define A52_CONFIG_H

#define PACKAGE "a52dec"
#define VERSION "0.7.4"

/* liba52 uses `static inline`; the MSVC C compiler spells it __inline. */
#ifdef _MSC_VER
#define inline __inline
#endif

#endif /* A52_CONFIG_H */
