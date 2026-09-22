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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/mtl.h>

#include "../obj/obj_internal.h"
#include "../core/number_internal.h"

/**
 * Read a colour property's value.
 *
 * Section 4.2 documents three forms. `K? r g b` is the ordinary one and
 * `K? r` means grey - the same value in all three channels. `K? xyz ...`
 * (CIE XYZ) and `K? spectral file [factor]` are real forms this library does
 * not implement.
 *
 * The unimplemented forms are GMDL_ERR_UNSUPPORTED, not GMDL_ERR_FORMAT.
 * Section 1 draws that line deliberately: the file is well-formed and the
 * reader is the one falling short, and a caller that meets the two wants to
 * do different things. Answering FORMAT to both meant a perfectly good
 * material library was rejected as corrupt.
 *
 * @param rest The text after the directive.
 * @param out Receives three channel values.
 * @return ::GMDL_OK, ::GMDL_ERR_FORMAT or ::GMDL_ERR_UNSUPPORTED.
 */
static GMDL_Result mtl_parse_color(const char * rest, float * out) {
  if (gmdl_line_is(rest, "xyz", NULL) || gmdl_line_is(rest, "spectral", NULL)) {
    return GMDL_ERR_UNSUPPORTED;
  }

  float values[3];
  size_t count = 0;
  const char * cursor = rest;
  while (count < 3) {
    char * end = NULL;
    float value = strtof(cursor, &end);
    if (end == cursor) {
      break;
    }
    values[count++] = value;
    cursor = end;
  }

  if (count == 3) {
    // Trailing text after the expected values is ignored (4.2).
    out[0] = values[0];
    out[1] = values[1];
    out[2] = values[2];
    return GMDL_OK;
  }
  if (count == 1) {
    // One value is grey - but only when it is the whole of the value.
    // "Kd 0.5 x" is a malformed three-value form, which 4.2 calls FORMAT.
    while (*cursor == ' ' || *cursor == '\t') {
      cursor++;
    }
    if (*cursor != '\0') {
      return GMDL_ERR_FORMAT;
    }
    out[0] = values[0];
    out[1] = values[0];
    out[2] = values[0];
    return GMDL_OK;
  }
  return GMDL_ERR_FORMAT;
}

/**
 * Read a dissolve value.
 *
 * `d -halo n` is a documented form this library does not implement, so it is
 * GMDL_ERR_UNSUPPORTED for the reason given on mtl_parse_color().
 *
 * @param rest The text after the directive.
 * @param out Receives the value.
 * @return ::GMDL_OK, ::GMDL_ERR_FORMAT or ::GMDL_ERR_UNSUPPORTED.
 */
static GMDL_Result mtl_parse_dissolve(const char * rest, float * out) {
  if (gmdl_line_is(rest, "-halo", NULL)) {
    return GMDL_ERR_UNSUPPORTED;
  }
  char * end = NULL;
  float value = strtof(rest, &end);
  if (end == rest) {
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
  gcu_allocator_free(allocator, material->map_Ka);
  gcu_allocator_free(allocator, material->map_Kd);
  gcu_allocator_free(allocator, material->map_Ks);
  gcu_allocator_free(allocator, material->map_Ns);
  gcu_allocator_free(allocator, material->map_d);
  gcu_allocator_free(allocator, material->map_bump);
  gcu_allocator_free(allocator, material->map_Ke);
  gcu_allocator_free(allocator, material->map_Pr);
  gcu_allocator_free(allocator, material->map_Pm);
  gcu_allocator_free(allocator, material->map_Ps);
  gcu_allocator_free(allocator, material->norm);
  gcu_allocator_free(allocator, material->disp);
  gcu_allocator_free(allocator, material->decal);
  for (size_t i = 0; i < GMDL_MTL_REFL_COUNT; i++) {
    gcu_allocator_free(allocator, material->refl[i]);
  }
}

/**
 * Read a texture map directive's filename.
 *
 * The filename is the whole of the rest of the line, trailing blanks
 * removed, so a path containing spaces is one path (4.5). Both Blender 4.3
 * and VTK 9.3 read it that way, which is the only reason to prefer it over
 * the first token - the format's own description says nothing either way.
 *
 * A line whose argument begins with `-` carries texture options, which this
 * library does not implement, and that is ::GMDL_ERR_UNSUPPORTED rather than
 * a silent guess. The two references disagree about what the options even
 * are: Blender knows `-clamp` and consumes it, VTK 9.3 does not and folds it
 * into the filename, so `map_Kd -clamp on t.png` names `t.png` in one and
 * `-clamp on t.png` in the other. Picking either would be picking a side in
 * a disagreement the caller cannot see, and dropping the options silently is
 * worse than refusing: `-s 2 2 2` is a scale a renderer would then not
 * apply, which is a wrong picture rather than a missing one.
 *
 * @param rest The text after the directive, already past leading blanks.
 * @param allocator The allocator for the copy.
 * @param slot The material field to fill in, freed first if already set.
 * @return ::GMDL_OK, ::GMDL_ERR_FORMAT for no filename,
 *   ::GMDL_ERR_UNSUPPORTED for options, or ::GMDL_ERR_OOM.
 */
static GMDL_Result mtl_parse_map(
    const char * rest, const GMDL_Allocator * allocator, char ** slot) {
  if (*rest == '-') {
    return GMDL_ERR_UNSUPPORTED;
  }

  size_t length = strlen(rest);
  while (length > 0 && (rest[length - 1] == ' ' || rest[length - 1] == '\t')) {
    length--;
  }
  if (length == 0) {
    // "map_Kd" with nothing after it. Both references ignore the line; this
    // library calls it FORMAT for the same reason 4.2 calls "Kd 0.5 x"
    // FORMAT, and 4.5 records the divergence.
    return GMDL_ERR_FORMAT;
  }

  char * copy = gcu_allocator_malloc(allocator, length + 1);
  if (!copy) {
    return GMDL_ERR_OOM;
  }
  memcpy(copy, rest, length);
  copy[length] = '\0';

  // A repeated directive means the later one: the format has no way to say
  // two maps of one kind, so the alternative is leaking the first.
  gcu_allocator_free(allocator, *slot);
  *slot = copy;
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
    const GMDL_Allocator * allocator, GMDL_Mtl_Material * material) {
  static const struct {
    const char * name;
    GMDL_Mtl_Refl_Type type;
  } types[] = {
    {"sphere", GMDL_MTL_REFL_SPHERE},
    {"cube_top", GMDL_MTL_REFL_CUBE_TOP},
    {"cube_bottom", GMDL_MTL_REFL_CUBE_BOTTOM},
    {"cube_front", GMDL_MTL_REFL_CUBE_FRONT},
    {"cube_back", GMDL_MTL_REFL_CUBE_BACK},
    {"cube_left", GMDL_MTL_REFL_CUBE_LEFT},
    {"cube_right", GMDL_MTL_REFL_CUBE_RIGHT},
  };

  GMDL_Mtl_Refl_Type type = GMDL_MTL_REFL_UNTYPED;
  if (*rest == '-') {
    const char * after = NULL;
    if (!gmdl_line_is(rest, "-type", &after)) {
      return GMDL_ERR_UNSUPPORTED; // Some option other than -type.
    }
    char name[32];
    if (gmdl_first_token(after, name, sizeof(name)) != GMDL_OK) {
      // Absent, or too long to be any of the seven. Either way the line
      // says -type and then does not name one.
      return GMDL_ERR_FORMAT;
    }
    size_t index = 0;
    for (; index < sizeof(types) / sizeof(types[0]); index++) {
      if (strcmp(name, types[index].name) == 0) {
        type = types[index].type;
        break;
      }
    }
    if (index == sizeof(types) / sizeof(types[0])) {
      return GMDL_ERR_FORMAT; // A -type this format does not define.
    }
    rest = after + strlen(name);
    while (*rest == ' ' || *rest == '\t') {
      rest++;
    }
    if (*rest == '-') {
      return GMDL_ERR_UNSUPPORTED; // -type, then a sampling option.
    }
  }
  return mtl_parse_map(rest, allocator, &material->refl[type]);
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

static GMDL_Result mtl_load_pinned(GMDL_Stream * stream,
    const GMDL_Limits * limits, const GMDL_Allocator * allocator,
    GMDL_Mtl ** out_mtl) {
  if (!out_mtl) {
    return GMDL_ERR_INVALID;
  }
  *out_mtl = NULL;
  if (!stream) {
    return GMDL_ERR_INVALID;
  }

  GMDL_Limits defaults;
  if (!limits) {
    gmdl_limits_default(&defaults);
    limits = &defaults;
  }

  size_t line_size = limits->max_line_length ? limits->max_line_length : 65536;
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
      // GMDL_ERR_LIMIT rather than its first 127 bytes (3.9).
      char name[GMDL_MTL_MAX_NAME_LENGTH];
      GMDL_Result named = gmdl_first_token(rest, name, sizeof(name));
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
      GMDL_Result parsed = mtl_parse_color(rest, material->Ka);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_KA;
    }
    else if (gmdl_line_is(line_text, "Kd", &rest)) {
      GMDL_Result parsed = mtl_parse_color(rest, material->Kd);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_KD;
    }
    else if (gmdl_line_is(line_text, "Ks", &rest)) {
      GMDL_Result parsed = mtl_parse_color(rest, material->Ks);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_KS;
    }
    else if (gmdl_line_is(line_text, "Ns", &rest)) {
      if (sscanf(rest, "%f", &material->Ns) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_NS;
    }
    else if (gmdl_line_is(line_text, "d", &rest)) {
      GMDL_Result parsed = mtl_parse_dissolve(rest, &material->d);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_D;
    }
    else if (gmdl_line_is(line_text, "illum", &rest)) {
      int value = 0;
      if (sscanf(rest, "%d", &value) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      material->illum = (int32_t)value;
      material->present |= GMDL_MTL_HAS_ILLUM;
    }
    else if (gmdl_line_is(line_text, "Ke", &rest)) {
      GMDL_Result parsed = mtl_parse_color(rest, material->Ke);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_KE;
    }
    else if (gmdl_line_is(line_text, "Tf", &rest)) {
      GMDL_Result parsed = mtl_parse_color(rest, material->Tf);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_TF;
    }
    else if (gmdl_line_is(line_text, "Ni", &rest)) {
      if (sscanf(rest, "%f", &material->Ni) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_NI;
    }
    else if (gmdl_line_is(line_text, "Tr", &rest)) {
      if (sscanf(rest, "%f", &material->Tr) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_TR;
    }
    else if (gmdl_line_is(line_text, "Pr", &rest)) {
      if (sscanf(rest, "%f", &material->Pr) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_PR;
    }
    else if (gmdl_line_is(line_text, "Pm", &rest)) {
      if (sscanf(rest, "%f", &material->Pm) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_PM;
    }
    else if (gmdl_line_is(line_text, "Ps", &rest)) {
      if (sscanf(rest, "%f", &material->Ps) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_PS;
    }
    else if (gmdl_line_is(line_text, "Pc", &rest)) {
      if (sscanf(rest, "%f", &material->Pc) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_PC;
    }
    else if (gmdl_line_is(line_text, "Pcr", &rest)) {
      if (sscanf(rest, "%f", &material->Pcr) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_PCR;
    }
    else if (gmdl_line_is(line_text, "aniso", &rest)) {
      if (sscanf(rest, "%f", &material->aniso) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_ANISO;
    }
    else if (gmdl_line_is(line_text, "anisor", &rest)) {
      if (sscanf(rest, "%f", &material->anisor) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      material->present |= GMDL_MTL_HAS_ANISOR;
    }
    else if (gmdl_line_is(line_text, "sharpness", &rest)) {
      int value = 0;
      if (sscanf(rest, "%d", &value) != 1) {
        result = GMDL_ERR_FORMAT;
        goto cleanup;
      }
      material->sharpness = (int32_t)value;
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
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Ka);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Kd", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Kd);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Ks", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Ks);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Ns", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Ns);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_d", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_d);
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
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_bump);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Ke", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Ke);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Pr", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Pr);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Pm", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Pm);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "map_Ps", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->map_Ps);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "norm", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->norm);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "disp", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->disp);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    else if (gmdl_line_is(line_text, "decal", &rest)) {
      GMDL_Result parsed = mtl_parse_map(rest, allocator, &material->decal);
      if (parsed != GMDL_OK) {
        result = parsed;
        goto cleanup;
      }
    }
    // "map_refl" is the same directive under the spelling Blender accepts.
    else if (gmdl_line_is(line_text, "refl", &rest)
        || gmdl_line_is(line_text, "map_refl", &rest)) {
      GMDL_Result parsed = mtl_parse_refl(rest, allocator, material);
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

/** Read an MTL document with the numeric locale pinned (number_internal.h). */
GMDL_Result gmdl_mtl_load(GMDL_Stream * stream, const GMDL_Limits * limits,
    const GMDL_Allocator * allocator, GMDL_Mtl ** out_mtl) {
  GMDL_Numeric_Scope numeric;
  gmdl_numeric_scope_begin(&numeric);
  GMDL_Result result = mtl_load_pinned(stream, limits, allocator, out_mtl);
  gmdl_numeric_scope_end(&numeric);
  return result;
}

GMDL_Result gmdl_mtl_load_file(const char * path, const GMDL_Limits * limits,
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
