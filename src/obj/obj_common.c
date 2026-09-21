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
 * Helpers shared by the OBJ and MTL parsers.
 */

#include <string.h>

#include <ghoti.io/model/macros.h>
#include "obj_internal.h"

bool gmdl_line_is(
    const char * line, const char * directive, const char ** out_rest) {
  size_t length = strlen(directive);
  if (strncmp(line, directive, length) != 0) {
    return false;
  }

  const char * rest = line + length;
  if (*rest != '\0' && *rest != ' ' && *rest != '\t') {
    return false;
  }
  while (*rest == ' ' || *rest == '\t') {
    rest++;
  }
  if (out_rest) {
    *out_rest = rest;
  }
  return true;
}

bool gmdl_limit_reached(size_t count, size_t limit) {
  return limit != 0 && count >= limit;
}
