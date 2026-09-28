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

/** True when @p line's first token is @p directive, after leading blanks. */
static bool line_starts_with_directive(
    const char * line, const char * directive) {
  while (*line == ' ' || *line == '\t') {
    line++;
  }
  size_t length = strlen(directive);
  if (strncmp(line, directive, length) != 0) {
    return false;
  }
  char next = line[length];
  return next == '\0' || next == ' ' || next == '\t';
}

/** True when `\` is the last non-blank character. Does not modify @p line. */
static bool line_ends_with_continuation(const char * line) {
  size_t length = strlen(line);
  while (length > 0 && (line[length - 1] == ' ' || line[length - 1] == '\t')) {
    length--;
  }
  return length > 0 && line[length - 1] == '\\';
}

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
  reader->literal = false;
  reader->keep_byte_order_mark = false;
  reader->join_before_comment = false;
  reader->reject_vertex_continuation = false;
  reader->reject_face_comment = false;
  reader->break_group_continuation = false;
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

    // FreeCAD's reader does none of the rewriting below. The line is the
    // bytes between the endings, and a directive has to start at column 0.
    if (reader->literal) {
      reader->at_start = false;
      break;
    }

    if (reader->at_start) {
      reader->at_start = false;
      // A UTF-8 byte-order mark is not part of the first directive (2.1).
      // The comparison stops at the terminator on a short line, so a one- or
      // two-byte first line is not read past. Blender leaves the mark, and
      // the first line then does not match.
      if (!reader->keep_byte_order_mark
          && (unsigned char)physical[0] == 0xEF
          && (unsigned char)physical[1] == 0xBB
          && (unsigned char)physical[2] == 0xBF) {
        memmove(physical, physical + 3, strlen(physical + 3) + 1);
      }
    }

    // These look at the first physical line, before a join hides which
    // statement the backslash belonged to.
    if (used == 0 && reader->reject_face_comment
        && line_starts_with_directive(physical, "f")
        && strchr(physical, '#') != NULL) {
      return GMDL_ERR_FORMAT;
    }

    // The comment goes first, so that a backslash inside one does not
    // continue the line and a comment on a continued line still disappears.
    // join_before_comment is Blender's order: the backslash is seen while
    // the comment still contains it, and the cut happens once the logical
    // line is complete.
    if (!reader->join_before_comment) {
      line_strip_comment(physical);
    }

    if (used == 0 && line_ends_with_continuation(physical)) {
      if (reader->reject_vertex_continuation
          && line_starts_with_directive(physical, "v")) {
        return GMDL_ERR_FORMAT;
      }
      if (reader->break_group_continuation
          && (line_starts_with_directive(physical, "g")
              || line_starts_with_directive(physical, "o"))) {
        break;
      }
    }

    size_t length = 0;
    bool continues = line_take_continuation(physical, &length);
    used += length;
    if (!continues) {
      break;
    }
  }

  if (!reader->literal && reader->join_before_comment) {
    line_strip_comment(reader->buffer);
  }

  if (reader->literal) {
    *out_line = reader->buffer;
    return GMDL_OK;
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

// One table for the body statements' spellings, shared by the reader and the
// writer for the reason the cstype table above is: five names written twice
// is the shape that drifts, and a kind the dumper spells differently from
// the parser round-trips through this library against its own output and
// agrees with itself the whole way. `parm` carries its direction here
// because that is what the file writes - `parm u` and `parm v` are two
// statements, not one with an argument, once each line is its own record.
static const struct {
    const char * name;
    GMDL_Obj_Body_Kind kind;
} gmdl_body_kinds[] = {
    {"parm u", GMDL_OBJ_BODY_PARM_U},
    {"parm v", GMDL_OBJ_BODY_PARM_V},
    {"trim", GMDL_OBJ_BODY_TRIM},
    {"hole", GMDL_OBJ_BODY_HOLE},
    {"scrv", GMDL_OBJ_BODY_SCRV},
    {"sp", GMDL_OBJ_BODY_SP},
};

const char * gmdl_body_kind_name(GMDL_Obj_Body_Kind kind) {
  for (size_t i = 0;
      i < sizeof(gmdl_body_kinds) / sizeof(gmdl_body_kinds[0]); i++) {
    if (gmdl_body_kinds[i].kind == kind) {
      return gmdl_body_kinds[i].name;
    }
  }
  return NULL; // Unreachable while the table covers the enumeration.
}

// The approximation techniques, read and written from one list each, for
// the reason the cstype table above is shared. The arity lives in the table
// beside the spelling because it is a property of the technique and not of
// the line: `ctech curv` carries two numbers wherever it appears, and a
// parser and a dumper that each held their own idea of that would disagree
// about how much of a line to read back.
//
// Two tables rather than one with a flag: `cparm` is a curve technique and
// `cparma` a surface one, and the words are close enough that a single
// lookup would accept each in the other's line without anything noticing.
static const struct {
  const char * name;
  GMDL_Obj_Ctech technique;
  size_t arity;
} gmdl_ctechs[] = {
    {"cparm", GMDL_OBJ_CTECH_CPARM, 1},
    {"cspace", GMDL_OBJ_CTECH_CSPACE, 1},
    {"curv", GMDL_OBJ_CTECH_CURV, 2},
};

static const struct {
  const char * name;
  GMDL_Obj_Stech technique;
  size_t arity;
} gmdl_stechs[] = {
    {"cparma", GMDL_OBJ_STECH_CPARMA, 2},
    {"cparmb", GMDL_OBJ_STECH_CPARMB, 1},
    {"cspace", GMDL_OBJ_STECH_CSPACE, 1},
    {"curv", GMDL_OBJ_STECH_CURV, 2},
};

bool gmdl_ctech_from_name(const char * word, GMDL_Obj_Ctech * out,
    size_t * out_arity, const char ** out_rest) {
  for (size_t i = 0; i < sizeof(gmdl_ctechs) / sizeof(gmdl_ctechs[0]); i++) {
    if (gmdl_line_is(word, gmdl_ctechs[i].name, out_rest)) {
      *out = gmdl_ctechs[i].technique;
      *out_arity = gmdl_ctechs[i].arity;
      return true;
    }
  }
  return false;
}

const char * gmdl_ctech_name(GMDL_Obj_Ctech technique, size_t * out_arity) {
  for (size_t i = 0; i < sizeof(gmdl_ctechs) / sizeof(gmdl_ctechs[0]); i++) {
    if (gmdl_ctechs[i].technique == technique) {
      *out_arity = gmdl_ctechs[i].arity;
      return gmdl_ctechs[i].name;
    }
  }
  return NULL; // GMDL_OBJ_CTECH_NONE, which the format cannot spell.
}

bool gmdl_stech_from_name(const char * word, GMDL_Obj_Stech * out,
    size_t * out_arity, const char ** out_rest) {
  for (size_t i = 0; i < sizeof(gmdl_stechs) / sizeof(gmdl_stechs[0]); i++) {
    if (gmdl_line_is(word, gmdl_stechs[i].name, out_rest)) {
      *out = gmdl_stechs[i].technique;
      *out_arity = gmdl_stechs[i].arity;
      return true;
    }
  }
  return false;
}

const char * gmdl_stech_name(GMDL_Obj_Stech technique, size_t * out_arity) {
  for (size_t i = 0; i < sizeof(gmdl_stechs) / sizeof(gmdl_stechs[0]); i++) {
    if (gmdl_stechs[i].technique == technique) {
      *out_arity = gmdl_stechs[i].arity;
      return gmdl_stechs[i].name;
    }
  }
  return NULL; // GMDL_OBJ_STECH_NONE, which the format cannot spell.
}

const GMDL_Obj_Freeform * gmdl_obj_freeform_of_kind(
    const GMDL_Obj * obj, GMDL_Obj_Freeform_Kind kind, int32_t ordinal) {
  if (!obj || ordinal < 0) {
    return NULL;
  }
  int32_t seen = 0;
  for (size_t i = 0; i < obj->freeform_count; i++) {
    if (obj->freeforms[i].kind != kind) {
      continue;
    }
    if (seen == ordinal) {
      return &obj->freeforms[i];
    }
    seen++;
  }
  return NULL; // A forward reference the file never satisfied.
}
