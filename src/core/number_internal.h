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
 * Pinning LC_NUMERIC for the duration of a parse or a dump.
 *
 * `strtof`, `sscanf("%f")` and `printf("%.9g")` all read LC_NUMERIC, whose
 * decimal separator is a comma in a good many locales. OBJ and MTL are byte
 * formats: the separator belongs to the format, not to the language settings
 * of whatever program happens to link this library. Without this, a caller
 * running under `de_DE.UTF-8` reads `v 0.5 0.5 0.5` as three zeroes and
 * writes `v 0,5 0,5 0,5`, which is not OBJ and does not load anywhere.
 *
 * **Why a scope rather than a converting helper per call site.** Ghoti.io
 * Text solved the same problem with `gtext_number_format()` and
 * `gtext_number_strtod()`, one call at a time, and the header comment there
 * records that the bug reached it three separate times - once per site that
 * had to remember. This library has nineteen conversions across four files
 * and every new directive adds another, so the thing to make impossible is
 * forgetting. A scope covers the sites that exist and the ones not yet
 * written.
 *
 * `uselocale()` changes the calling thread only, so pinning here cannot
 * disturb another thread mid-print.
 */

#ifndef GHOTI_IO_GMDL_SRC_CORE_NUMBER_INTERNAL_H
#define GHOTI_IO_GMDL_SRC_CORE_NUMBER_INTERNAL_H

#include <ghoti.io/model/macros.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An LC_NUMERIC pin, held for as long as the scope is open.
 *
 * Treat the fields as opaque. The struct is exposed so that it can live on
 * the stack of the function that opens it, which is what makes the pin
 * exception-free and impossible to leak past a `goto cleanup`.
 */
typedef struct GMDL_Numeric_Scope {
  void * applied;  ///< The C-numeric locale this scope created, or NULL.
  void * previous; ///< What to put back, meaningful only when @c applied is set.
} GMDL_Numeric_Scope;

/**
 * @brief Pin LC_NUMERIC to the C locale until the matching end().
 *
 * Never fails in a way the caller must handle. Where the platform has no
 * per-thread locales, or creating one fails, the scope is inert and
 * conversions happen in the caller's locale - which is right wherever the
 * separator is already `.` and no worse than not having called this.
 *
 * @param scope Receives the pin. Must not be NULL.
 */
GMDL_INTERNAL_API void gmdl_numeric_scope_begin(GMDL_Numeric_Scope * scope);

/**
 * @brief Release a pin taken by gmdl_numeric_scope_begin().
 *
 * Safe on an inert scope, and safe to call once on every exit path.
 *
 * @param scope The pin. Must not be NULL.
 */
GMDL_INTERNAL_API void gmdl_numeric_scope_end(GMDL_Numeric_Scope * scope);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GMDL_SRC_CORE_NUMBER_INTERNAL_H
