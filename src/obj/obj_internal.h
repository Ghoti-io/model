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
#include <ghoti.io/model/stream.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reads the logical lines of a model file.
 *
 * A physical line is what sits between two line endings; a logical line is
 * what the parser is entitled to dispatch on. Four of the rules in section 2
 * of `documentation/obj-mtl.md` separate the two, and every one of them was
 * previously left to the individual parsers, which is to say to neither:
 *
 * - a UTF-8 byte-order mark at the very start is not part of the first
 *   directive (2.1);
 * - `#` begins a comment anywhere on the line (2.4);
 * - leading whitespace before the directive is ignored (2.5);
 * - a `\` as the last non-blank character joins the next line to this one,
 *   and `max_line_length` applies to the join as a whole (2.6).
 *
 * Both parsers read through this, so a file is seen the same way by each.
 */
typedef struct {
  GMDL_Stream * stream; ///< Where the bytes come from.
  char * buffer;        ///< Scratch, at least `max_length + 1` bytes.
  size_t max_length;    ///< Longest logical line, excluding the terminator.
  bool at_start;        ///< No byte of the input has been consumed yet.
} GMDL_Line_Reader;

/**
 * Prepare a reader.
 *
 * @param reader The reader to initialise.
 * @param stream The stream to read.
 * @param buffer Scratch space of at least @p max_length + 1 bytes, owned by
 *   the caller and outliving the reader.
 * @param max_length Longest logical line, excluding the terminator.
 */
void gmdl_line_reader_init(GMDL_Line_Reader * reader, GMDL_Stream * stream,
    char * buffer, size_t max_length);

/**
 * Read the next logical line.
 *
 * @param reader The reader.
 * @param out_line Receives a pointer into the reader's buffer, positioned at
 *   the first character after any leading whitespace. Valid until the next
 *   call. A line that was blank or wholly a comment yields `""`.
 * @return ::GMDL_OK, ::GMDL_ERR_IO at the end of the stream, or
 *   ::GMDL_ERR_LIMIT when the logical line exceeds `max_length`.
 */
GMDL_Result gmdl_line_next(GMDL_Line_Reader * reader, const char ** out_line);

/**
 * Copy the first whitespace-delimited token of @p rest.
 *
 * Nothing is truncated: a token too long for the field is refused, because a
 * name cut to its first 127 bytes names a different thing (section 1).
 *
 * @param rest The text after a directive.
 * @param out Receives the token, NUL-terminated.
 * @param out_size Bytes available at @p out, including the terminator.
 * @return ::GMDL_OK, ::GMDL_ERR_FORMAT when there is no token, or
 *   ::GMDL_ERR_LIMIT when the token does not fit.
 */
GMDL_Result gmdl_first_token(const char * rest, char * out, size_t out_size);

/**
 * Read everything after a directive as one value, blanks in the middle kept.
 *
 * For the directives whose argument is a single path or name that may contain
 * spaces. Blender 4.3.2 reads them this way, and writes them this way too: a
 * document exported to "my model.obj" carries `mtllib my model.mtl`, which
 * gmdl_first_token() reduces to the path "my" - a file that does not exist.
 *
 * Not every directive can be read like this. `g a b` is documented as putting
 * an element in two groups at once (3.12), so the first-token reading stays
 * there until that is resolved; `usemtl`, `newmtl` and `mtllib` have no such
 * form and take the whole line.
 *
 * Leading and trailing blanks are dropped. Comments are cut from the line
 * before any directive is matched, so nothing else needs stripping.
 *
 * @param rest The text after a directive.
 * @param out Receives the value, NUL-terminated.
 * @param out_size Bytes available at @p out, including the terminator.
 * @return ::GMDL_OK, ::GMDL_ERR_FORMAT when there is nothing there, or
 *   ::GMDL_ERR_LIMIT when it does not fit.
 */
GMDL_Result gmdl_rest_of_line(const char * rest, char * out, size_t out_size);

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
