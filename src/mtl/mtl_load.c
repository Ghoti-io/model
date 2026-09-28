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
 * Wavefront MTL parsing.
 *
 * Reference document:
 *   http://www.paulbourke.net/dataformats/mtl/
 */

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/cutil/array.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/mtl.h>

#include "../obj/obj_internal.h"
#include "../core/number_internal.h"

/**
 * Copy a token into a fresh buffer.
 *
 * @param token The first byte.
 * @param length How many bytes, not including a terminator.
 * @param allocator The allocator for the copy.
 * @param out Receives the copy. Unchanged on failure.
 * @return ::GMDL_OK, ::GMDL_ERR_FORMAT when there is nothing to copy, or
 *   ::GMDL_ERR_OOM.
 */
static GMDL_Result mtl_copy_token(const char * token, size_t length,
    const GMDL_Allocator * allocator, char ** out) {
  if (length == 0) {
    return GMDL_ERR_FORMAT;
  }
  char * copy = gcu_allocator_malloc(allocator, length + 1);
  if (!copy) {
    return GMDL_ERR_OOM;
  }
  memcpy(copy, token, length);
  copy[length] = '\0';
  *out = copy;
  return GMDL_OK;
}

/**
 * Read a colour property.
 *
 * Three forms, and the statement is part of what is recorded (4.2). `K? r g b`
 * and `K? r` are RGB, the second expanded to grey. `K? xyz x y z` is CIE XYZ,
 * kept as XYZ rather than converted, because a conversion is a colour-space
 * decision and the dump would then be unable to write the line back.
 * `spectral file [factor]` records the path and does not open it. The three
 * numbers are left at zero for a spectral statement: they are not a colour
 * the file stated.
 *
 * @param rest The text after the directive.
 * @param allocator Allocator for a spectral path.
 * @param out Receives three numbers. Zero for a spectral statement.
 * @param statement Receives how the line was written. Its previous path is
 *   freed when this replaces it.
 * @return ::GMDL_OK, ::GMDL_ERR_FORMAT or ::GMDL_ERR_OOM.
 */
static GMDL_Result mtl_parse_color(const char * rest,
    const GMDL_Allocator * allocator, float * out, GMDL_Mtl_Color * statement,
    bool reject_non_finite) {
  GMDL_Mtl_Color parsed;
  memset(&parsed, 0, sizeof(parsed));
  float values[3] = {0.0f, 0.0f, 0.0f};

  const char * after = NULL;
  if (gmdl_line_is(rest, "xyz", &after)) {
    size_t count = 0;
    const char * cursor = after;
    while (count < 3) {
      char * end = NULL;
      float value = strtof(cursor, &end);
      if (end == cursor) {
        break;
      }
      if (reject_non_finite && !isfinite(value)) {
        return GMDL_ERR_FORMAT;
      }
      values[count++] = value;
      cursor = end;
    }
    if (count != 3) {
      return GMDL_ERR_FORMAT;
    }
    parsed.form = GMDL_MTL_COLOR_XYZ;
  }
  else if (gmdl_line_is(rest, "spectral", &after)) {
    while (*after == ' ' || *after == '\t') {
      after++;
    }
    const char * start = after;
    while (*after && *after != ' ' && *after != '\t') {
      after++;
    }
    GMDL_Result copied = mtl_copy_token(
        start, (size_t)(after - start), allocator, &parsed.spectral);
    if (copied != GMDL_OK) {
      return copied;
    }
    while (*after == ' ' || *after == '\t') {
      after++;
    }
    parsed.factor = 1.0f;
    if (*after) {
      char * end = NULL;
      float factor = strtof(after, &end);
      if (end == after || (reject_non_finite && !isfinite(factor))) {
        gcu_allocator_free(allocator, parsed.spectral);
        return GMDL_ERR_FORMAT;
      }
      parsed.factor = factor;
      parsed.factor_stated = true;
    }
    parsed.form = GMDL_MTL_COLOR_SPECTRAL;
  }
  else {
    size_t count = 0;
    const char * cursor = rest;
    while (count < 3) {
      char * end = NULL;
      float value = strtof(cursor, &end);
      if (end == cursor) {
        break;
      }
      if (reject_non_finite && !isfinite(value)) {
        return GMDL_ERR_FORMAT;
      }
      values[count++] = value;
      cursor = end;
    }

    if (count == 3) {
      // Trailing text after the expected values is ignored (4.2).
    }
    else if (count == 1) {
      // One value is grey - but only when it is the whole of the value.
      // "Kd 0.5 x" is a malformed three-value form, which 4.2 calls FORMAT.
      while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
      }
      if (*cursor != '\0') {
        return GMDL_ERR_FORMAT;
      }
      values[1] = values[0];
      values[2] = values[0];
    }
    else {
      return GMDL_ERR_FORMAT;
    }
    parsed.form = GMDL_MTL_COLOR_RGB;
  }

  gcu_allocator_free(allocator, statement->spectral);
  *statement = parsed;
  out[0] = values[0];
  out[1] = values[1];
  out[2] = values[2];
  return GMDL_OK;
}

/**
 * Read a dissolve value.
 *
 * `d -halo n` is the format's orientation-dependent dissolve. The factor is
 * the same field as a plain `d`; `halo` says which spelling the file used,
 * because the dump has to write that spelling back (4.2).
 *
 * @param rest The text after the directive.
 * @param out Receives the value.
 * @param halo Receives whether the line said `-halo`.
 * @return ::GMDL_OK or ::GMDL_ERR_FORMAT.
 */
static GMDL_Result mtl_parse_dissolve(
    const char * rest, float * out, bool * halo, bool reject_non_finite) {
  const char * cursor = rest;
  *halo = false;
  if (gmdl_line_is(rest, "-halo", &cursor)) {
    *halo = true;
  }
  char * end = NULL;
  float value = strtof(cursor, &end);
  if (end == cursor || (reject_non_finite && !isfinite(value))) {
    return GMDL_ERR_FORMAT;
  }
  *out = value;
  return GMDL_OK;
}

/**
 * Release the texture map paths a material owns.
 *
 * @param allocator The allocator they were taken from.
 * @param material The material.
 */
static void mtl_material_free_paths(
    const GMDL_Allocator * allocator, GMDL_Mtl_Material * material) {
  gcu_allocator_free(allocator, material->map_Ka.path);
  gcu_allocator_free(allocator, material->map_Kd.path);
  gcu_allocator_free(allocator, material->map_Ks.path);
  gcu_allocator_free(allocator, material->map_Ns.path);
  gcu_allocator_free(allocator, material->map_d.path);
  gcu_allocator_free(allocator, material->map_bump.path);
  gcu_allocator_free(allocator, material->map_Ke.path);
  gcu_allocator_free(allocator, material->map_Pr.path);
  gcu_allocator_free(allocator, material->map_Pm.path);
  gcu_allocator_free(allocator, material->map_Ps.path);
  gcu_allocator_free(allocator, material->norm.path);
  gcu_allocator_free(allocator, material->disp.path);
  gcu_allocator_free(allocator, material->decal.path);
  gcu_allocator_free(allocator, material->Ka_color.spectral);
  gcu_allocator_free(allocator, material->Kd_color.spectral);
  gcu_allocator_free(allocator, material->Ks_color.spectral);
  gcu_allocator_free(allocator, material->Ke_color.spectral);
  gcu_allocator_free(allocator, material->Tf_color.spectral);
  for (size_t i = 0; i < GMDL_MTL_REFL_COUNT; i++) {
    gcu_allocator_free(allocator, material->refl[i].path);
  }
}

/** Fill a map with the defaults the format documents for an unstated option. */
static void mtl_map_defaults(GMDL_Mtl_Map * map) {
  memset(map, 0, sizeof(*map));
  map->blendu = true;
  map->blendv = true;
  map->mm[1] = 1.0f;
  map->s[0] = map->s[1] = map->s[2] = 1.0f;
  map->bm = 1.0f;
  map->imfchan = GMDL_MTL_IMFCHAN_L;
}

/** The next whitespace-delimited token, with its length. */
static const char * mtl_next_token(const char * cursor, size_t * length) {
  while (*cursor == ' ' || *cursor == '\t') {
    cursor++;
  }
  const char * start = cursor;
  while (*cursor && *cursor != ' ' && *cursor != '\t') {
    cursor++;
  }
  *length = (size_t)(cursor - start);
  return start;
}

/** Is @p token exactly @p name? */
static bool mtl_token_is(const char * token, size_t length, const char * name) {
  return strlen(name) == length && memcmp(token, name, length) == 0;
}

/**
 * Read a token as a float, but only if the WHOLE token is one.
 *
 * This is what keeps an option's arguments from eating the path. "-o 1 2"
 * followed by "2.png" must stop at two arguments: strtof would happily take
 * the "2" off the front of "2.png" and leave ".png" behind as the filename.
 * A token counts as a number only when the conversion consumes all of it.
 */
static bool mtl_token_float(const char * token, size_t length, float * out,
    bool reject_non_finite) {
  char buffer[64];
  if (length == 0 || length >= sizeof(buffer)) {
    return false;
  }
  memcpy(buffer, token, length);
  buffer[length] = '\0';
  char * end = NULL;
  float value = strtof(buffer, &end);
  if (end != buffer + length) {
    return false;
  }
  if (reject_non_finite && !isfinite(value)) {
    return false;
  }
  *out = value;
  return true;
}

/** Read up to @p max floats, stopping at the first token that is not one. */
static size_t mtl_take_floats(const char ** cursor, float * out, size_t max,
    size_t min, bool reject_non_finite) {
  size_t taken = 0;
  while (taken < max) {
    size_t length = 0;
    const char * token = mtl_next_token(*cursor, &length);
    float value = 0.0f;
    // A token of no length is mtl_token_float()'s answer too, and checking
    // it here as well left that clause of its guard unreachable: one
    // predicate written twice, where the copy nobody can reach is the copy
    // that stops being maintained.
    if (!mtl_token_float(token, length, &value, reject_non_finite)) {
      break;
    }
    out[taken++] = value;
    *cursor = token + length;
  }
  return taken >= min ? taken : 0;
}

/** Read an "on"/"off" argument. */
static bool mtl_take_toggle(const char ** cursor, bool * out) {
  size_t length = 0;
  const char * token = mtl_next_token(*cursor, &length);
  if (mtl_token_is(token, length, "on")) {
    *out = true;
  }
  else if (mtl_token_is(token, length, "off")) {
    *out = false;
  }
  else {
    return false;
  }
  *cursor = token + length;
  return true;
}

/** Names for `-type`, indexed by ::GMDL_Mtl_Refl_Type. */
static const char * const kReflTypeNames[GMDL_MTL_REFL_COUNT] = {"", "sphere",
    "cube_top", "cube_bottom", "cube_front", "cube_back", "cube_left",
    "cube_right"};

/**
 * Read a texture map: its options, then its path.
 *
 * Options come first, each introduced by a leading `-`; the path is the whole
 * of the rest of the line once they are consumed (4.5). An option this
 * library does not know is GMDL_ERR_UNSUPPORTED rather than skipped, because
 * several of them change what the map means - `-clamp` and `-imfchan` among
 * them - and a renderer given the path without them draws something the file
 * did not describe.
 *
 * @param rest The text after the directive.
 * @param allocator Allocator for the path.
 * @param slot Receives the map. Its previous path is freed.
 * @param out_type Receives `-type` for a `refl`, or NULL where `-type` is not
 *   a legal option.
 */
static GMDL_Result mtl_parse_map(const char * rest,
    const GMDL_Allocator * allocator, GMDL_Mtl_Map * slot,
    GMDL_Mtl_Refl_Type * out_type, bool accept_without_path,
    bool reject_non_finite) {
  GMDL_Mtl_Map parsed;
  mtl_map_defaults(&parsed);
  if (out_type) {
    *out_type = GMDL_MTL_REFL_UNTYPED;
  }

  const char * cursor = rest;
  for (;;) {
    size_t length = 0;
    const char * token = mtl_next_token(cursor, &length);
    if (length == 0 || token[0] != '-') {
      break; // No more options; whatever is left is the path.
    }

    const char * after = token + length;
    const char * argument = after;
    bool ok = true;

    if (mtl_token_is(token, length, "-blendu")) {
      ok = mtl_take_toggle(&argument, &parsed.blendu);
      parsed.present |= GMDL_MTL_MAP_HAS_BLENDU;
    }
    else if (mtl_token_is(token, length, "-blendv")) {
      ok = mtl_take_toggle(&argument, &parsed.blendv);
      parsed.present |= GMDL_MTL_MAP_HAS_BLENDV;
    }
    else if (mtl_token_is(token, length, "-clamp")) {
      ok = mtl_take_toggle(&argument, &parsed.clamp);
      parsed.present |= GMDL_MTL_MAP_HAS_CLAMP;
    }
    else if (mtl_token_is(token, length, "-boost")) {
      ok = mtl_take_floats(&argument, &parsed.boost, 1, 1, reject_non_finite)
          == 1;
      parsed.present |= GMDL_MTL_MAP_HAS_BOOST;
    }
    else if (mtl_token_is(token, length, "-bm")) {
      ok = mtl_take_floats(&argument, &parsed.bm, 1, 1, reject_non_finite) == 1;
      parsed.present |= GMDL_MTL_MAP_HAS_BM;
    }
    else if (mtl_token_is(token, length, "-mm")) {
      ok = mtl_take_floats(&argument, parsed.mm, 2, 2, reject_non_finite) == 2;
      parsed.present |= GMDL_MTL_MAP_HAS_MM;
    }
    else if (mtl_token_is(token, length, "-o")) {
      ok = mtl_take_floats(&argument, parsed.o, 3, 1, reject_non_finite) != 0;
      parsed.present |= GMDL_MTL_MAP_HAS_O;
    }
    else if (mtl_token_is(token, length, "-s")) {
      ok = mtl_take_floats(&argument, parsed.s, 3, 1, reject_non_finite) != 0;
      parsed.present |= GMDL_MTL_MAP_HAS_S;
    }
    else if (mtl_token_is(token, length, "-t")) {
      ok = mtl_take_floats(&argument, parsed.t, 3, 1, reject_non_finite) != 0;
      parsed.present |= GMDL_MTL_MAP_HAS_T;
    }
    else if (mtl_token_is(token, length, "-texres")) {
      float value = 0.0f;
      ok = mtl_take_floats(&argument, &value, 1, 1, reject_non_finite) == 1;
      if (ok) {
        // Casting a float that does not fit is undefined behaviour, and the
        // bounds are written as powers of two because those are the ones a
        // float represents exactly: everything in [-2^31, 2^31) narrows
        // cleanly and nothing else does. A NaN fails both comparisons, which
        // is why the test is negated rather than written the obvious way.
        if (!(value >= -2147483648.0f && value < 2147483648.0f)) {
          return GMDL_ERR_LIMIT;
        }
        parsed.texres = (int32_t)value;
        parsed.present |= GMDL_MTL_MAP_HAS_TEXRES;
      }
    }
    else if (mtl_token_is(token, length, "-imfchan")) {
      size_t argument_length = 0;
      const char * name = mtl_next_token(argument, &argument_length);
      static const char kChannels[] = "rgbmlz";
      const char * found = (argument_length == 1)
          ? memchr(kChannels, name[0], sizeof(kChannels) - 1)
          : NULL;
      if (!found) {
        return GMDL_ERR_FORMAT;
      }
      parsed.imfchan = (GMDL_Mtl_Imfchan)(found - kChannels);
      parsed.present |= GMDL_MTL_MAP_HAS_IMFCHAN;
      argument = name + argument_length;
    }
    else if (mtl_token_is(token, length, "-type")) {
      // Recorded on any map; acted on only by "refl", which uses it to pick
      // a slot. Elsewhere it is as inert as "-bm" on a colour map - and that
      // one has always been read rather than refused, which is the
      // inconsistency this removes.
      size_t argument_length = 0;
      const char * name = mtl_next_token(argument, &argument_length);
      GMDL_Mtl_Refl_Type found = GMDL_MTL_REFL_COUNT;
      for (int i = 1; i < GMDL_MTL_REFL_COUNT; i++) {
        if (mtl_token_is(name, argument_length, kReflTypeNames[i])) {
          found = (GMDL_Mtl_Refl_Type)i;
          break;
        }
      }
      if (found == GMDL_MTL_REFL_COUNT) {
        return GMDL_ERR_FORMAT;
      }
      parsed.type = found;
      parsed.present |= GMDL_MTL_MAP_HAS_TYPE;
      if (out_type) {
        *out_type = found;
      }
      argument = name + argument_length;
    }
    else {
      // A documented option this library does not implement, or one no
      // reference defines. Either way the map would mean something other
      // than what we would store.
      return GMDL_ERR_UNSUPPORTED;
    }

    if (!ok) {
      return GMDL_ERR_FORMAT;
    }
    cursor = argument;
  }

  while (*cursor == ' ' || *cursor == '\t') {
    cursor++;
  }
  size_t length = strlen(cursor);
  while (length > 0
      && (cursor[length - 1] == ' ' || cursor[length - 1] == '\t')) {
    length--;
  }
  if (length == 0) {
    // "map_Kd" with nothing after it. The default is FORMAT, for the same
    // reason 4.2 calls "Kd 0.5 x" FORMAT. accept_without_path skips the
    // line instead, which is what both reference readers do (4.5).
    if (accept_without_path) {
      return GMDL_OK;
    }
    return GMDL_ERR_FORMAT;
  }

  char * copy = gcu_allocator_malloc(allocator, length + 1);
  if (!copy) {
    return GMDL_ERR_OOM;
  }
  memcpy(copy, cursor, length);
  copy[length] = '\0';
  parsed.path = copy;

  // A repeated directive means the later one: the format has no way to say
  // two maps of one kind, so the alternative is leaking the first.
  gcu_allocator_free(allocator, slot->path);
  *slot = parsed;
  return GMDL_OK;
}

/**
 * Read a `refl` directive, which is the one map that names its own slot.
 *
 * `refl -type sphere file` and the six `cube_*` spellings are a material's
 * reflection maps; a cube map arrives as six separate lines. The leading
 * `-type` is not a sampling option like `-s` - it says which of seven
 * surfaces the file covers - so unlike every other map directive a leading
 * `-` is read rather than refused here. Any *other* option is
 * ::GMDL_ERR_UNSUPPORTED exactly as elsewhere (4.5).
 *
 * A `refl` with no `-type` is kept as ::GMDL_MTL_REFL_UNTYPED. The format
 * says the option is required and both references accept the line anyway, so
 * refusing it would reject files that exist; guessing which surface it meant
 * would invent something the file did not say.
 *
 * @param rest The text after the directive.
 * @param allocator The allocator for the copy.
 * @param material The material to fill in.
 * @return ::GMDL_OK, ::GMDL_ERR_FORMAT, ::GMDL_ERR_UNSUPPORTED or
 *   ::GMDL_ERR_OOM.
 */
static GMDL_Result mtl_parse_refl(const char * rest,
    const GMDL_Allocator * allocator, GMDL_Mtl_Material * material,
    bool accept_without_path, bool reject_non_finite) {
  // The slot a "refl" belongs in is decided by an option inside it, so the
  // map is parsed into a scratch record first and only then committed. That
  // also means "-type" may appear anywhere among the other options rather
  // than having to come first.
  GMDL_Mtl_Map parsed;
  mtl_map_defaults(&parsed);
  GMDL_Mtl_Refl_Type type = GMDL_MTL_REFL_UNTYPED;
  GMDL_Result result = mtl_parse_map(rest, allocator, &parsed, &type,
      accept_without_path, reject_non_finite);
  if (result != GMDL_OK) {
    return result;
  }
  // A bare `refl` that the caller asked to ignore never filled the scratch
  // record in. Writing it over the slot would clear a reflection the file
  // had already named.
  if (!parsed.path) {
    return GMDL_OK;
  }
  gcu_allocator_free(allocator, material->refl[type].path);
  material->refl[type] = parsed;
  return GMDL_OK;
}

/**
 * Read a `map_aat on|off` toggle.
 *
 * @param rest The text after the directive.
 * @param out Receives the value.
 * @return ::GMDL_OK or ::GMDL_ERR_FORMAT.
 */
static GMDL_Result mtl_parse_toggle(const char * rest, bool * out) {
  char token[8];
  if (gmdl_first_token(rest, token, sizeof(token)) != GMDL_OK) {
    return GMDL_ERR_FORMAT;
  }
  if (strcmp(token, "on") == 0) {
    *out = true;
    return GMDL_OK;
  }
  if (strcmp(token, "off") == 0) {
    *out = false;
    return GMDL_OK;
  }
  return GMDL_ERR_FORMAT;
}

/** Read one float, rejecting a non-finite value when @p reject_non_finite. */
static GMDL_Result mtl_read_float(
    const char * rest, float * out, bool reject_non_finite) {
  char * end = NULL;
  float value = strtof(rest, &end);
  if (end == rest || (reject_non_finite && !isfinite(value))) {
    return GMDL_ERR_FORMAT;
  }
  *out = value;
  return GMDL_OK;
}

static GMDL_Result mtl_load_pinned(GMDL_Stream * stream,
    const GMDL_Mtl_Options * limits, const GMDL_Allocator * allocator,
    GMDL_Mtl ** out_mtl) {
  if (!out_mtl) {
    return GMDL_ERR_INVALID;
  }
  *out_mtl = NULL;
  if (!stream) {
    return GMDL_ERR_INVALID;
  }

  GMDL_Mtl_Options defaults;
  if (!limits) {
    gmdl_mtl_options_default(&defaults);
    limits = &defaults;
  }

  // Zero is not "unlimited" here - see GMDL_Mtl_Options.max_line_length - because
  // this buffer is allocated before the first line is read.
  size_t line_size = limits->max_line_length ? limits->max_line_length
                                             : GMDL_DEFAULT_MAX_LINE_LENGTH;
  char * line = gcu_allocator_malloc(allocator, line_size + 1);
  if (!line) {
    return GMDL_ERR_OOM;
  }

  GCU_Array materials;
  if (!gcu_array_create_in_place(
          &materials, sizeof(GMDL_Mtl_Material), 8, allocator)) {
    gcu_allocator_free(allocator, line);
    return GMDL_ERR_OOM;
  }

  GMDL_Result result = GMDL_OK;
  // The material being filled in. Held as an index rather than a pointer,
  // because appending another material may move the array.
  size_t current = 0;
  bool have_current = false;

  GMDL_Line_Reader reader;
  gmdl_line_reader_init(&reader, stream, line, line_size);

  for (;;) {
    const char * line_text = NULL;
    GMDL_Result line_result = gmdl_line_next(&reader, &line_text);
    if (line_result == GMDL_ERR_IO) {
      break; // End of stream.
    }
    if (line_result != GMDL_OK) {
      result = line_result;
      goto cleanup;
    }

    // A blank line, or one that was wholly a comment, has nothing to do.
    if (line_text[0] == '\0') {
      continue;
    }

    const char * rest = NULL;
    if (gmdl_line_is(line_text, "newmtl", &rest)) {
      // A bare "newmtl" is GMDL_ERR_FORMAT; an over-long name is
      // GMDL_ERR_LIMIT rather than its first 127 bytes (3.9). The whole line
      // is the name: Blender reads `newmtl two words` as one material and
      // matches it against an OBJ's `usemtl two words`, and the two sides
      // have to agree about where a name ends.
      char name[GMDL_MTL_MAX_NAME_LENGTH];
      GMDL_Result named = gmdl_rest_of_line(rest, name, sizeof(name));
      if (named != GMDL_OK) {
        result = named;
        goto cleanup;
      }
      if (gmdl_limit_reached(
              gcu_array_count(&materials), limits->max_materials)) {
        result = GMDL_ERR_LIMIT;
        goto cleanup;
      }
      GMDL_Mtl_Material * material =
          (GMDL_Mtl_Material *)gcu_array_emplace(&materials);
      if (!material) {
        result = GMDL_ERR_OOM;
        goto cleanup;
      }
      // Zero first, so a field added later starts defined rather than
      // holding whatever the array's memory did.
      memset(material, 0, sizeof(*material));
      // Then the one property whose zero means something. An absent "d" is
      // fully opaque, which is what the format means and what every other
      // reader assumes; leaving it at zero made an ordinary material
      // invisible, and made gmdl_mtl_dump() write "d 0" and say so out
      // loud to anything that read the result.
      material->d = 1.0f;
      // Every map starts at the format's documented defaults, so a consumer
      // that never looks at a map's `present` mask still gets a scale of 1
      // and a bump multiplier of 1 rather than zeroes.
      GMDL_Mtl_Map * maps[] = {&material->map_Ka, &material->map_Kd,
          &material->map_Ks, &material->map_Ns, &material->map_d,
          &material->map_bump, &material->map_Ke, &material->map_Pr,
          &material->map_Pm, &material->map_Ps, &material->norm,
          &material->disp, &material->decal};
      for (size_t i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
        mtl_map_defaults(maps[i]);
      }
      for (int i = 0; i < GMDL_MTL_REFL_COUNT; i++) {
        mtl_map_defaults(&material->refl[i]);
      }
      memcpy(material->name, name, strlen(name) + 1);
      current = gcu_array_count(&materials) - 1;
      have_current = true;
      continue;
    }

    if (!have_current) {
      // A property before any "newmtl" has nothing to apply to.
      continue;
    }

    GMDL_Mtl_Material * material =
        (GMDL_Mtl_Material *)gcu_array_at(&materials, current);

    if (gmdl_line_is(line_text, "Ka", &rest)) {
      GMDL_Result parsed = mtl_parse_color(
          rest, allocator, material->Ka, &material->Ka_color,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_KA;
    }
    else if (gmdl_line_is(line_text, "Kd", &rest)) {
      GMDL_Result parsed = mtl_parse_color(
          rest, allocator, material->Kd, &material->Kd_color,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_KD;
    }
    else if (gmdl_line_is(line_text, "Ks", &rest)) {
      GMDL_Result parsed = mtl_parse_color(
          rest, allocator, material->Ks, &material->Ks_color,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_KS;
    }
    else if (gmdl_line_is(line_text, "Ns", &rest)) {
      GMDL_Result parsed = mtl_read_float(
          rest, &material->Ns,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_NS;
    }
    else if (gmdl_line_is(line_text, "d", &rest)) {
      GMDL_Result parsed = mtl_parse_dissolve(
          rest, &material->d, &material->d_halo,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_D;
    }
    else if (gmdl_line_is(line_text, "illum", &rest)) {
      int32_t value = 0;
      GMDL_Result parsed = gmdl_parse_int32(rest, &value);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->illum = value;
      material->present |= GMDL_MTL_HAS_ILLUM;
    }
    else if (gmdl_line_is(line_text, "Ke", &rest)) {
      GMDL_Result parsed = mtl_parse_color(
          rest, allocator, material->Ke, &material->Ke_color,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_KE;
    }
    else if (gmdl_line_is(line_text, "Tf", &rest)) {
      GMDL_Result parsed = mtl_parse_color(
          rest, allocator, material->Tf, &material->Tf_color,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_TF;
    }
    else if (gmdl_line_is(line_text, "Ni", &rest)) {
      GMDL_Result parsed = mtl_read_float(
          rest, &material->Ni,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_NI;
    }
    else if (gmdl_line_is(line_text, "Tr", &rest)) {
      GMDL_Result parsed = mtl_read_float(
          rest, &material->Tr,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_TR;
    }
    else if (gmdl_line_is(line_text, "Pr", &rest)) {
      GMDL_Result parsed = mtl_read_float(
          rest, &material->Pr,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_PR;
    }
    else if (gmdl_line_is(line_text, "Pm", &rest)) {
      GMDL_Result parsed = mtl_read_float(
          rest, &material->Pm,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_PM;
    }
    else if (gmdl_line_is(line_text, "Ps", &rest)) {
      GMDL_Result parsed = mtl_read_float(
          rest, &material->Ps,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_PS;
    }
    else if (gmdl_line_is(line_text, "Pc", &rest)) {
      GMDL_Result parsed = mtl_read_float(
          rest, &material->Pc,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_PC;
    }
    else if (gmdl_line_is(line_text, "Pcr", &rest)) {
      GMDL_Result parsed = mtl_read_float(
          rest, &material->Pcr,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_PCR;
    }
    else if (gmdl_line_is(line_text, "aniso", &rest)) {
      GMDL_Result parsed = mtl_read_float(
          rest, &material->aniso,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_ANISO;
    }
    else if (gmdl_line_is(line_text, "anisor", &rest)) {
      GMDL_Result parsed = mtl_read_float(
          rest, &material->anisor,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_ANISOR;
    }
    else if (gmdl_line_is(line_text, "sharpness", &rest)) {
      int32_t value = 0;
      GMDL_Result parsed = gmdl_parse_int32(rest, &value);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->sharpness = value;
      material->present |= GMDL_MTL_HAS_SHARPNESS;
    }
    else if (gmdl_line_is(line_text, "map_aat", &rest)) {
      GMDL_Result parsed = mtl_parse_toggle(rest, &material->map_aat);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_MAP_AAT;
    }
    else if (gmdl_line_is(line_text, "map_Ka", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Ka, NULL,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Kd", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Kd, NULL,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Ks", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Ks, NULL,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Ns", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Ns, NULL,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_d", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_d, NULL,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    // "bump", "map_bump" and "map_Bump" are three spellings of one property,
    // and exporters write all three. They share a field; the dump writes
    // "map_bump".
    //
    // "map_Bump" is not a case-folding concession: matching stays exact (2.7).
    // Blender 4.3.2 accepts this spelling and "map_refl" below, while ignoring
    // "kd", "KD", "map_kd", "map_BUMP" and "Map_Bump" - it carries specific
    // extra spellings rather than folding case, and so do we. Accepting
    // arbitrary case would take input the reference rejects, which is a worse
    // disagreement than the one it fixes.
    else if (gmdl_line_is(line_text, "map_bump", &rest)
        || gmdl_line_is(line_text, "bump", &rest)
        || gmdl_line_is(line_text, "map_Bump", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_bump, NULL,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Ke", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Ke, NULL,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Pr", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Pr, NULL,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Pm", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Pm, NULL,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Ps", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Ps, NULL,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "norm", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->norm, NULL,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "disp", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->disp, NULL,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "decal", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->decal, NULL,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    // "map_refl" is the same directive under the spelling Blender accepts.
    else if (gmdl_line_is(line_text, "refl", &rest)
        || gmdl_line_is(line_text, "map_refl", &rest)) {
      GMDL_Result parsed = mtl_parse_refl(rest, allocator, material,
          limits->accept_map_without_path,
          limits->reject_non_finite);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    // Anything left is a directive this format does not define (4.3).
  }

  {
    GMDL_Mtl * mtl = gcu_allocator_calloc(allocator, 1, sizeof(GMDL_Mtl));
    if (!mtl) {
      result = GMDL_ERR_OOM;
      goto cleanup;
    }
    (void)gcu_array_shrink_to_fit(&materials);
    size_t count = 0;
    mtl->materials = (GMDL_Mtl_Material *)gcu_array_steal(&materials, &count);
    mtl->material_count = count;
    mtl->allocator = allocator;

    gcu_array_destroy_in_place(&materials);
    gcu_allocator_free(allocator, line);
    *out_mtl = mtl;
    return GMDL_OK;
  }

cleanup:
  // The materials are about to be thrown away, and each may own paths.
  for (size_t i = 0; i < gcu_array_count(&materials); i++) {
    mtl_material_free_paths(
        allocator, (GMDL_Mtl_Material *)gcu_array_at(&materials, i));
  }
  gcu_array_destroy_in_place(&materials);
  gcu_allocator_free(allocator, line);
  return result;
}

void gmdl_mtl_options_default(GMDL_Mtl_Options * options) {
  if (!options) {
    return;
  }
  // Same line-cap reason as OBJ. The material cap is open because the input
  // already bounds it. A reading's zero is the behaviour the specification
  // states.
  *options = (GMDL_Mtl_Options) {
    .max_line_length = GMDL_DEFAULT_MAX_LINE_LENGTH,
  };
}

/** Read an MTL document with the numeric locale pinned (number_internal.h). */
GMDL_Result gmdl_mtl_load(GMDL_Stream * stream, const GMDL_Mtl_Options * limits,
    const GMDL_Allocator * allocator, GMDL_Mtl ** out_mtl) {
  GMDL_Numeric_Scope numeric;
  gmdl_numeric_scope_begin(&numeric);
  GMDL_Result result = mtl_load_pinned(stream, limits, allocator, out_mtl);
  gmdl_numeric_scope_end(&numeric);
  return result;
}

GMDL_Result gmdl_mtl_load_file(const char * path, const GMDL_Mtl_Options * limits,
    const GMDL_Allocator * allocator, GMDL_Mtl ** out_mtl) {
  if (!out_mtl) {
    return GMDL_ERR_INVALID;
  }
  *out_mtl = NULL;

  GMDL_Stream * stream = NULL;
  GMDL_Result result = gmdl_stream_create_file(path, allocator, &stream);
  if (result != GMDL_OK) {
    return result;
  }

  result = gmdl_mtl_load(stream, limits, allocator, out_mtl);
  gmdl_stream_destroy(stream);
  return result;
}

void gmdl_mtl_free(GMDL_Mtl * mtl) {
  if (!mtl) {
    return;
  }
  const GMDL_Allocator * allocator = mtl->allocator;
  for (size_t i = 0; i < mtl->material_count; i++) {
    mtl_material_free_paths(allocator, &mtl->materials[i]);
  }
  gcu_allocator_free(allocator, mtl->materials);
  gcu_allocator_free(allocator, mtl);
}

const GMDL_Mtl_Material * gmdl_mtl_find(
    const GMDL_Mtl * mtl, const char * name) {
  if (!mtl || !name || !mtl->materials) {
    return NULL;
  }
  for (size_t i = 0; i < mtl->material_count; i++) {
    if (strcmp(mtl->materials[i].name, name) == 0) {
      return &mtl->materials[i];
    }
  }
  return NULL;
}
