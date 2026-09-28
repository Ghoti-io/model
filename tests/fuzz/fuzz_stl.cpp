/**
 * @file
 *
 * libFuzzer harness for the STL parser.
 *
 * Build with: make fuzz-stl
 * Run:        make fuzz-run-stl FUZZ_TIME=300
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
  std::fprintf(stderr, "stl invariant broken: %s\n", what);
  std::abort();
}

#define REQUIRE(cond, what) \
  do {                      \
    if (!(cond)) {          \
      broken(what);         \
    }                       \
  } while (0)

char * dump_to_buffer(const GMDL_Stl * stl, size_t * out_len) {
  char * buffer = nullptr;
  size_t size = 0;
  FILE * fd = open_memstream(&buffer, &size);
  if (!fd) {
    return nullptr;
  }
  if (gmdl_stl_dump(stl, nullptr, fd) != GMDL_OK) {
    fclose(fd);
    free(buffer);
    return nullptr;
  }
  fclose(fd);
  *out_len = size;
  return buffer;
}

void check_round_trip(const GMDL_Stl * stl) {
  size_t len = 0;
  char * dumped = dump_to_buffer(stl, &len);
  if (!dumped) {
    return;
  }
  GMDL_Stream * stream = nullptr;
  if (gmdl_stream_create_memory(dumped, len, &stream) != GMDL_OK) {
    free(dumped);
    return;
  }
  GMDL_Stl * again = nullptr;
  GMDL_Result result = gmdl_stl_load(stream, nullptr, nullptr, &again);
  gmdl_stream_destroy(stream);
  free(dumped);
  REQUIRE(result == GMDL_OK && again != nullptr, "binary dump did not reload");
  REQUIRE(again->triangle_count == stl->triangle_count,
      "triangle count changed across a dump");
  gmdl_stl_free(again);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  GMDL_Stl_Options limits;
  gmdl_stl_options_default(&limits);
  if (size) {
    uint8_t options = data[0];
    data++;
    size--;
    if (options & 0x01) {
      limits.max_line_length = 64;
    }
    if (options & 0x02) {
      limits.max_triangles = 16;
    }
  }

  GMDL_Stream * stream = nullptr;
  if (gmdl_stream_create_memory(data, size, &stream) != GMDL_OK) {
    return 0;
  }
  GMDL_Stl * stl = nullptr;
  GMDL_Result result = gmdl_stl_load(stream, &limits, nullptr, &stl);
  gmdl_stream_destroy(stream);
  if (result == GMDL_OK && stl) {
    check_round_trip(stl);
  }
  gmdl_stl_free(stl);
  return 0;
}
