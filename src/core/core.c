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
 * Result strings.
 */

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/core.h>

const char * gmdl_result_string(GMDL_Result result) {
  switch (result) {
    case GMDL_OK:
      return "No error";
    case GMDL_ERR_IO:
      return "I/O error";
    case GMDL_ERR_FORMAT:
      return "Invalid file format";
    case GMDL_ERR_UNSUPPORTED:
      return "Unsupported feature";
    case GMDL_ERR_LIMIT:
      return "Limit exceeded";
    case GMDL_ERR_CORRUPT:
      return "Corrupt data";
    case GMDL_ERR_OOM:
      return "Out of memory";
    case GMDL_ERR_INVALID:
      return "Invalid argument";
    case GMDL_ERR_INTERNAL:
      return "Internal error";
    case GMDL_RESULT_COUNT:
    default:
      return "Unknown error";
  }
}

