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
 * Core types, result codes, and limits for the Ghoti.io Model library.
 */

#ifndef GHOTI_IO_GMDL_CORE_H
#define GHOTI_IO_GMDL_CORE_H

#include <ghoti.io/model/allocator.h>
#include <ghoti.io/model/macros.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result code for model library operations.
 */
typedef enum {
  GMDL_OK = 0,          ///< Operation succeeded.
  GMDL_ERR_IO,          ///< I/O error (read/write/seek failed).
  GMDL_ERR_FORMAT,      ///< Unrecognized or invalid format.
  GMDL_ERR_UNSUPPORTED, ///< Feature or format not supported.
  GMDL_ERR_LIMIT,       ///< Resource or size limit exceeded.
  GMDL_ERR_CORRUPT,     ///< Corrupt or invalid data.
  GMDL_ERR_OOM,         ///< Out of memory.
  GMDL_ERR_INVALID,     ///< Invalid argument.
  GMDL_ERR_INTERNAL,    ///< Internal library error.
  GMDL_RESULT_COUNT
} GMDL_Result;

/**
 * @brief Convert a result code to a human-readable string.
 *
 * The returned string is statically allocated and must not be freed.
 *
 * @param result The result code.
 * @return A description of the result code, never NULL.
 */
GMDL_API const char * gmdl_result_string(GMDL_Result result);

/**
 * @brief The line length a text parser uses when its max_line_length is zero.
 *
 * Named rather than repeated because each format's options carry their own
 * field, and a caller reading one header had no way to know the defaults
 * agreed. A binary format has no line length and does not use this.
 */
#define GMDL_DEFAULT_MAX_LINE_LENGTH 65536u

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GMDL_CORE_H
