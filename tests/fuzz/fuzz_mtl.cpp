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

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <ghoti.io/model/model.h>

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
    FILE * sink = fopen("/dev/null", "w");
    if (sink) {
      (void)gmdl_mtl_dump(mtl, sink);
      fclose(sink);
    }
  }

  gmdl_mtl_free(mtl);
  return 0;
}
