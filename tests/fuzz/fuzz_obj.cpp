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
 * Build with: make fuzz-obj
 * Run:        make fuzz-run-obj FUZZ_TIME=300
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <ghoti.io/model/model.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  // The first byte selects the limits, so that the capped paths are reachable
  // too rather than only the wide-open defaults.
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
  }

  GMDL_Stream * stream = nullptr;
  if (gmdl_stream_create_memory(data, size, &stream) != GMDL_OK) {
    return 0;
  }

  GMDL_Obj * obj = nullptr;
  GMDL_Result result = gmdl_obj_load(stream, &limits, nullptr, &obj);
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

    // The dumper runs over the same data, and has to survive it.
    FILE * sink = fopen("/dev/null", "w");
    if (sink) {
      (void)gmdl_obj_dump(obj, sink);
      fclose(sink);
    }
  }

  gmdl_obj_free(obj);
  return 0;
}
