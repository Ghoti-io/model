/**
 * @file
 *
 * libFuzzer harness for the MTL parser.
 *
 * Build with: make fuzz-mtl
 * Run:        make fuzz-run-mtl FUZZ_TIME=300
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <ghoti.io/model/model.h>

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

/** NaN is never equal to itself, and a NaN value is legal input (3.4). */
bool same_float(float a, float b) {
  return (std::isnan(a) && std::isnan(b)) || a == b;
}

/** Whether a text would be the last thing on its line and end in a backslash. */
bool ends_with_backslash(const char * text) {
  return text && *text && text[strlen(text) - 1] == '\\';
}

/**
 * Whether every name and path in the library can be written and read back.
 *
 * The one exemption, and it applies to a map path for the same reason it
 * applies to a name: written last on its line, a trailing backslash is a
 * continuation under 2.6, so "map_Kd a\\" swallows the line after it and
 * comes back as something else. "map_Kd a\\\\" at the end of a file parses to
 * the path "a\\", which is how such a library is reached at all. The format
 * has no escape for it.
 */
/** Every map a material can hold, in one place, so nothing is forgotten. */
void collect_maps(
    const GMDL_Mtl_Material * m, std::vector<const GMDL_Mtl_Map *> & out) {
  const GMDL_Mtl_Map * const fixed[] = {&m->map_Ka, &m->map_Kd, &m->map_Ks,
      &m->map_Ns, &m->map_d, &m->map_bump, &m->map_Ke, &m->map_Pr, &m->map_Pm,
      &m->map_Ps, &m->norm, &m->disp, &m->decal};
  for (const GMDL_Mtl_Map * p : fixed) {
    out.push_back(p);
  }
  for (size_t i = 0; i < GMDL_MTL_REFL_COUNT; i++) {
    out.push_back(&m->refl[i]);
  }
}

/** Whether two maps say the same thing - path, options and all. */
bool same_map(const GMDL_Mtl_Map * a, const GMDL_Mtl_Map * b) {
  if (!a->path || !b->path) {
    return a->path == b->path;
  }
  if (strcmp(a->path, b->path) != 0 || a->present != b->present) {
    return false;
  }
  if (a->blendu != b->blendu || a->blendv != b->blendv || a->clamp != b->clamp
      || a->texres != b->texres || a->imfchan != b->imfchan) {
    return false;
  }
  if (memcmp(&a->boost, &b->boost, sizeof(a->boost)) != 0
      || memcmp(a->mm, b->mm, sizeof(a->mm)) != 0
      || memcmp(a->o, b->o, sizeof(a->o)) != 0
      || memcmp(a->s, b->s, sizeof(a->s)) != 0
      || memcmp(a->t, b->t, sizeof(a->t)) != 0
      || memcmp(&a->bm, &b->bm, sizeof(a->bm)) != 0) {
    return false;
  }
  return true;
}

bool is_representable(const GMDL_Mtl * mtl) {
  for (size_t i = 0; i < mtl->material_count; i++) {
    const GMDL_Mtl_Material * m = &mtl->materials[i];
    if (ends_with_backslash(m->name)) {
      return false;
    }
    std::vector<const GMDL_Mtl_Map *> maps;
    collect_maps(m, maps);
    for (const GMDL_Mtl_Map * map : maps) {
      if (ends_with_backslash(map->path)) {
        return false;
      }
    }
  }
  return true;
}

/** Whether two map paths say the same thing, NULL meaning "none stated". */
bool same_path(const char * a, const char * b) {
  if (!a || !b) {
    return a == b;
  }
  return strcmp(a, b) == 0;
}

/**
 * Dump the library, parse the dump, and check the two agree.
 *
 * Every other field has an MTL spelling that reads back as itself, so the
 * backslash above is the only thing excluded.
 */
void check_round_trip(const GMDL_Mtl * mtl) {
  if (!is_representable(mtl)) {
    return;
  }

  char * text = nullptr;
  size_t length = 0;
  FILE * sink = open_memstream(&text, &length);
  if (!sink) {
    return; // Out of memory is not a finding.
  }
  GMDL_Result dumped = gmdl_mtl_dump(mtl, sink);
  fclose(sink);
  REQUIRE(dumped == GMDL_OK, "dump of a GMDL_OK library failed");

  GMDL_Stream * stream = nullptr;
  if (gmdl_stream_create_memory(text, length, &stream) != GMDL_OK) {
    free(text);
    return;
  }
  // Limits that cannot refuse our own output; see the OBJ harness.
  GMDL_Limits generous;
  gmdl_limits_default(&generous);
  generous.max_line_length = 1u << 20;
  GMDL_Mtl * again = nullptr;
  GMDL_Result reloaded = gmdl_mtl_load(stream, &generous, nullptr, &again);
  gmdl_stream_destroy(stream);
  free(text);
  REQUIRE(reloaded == GMDL_OK && again, "our own dump did not parse back");

  REQUIRE(mtl->material_count == again->material_count, "material_count");
  for (size_t i = 0; i < mtl->material_count; i++) {
    const GMDL_Mtl_Material * a = &mtl->materials[i];
    const GMDL_Mtl_Material * b = &again->materials[i];
    REQUIRE(strcmp(a->name, b->name) == 0, "material name");
    // Which properties the source stated is part of the model, because the
    // dump writes only those; without this the check cannot see a dumper
    // that invents a property or drops one.
    REQUIRE(a->present == b->present, "present");
    for (int c = 0; c < 3; c++) {
      REQUIRE(same_float(a->Ka[c], b->Ka[c]), "Ka");
      REQUIRE(same_float(a->Kd[c], b->Kd[c]), "Kd");
      REQUIRE(same_float(a->Ks[c], b->Ks[c]), "Ks");
    }
    REQUIRE(same_float(a->Ns, b->Ns), "Ns");
    REQUIRE(same_float(a->d, b->d), "d");
    REQUIRE(a->illum == b->illum, "illum");
    for (int c = 0; c < 3; c++) {
      REQUIRE(same_float(a->Ke[c], b->Ke[c]), "Ke");
      REQUIRE(same_float(a->Tf[c], b->Tf[c]), "Tf");
    }
    REQUIRE(same_float(a->Ni, b->Ni), "Ni");
    REQUIRE(same_float(a->Tr, b->Tr), "Tr");
    REQUIRE(a->sharpness == b->sharpness, "sharpness");
    REQUIRE(same_float(a->Pr, b->Pr), "Pr");
    REQUIRE(same_float(a->Pm, b->Pm), "Pm");
    REQUIRE(same_float(a->Ps, b->Ps), "Ps");
    REQUIRE(same_float(a->Pc, b->Pc), "Pc");
    REQUIRE(same_float(a->Pcr, b->Pcr), "Pcr");
    REQUIRE(same_float(a->aniso, b->aniso), "aniso");
    REQUIRE(same_float(a->anisor, b->anisor), "anisor");
    REQUIRE(a->map_aat == b->map_aat, "map_aat");

    // Including whether one was stated at all: a NULL that comes back as a
    // path, or the reverse, is exactly what a dumper keying on the wrong
    // thing would produce. Collected rather than listed, so a path added to
    // the material cannot be left out of the comparison and quietly narrow
    // what this harness checks.
    std::vector<const GMDL_Mtl_Map *> pa;
    std::vector<const GMDL_Mtl_Map *> pb;
    collect_maps(a, pa);
    collect_maps(b, pb);
    REQUIRE(pa.size() == pb.size(), "map count");
    for (size_t k = 0; k < pa.size(); k++) {
      REQUIRE(same_map(pa[k], pb[k]), "map path or option");
    }
  }

  gmdl_mtl_free(again);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  GMDL_Limits limits;
  gmdl_limits_default(&limits);
  if (size) {
    uint8_t options = data[0];
    data++;
    size--;
    if (options & 0x01) {
      limits.max_line_length = 64;
    }
    if (options & 0x02) {
      limits.max_materials = 4;
    }
  }

  GMDL_Stream * stream = nullptr;
  if (gmdl_stream_create_memory(data, size, &stream) != GMDL_OK) {
    return 0;
  }

  GMDL_Mtl * mtl = nullptr;
  GMDL_Result result = gmdl_mtl_load(stream, &limits, nullptr, &mtl);
  gmdl_stream_destroy(stream);

  if (result == GMDL_OK && mtl) {
    for (size_t i = 0; i < mtl->material_count; i++) {
      (void)gmdl_mtl_find(mtl, mtl->materials[i].name);
    }
    // Dump it, parse the dump, and hold the two against each other. This is
    // the invariant section 10 describes, and until now was not checked.
    check_round_trip(mtl);
  }

  gmdl_mtl_free(mtl);
  return 0;
}
