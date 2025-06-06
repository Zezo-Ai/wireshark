/** @file
 * Cross platform defines for exporting symbols from shared libraries
 *
 * Wireshark - Network traffic analyzer
 * By Balint Reczey <balint@balintreczey.hu>
 * Copyright 2013 Balint Reczey
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ws_compiler_tests.h"

/** Reset symbol export behavior.
 * If you {un}define WS_BUILD_DLL on the fly you'll have to define this
 * as well.
 */
#ifdef RESET_SYMBOL_EXPORT

#ifdef SYMBOL_EXPORT_H
#undef SYMBOL_EXPORT_H
#endif

#ifdef WS_DLL_PUBLIC
#undef WS_DLL_PUBLIC
#endif

#ifdef WS_DLL_PUBLIC_DEF
#undef WS_DLL_PUBLIC_DEF
#endif

#ifdef WS_DLL_LOCAL
#undef WS_DLL_LOCAL
#endif

#endif /* RESET_SYMBOL_EXPORT */

#ifndef SYMBOL_EXPORT_H
#define SYMBOL_EXPORT_H

/*
 * NOTE: G_HAVE_GNUC_VISIBILITY is defined only if all of
 *
 *    __attribute__ ((visibility ("hidden")))
 *
 *    __attribute__ ((visibility ("internal")))
 *
 *    __attribute__ ((visibility ("protected")))
 *
 *    __attribute__ ((visibility ("default")))
 *
 * are supported, and at least some versions of GCC from Apple support
 * "default" and "hidden" but not "internal" or "protected", so it
 * shouldn't be used to determine whether "hidden" or "default" is
 * supported.
 *
 * This also means that we shouldn't use G_GNUC_INTERNAL instead of
 * WS_DLL_LOCAL, as GLib uses G_HAVE_GNUC_VISIBILITY to determine
 * whether to use __attribute__ ((visibility ("hidden"))) for
 * G_GNUC_INTERNAL, and that will not use it even with compilers
 * that support it.
 */

/* Originally copied from GCC Wiki at https://gcc.gnu.org/wiki/Visibility */
#if defined _WIN32 || defined __CYGWIN__
  /* Compiling for Windows, so we use the Windows DLL declarations. */
  #ifdef WS_BUILD_DLL
    /*
     * Building a DLL; for all definitions, we want dllexport, and
     * (presumably so source from DLL and source from a program using the
     * DLL can both include a header that declares APIs and exported data
     * for the DLL), for declarations, either dllexport or dllimport will
     * work (they mean the same thing for a declaration when building a DLL).
     */
    #ifdef __GNUC__
      /* GCC */
      #define WS_DLL_PUBLIC_DEF __attribute__ ((dllexport))
    #else /* ! __GNUC__ */
      /*
       * Presumably MSVC.
       * Note: actually gcc seems to also support this syntax.
       */
      #define WS_DLL_PUBLIC_DEF __declspec(dllexport)
    #endif /* __GNUC__ */
  #else /* WS_BUILD_DLL */
    /*
     * Building a program; we should only see declarations, not definitions,
     * with WS_DLL_PUBLIC, and they all represent APIs or data imported
     * from a DLL, so use dllimport.
     *
     * For functions, export shouldn't be necessary; for data, it might
     * be necessary, e.g. if what's declared is an array whose size is
     * not given in the declaration.
     */
    #ifdef ENABLE_STATIC
      /*
       * We're building all-static, so we're not building any DLLs.
       */
      #define WS_DLL_PUBLIC_DEF
    #elif defined(__GNUC__)
      /* GCC */
      #define WS_DLL_PUBLIC_DEF __attribute__ ((dllimport))
    #else /* ! BUILD_SHARED_LIBS && ! __GNUC__ */
      /*
       * Presumably MSVC, and we're not building all-static.
       * Note: actually gcc seems to also support this syntax.
       */
      #define WS_DLL_PUBLIC_DEF __declspec(dllimport)
    #endif
  #endif /* WS_BUILD_DLL */

  /*
   * Symbols in a DLL are *not* exported unless they're specifically
   * flagged as exported, so, for a non-static but non-exported
   * symbol, we don't have to do anything.
   */
  #define WS_DLL_LOCAL
#else /* defined _WIN32 || defined __CYGWIN__ */
  /*
   * Compiling for UN*X, where the dllimport and dllexport stuff
   * is neither necessary nor supported; just specify the
   * visibility if we have a compiler that supports doing so.
   */
  #if WS_IS_AT_LEAST_GNUC_VERSION(3,4) \
      || WS_IS_AT_LEAST_XL_C_VERSION(12,0)
    /*
     * GCC 3.4 or later, or some compiler asserting compatibility with
     * GCC 3.4 or later, or XL C 13.0 or later, so we have
     * __attribute__((visibility()).
     */

    /*
     * Symbols exported from libraries.
     */
    #define WS_DLL_PUBLIC_DEF __attribute__ ((visibility ("default")))

    /*
     * Non-static symbols *not* exported from libraries.
     */
    #define WS_DLL_LOCAL  __attribute__ ((visibility ("hidden")))
  #elif WS_IS_AT_LEAST_SUNC_VERSION(5,5)
    /*
     * Sun C 5.5 or later, so we have __global and __hidden.
     * (Sun C 5.9 and later also have __attribute__((visibility()),
     * but there's no reason to prefer it with Sun C.)
     */

    /*
     * Symbols exported from libraries.
     */
    #define WS_DLL_PUBLIC_DEF __global

    /*
     * Non-static symbols *not* exported from libraries.
     */
    #define WS_DLL_LOCAL __hidden
  #else
    /*
     * We have neither a way to make stuff not explicitly marked as
     * visible invisible outside a library nor a way to make stuff
     * explicitly marked as local invisible outside the library.
     */

    /*
     * Symbols exported from libraries.
     */
    #define WS_DLL_PUBLIC_DEF

    /*
     * Non-static symbols *not* exported from libraries.
     */
    #define WS_DLL_LOCAL
  #endif
#endif

/*
 * You *must* use this for exported data *declarations*; if you use
 * WS_DLL_PUBLIC_DEF, some compilers, such as MSVC++, will complain
 * about array definitions with no size.
 *
 * You must *not* use this for exported data *definitions*, as that
 * will, for some compilers, cause warnings about items being initialized
 * and declared extern.
 *
 * Either can be used for exported *function* declarations and definitions.
 */
#define WS_DLL_PUBLIC  WS_DLL_PUBLIC_DEF extern

/*
 * This is necessary to export symbols from wsutil to another DLL
 * (not an executable) using MSVC.
 */
#ifdef _MSC_VER
#  ifdef ENABLE_STATIC
   /*
    * We're building all-static, so we're not building any DLLs.
    * Anything const and not initialized in the header (e.g., ws_utf8_seqlen)
    * must be extern to avoid C2734.
    */
#    define WSUTIL_EXPORT  extern
#  elif defined(BUILD_WSUTIL)
#    define WSUTIL_EXPORT  __declspec(dllexport) extern
#  else
#    define WSUTIL_EXPORT  __declspec(dllimport) extern
#  endif
#else /* _MSC_VER */
#  define WSUTIL_EXPORT    WS_DLL_PUBLIC
#endif /* _MSC_VER */



#endif /* SYMBOL_EXPORT_H */

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local Variables:
 * c-basic-offset: 2
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=2 tabstop=8 expandtab:
 * :indentSize=2:tabSize=8:noTabs=true:
 */
