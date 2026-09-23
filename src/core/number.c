/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Model.
 *
 * Ghoti.io Model is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Model is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * LC_NUMERIC pinning. See number_internal.h for why this exists.
 */

/* uselocale() and newlocale() are POSIX 2008. The feature macro has to be set
   BEFORE the first include, because it is what makes glibc's headers declare
   them - asking `#if defined(_POSIX_C_SOURCE)` up here instead tests a macro
   that features.h has not defined yet and is therefore always false. That
   mistake compiles: the fallback arm below is valid C that quietly does
   nothing, so the pin is simply absent and every number goes through the
   caller's locale. It is how the first version of this file behaved, and the
   only reason it was caught is that a test asserted the pin WORKED rather
   than that the code was present. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <locale.h>
/* Older Apple and FreeBSD SDKs declare newlocale() in <xlocale.h> rather than
   in <locale.h>. These two are compiler predefines, so unlike
   _POSIX_C_SOURCE above they ARE defined before any include and this test is
   sound where that one was not. */
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <xlocale.h>
#endif

/* Test for the thing actually used, not for a standards level that implies
   it. glibc defines LC_NUMERIC_MASK only when newlocale() is declared, so
   this cannot drift away from what the code below needs. */
#ifdef LC_NUMERIC_MASK
#define GMDL_HAVE_USELOCALE 1
#else
#define GMDL_HAVE_USELOCALE 0
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "number_internal.h"

#if GMDL_HAVE_USELOCALE

GMDL_INTERNAL_API void gmdl_numeric_scope_begin(GMDL_Numeric_Scope * scope) {
  scope->applied = NULL;
  scope->previous = NULL;

  locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
  if (!c_locale) {
    // Inert. Conversions run in the caller's locale, which is what they did
    // before this file existed.
    //
    // This return and the one in _end() are the two uncovered lines in this
    // file. There used to be a percentage here too; it drifted the moment the
    // file grew, so it is gone - a count of what is uncovered survives an
    // edit, a percentage of a line total does not.
    //
    // Reaching them needs newlocale() to fail, which on this platform means
    // allocation failure while building a locale compiled into libc. No input
    // to this library produces it, and the allocation-failure sweep cannot
    // either: newlocale() takes its memory from libc, not from the allocator
    // the caller handed us. They are left untested deliberately rather than
    // overlooked - a hook to force the failure would add production surface
    // to make two unreachable lines green, which is a worse trade.
    return;
  }
  scope->previous = (void *)uselocale(c_locale);
  scope->applied = (void *)c_locale;
}

GMDL_INTERNAL_API void gmdl_numeric_scope_end(GMDL_Numeric_Scope * scope) {
  if (!scope->applied) {
    return;
  }
  uselocale((locale_t)scope->previous);
  freelocale((locale_t)scope->applied);
  scope->applied = NULL;
  scope->previous = NULL;
}

GMDL_INTERNAL_API bool gmdl_numeric_pin_is_thread_local(void) {
  return true;
}

#elif defined(_WIN32)

// The Windows CRT has no uselocale(), but _configthreadlocale() gives the
// calling thread a locale of its own, after which setlocale() changes only
// that thread's. That is the same promise uselocale() makes, reached another
// way: switch the thread to its own locale, set LC_NUMERIC to "C" in it, and
// put back both the category and the thread's mode afterwards.
//
// The mode is saved as well as the name because a thread that was already
// per-thread must stay so - switching it back to the global locale would
// discard whatever the caller had set in it.
//
// TODO(windows): the notes/suite/WINDOWS-TODO.md entry for the model library
// asks for this to be run, not assumed: the thread-locality test exercises it,
// and it passing under MINGW64 is what verifies it.
typedef struct {
  int mode;    // _configthreadlocale()'s previous setting.
  char name[]; // LC_NUMERIC as it was, in the thread's own locale.
} GMDL_Numeric_Saved;

GMDL_INTERNAL_API void gmdl_numeric_scope_begin(GMDL_Numeric_Scope * scope) {
  scope->applied = NULL;
  scope->previous = NULL;

  int mode = _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
  if (mode == -1) {
    // Inert, as the POSIX arm is when newlocale() fails.
    return;
  }

  const char * current = setlocale(LC_NUMERIC, NULL);
  if (!current) {
    current = "C";
  }
  size_t length = strlen(current);
  GMDL_Numeric_Saved * saved =
      (GMDL_Numeric_Saved *)malloc(sizeof(*saved) + length + 1);
  if (!saved) {
    _configthreadlocale(mode);
    return;
  }
  saved->mode = mode;
  memcpy(saved->name, current, length + 1);

  if (!setlocale(LC_NUMERIC, "C")) {
    _configthreadlocale(mode);
    free(saved);
    return;
  }
  scope->previous = saved;
  // Non-NULL marks a pin in force; see the process-wide arm below.
  scope->applied = scope;
}

GMDL_INTERNAL_API void gmdl_numeric_scope_end(GMDL_Numeric_Scope * scope) {
  if (!scope->applied) {
    return;
  }
  GMDL_Numeric_Saved * saved = (GMDL_Numeric_Saved *)scope->previous;
  setlocale(LC_NUMERIC, saved->name);
  _configthreadlocale(saved->mode);
  free(saved);
  scope->applied = NULL;
  scope->previous = NULL;
}

GMDL_INTERNAL_API bool gmdl_numeric_pin_is_thread_local(void) {
  return true;
}

#elif defined(GMDL_ALLOW_PROCESS_WIDE_LOCALE)

// The caller has accepted a process-wide pin, so give them the best version
// of that trade rather than the worst.
//
// An earlier version of this arm compiled stubs that did nothing, which
// handed anyone who opted in silently wrong numbers - precisely the defect
// the rest of this file exists to remove. setlocale() is C89 and available
// everywhere, so "correct numbers, and the whole process's LC_NUMERIC moves
// while we convert" is always available and is never worse than "wrong
// numbers, and nothing moves". Nobody would knowingly choose the latter, so
// it should not be what the escape hatch gives them.
//
// What it costs is the promise uselocale() buys: another thread formatting
// output during a load or dump sees the C separator. That is why this is not
// the default and why the build refuses without the opt-in.
GMDL_INTERNAL_API void gmdl_numeric_scope_begin(GMDL_Numeric_Scope * scope) {
  scope->applied = NULL;
  scope->previous = NULL;

  const char * current = setlocale(LC_NUMERIC, NULL);
  if (current) {
    size_t length = strlen(current);
    char * saved = (char *)malloc(length + 1);
    if (saved) {
      memcpy(saved, current, length + 1);
      scope->previous = saved;
    }
  }
  setlocale(LC_NUMERIC, "C");
  // Non-NULL marks a pin in force. It points at the scope itself rather than
  // at a locale, because there is no locale_t here to point at.
  scope->applied = scope;
}

GMDL_INTERNAL_API void gmdl_numeric_scope_end(GMDL_Numeric_Scope * scope) {
  if (!scope->applied) {
    return;
  }
  // A failed save leaves "C" in force rather than guessing. That is the same
  // choice the C library starts a program with.
  setlocale(LC_NUMERIC, scope->previous ? (const char *)scope->previous : "C");
  free(scope->previous);
  scope->applied = NULL;
  scope->previous = NULL;
}

GMDL_INTERNAL_API bool gmdl_numeric_pin_is_thread_local(void) {
  return false;
}

#else

// No per-thread locale, so this build refuses rather than quietly producing a
// library that misparses.
//
// The earlier version of this arm was silent: it compiled stubs that did
// nothing, and a build on such a platform would read "v 0.5 0.5 0.5" as three
// zeroes and write "v 0,5 0,5 0,5" with no diagnostic anywhere. That is not a
// theoretical platform - MSVC has no LC_NUMERIC_MASK, and this library's
// Makefile and macros.h both claim Windows - so the silent arm was a shipped
// correctness bug on a platform we claim and cannot test here.
//
// A loud failure is cheaper than a silent one for everybody involved: the
// porter meets it once, at build time, with the fix named, while the silent
// version is met by their user as wrong geometry in another program. The
// escape hatch keeps that from being a wall - define
// GMDL_ALLOW_PROCESS_WIDE_LOCALE and numbers are pinned with setlocale
// instead, with the trade recorded in your build system where someone can
// find it.
//
// For MSVC the implementation is _configthreadlocale(_ENABLE_PER_THREAD_LOCALE)
// followed by setlocale(LC_NUMERIC, "C") and a restore, which IS thread-local
// there. It is deliberately not written blind: an arm nobody compiles is an
// arm nobody has checked, and a wrong one here misparses silently rather than
// failing to build.
#error "No per-thread locale (uselocale/newlocale) on this platform, so gmdl cannot keep number parsing independent of LC_NUMERIC. Numbers in OBJ and MTL always use '.', and without a pin this library would misread and miswrite every float wherever the C locale's separator is a comma. Implement the platform's per-thread locale in src/core/number.c (for MSVC: _configthreadlocale plus setlocale(LC_NUMERIC, \"C\")), or define GMDL_ALLOW_PROCESS_WIDE_LOCALE to pin LC_NUMERIC for the whole process instead - correct numbers, but another thread's separator moves while this library converts."

#endif
