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

/** Whether a name would be the last thing on its line and end in a backslash. */
bool ends_with_backslash(const char * name) {
  size_t length = strlen(name);
  return length > 0 && name[length - 1] == '\\';
}

/**
 * Dump the library, parse the dump, and check the two agree.
 *
 * Unlike the OBJ side this needs no exemptions: every field of every
 * material is written unconditionally, and every one of them has an MTL
 * spelling that reads back as itself.
 */
void check_round_trip(const GMDL_Mtl * mtl) {
  // A name ending in a backslash is written last on its line, where 2.6
  // reads a continuation: "newmtl a\\" comes back as the material "aKa",
  // having swallowed the Ka line after it. The format has no escape for
  // that, so such a library is outside the round trip.
  for (size_t i = 0; i < mtl->material_count; i++) {
    if (ends_with_backslash(mtl->materials[i].name)) {
      return;
    }
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
    for (int c = 0; c < 3; c++) {
      REQUIRE(same_float(a->Ka[c], b->Ka[c]), "Ka");
      REQUIRE(same_float(a->Kd[c], b->Kd[c]), "Kd");
      REQUIRE(same_float(a->Ks[c], b->Ks[c]), "Ks");
    }
    REQUIRE(same_float(a->Ns, b->Ns), "Ns");
    REQUIRE(same_float(a->d, b->d), "d");
    REQUIRE(a->illum == b->illum, "illum");
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
