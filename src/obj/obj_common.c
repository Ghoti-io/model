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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/model/macros.h>
#include "obj_internal.h"

/** Cut a line at its first '#'; a comment runs to the end of the line (2.4). */
static void line_strip_comment(char * line) {
  char * hash = strchr(line, '#');
  if (hash) {
    *hash = '\0';
  }
}

/**
 * Remove a trailing continuation backslash, if the line carries one.
 *
 * The backslash must be the last non-blank character (2.6), so blanks after
 * it are skipped when looking for it and dropped along with it.
 *
 * @param line The line, modified in place.
 * @param out_length Receives the length after any removal.
 * @return true when the line continues onto the next.
 */
static bool line_take_continuation(char * line, size_t * out_length) {
  size_t length = strlen(line);
  size_t trimmed = length;
  while (trimmed > 0 && (line[trimmed - 1] == ' ' || line[trimmed - 1] == '\t')) {
    trimmed--;
  }
  if (trimmed > 0 && line[trimmed - 1] == '\\') {
    line[trimmed - 1] = '\0';
    *out_length = trimmed - 1;
    return true;
  }
  *out_length = length;
  return false;
}

void gmdl_line_reader_init(GMDL_Line_Reader * reader, GMDL_Stream * stream,
    char * buffer, size_t max_length) {
  reader->stream = stream;
  reader->buffer = buffer;
  reader->max_length = max_length;
  reader->at_start = true;
  buffer[0] = '\0';
}

GMDL_Result gmdl_line_next(GMDL_Line_Reader * reader, const char ** out_line) {
  if (!reader || !out_line) {
    return GMDL_ERR_INVALID;
  }
  *out_line = NULL;

  size_t used = 0;
  for (;;) {
    // used never exceeds max_length, so there is always room for at least the
    // terminator, and the cap applies to the joined line rather than to each
    // physical piece of it (2.6).
    size_t space = reader->max_length + 1 - used;
    GMDL_Result result =
        gmdl_stream_read_line(reader->stream, reader->buffer + used, space, NULL);
    if (result == GMDL_ERR_IO) {
      if (used == 0) {
        return GMDL_ERR_IO; // End of stream, with nothing accumulated.
      }
      break; // A continuation with no line after it ends where it is.
    }
    if (result != GMDL_OK) {
      return result;
    }

    char * physical = reader->buffer + used;

    if (reader->at_start) {
      reader->at_start = false;
      // A UTF-8 byte-order mark is not part of the first directive (2.1).
      // The comparison stops at the terminator on a short line, so a one- or
      // two-byte first line is not read past.
      if ((unsigned char)physical[0] == 0xEF
          && (unsigned char)physical[1] == 0xBB
          && (unsigned char)physical[2] == 0xBF) {
        memmove(physical, physical + 3, strlen(physical + 3) + 1);
      }
    }

    // The comment goes first, so that a backslash inside one does not
    // continue the line and a comment on a continued line still disappears.
    line_strip_comment(physical);

    size_t length = 0;
    bool continues = line_take_continuation(physical, &length);
    used += length;
    if (!continues) {
      break;
    }
  }

  const char * cursor = reader->buffer;
  while (*cursor == ' ' || *cursor == '\t') {
    cursor++; // Leading whitespace before the directive is ignored (2.5).
  }
  *out_line = cursor;
  return GMDL_OK;
}

GMDL_Result gmdl_first_token(const char * rest, char * out, size_t out_size) {
  // Never taken today: every caller gets `rest` from gmdl_line_is(), which
  // has already skipped the blanks. Kept so the function's contract is the
  // text after a directive rather than the text after a directive provided
  // somebody else trimmed it first, which is a precondition nothing checks.
  while (*rest == ' ' || *rest == '\t') {
    rest++;
  }
  size_t length = 0;
  while (rest[length] && rest[length] != ' ' && rest[length] != '\t') {
    length++;
  }
  if (length == 0) {
    return GMDL_ERR_FORMAT;
  }
  if (length >= out_size) {
    return GMDL_ERR_LIMIT;
  }
  memcpy(out, rest, length);
  out[length] = '\0';
  return GMDL_OK;
}

GMDL_Result gmdl_parse_int32(const char * rest, int32_t * out) {
  while (*rest == ' ' || *rest == '\t') {
    rest++;
  }
  errno = 0;
  char * end = NULL;
  long value = strtol(rest, &end, 10);
  if (end == rest) {
    return GMDL_ERR_FORMAT;
  }
  // ERANGE covers a value too big for a long; the comparisons cover one that
  // fits a 64-bit long and not an int32_t field. Both are the same answer.
  if (errno == ERANGE || value > INT32_MAX || value < INT32_MIN) {
    return GMDL_ERR_LIMIT;
  }
  *out = (int32_t)value;
  return GMDL_OK;
}

GMDL_Result gmdl_rest_of_line(
    const char * rest, char * out, size_t out_size) {
  while (*rest == ' ' || *rest == '\t') {
    rest++;
  }
  size_t length = strlen(rest);
  // Comments are already gone by the time a directive is read, so anything
  // trailing here is whitespace the writer left behind.
  while (length > 0 && (rest[length - 1] == ' ' || rest[length - 1] == '\t')) {
    length--;
  }
  if (length == 0) {
    return GMDL_ERR_FORMAT;
  }
  if (length >= out_size) {
    return GMDL_ERR_LIMIT;
  }
  memcpy(out, rest, length);
  out[length] = '\0';
  return GMDL_OK;
}

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

/** The five bases `cstype` names, read and written from one list. */
static const struct {
  const char * name;
  GMDL_Obj_Cstype type;
} gmdl_cstypes[] = {
    {"bmatrix", GMDL_OBJ_CSTYPE_BMATRIX},
    {"bezier", GMDL_OBJ_CSTYPE_BEZIER},
    {"bspline", GMDL_OBJ_CSTYPE_BSPLINE},
    {"cardinal", GMDL_OBJ_CSTYPE_CARDINAL},
    {"taylor", GMDL_OBJ_CSTYPE_TAYLOR},
};

bool gmdl_cstype_from_name(const char * word, GMDL_Obj_Cstype * out) {
  for (size_t i = 0; i < sizeof(gmdl_cstypes) / sizeof(gmdl_cstypes[0]); i++) {
    if (gmdl_line_is(word, gmdl_cstypes[i].name, NULL)) {
      *out = gmdl_cstypes[i].type;
      return true;
    }
  }
  return false;
}

const char * gmdl_cstype_name(GMDL_Obj_Cstype type) {
  for (size_t i = 0; i < sizeof(gmdl_cstypes) / sizeof(gmdl_cstypes[0]); i++) {
    if (gmdl_cstypes[i].type == type) {
      return gmdl_cstypes[i].name;
    }
  }
  return NULL; // GMDL_OBJ_CSTYPE_NONE, which the format cannot spell.
}
