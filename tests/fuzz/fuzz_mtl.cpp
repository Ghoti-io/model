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
bool is_representable(const GMDL_Mtl * mtl) {
  for (size_t i = 0; i < mtl->material_count; i++) {
    const GMDL_Mtl_Material * m = &mtl->materials[i];
    if (ends_with_backslash(m->name) || ends_with_backslash(m->map_Ka)
        || ends_with_backslash(m->map_Kd) || ends_with_backslash(m->map_Ks)
        || ends_with_backslash(m->map_Ns) || ends_with_backslash(m->map_d)
        || ends_with_backslash(m->map_bump)) {
      return false;
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
    // Including whether one was stated at all: a NULL that comes back as a
    // path, or the reverse, is exactly what a dumper keying on the wrong
    // thing would produce.
    REQUIRE(same_path(a->map_Ka, b->map_Ka), "map_Ka");
    REQUIRE(same_path(a->map_Kd, b->map_Kd), "map_Kd");
    REQUIRE(same_path(a->map_Ks, b->map_Ks), "map_Ks");
    REQUIRE(same_path(a->map_Ns, b->map_Ns), "map_Ns");
    REQUIRE(same_path(a->map_d, b->map_d), "map_d");
    REQUIRE(same_path(a->map_bump, b->map_bump), "map_bump");
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
