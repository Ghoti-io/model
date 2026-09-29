/**
 * @file
 *
 * libFuzzer harness for the OFF parser.
 *
 * Build with: make fuzz-off
 * Run:        make fuzz-run-off FUZZ_TIME=300
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <ghoti.io/model/model.h>

#include "../failing_allocator.h"

namespace {

[[noreturn]] void broken(const char * what) {
  std::fprintf(stderr, "off invariant broken: %s\n", what);
  std::abort();
}

#define REQUIRE(cond, what) \
  do {                      \
    if (!(cond)) {          \
      broken(what);         \
    }                       \
  } while (0)

char * dump_to_buffer(const GMDL_Off * off, size_t * out_len) {
  char * buffer = nullptr;
  size_t size = 0;
  FILE * fd = open_memstream(&buffer, &size);
  if (!fd) {
    return nullptr;
  }
  if (gmdl_off_dump(off, nullptr, fd) != GMDL_OK) {
    fclose(fd);
    free(buffer);
    return nullptr;
  }
  fclose(fd);
  *out_len = size;
  return buffer;
}

void check_round_trip(const GMDL_Off * off) {
  size_t len = 0;
  char * dumped = dump_to_buffer(off, &len);
  if (!dumped) {
    return;
  }
  GMDL_Stream * stream = nullptr;
  if (gmdl_stream_create_memory(dumped, len, &stream) != GMDL_OK) {
    free(dumped);
    return;
  }
  GMDL_Off * again = nullptr;
  GMDL_Result result = gmdl_off_load(stream, nullptr, nullptr, &again);
  gmdl_stream_destroy(stream);
  free(dumped);
  REQUIRE(result == GMDL_OK && again != nullptr, "dump did not reload");
  REQUIRE(again->vertex_count == off->vertex_count,
      "vertex count changed across a dump");
  REQUIRE(again->face_count == off->face_count,
      "face count changed across a dump");
  gmdl_off_free(again);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  GMDL_Off_Options limits;
  gmdl_off_options_default(&limits);
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
      limits.max_face_corners = 8;
    }
    if (options & 0x10) {
      limits.reject_non_finite = true;
    }
  }

  GMDL_Stream * stream = nullptr;
  if (gmdl_stream_create_memory(data, size, &stream) != GMDL_OK) {
    return 0;
  }
  GMDL_Off * off = nullptr;
  GMDL_Result result = gmdl_off_load(stream, &limits, nullptr, &off);
  gmdl_stream_destroy(stream);
  if (result == GMDL_OK && off) {
    check_round_trip(off);
  }
  gmdl_off_free(off);
  return 0;
}
