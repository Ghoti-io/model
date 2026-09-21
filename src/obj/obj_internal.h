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

#ifndef GHOTI_IO_GMDL_SRC_OBJ_OBJ_INTERNAL_H
#define GHOTI_IO_GMDL_SRC_OBJ_OBJ_INTERNAL_H

#include <ghoti.io/model/macros.h>

#include <ghoti.io/model/core.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Match a directive at the start of a line.
 *
 * A directive ends at whitespace or at the end of the line, so that "usemtlx"
 * is not read as "usemtl" with an argument of "x" - which is what a plain
 * length-limited comparison did.
 *
 * @param line The line.
 * @param directive The keyword to match.
 * @param out_rest Receives a pointer to the first character after the
 *   directive and any whitespace following it. Optional.
 * @return true when the line begins with the directive.
 */
bool gmdl_line_is(
    const char * line, const char * directive, const char ** out_rest);

/**
 * Whether a limit is exceeded. A limit of 0 means "no limit".
 *
 * @param count The count that is about to be increased by one.
 * @param limit The cap, or 0.
 * @return true when appending would exceed the cap.
 */
bool gmdl_limit_reached(size_t count, size_t limit);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GMDL_SRC_OBJ_OBJ_INTERNAL_H
