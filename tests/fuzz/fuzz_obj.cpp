/**
 * @file
 *
 * libFuzzer harness for the OBJ parser.
 *
 * The input is parsed straight from memory - no temporary file - which is the
 * reason the parsers take a stream. Arbitrary or truncated input must not
 * crash; it should come back as GMDL_ERR_FORMAT, GMDL_ERR_LIMIT, or another
 * result code.
 *
 * A model that comes back GMDL_OK is also dumped, re-parsed, and compared
 * against the original, which is the structural round-trip section 9
 * promises. Two things are deliberately outside that comparison, because OBJ
 * cannot express them rather than because the dumper is wrong; each is
 * explained where it is handled.
 *
 * Build with: make fuzz-obj
 * Run:        make fuzz-run-obj FUZZ_TIME=300
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <ghoti.io/model/model.h>

#include "../failing_allocator.h"

namespace {

/** Report a broken invariant and stop, so libFuzzer records the input. */
[[noreturn]] void broken(const char * what) {
  std::fprintf(stderr, "round-trip invariant broken: %s\n", what);
  std::abort();
}

#define REQUIRE(cond, what) \
  do {                      \
    if (!(cond)) {          \
      broken(what);         \
    }                       \
  } while (0)

/** NaN is never equal to itself, and a NaN coordinate is legal input (3.4). */
bool same_float(float a, float b) {
  return (std::isnan(a) && std::isnan(b)) || a == b;
}

/**
 * Whether every index in the model is one the dump can spell.
 *
 * An index of k >= 0 is written as k + 1 and read back as k, and -1 (absent)
 * is written as 0 and read back as -1. An index of -2 or below - which only
 * a relative index reaching past the beginning of the file can produce, and
 * which 3.5 says to record rather than reject - is written as a negative
 * number, and OBJ reads a negative index as relative. There is no OBJ
 * spelling for it, so the index comparison does not apply to such a model.
 *
 * **Every element kind that carries an index has to be named here.** One
 * that is not makes the harness call the format's own limit a library bug,
 * and it does it on a real input rather than a rare one. tools/check-lists.py
 * holds this function to the set of index-carrying types in obj.h, because
 * nothing else could: the omission looks exactly like a passing harness
 * until the fuzzer reaches the shape.
 */
bool indices_are_representable(const GMDL_Obj * obj) {
  for (size_t i = 0; i < obj->face_count; i++) {
    const GMDL_Obj_Face * face = &obj->faces[i];
    size_t inline_count = face->count < 4 ? face->count : 4;
    for (size_t j = 0; j < inline_count; j++) {
      if (face->vertex[j] < -1 || face->texcoord[j] < -1
          || face->normal[j] < -1) {
        return false;
      }
    }
    if (face->overflow) {
      for (size_t j = 4; j < face->count; j++) {
        const GMDL_Obj_Face_Overflow * extra = &face->overflow[j - 4];
        if (extra->vertex < -1 || extra->texcoord < -1 || extra->normal < -1) {
          return false;
        }
      }
    }
  }
  // Polylines and points resolve their indices exactly as faces do, so the
  // same exemption reaches them: a relative index pointing past the start of
  // the file has no OBJ spelling either way.
  for (size_t i = 0; i < obj->line_vertex_count; i++) {
    if (obj->line_vertices[i].vertex < -1
        || obj->line_vertices[i].texcoord < -1) {
      return false;
    }
  }
  for (size_t i = 0; i < obj->point_count; i++) {
    if (obj->points[i].vertex < -1) {
      return false;
    }
  }
  // Free-form elements resolve their control points exactly as faces do, so
  // the same exemption reaches them. This clause was missing when `curv`,
  // `curv2` and `surf` were added, and the fuzzer found it in 150 seconds:
  // `v` three times and then `curv 0 1 -10` gives a reference of -7, which
  // the dump writes as -6 and the reload reads as -3. That is the documented
  // behaviour of an index below -1 (section 9), not a defect - but the
  // harness has to know it, and adding a fourth element kind without
  // touching this list makes the harness report the format's own limit as a
  // library bug.
  for (size_t i = 0; i < obj->freeform_vertex_count; i++) {
    const GMDL_Obj_Freeform_Vertex * ref = &obj->freeform_vertices[i];
    if (ref->vertex < -1 || ref->texcoord < -1 || ref->normal < -1) {
      return false;
    }
  }
  // The body statements and `con` carry indices too, into populations of
  // their own: a GMDL_Obj_Curve_Ref names a `curv2` by ordinal, a
  // GMDL_Obj_Connection_End names a `surf`, and `sp` names a `vp`. All three
  // resolve the same way and so reach the same below--1 case. They are here
  // because check-lists.py required it: the first version of that gate
  // recognised an index by the field name `vertex` and could not see a
  // `curve2d` or a `surface`, which is how the previous element kind got in
  // without this list.
  for (size_t i = 0; i < obj->curve_ref_count; i++) {
    if (obj->curve_refs[i].curve2d < -1) {
      return false;
    }
  }
  for (size_t i = 0; i < obj->connection_count; i++) {
    const GMDL_Obj_Connection_End * ends[2] = {
        &obj->connections[i].a, &obj->connections[i].b};
    for (size_t e = 0; e < 2; e++) {
      if (ends[e]->surface < -1 || ends[e]->curve.curve2d < -1) {
        return false;
      }
    }
  }
  for (size_t i = 0; i < obj->special_point_count; i++) {
    if (obj->special_points[i] < -1) {
      return false;
    }
  }
  return true;
}

/** Whether a name would be the last thing on its line and end in a backslash. */
bool ends_with_backslash(const char * name) {
  size_t length = strlen(name);
  return length > 0 && name[length - 1] == '\\';
}

/**
 * Whether every name the dump will write can be read back.
 *
 * A name is written last on its line, so one ending in a backslash lands
 * where 2.6 reads a line continuation. Re-reading it joins the following
 * line into the name, or at end of file drops the backslash and with it the
 * name. OBJ has no escape for a trailing backslash - doubling it just moves
 * the continuation - so a model holding such a name is outside the round
 * trip in the same way as an index below -1: not because the dumper is
 * wrong, but because the format cannot say it.
 *
 * Such a name only arises from a line ending in two backslashes, which no
 * real file contains; it is reachable here because the fuzzer writes bytes
 * rather than files.
 */
bool names_are_representable(const GMDL_Obj * obj) {
  if (ends_with_backslash(obj->mtllib)) {
    return false;
  }
  // The list, not only the compatibility scalar: that scalar is the FIRST
  // path, so checking it alone leaves every later `mtllib` line unexamined.
  for (size_t i = 0; i < obj->mtllib_count; i++) {
    if (ends_with_backslash(obj->mtllibs[i].path)) {
      return false;
    }
  }
  for (size_t i = 0; i < obj->maplib_count; i++) {
    if (ends_with_backslash(obj->maplibs[i].path)) {
      return false;
    }
  }
  for (size_t i = 0; i < obj->shadow_obj_count; i++) {
    if (ends_with_backslash(obj->shadow_objs[i].path)) {
      return false;
    }
  }
  for (size_t i = 0; i < obj->trace_obj_count; i++) {
    if (ends_with_backslash(obj->trace_objs[i].path)) {
      return false;
    }
  }
  for (size_t i = 0; i < obj->map_mapping_count; i++) {
    if (ends_with_backslash(obj->map_mappings[i].name)) {
      return false;
    }
  }
  for (size_t i = 0; i < obj->group_count; i++) {
    if (ends_with_backslash(obj->groups[i].name)) {
      return false;
    }
  }
  for (size_t i = 0; i < obj->material_mapping_count; i++) {
    if (ends_with_backslash(obj->material_mappings[i].name)) {
      return false;
    }
  }
  // A recorded "call" or "csh" is written last on its line like any other
  // text, so a trailing backslash continues into whatever follows.
  for (size_t i = 0; i < obj->statement_count; i++) {
    if (ends_with_backslash(obj->statements[i].text)) {
      return false;
    }
  }
  return true;
}

/**
 * The material a face uses, by name.
 *
 * Faces are compared by name rather than by index because a mapping that no
 * face uses is never written - the dumper emits "usemtl" only where the
 * material changes between faces - so material_mapping_count is not stable
 * across a round trip and the assignment that matters still is.
 */
const char * material_name(const GMDL_Obj * obj, int32_t index) {
  if (index < 0) {
    return "";
  }
  for (size_t i = 0; i < obj->material_mapping_count; i++) {
    if (obj->material_mappings[i].index == index) {
      return obj->material_mappings[i].name;
    }
  }
  return "?unmapped";
}

/**
 * The texture map an element uses, by name.
 *
 * By name for the reason material_name() is, and with one extra: a mapping
 * no element uses is never written, so map_mapping_count is not stable
 * across a round trip either. An index naming no mapping cannot come from a
 * parse - every index the parser hands out comes from a mapping it made -
 * so "?unmapped" here means the parser is wrong, and the reparse would
 * answer "" because the dump writes `usemap off` for that state.
 */
const char * map_name(const GMDL_Obj * obj, int32_t index) {
  if (index < 0) {
    return "";
  }
  for (size_t i = 0; i < obj->map_mapping_count; i++) {
    if (obj->map_mappings[i].index == index) {
      return obj->map_mappings[i].name;
    }
  }
  return "?unmapped";
}

/**
 * The render attributes an element names, by value.
 *
 * By value for the reason material_name() compares names: a record no
 * element uses is never written, so render_state_count is not stable across
 * a round trip. An index naming no record is the all-defaults state, which
 * is what -1 means and what the dump writes it back as.
 */
GMDL_Obj_Render_State render_state_of(const GMDL_Obj * obj, int32_t index) {
  if (index >= 0 && (size_t)index < obj->render_state_count) {
    return obj->render_states[index];
  }
  return GMDL_Obj_Render_State{false, false, false, 0};
}

/** Field by field: three bools and an int32_t leave padding memcmp reads. */
bool same_render(const GMDL_Obj_Render_State & a,
    const GMDL_Obj_Render_State & b) {
  return a.bevel == b.bevel && a.c_interp == b.c_interp
      && a.d_interp == b.d_interp && a.lod == b.lod;
}

/**
 * Whether two free-form states hold the same thing, values and all.
 *
 * The basis matrices are compared by **value**, not by span. The dump writes
 * a `bmat` line per distinct span and the reparse numbers its spans from
 * scratch, so two models describing the same matrix hold it at different
 * offsets - comparing starts would fail on every document that carries one,
 * and comparing counts alone would pass for two different matrices of the
 * same length.
 */
bool same_freeform_state(const GMDL_Obj * a_obj,
    const GMDL_Obj_Freeform_State & a, const GMDL_Obj * b_obj,
    const GMDL_Obj_Freeform_State & b) {
  if (a.type != b.type || a.rational != b.rational || a.degree_u != b.degree_u
      || a.degree_v != b.degree_v || a.degree_count != b.degree_count
      || a.step_u != b.step_u || a.step_v != b.step_v
      || a.step_count != b.step_count || a.basis_u_count != b.basis_u_count
      || a.basis_v_count != b.basis_v_count) {
    return false;
  }
  // The approximation directives, which are state on the element like the
  // rest of this struct (3.18). Their numbers go through same_float() for
  // the reason every other float here does: the dump writes them as text and
  // the reparse reads that text back.
  if (a.ctech != b.ctech || a.stech != b.stech || a.merge != b.merge
      || a.merge_group != b.merge_group || a.merge_count != b.merge_count
      || !same_float(a.merge_resolution, b.merge_resolution)) {
    return false;
  }
  for (size_t i = 0; i < 2; i++) {
    if (!same_float(a.ctech_value[i], b.ctech_value[i])
        || !same_float(a.stech_value[i], b.stech_value[i])) {
      return false;
    }
  }
  for (size_t i = 0; i < a.basis_u_count; i++) {
    if (a.basis_u_start + i >= a_obj->basis_value_count
        || b.basis_u_start + i >= b_obj->basis_value_count
        || !same_float(a_obj->basis_values[a.basis_u_start + i],
            b_obj->basis_values[b.basis_u_start + i])) {
      return false;
    }
  }
  for (size_t i = 0; i < a.basis_v_count; i++) {
    if (a.basis_v_start + i >= a_obj->basis_value_count
        || b.basis_v_start + i >= b_obj->basis_value_count
        || !same_float(a_obj->basis_values[a.basis_v_start + i],
            b_obj->basis_values[b.basis_v_start + i])) {
      return false;
    }
  }
  return true;
}

/**
 * Dump the model, parse the dump, and check the two agree.
 *
 * Section 9 promises a structural round-trip and section 10 says this is the
 * invariant the fuzzers check. It was not: the harness dumped to /dev/null
 * and never read anything back, so a dumper that dropped faces went
 * unnoticed through millions of executions.
 */
void check_round_trip(const GMDL_Obj * obj) {
  if (!names_are_representable(obj)) {
    return;
  }

  char * text = nullptr;
  size_t length = 0;
  FILE * sink = open_memstream(&text, &length);
  if (!sink) {
    return; // Out of memory is not a finding.
  }
  GMDL_Result dumped = gmdl_obj_dump(obj, sink);
  fclose(sink);
  REQUIRE(dumped == GMDL_OK, "dump of a GMDL_OK model failed");

  GMDL_Stream * stream = nullptr;
  if (gmdl_stream_create_memory(text, length, &stream) != GMDL_OK) {
    free(text);
    return;
  }
  // Read our own output back with limits that cannot refuse it. The caps
  // the original parse used came from the options byte, and the question
  // here is whether the dump describes the same model - not whether it fits
  // inside somebody's caps.
  GMDL_Limits generous;
  gmdl_limits_default(&generous);
  generous.max_line_length = 1u << 20;
  GMDL_Obj * again = nullptr;
  GMDL_Result reloaded = gmdl_obj_load(stream, &generous, nullptr, &again);
  gmdl_stream_destroy(stream);
  free(text);
  // The dump is this library's own output, so it must parse with the default
  // limits. Anything else means the writer emits what the reader rejects.
  REQUIRE(reloaded == GMDL_OK && again, "our own dump did not parse back");

  REQUIRE(obj->vertex_count == again->vertex_count, "vertex_count");
  REQUIRE(obj->color_count == again->color_count, "color_count");
  REQUIRE(obj->texcoord_count == again->texcoord_count, "texcoord_count");
  REQUIRE(obj->normal_count == again->normal_count, "normal_count");
  REQUIRE(obj->param_vertex_count == again->param_vertex_count,
      "param_vertex_count");
  REQUIRE(obj->freeform_count == again->freeform_count, "freeform_count");
  REQUIRE(obj->freeform_body_count == again->freeform_body_count,
      "freeform_body_count");
  REQUIRE(obj->parm_value_count == again->parm_value_count,
      "parm_value_count");
  REQUIRE(obj->curve_ref_count == again->curve_ref_count, "curve_ref_count");
  REQUIRE(obj->special_point_count == again->special_point_count,
      "special_point_count");
  REQUIRE(obj->connection_count == again->connection_count,
      "connection_count");
  REQUIRE(obj->freeform_vertex_count == again->freeform_vertex_count,
      "freeform_vertex_count");
  REQUIRE(obj->face_count == again->face_count, "face_count");
  REQUIRE(obj->group_count == again->group_count, "group_count");
  REQUIRE(obj->line_count == again->line_count, "line_count");
  REQUIRE(obj->line_vertex_count == again->line_vertex_count,
      "line_vertex_count");
  REQUIRE(obj->point_count == again->point_count, "point_count");
  REQUIRE(obj->statement_count == again->statement_count, "statement_count");
  REQUIRE(strcmp(obj->mtllib, again->mtllib) == 0, "mtllib");
  // Every library line, not just the first. A dumper that wrote only the
  // compatibility scalar would pass the line above and lose the rest, which
  // is the exact shape of the defect the list was added to fix.
  REQUIRE(obj->mtllib_count == again->mtllib_count, "mtllib_count");
  for (size_t i = 0; i < obj->mtllib_count; i++) {
    REQUIRE(strcmp(obj->mtllibs[i].path, again->mtllibs[i].path) == 0,
        "mtllib path");
  }
  REQUIRE(obj->maplib_count == again->maplib_count, "maplib_count");
  for (size_t i = 0; i < obj->maplib_count; i++) {
    REQUIRE(strcmp(obj->maplibs[i].path, again->maplibs[i].path) == 0,
        "maplib path");
  }
  REQUIRE(obj->shadow_obj_count == again->shadow_obj_count,
      "shadow_obj_count");
  for (size_t i = 0; i < obj->shadow_obj_count; i++) {
    REQUIRE(strcmp(obj->shadow_objs[i].path, again->shadow_objs[i].path) == 0,
        "shadow_obj path");
  }
  REQUIRE(obj->trace_obj_count == again->trace_obj_count, "trace_obj_count");
  for (size_t i = 0; i < obj->trace_obj_count; i++) {
    REQUIRE(strcmp(obj->trace_objs[i].path, again->trace_objs[i].path) == 0,
        "trace_obj path");
  }

  for (size_t i = 0; i < obj->vertex_count; i++) {
    REQUIRE(same_float(obj->vertices[i].x, again->vertices[i].x)
            && same_float(obj->vertices[i].y, again->vertices[i].y)
            && same_float(obj->vertices[i].z, again->vertices[i].z),
        "vertex value");
  }
  for (size_t i = 0; i < obj->color_count; i++) {
    // `present` too, not just the three numbers: an absent colour holds
    // white, so a dumper that wrote white for it would reload as a colour
    // whose components all matched and only this flag would say otherwise.
    REQUIRE(obj->colors[i].present == again->colors[i].present,
        "colour presence");
    REQUIRE(same_float(obj->colors[i].r, again->colors[i].r)
            && same_float(obj->colors[i].g, again->colors[i].g)
            && same_float(obj->colors[i].b, again->colors[i].b),
        "colour value");
  }
  for (size_t i = 0; i < obj->texcoord_count; i++) {
    REQUIRE(same_float(obj->texcoords[i].u, again->texcoords[i].u)
            && same_float(obj->texcoords[i].v, again->texcoords[i].v),
        "texcoord value");
  }
  for (size_t i = 0; i < obj->normal_count; i++) {
    REQUIRE(same_float(obj->normals[i].x, again->normals[i].x)
            && same_float(obj->normals[i].y, again->normals[i].y)
            && same_float(obj->normals[i].z, again->normals[i].z),
        "normal value");
  }
  // The arity as well as the coordinates: a dump that padded every `vp` to
  // three numbers would round-trip its own output perfectly and still change
  // a curve's control point into a surface's (3.19).
  for (size_t i = 0; i < obj->param_vertex_count; i++) {
    REQUIRE(obj->param_vertices[i].count == again->param_vertices[i].count
            && same_float(obj->param_vertices[i].u, again->param_vertices[i].u)
            && same_float(obj->param_vertices[i].v, again->param_vertices[i].v)
            && same_float(obj->param_vertices[i].w, again->param_vertices[i].w),
        "param vertex");
  }
  // The free-form elements: the kind, the parameter range, the state and
  // every control-point reference. The kind is named explicitly rather than
  // left to agree with itself - a dumper writing every element as `curv` and
  // a loader reading it back as `curv` agree perfectly, and the surface is
  // gone.
  for (size_t i = 0; i < obj->freeform_count; i++) {
    const GMDL_Obj_Freeform & a = obj->freeforms[i];
    const GMDL_Obj_Freeform & b = again->freeforms[i];
    REQUIRE(a.kind == b.kind, "freeform kind");
    REQUIRE(same_float(a.range[0], b.range[0])
            && same_float(a.range[1], b.range[1])
            && same_float(a.range[2], b.range[2])
            && same_float(a.range[3], b.range[3]),
        "freeform range");
    REQUIRE(same_freeform_state(obj, a.state, again, b.state),
        "freeform state");
    REQUIRE(a.count == b.count, "freeform reference count");
    // One record per body line, so a dumper that merged two `trim` lines
    // into one would join two trimming loops and be caught here rather than
    // agreeing with itself.
    REQUIRE(a.body_count == b.body_count, "freeform body count");
    for (size_t k = 0; k < a.body_count && k < b.body_count; k++) {
      const GMDL_Obj_Freeform_Body & x = obj->freeform_bodies[a.body_start + k];
      const GMDL_Obj_Freeform_Body & y =
          again->freeform_bodies[b.body_start + k];
      REQUIRE(x.kind == y.kind, "body kind");
      REQUIRE(x.count == y.count, "body entry count");
      if (x.count != y.count) {
        continue;
      }
      if (x.kind == GMDL_OBJ_BODY_PARM_U || x.kind == GMDL_OBJ_BODY_PARM_V) {
        for (size_t e = 0; e < x.count; e++) {
          REQUIRE(same_float(obj->parm_values[x.start + e],
                      again->parm_values[y.start + e]),
              "parm value");
        }
      }
      else if (x.kind != GMDL_OBJ_BODY_SP) {
        for (size_t e = 0; e < x.count; e++) {
          REQUIRE(same_float(obj->curve_refs[x.start + e].u0,
                      again->curve_refs[y.start + e].u0)
                  && same_float(obj->curve_refs[x.start + e].u1,
                      again->curve_refs[y.start + e].u1),
              "curve reference range");
        }
      }
    }
    REQUIRE(strcmp(material_name(obj, a.material_index),
                material_name(again, b.material_index))
            == 0,
        "freeform material");
    REQUIRE(strcmp(map_name(obj, a.map_index), map_name(again, b.map_index))
            == 0,
        "freeform map");
    REQUIRE(same_render(render_state_of(obj, a.render_index),
                render_state_of(again, b.render_index)),
        "freeform render state");
  }
  for (size_t g = 0; g < obj->group_count; g++) {
    REQUIRE(strcmp(obj->groups[g].name, again->groups[g].name) == 0,
        "group name");
    REQUIRE(obj->groups[g].start_face == again->groups[g].start_face
            && obj->groups[g].face_count == again->groups[g].face_count,
        "group span");
    REQUIRE(obj->groups[g].is_object == again->groups[g].is_object,
        "group spelling");
  }
  for (size_t i = 0; i < obj->face_count; i++) {
    REQUIRE(strcmp(material_name(obj, obj->faces[i].material_index),
                material_name(again, again->faces[i].material_index))
            == 0,
        "face material");
    // "s" is state written between faces, so a dumper that re-derived it per
    // group rather than carrying it would drift exactly here.
    REQUIRE(obj->faces[i].smoothing_group == again->faces[i].smoothing_group,
        "face smoothing group");
    // `usemap` is state written between faces exactly as `usemtl` is, and
    // unlike it can go back to naming nothing - so a dumper that forgot the
    // `usemap off` would drift here and nowhere else.
    REQUIRE(strcmp(map_name(obj, obj->faces[i].map_index),
                map_name(again, again->faces[i].map_index))
            == 0,
        "face map");
    REQUIRE(same_render(render_state_of(obj, obj->faces[i].render_index),
                render_state_of(again, again->faces[i].render_index)),
        "face render attributes");
  }
  for (size_t i = 0; i < obj->line_count; i++) {
    REQUIRE(obj->lines[i].count == again->lines[i].count, "line span");
    REQUIRE(strcmp(material_name(obj, obj->lines[i].material_index),
                material_name(again, again->lines[i].material_index))
            == 0,
        "line material");
    REQUIRE(strcmp(map_name(obj, obj->lines[i].map_index),
                map_name(again, again->lines[i].map_index))
            == 0,
        "line map");
    REQUIRE(same_render(render_state_of(obj, obj->lines[i].render_index),
                render_state_of(again, again->lines[i].render_index)),
        "line render attributes");
  }
  for (size_t i = 0; i < obj->point_count; i++) {
    REQUIRE(strcmp(material_name(obj, obj->points[i].material_index),
                material_name(again, again->points[i].material_index))
            == 0,
        "point material");
    REQUIRE(strcmp(map_name(obj, obj->points[i].map_index),
                map_name(again, again->points[i].map_index))
            == 0,
        "point map");
    REQUIRE(same_render(render_state_of(obj, obj->points[i].render_index),
                render_state_of(again, again->points[i].render_index)),
        "point render attributes");
  }
  for (size_t i = 0; i < obj->statement_count; i++) {
    REQUIRE(obj->statements[i].kind == again->statements[i].kind,
        "statement kind");
    REQUIRE(strcmp(obj->statements[i].text, again->statements[i].text) == 0,
        "statement text");
  }
  if (indices_are_representable(obj)) {
    // Free-form control points are indices like any other, so they belong
    // under this guard and not up with the element's kind and range. They
    // were outside it at first, which made a document with no `v` lines and
    // a `curv 0 1 -1 -2` report a library defect: -2 resolves to -2, the
    // dump writes it as -1, and the reload reads -1 against zero vertices as
    // -1 again. That is the below--1 case this guard exists for, reached
    // from a direction the guard could not see because the comparison was
    // not inside it.
    for (size_t i = 0; i < obj->freeform_count; i++) {
      const GMDL_Obj_Freeform & a = obj->freeforms[i];
      const GMDL_Obj_Freeform & b = again->freeforms[i];
      for (size_t k = 0; k < a.count && k < b.count; k++) {
        const GMDL_Obj_Freeform_Vertex & x =
            obj->freeform_vertices[a.start + k];
        const GMDL_Obj_Freeform_Vertex & y =
            again->freeform_vertices[b.start + k];
        REQUIRE(x.vertex == y.vertex && x.texcoord == y.texcoord
                && x.normal == y.normal,
            "freeform reference");
      }
    }
    for (size_t i = 0; i < obj->curve_ref_count; i++) {
      REQUIRE(obj->curve_refs[i].curve2d == again->curve_refs[i].curve2d,
          "curve reference index");
    }
    for (size_t i = 0; i < obj->special_point_count; i++) {
      REQUIRE(obj->special_points[i] == again->special_points[i],
          "special point index");
    }
    for (size_t i = 0; i < obj->connection_count; i++) {
      const GMDL_Obj_Connection & a = obj->connections[i];
      const GMDL_Obj_Connection & b = again->connections[i];
      REQUIRE(a.a.surface == b.a.surface && a.b.surface == b.b.surface,
          "connection surface index");
      REQUIRE(a.a.curve.curve2d == b.a.curve.curve2d
              && a.b.curve.curve2d == b.b.curve.curve2d,
          "connection curve index");
      REQUIRE(same_float(a.a.curve.u0, b.a.curve.u0)
              && same_float(a.a.curve.u1, b.a.curve.u1)
              && same_float(a.b.curve.u0, b.b.curve.u0)
              && same_float(a.b.curve.u1, b.b.curve.u1),
          "connection range");
    }
    for (size_t i = 0; i < obj->line_vertex_count; i++) {
      REQUIRE(obj->line_vertices[i].vertex == again->line_vertices[i].vertex,
          "line vertex index");
      REQUIRE(obj->line_vertices[i].texcoord
              == again->line_vertices[i].texcoord,
          "line texcoord index");
    }
    for (size_t i = 0; i < obj->point_count; i++) {
      REQUIRE(obj->points[i].vertex == again->points[i].vertex, "point index");
    }
    for (size_t i = 0; i < obj->face_count; i++) {
      const GMDL_Obj_Face * a = &obj->faces[i];
      const GMDL_Obj_Face * b = &again->faces[i];
      REQUIRE(a->count == b->count, "face vertex count");
      size_t inline_count = a->count < 4 ? a->count : 4;
      for (size_t j = 0; j < inline_count; j++) {
        REQUIRE(a->vertex[j] == b->vertex[j] && a->texcoord[j] == b->texcoord[j]
                && a->normal[j] == b->normal[j],
            "face index");
      }
      if (a->overflow && b->overflow) {
        for (size_t j = 4; j < a->count; j++) {
          REQUIRE(a->overflow[j - 4].vertex == b->overflow[j - 4].vertex
                  && a->overflow[j - 4].texcoord == b->overflow[j - 4].texcoord
                  && a->overflow[j - 4].normal == b->overflow[j - 4].normal,
              "face overflow index");
        }
      }
    }
  }

  gmdl_obj_free(again);
}


/**
 * How an allocation failure is injected for this input, if at all.
 *
 * The unit sweep in tests/unit/test_allocator.cpp refuses every allocation
 * the loader makes, one at a time, in three widths - but against one
 * hand-written document. Every arm is reached; what that cannot establish is
 * that a *survived* refusal is lossless on document shapes the hand-written
 * one does not contain, because the comparison is against an unrefused parse
 * of the same input. Here the corpus supplies the shapes, so the injection
 * rides an axis that is already being varied.
 */
struct Injection {
  bool active = false;
  size_t raw = 0; ///< The fuzzer's choice, before it is bounded.
  size_t run = 1; ///< Requests to refuse; 0 refuses everything from fail_at.
};

/** Dump a model to a heap buffer, or return nullptr. Caller frees. */
char * dump_to_buffer(const GMDL_Obj * obj, size_t * out_length) {
  char * text = nullptr;
  size_t length = 0;
  FILE * sink = open_memstream(&text, &length);
  if (!sink) {
    return nullptr;
  }
  GMDL_Result dumped = gmdl_obj_dump(obj, sink);
  fclose(sink);
  if (dumped != GMDL_OK) {
    free(text);
    return nullptr;
  }
  *out_length = length;
  return text;
}

/**
 * Parse the same bytes again with one allocation refused, and hold the result
 * against the unrefused parse.
 *
 * @param reference What the unrefused parse returned.
 * @param reference_obj The model it produced, or nullptr.
 */
void check_under_refusal(const uint8_t * data, size_t size,
    const GMDL_Limits * limits, const Injection & how, GMDL_Result reference,
    const GMDL_Obj * reference_obj) {
  GMDL_Stream * stream = nullptr;
  if (gmdl_stream_create_memory(data, size, &stream) != GMDL_OK) {
    return;
  }
  gmdltest::FailingAllocator allocator(how.raw, how.run);
  GMDL_Obj * obj = nullptr;
  GMDL_Result result = gmdl_obj_load(stream, limits, allocator.get(), &obj);
  gmdl_stream_destroy(stream);
  // The model carries this allocator, so everything below would be refused
  // too. The refusal has done its work by now.
  allocator.stop_failing();

  // The caller bounded the choice by the number of requests the reference
  // parse actually made, so the refusal lands. It can still miss when an
  // earlier refusal shortens the parse - a sustained refusal at request 3
  // means requests 4 onwards never happen - and that is not a finding.
  if (!allocator.failed()) {
    REQUIRE(result == reference, "an uninjected parse disagreed with itself");
    gmdl_obj_free(obj);
    return;
  }

  // Refusing an allocation can only turn a result into GMDL_ERR_OOM. It can
  // never produce a different diagnosis, and it can never rescue a document
  // the reference refused.
  REQUIRE(result == reference || result == GMDL_ERR_OOM,
      "a refused allocation changed the diagnosis");

  if (result == GMDL_ERR_OOM) {
    REQUIRE(obj == nullptr, "GMDL_ERR_OOM handed back a model anyway");
    return;
  }

  // Survived. Then nothing may have been lost: the only allocations a loader
  // is entitled to survive losing are the shrink-to-fit calls, whose whole
  // effect is spare capacity the model does not expose.
  REQUIRE((obj != nullptr) == (reference_obj != nullptr),
      "a survived refusal changed whether a model was produced");
  if (obj && reference_obj) {
    size_t a_len = 0;
    size_t b_len = 0;
    char * a = dump_to_buffer(obj, &a_len);
    char * b = dump_to_buffer(reference_obj, &b_len);
    if (a && b) {
      REQUIRE(a_len == b_len && memcmp(a, b, a_len) == 0,
          "a survived refusal produced a different model");
    }
    free(a);
    free(b);
  }
  gmdl_obj_free(obj);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  // The first byte selects the limits, so that the capped paths are reachable
  // too rather than only the wide-open defaults.
  GMDL_Limits limits;
  gmdl_limits_default(&limits);
  Injection how;
  const uint8_t * body = data;
  size_t body_size = size;
  if (size) {
    uint8_t options = data[0];
    data++;
    size--;
    if (options & 0x01) {
      limits.max_line_length = 64;
    }
    if (options & 0x02) {
      limits.max_vertices = 16;
    }
    if (options & 0x04) {
      limits.max_faces = 16;
    }
    if (options & 0x08) {
      limits.max_face_indices = 8;
    }
    if (options & 0x10) {
      limits.max_groups = 4;
    }
    if (options & 0x20) {
      limits.max_materials = 4;
    }

    // The top two bits choose how an allocation failure is injected, and the
    // three widths are the three from the unit sweep: refusing one request
    // cannot fail an array append at all, because cutil's reserve_n() retries
    // a refused 1.5x growth at the exact size; refusing everything onwards
    // reaches that arm but can never survive it, so an arm that drops its
    // error reads as a correct failure. A quarter of inputs inject nothing,
    // which keeps the plain round-trip on the same corpus.
    static const size_t kWidths[4] = {1, 1, 2, 0};
    how.run = kWidths[(options >> 6) & 0x03];
    how.active = ((options >> 6) & 0x03) != 0;
    if (how.active && size >= 2) {
      // Which allocation to refuse, before bounding. Two bytes, so the fuzzer
      // can walk it the way the unit sweep walks its loop rather than only
      // ever reaching the first few.
      how.raw = (size_t)data[0] | ((size_t)data[1] << 8);
      data += 2;
      size -= 2;
    }
    body = data;
    body_size = size;
  }

  GMDL_Stream * stream = nullptr;
  if (gmdl_stream_create_memory(data, size, &stream) != GMDL_OK) {
    return 0;
  }

  // Counted rather than merely served, so the refusal below can be bounded by
  // what this parse actually costs. A 16-bit choice against a document that
  // allocates a dozen times would otherwise land past the end every time, and
  // the injection would silently never fire - which is what the first version
  // of this did, through 2.5M executions, while looking like it was working.
  gmdltest::FailingAllocator counting((size_t)-1);
  GMDL_Obj * obj = nullptr;
  GMDL_Result result = gmdl_obj_load(stream, &limits, counting.get(), &obj);
  gmdl_stream_destroy(stream);

  if (result == GMDL_OK && obj) {
    // Walk what the parser produced: an index out of range here would be a
    // read past the end of the arrays in any consumer.
    for (size_t i = 0; i < obj->face_count; i++) {
      const GMDL_Obj_Face * face = &obj->faces[i];
      size_t inline_count = face->count < 4 ? face->count : 4;
      for (size_t j = 0; j < inline_count; j++) {
        volatile int32_t v = face->vertex[j];
        (void)v;
      }
      if (face->count > 4 && face->overflow) {
        for (size_t j = 0; j < face->count - 4; j++) {
          volatile int32_t v = face->overflow[j].vertex;
          (void)v;
        }
      }
    }

    // Dump it, parse the dump, and hold the two against each other. This is
    // the invariant section 10 describes, and until now was not checked.
    check_round_trip(obj);
  }

  // Now the same bytes with one allocation refused, held against what the
  // unrefused parse just returned. This runs whatever the reference result
  // was: a document the parser refuses still allocates on the way to
  // refusing it, and those arms want reaching too.
  if (how.active && counting.requests() > 0) {
    how.raw %= counting.requests();
    check_under_refusal(body, body_size, &limits, how, result, obj);
  }

  gmdl_obj_free(obj);
  return 0;
}
