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
 * Writing a parsed model back out as OBJ text.
 */

#include <stdbool.h>
#include <stdio.h>

#include <ghoti.io/model/macros.h>
#include <ghoti.io/model/obj.h>
#include "obj_internal.h"
#include "../core/number_internal.h"

/**
 * Print one "v", "v/vt", "v//vn" or "v/vt/vn" reference.
 *
 * The "+ 1" is done in a wider type on purpose. These are 0-based int32_t
 * indices and OBJ writes them 1-based, so an index of INT32_MAX - which
 * "f 2147483648" produces, and which 3.5 says to record rather than reject -
 * overflows a plain int. That is undefined behaviour in a function whose job
 * is to serialise whatever was read.
 */
static int obj_dump_reference(
    FILE * fd, int32_t vertex, int32_t texcoord, int32_t normal) {
  if (fprintf(fd, " %lld", (long long)vertex + 1) < 0) {
    return -1;
  }
  if (texcoord == -1 && normal == -1) {
    return 0;
  }
  if (fprintf(fd, "/") < 0) {
    return -1;
  }
  if (texcoord != -1 && fprintf(fd, "%lld", (long long)texcoord + 1) < 0) {
    return -1;
  }
  if (normal != -1 && fprintf(fd, "/%lld", (long long)normal + 1) < 0) {
    return -1;
  }
  return 0;
}

/** Print one face, including any vertices held in its overflow array. */
static int obj_dump_face(FILE * fd, const GMDL_Obj_Face * face) {
  if (fprintf(fd, "f") < 0) {
    return -1;
  }

  size_t inline_count = face->count > 4 ? 4 : face->count;
  for (size_t j = 0; j < inline_count; j++) {
    if (obj_dump_reference(
            fd, face->vertex[j], face->texcoord[j], face->normal[j]) < 0) {
      return -1;
    }
  }

  if (face->count > 4 && face->overflow) {
    size_t overflow_count = face->count - 4;
    for (size_t j = 0; j < overflow_count; j++) {
      if (obj_dump_reference(fd, face->overflow[j].vertex,
              face->overflow[j].texcoord, face->overflow[j].normal) < 0) {
        return -1;
      }
    }
  }

  return fprintf(fd, "\n") < 0 ? -1 : 0;
}

/**
 * The state the dump carries from one element to the next.
 *
 * `usemtl` and `s` are both state in the file, and the dump emits elements in
 * several runs - the faces before the first `g`, then each group, then the
 * polylines, then the points. The two need carrying for different reasons,
 * and only one of them is a correctness question.
 *
 * **Smoothing must be carried.** Zero is a real value and also the state a
 * file starts in, so a run that begins afresh writes nothing for a face at
 * zero - and if the run before it left `s 7` in force, the reload smooths
 * faces the source did not. Measured: a mutation that re-derives it per run
 * fails ObjSmoothing.SurvivesAcrossGroupBoundaries.
 *
 * **Material is carried for the output's sake.** Beginning afresh is still
 * *correct*, because -1 means "none" and is never written either way, so any
 * real material differs from the fresh state and gets its `usemtl`. What it
 * produces is a redundant line at every run boundary. Carrying it means the
 * file says what the model says and no more.
 */
typedef struct {
  int32_t material;  ///< Material index the file currently names.
  int32_t map;       ///< Texture map index the file currently names.
  int32_t smoothing; ///< Smoothing group currently in force.
  /**
   * The render attributes currently in force, as values rather than as an
   * index.
   *
   * The values are what is carried, because each of the four directives sets
   * one attribute on its own: moving between two states writes only the
   * lines that differ, and an index cannot say which those are.
   */
  GMDL_Obj_Render_State render;
  /**
   * The free-form state currently in force, as values for the same reason
   * the render attributes are: `cstype`, `deg`, `bmat` and `step` each set
   * one part of it, so moving between two states writes only the directives
   * that differ and an index cannot say which those are.
   */
  GMDL_Obj_Freeform_State freeform;
} obj_dump_state_t;

/** The free-form state a file starts in: nothing set (3.19). */
static const GMDL_Obj_Freeform_State obj_dump_freeform_none = {
    GMDL_OBJ_CSTYPE_NONE, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/**
 * Print a "usemtl" line naming a material.
 *
 * Per https://paulbourke.net/dataformats/obj/ an unnamed material renders
 * white, which is what a `material_index` naming no mapping becomes.
 */
static int obj_dump_material(
    FILE * fd, const GMDL_Obj * obj, int32_t material_index) {
  for (size_t j = 0; j < obj->material_mapping_count; j++) {
    if (obj->material_mappings[j].index == material_index) {
      return fprintf(fd, "usemtl %s\n", obj->material_mappings[j].name) < 0
          ? -1
          : 0;
    }
  }
  return fprintf(fd, "usemtl white\n") < 0 ? -1 : 0;
}

/**
 * Move the file's material to @p wanted, writing a `usemtl` if that changes
 * it.
 *
 * A material cannot be turned off, only changed, so an element that names
 * none while one is in force has no OBJ spelling: nothing is written and the
 * reload reads the previous material. Section 9 records that; it is not a
 * state a parse produces, since `material_index` only ever moves from -1 to a
 * mapping and never back.
 *
 * @param fd Destination.
 * @param obj The model.
 * @param wanted The material the element names.
 * @param state Carried state, updated.
 * @return 0, or -1 on a write failure.
 */
static int obj_dump_material_change(
    FILE * fd, const GMDL_Obj * obj, int32_t wanted, obj_dump_state_t * state) {
  if (wanted == state->material) {
    return 0;
  }
  state->material = wanted;
  if (wanted == -1) {
    return 0;
  }
  return obj_dump_material(fd, obj, wanted);
}

/**
 * Move the file's texture map to @p wanted, writing a `usemap` if that
 * changes it.
 *
 * This is obj_dump_material_change() without its hard case. A map **can** be
 * turned off - `usemap off` is the format's own spelling for it - so an
 * element naming none after one that named a map is writable, and the
 * lines-and-points split that materials need (below) buys maps nothing.
 *
 * An index naming no mapping is written as `usemap off`, because "a map this
 * model cannot name" and "no map" read back identically and only one of them
 * has a spelling. That differs from the material fallback, which writes
 * `usemtl white`: the format gives an unnamed material a defined appearance
 * and gives an unnamed map nothing.
 *
 * @param fd Destination.
 * @param obj The model.
 * @param wanted The map the element names.
 * @param state Carried state, updated.
 * @return 0, or -1 on a write failure.
 */
static int obj_dump_map_change(
    FILE * fd, const GMDL_Obj * obj, int32_t wanted, obj_dump_state_t * state) {
  if (wanted == state->map) {
    return 0;
  }
  state->map = wanted;
  if (wanted != -1) {
    for (size_t j = 0; j < obj->map_mapping_count; j++) {
      if (obj->map_mappings[j].index == wanted) {
        return fprintf(fd, "usemap %s\n", obj->map_mappings[j].name) < 0 ? -1
                                                                        : 0;
      }
    }
  }
  return fprintf(fd, "usemap off\n") < 0 ? -1 : 0;
}

/**
 * Move the file's render attributes to the state @p wanted names, writing a
 * line for each attribute that changes.
 *
 * Only the attributes that differ are written, which is what makes the dump
 * say what the model says and no more - the same reason the material is
 * carried across ranges rather than re-derived. Every attribute has a
 * spelling for its default (`off`, and `lod 0`), so unlike a material this
 * can always be written, in either direction.
 *
 * An index naming no record is the all-defaults state, which is also what -1
 * means. No parse produces the first - every index comes from a record the
 * parser made - but a model built through the struct can hold it, and
 * reading it as "defaults" is the only answer that round-trips.
 *
 * @param fd Destination.
 * @param obj The model.
 * @param wanted The render index the element carries.
 * @param state Carried state, updated.
 * @return 0, or -1 on a write failure.
 */
static int obj_dump_render_change(
    FILE * fd, const GMDL_Obj * obj, int32_t wanted, obj_dump_state_t * state) {
  GMDL_Obj_Render_State target = {false, false, false, 0};
  if (wanted >= 0 && (size_t)wanted < obj->render_state_count) {
    target = obj->render_states[wanted];
  }
  if (target.bevel != state->render.bevel
      && fprintf(fd, "bevel %s\n", target.bevel ? "on" : "off") < 0) {
    return -1;
  }
  if (target.c_interp != state->render.c_interp
      && fprintf(fd, "c_interp %s\n", target.c_interp ? "on" : "off") < 0) {
    return -1;
  }
  if (target.d_interp != state->render.d_interp
      && fprintf(fd, "d_interp %s\n", target.d_interp ? "on" : "off") < 0) {
    return -1;
  }
  if (target.lod != state->render.lod
      && fprintf(fd, "lod %d\n", target.lod) < 0) {
    return -1;
  }
  state->render = target;
  return 0;
}

/**
 * Print a run of faces, emitting "usemtl" and "s" whenever they change.
 *
 * @param fd Destination.
 * @param obj The model.
 * @param start First face.
 * @param count How many.
 * @param state The material and smoothing group the file currently names,
 *   carried across calls and updated. A range cannot assume either starts
 *   fresh: the run before it may have left one set, and re-deriving them per
 *   range is how a dump that reads back differently gets written.
 * @return 0, or -1 on a write failure.
 */
static int obj_dump_face_range(FILE * fd, const GMDL_Obj * obj, size_t start,
    size_t count, obj_dump_state_t * state) {
  for (size_t i = start; i < start + count && i < obj->face_count; i++) {
    if (obj_dump_material_change(fd, obj, obj->faces[i].material_index, state)
        < 0) {
      return -1;
    }
    if (obj_dump_map_change(fd, obj, obj->faces[i].map_index, state) < 0) {
      return -1;
    }
    if (obj_dump_render_change(fd, obj, obj->faces[i].render_index, state) < 0) {
      return -1;
    }
    if (obj->faces[i].smoothing_group != state->smoothing) {
      state->smoothing = obj->faces[i].smoothing_group;
      // "off" rather than "0": both parse to zero, and it is the spelling
      // the format leads with.
      int written = state->smoothing == 0
          ? fprintf(fd, "s off\n")
          : fprintf(fd, "s %d\n", state->smoothing);
      if (written < 0) {
        return -1;
      }
    }
    if (obj_dump_face(fd, &obj->faces[i]) < 0) {
      return -1;
    }
  }
  return 0;
}

/**
 * Write one `bmat` line, if the span in force has changed.
 *
 * Spans are compared rather than values. Within one parse that is exact: a
 * `bmat` line appends once and every element declared while it is in force
 * copies the same start and count, so two elements share a span exactly when
 * they share a matrix. For a model assembled through the struct it is
 * conservative in the harmless direction - two equal matrices at different
 * spans write the line twice, which the reload reads back the same.
 */
static int obj_dump_basis(FILE * fd, const GMDL_Obj * obj, const char * axis,
    size_t start, size_t count, size_t * carried_start,
    size_t * carried_count) {
  if (start == *carried_start && count == *carried_count) {
    return 0;
  }
  *carried_start = start;
  *carried_count = count;
  // A span of nothing is "no basis in force", which the format has no
  // spelling for - the same shape as an element naming no material while one
  // is in force (section 9). Nothing is written and the reload keeps what it
  // had; a parse cannot produce it, since `bmat` only ever sets a basis.
  if (count == 0 || start + count > obj->basis_value_count) {
    return 0;
  }
  if (fprintf(fd, "bmat %s", axis) < 0) {
    return -1;
  }
  for (size_t i = 0; i < count; i++) {
    if (fprintf(fd, " %.9g", obj->basis_values[start + i]) < 0) {
      return -1;
    }
  }
  return fprintf(fd, "\n") < 0 ? -1 : 0;
}

/**
 * Write a `deg` or `step` line, if that pair has changed.
 *
 * How many numbers to write comes from the state's own count, not from
 * testing a number against a sentinel. Every `int32_t` is a value some file
 * can write - `step -1 1` among them - so a sentinel here made a real state
 * indistinguishable from "none in force", and the dump wrote nothing for it
 * (3.19).
 */
static int obj_dump_int_pair(FILE * fd, const char * directive, int32_t u,
    int32_t v, int32_t count, int32_t * carried_u, int32_t * carried_v,
    int32_t * carried_count) {
  if (u == *carried_u && v == *carried_v && count == *carried_count) {
    return 0;
  }
  *carried_u = u;
  *carried_v = v;
  *carried_count = count;
  if (count <= 0) {
    return 0; // "The file set none", which has no spelling.
  }
  int written = count == 1 ? fprintf(fd, "%s %d\n", directive, u)
                           : fprintf(fd, "%s %d %d\n", directive, u, v);
  return written < 0 ? -1 : 0;
}

/**
 * Move the file's free-form state to @p wanted, writing only the directives
 * that differ.
 *
 * Each of the four sets one part of the state and the parts are independent,
 * so this is the render-attribute writer's shape rather than the material
 * one's: there is no single line that says "all of it".
 *
 * A state this cannot reach writes nothing for that part, and the reload
 * keeps what it had. None of those is a state a parse produces - `cstype`,
 * `deg`, `step` and `bmat` each only ever set a value, and the format gives
 * none of them an "off" - so this is the same class as an element naming no
 * material while one is in force (section 9).
 *
 * @param fd Destination.
 * @param obj The model, for the basis values.
 * @param wanted The state the element was declared in.
 * @param state Carried state, updated.
 * @return 0, or -1 on a write failure.
 */
static int obj_dump_freeform_change(FILE * fd, const GMDL_Obj * obj,
    const GMDL_Obj_Freeform_State * wanted, obj_dump_state_t * state) {
  if (wanted->type != state->freeform.type
      || wanted->rational != state->freeform.rational) {
    state->freeform.type = wanted->type;
    state->freeform.rational = wanted->rational;
    const char * name = gmdl_cstype_name(wanted->type);
    if (name
        && fprintf(fd, "cstype %s%s\n", wanted->rational ? "rat " : "", name)
            < 0) {
      return -1;
    }
  }
  if (obj_dump_int_pair(fd, "deg", wanted->degree_u, wanted->degree_v,
          wanted->degree_count, &state->freeform.degree_u,
          &state->freeform.degree_v, &state->freeform.degree_count)
      < 0) {
    return -1;
  }
  if (obj_dump_int_pair(fd, "step", wanted->step_u, wanted->step_v,
          wanted->step_count, &state->freeform.step_u,
          &state->freeform.step_v, &state->freeform.step_count)
      < 0) {
    return -1;
  }
  if (obj_dump_basis(fd, obj, "u", wanted->basis_u_start,
          wanted->basis_u_count, &state->freeform.basis_u_start,
          &state->freeform.basis_u_count)
      < 0) {
    return -1;
  }
  return obj_dump_basis(fd, obj, "v", wanted->basis_v_start,
      wanted->basis_v_count, &state->freeform.basis_v_start,
      &state->freeform.basis_v_count);
}

/**
 * Write one curve reference, as `trim`, `hole`, `scrv` and `con` spell it.
 */
static int obj_dump_curve_ref(FILE * fd, const GMDL_Obj_Curve_Ref * ref) {
  // +1 as a long long for the reason obj_dump_reference() gives: an index
  // held at INT32_MIN because the file wrote one too large to represent
  // overflows a plain int on the way back out.
  return fprintf(fd, " %.9g %.9g %lld", ref->u0, ref->u1,
             (long long)ref->curve2d + 1)
      < 0
      ? -1
      : 0;
}

/**
 * Write one body statement of a free-form element (3.19).
 *
 * **A span that leaves its array writes nothing at all**, the way
 * obj_dump_basis() handles the same case and for a sharper reason: writing
 * the directive and then no entries would produce a line - `trim` alone -
 * that this parser refuses, so a dump of a hand-built model would fail to
 * reload rather than losing a statement it could not spell. A parse cannot
 * produce such a span; a caller assembling a model by hand can.
 *
 * @param fd The destination.
 * @param obj The model, for whichever array the kind selects.
 * @param body The statement.
 * @return 0, or -1 on a write failure.
 */
static int obj_dump_body(
    FILE * fd, const GMDL_Obj * obj, const GMDL_Obj_Freeform_Body * body) {
  const char * directive = gmdl_body_kind_name(body->kind);
  if (!directive) {
    // A kind the table does not cover, which a parse cannot produce and a
    // caller writing a number into the field can. Nothing is written, the
    // way an out-of-range span writes nothing: there is no directive to
    // spell it with, and inventing one would put a statement in the file
    // that the model does not hold.
    return 0;
  }
  size_t available = obj->curve_ref_count;
  if (body->kind == GMDL_OBJ_BODY_PARM_U
      || body->kind == GMDL_OBJ_BODY_PARM_V) {
    available = obj->parm_value_count;
  }
  else if (body->kind == GMDL_OBJ_BODY_SP) {
    available = obj->special_point_count;
  }
  if (body->count == 0 || body->start > available
      || body->count > available - body->start) {
    return 0;
  }

  if (fprintf(fd, "%s", directive) < 0) {
    return -1;
  }
  for (size_t i = 0; i < body->count; i++) {
    size_t at = body->start + i;
    int written = 0;
    if (body->kind == GMDL_OBJ_BODY_PARM_U
        || body->kind == GMDL_OBJ_BODY_PARM_V) {
      written = fprintf(fd, " %.9g", obj->parm_values[at]);
    }
    else if (body->kind == GMDL_OBJ_BODY_SP) {
      written = fprintf(fd, " %lld", (long long)obj->special_points[at] + 1);
    }
    else if (obj_dump_curve_ref(fd, &obj->curve_refs[at]) < 0) {
      return -1;
    }
    if (written < 0) {
      return -1;
    }
  }
  return fprintf(fd, "\n") < 0 ? -1 : 0;
}

/**
 * Write one free-form element, and the `end` that closes it.
 *
 * `end` is written although nothing records it: the specification requires
 * it, and a reader that needs it to know where an element stops would
 * otherwise read the next directive as part of this one.
 */
static int obj_dump_freeform(
    FILE * fd, const GMDL_Obj * obj, const GMDL_Obj_Freeform * element) {
  const char * directive = element->kind == GMDL_OBJ_CURVE2 ? "curv2"
      : (element->kind == GMDL_OBJ_SURFACE                  ? "surf"
                                                            : "curv");
  if (fprintf(fd, "%s", directive) < 0) {
    return -1;
  }
  size_t ranges = element->kind == GMDL_OBJ_SURFACE ? 4u
      : (element->kind == GMDL_OBJ_CURVE            ? 2u
                                                    : 0u);
  for (size_t i = 0; i < ranges; i++) {
    if (fprintf(fd, " %.9g", element->range[i]) < 0) {
      return -1;
    }
  }
  for (size_t j = 0; j < element->count; j++) {
    const GMDL_Obj_Freeform_Vertex * ref =
        &obj->freeform_vertices[element->start + j];
    if (obj_dump_reference(fd, ref->vertex, ref->texcoord, ref->normal) < 0) {
      return -1;
    }
  }
  if (fprintf(fd, "\n") < 0) {
    return -1;
  }
  // The body statements, in the order the file wrote them - `trim` and
  // `hole` interleave to describe a surface with holes in it, and each line
  // is its own loop (3.19).
  for (size_t k = 0; k < element->body_count; k++) {
    size_t at = element->body_start + k;
    if (at >= obj->freeform_body_count) {
      break; // A hand-built span past the array; see obj_dump_body().
    }
    if (obj_dump_body(fd, obj, &obj->freeform_bodies[at]) < 0) {
      return -1;
    }
  }
  return fprintf(fd, "end\n") < 0 ? -1 : 0;
}

/**
 * Print the polylines and the points, which no group covers.
 *
 * Called twice, because OBJ cannot turn a material off. An element declared
 * before the file's first `usemtl` carries -1, and once anything has set a
 * material there is no way to write that element and have it read back the
 * same. The elements holding -1 therefore go out **before** the faces, while
 * nothing is in force yet, and the rest after.
 *
 * That split preserves the arrays' own order rather than disturbing it:
 * within one parse `material_index` moves from -1 to a mapping and never
 * back, so the -1 entries are a prefix of each array and the two passes
 * emit the prefix and then the remainder.
 *
 * Found by the fuzzer within ninety seconds of the material being added to
 * these elements: "l 1 2" before any usemtl, then a usemtl and a face, and
 * the polyline came back carrying the face's material.
 *
 * @param fd Destination.
 * @param obj The model.
 * @param state Carried material and smoothing state.
 * @param unmaterialed true for the elements naming no material, false for
 *   the rest.
 * @return 0, or -1 on a write failure.
 */
static int obj_dump_nonface_elements(FILE * fd, const GMDL_Obj * obj,
    obj_dump_state_t * state, bool unmaterialed) {
  for (size_t i = 0; i < obj->line_count; i++) {
    if ((obj->lines[i].material_index == -1) != unmaterialed) {
      continue;
    }
    if (obj_dump_material_change(fd, obj, obj->lines[i].material_index, state)
        < 0) {
      return -1;
    }
    if (obj_dump_map_change(fd, obj, obj->lines[i].map_index, state) < 0) {
      return -1;
    }
    if (obj_dump_render_change(fd, obj, obj->lines[i].render_index, state) < 0) {
      return -1;
    }
    if (fprintf(fd, "l") < 0) {
      return -1;
    }
    const GMDL_Obj_Line * element = &obj->lines[i];
    for (size_t j = 0; j < element->count; j++) {
      const GMDL_Obj_Line_Vertex * entry =
          &obj->line_vertices[element->start + j];
      int written = entry->texcoord == -1
          ? fprintf(fd, " %lld", (long long)entry->vertex + 1)
          : fprintf(fd, " %lld/%lld", (long long)entry->vertex + 1,
                (long long)entry->texcoord + 1);
      if (written < 0) {
        return -1;
      }
    }
    if (fprintf(fd, "\n") < 0) {
      return -1;
    }
  }

  // One statement per point. The format allows several on a line and the
  // grouping means nothing once parsed, so there is nothing to preserve.
  for (size_t i = 0; i < obj->point_count; i++) {
    if ((obj->points[i].material_index == -1) != unmaterialed) {
      continue;
    }
    if (obj_dump_material_change(fd, obj, obj->points[i].material_index, state)
        < 0) {
      return -1;
    }
    if (obj_dump_map_change(fd, obj, obj->points[i].map_index, state) < 0) {
      return -1;
    }
    if (obj_dump_render_change(fd, obj, obj->points[i].render_index, state) < 0) {
      return -1;
    }
    if (fprintf(fd, "p %lld\n", (long long)obj->points[i].vertex + 1) < 0) {
      return -1;
    }
  }

  // The free-form curves and surfaces, written here rather than among the
  // faces for the reason the polylines are: they take a material, and an
  // element naming none has to be written while none is in force.
  for (size_t i = 0; i < obj->freeform_count; i++) {
    if ((obj->freeforms[i].material_index == -1) != unmaterialed) {
      continue;
    }
    if (obj_dump_material_change(
            fd, obj, obj->freeforms[i].material_index, state)
        < 0) {
      return -1;
    }
    if (obj_dump_map_change(fd, obj, obj->freeforms[i].map_index, state) < 0) {
      return -1;
    }
    if (obj_dump_render_change(fd, obj, obj->freeforms[i].render_index, state)
        < 0) {
      return -1;
    }
    if (obj_dump_freeform_change(fd, obj, &obj->freeforms[i].state, state)
        < 0) {
      return -1;
    }
    if (obj_dump_freeform(fd, obj, &obj->freeforms[i]) < 0) {
      return -1;
    }
  }
  return 0;
}

static GMDL_Result obj_dump_pinned(const GMDL_Obj * obj, FILE * fd) {
  if (!obj || !fd) {
    return GMDL_ERR_INVALID;
  }

  // Every library the document named, in the order it named them. Writing
  // only obj->mtllib here would drop the rest on a round trip, which is the
  // same loss the single field used to cause on the way in.
  for (size_t i = 0; i < obj->mtllib_count; i++) {
    if (fprintf(fd, "mtllib %s\n", obj->mtllibs[i].path) < 0) {
      return GMDL_ERR_IO;
    }
  }
  // A model built by hand through the struct may set the compatibility field
  // without building a list, so that case still writes a line.
  if (obj->mtllib_count == 0 && obj->mtllib[0] != '\0'
      && fprintf(fd, "mtllib %s\n", obj->mtllib) < 0) {
    return GMDL_ERR_IO;
  }

  // The texture map libraries, read and written the way the material ones
  // are (3.15).
  for (size_t i = 0; i < obj->maplib_count; i++) {
    if (fprintf(fd, "maplib %s\n", obj->maplibs[i].path) < 0) {
      return GMDL_ERR_IO;
    }
  }

  // The shadow and ray-tracing objects (3.17). Written here with the other
  // paths rather than among the elements: the specification calls them one
  // per file, so there is no position among the geometry to preserve.
  for (size_t i = 0; i < obj->shadow_obj_count; i++) {
    if (fprintf(fd, "shadow_obj %s\n", obj->shadow_objs[i].path) < 0) {
      return GMDL_ERR_IO;
    }
  }
  for (size_t i = 0; i < obj->trace_obj_count; i++) {
    if (fprintf(fd, "trace_obj %s\n", obj->trace_objs[i].path) < 0) {
      return GMDL_ERR_IO;
    }
  }

  for (size_t i = 0; i < obj->vertex_count; i++) {
    // A vertex whose colour is absent is written without one even in a file
    // that has colours, because that is what the file said and writing white
    // instead would turn "no colour here" into a colour on the way out.
    const GMDL_Obj_Color * color
        = (obj->colors && i < obj->color_count && obj->colors[i].present)
        ? &obj->colors[i]
        : NULL;
    int written = color
        ? fprintf(fd, "v %.9g %.9g %.9g %.9g %.9g %.9g\n", obj->vertices[i].x,
            obj->vertices[i].y, obj->vertices[i].z, color->r, color->g,
            color->b)
        : fprintf(fd, "v %.9g %.9g %.9g\n", obj->vertices[i].x,
            obj->vertices[i].y, obj->vertices[i].z);
    if (written < 0) {
      return GMDL_ERR_IO;
    }
  }
  for (size_t i = 0; i < obj->texcoord_count; i++) {
    if (fprintf(fd, "vt %.9g %.9g\n", obj->texcoords[i].u, obj->texcoords[i].v)
        < 0) {
      return GMDL_ERR_IO;
    }
  }
  for (size_t i = 0; i < obj->normal_count; i++) {
    if (fprintf(fd, "vn %.9g %.9g %.9g\n", obj->normals[i].x, obj->normals[i].y,
            obj->normals[i].z) < 0) {
      return GMDL_ERR_IO;
    }
  }

  // The parameter-space control points (3.19). Written with exactly as many
  // numbers as the line that made them carried: a `vp` with one number is a
  // point on a curve and one with two is a point on a surface, so padding
  // out to three would change what the statement says - and a reader that
  // then counted them, as this one does, would come back with a different
  // model. A count outside 1 to 3 cannot come from a parse; a model built by
  // hand through the struct can hold one, and it writes the `u` it is sure
  // of rather than reading past the record.
  for (size_t i = 0; i < obj->param_vertex_count; i++) {
    const GMDL_Obj_Param_Vertex * vp = &obj->param_vertices[i];
    int written = vp->count >= 3
        ? fprintf(fd, "vp %.9g %.9g %.9g\n", vp->u, vp->v, vp->w)
        : (vp->count == 2 ? fprintf(fd, "vp %.9g %.9g\n", vp->u, vp->v)
                          : fprintf(fd, "vp %.9g\n", vp->u));
    if (written < 0) {
      return GMDL_ERR_IO;
    }
  }

  // The parser starts with no material, no texture map, no smoothing group
  // and every render attribute at its default, so the dump does too; a model
  // whose faces all say zero writes no "s" at all, and one that never
  // mentions the render attributes writes none of them.
  obj_dump_state_t state = {
      -1, -1, 0, {false, false, false, 0}, obj_dump_freeform_none};

  // The general statements lead, in file order. Their position relative to
  // the geometry is not recorded - nothing else in this model is ordered
  // against the geometry either - and they are text this library never acts
  // on, so writing them first costs nothing a consumer can observe.
  for (size_t i = 0; i < obj->statement_count; i++) {
    const char * directive =
        obj->statements[i].kind == GMDL_OBJ_STATEMENT_CALL ? "call" : "csh";
    if (fprintf(fd, "%s %s\n", directive, obj->statements[i].text) < 0) {
      return GMDL_ERR_IO;
    }
  }

  // The free-form approximation directives, in file order (3.18). With them
  // and the statements above, everything this dump writes before the
  // elements is text it never acted on.
  for (size_t i = 0; i < obj->freeform_attr_count; i++) {
    const char * directive = "mg";
    if (obj->freeform_attrs[i].kind == GMDL_OBJ_FREEFORM_CTECH) {
      directive = "ctech";
    }
    else if (obj->freeform_attrs[i].kind == GMDL_OBJ_FREEFORM_STECH) {
      directive = "stech";
    }
    if (fprintf(fd, "%s %s\n", directive, obj->freeform_attrs[i].text) < 0) {
      return GMDL_ERR_IO;
    }
  }

  // Everything naming no material first, while none is in force. See
  // obj_dump_nonface_elements().
  if (obj_dump_nonface_elements(fd, obj, &state, true) < 0) {
    return GMDL_ERR_IO;
  }

  if (obj->group_count > 0) {
    // Faces declared before the first "g" belong to no group (3.6). The
    // group loop below only walks group ranges, so without this they were
    // written nowhere and the dump did not round-trip - silently, because
    // the reload succeeded and simply had fewer faces.
    if (obj->groups[0].start_face > 0
        && obj_dump_face_range(
               fd, obj, 0, obj->groups[0].start_face, &state)
            < 0) {
      return GMDL_ERR_IO;
    }
    for (size_t g = 0; g < obj->group_count; g++) {
      // The spelling the file used, so `o` does not become `g` on the way
      // out: Blender makes an object of one and a vertex group of the other.
      if (fprintf(fd, "%s %s\n", obj->groups[g].is_object ? "o" : "g",
              obj->groups[g].name)
          < 0) {
        return GMDL_ERR_IO;
      }
      if (obj_dump_face_range(fd, obj, obj->groups[g].start_face,
              obj->groups[g].face_count, &state)
          < 0) {
        return GMDL_ERR_IO;
      }
    }
  }
  else if (obj_dump_face_range(fd, obj, 0, obj->face_count, &state) < 0) {
    return GMDL_ERR_IO;
  }

  if (obj_dump_nonface_elements(fd, obj, &state, false) < 0) {
    return GMDL_ERR_IO;
  }

  // The connections last, because each names surfaces by their ordinal and
  // every `surf` has been written by now. A `con` before them would name
  // patches a reader had not seen, which the format allows and no reader
  // should have to accommodate.
  for (size_t i = 0; i < obj->connection_count; i++) {
    const GMDL_Obj_Connection_End * ends[2] = {
        &obj->connections[i].a, &obj->connections[i].b};
    if (fprintf(fd, "con") < 0) {
      return GMDL_ERR_IO;
    }
    for (size_t e = 0; e < 2; e++) {
      if (fprintf(fd, " %lld", (long long)ends[e]->surface + 1) < 0
          || obj_dump_curve_ref(fd, &ends[e]->curve) < 0) {
        return GMDL_ERR_IO;
      }
    }
    if (fprintf(fd, "\n") < 0) {
      return GMDL_ERR_IO;
    }
  }

  return GMDL_OK;
}


/**
 * Write an OBJ document with the numeric locale pinned.
 *
 * Every coordinate this writes goes through printf("%.9g"), which consults
 * LC_NUMERIC. Without the pin a caller in a comma locale gets
 * "v 0,5 0,5 0,5" - a file no reader accepts, produced by a function that
 * reported success. See src/core/number_internal.h.
 */
GMDL_Result gmdl_obj_dump(const GMDL_Obj * obj, FILE * fd) {
  GMDL_Numeric_Scope numeric;
  gmdl_numeric_scope_begin(&numeric);
  GMDL_Result result = obj_dump_pinned(obj, fd);
  gmdl_numeric_scope_end(&numeric);
  return result;
}
