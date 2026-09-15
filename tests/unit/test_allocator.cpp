/**
 * @file
 *
 * Tests that the library really does route every allocation through the
 * allocator it is handed, rather than reaching for malloc() in places.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <cstdlib>
#include <string>

#include <cutil/allocator.h>

using gmdltest::MemStream;

namespace {

/** A stdlib-backed allocator that counts what passes through it. */
struct Counting {
  size_t allocations = 0;
  size_t frees = 0;
  size_t live = 0;
};

void * counting_malloc(void * ctx, size_t size) {
  Counting * c = (Counting *)ctx;
  void * p = malloc(size ? size : 1);
  if (p) {
    c->allocations++;
    c->live++;
  }
  return p;
}

void * counting_calloc(void * ctx, size_t nitems, size_t size) {
  Counting * c = (Counting *)ctx;
  if (nitems && size > (size_t)-1 / nitems) {
    return nullptr;
  }
  size_t total = nitems * size;
  void * p = calloc(1, total ? total : 1);
  if (p) {
    c->allocations++;
    c->live++;
  }
  return p;
}

void * counting_realloc(void * ctx, void * ptr, size_t size) {
  Counting * c = (Counting *)ctx;
  void * p = realloc(ptr, size ? size : 1);
  if (p && !ptr) {
    c->allocations++;
    c->live++;
  }
  return p;
}

void counting_free(void * ctx, void * ptr) {
  Counting * c = (Counting *)ctx;
  if (ptr) {
    c->frees++;
    c->live--;
  }
  free(ptr);
}

GMDL_Allocator make_allocator(Counting * c) {
  GMDL_Allocator a;
  a.ctx = c;
  a.malloc_fn = counting_malloc;
  a.calloc_fn = counting_calloc;
  a.realloc_fn = counting_realloc;
  a.free_fn = counting_free;
  return a;
}

} // namespace

TEST(Allocator, DefaultIsTheSuiteDefault) {
  const GMDL_Allocator * a = gmdl_allocator_default();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a, gcu_allocator_default())
      << "the model library shares cutil's allocator rather than defining "
         "another one";
  EXPECT_NE(a->malloc_fn, nullptr);
  EXPECT_NE(a->calloc_fn, nullptr);
  EXPECT_NE(a->realloc_fn, nullptr);
  EXPECT_NE(a->free_fn, nullptr);
}

TEST(Allocator, ObjParseUsesTheGivenAllocatorAndReturnsEverything) {
  Counting counting;
  GMDL_Allocator allocator = make_allocator(&counting);

  {
    // Enough records to force the internal arrays to grow, and an n-gon so the
    // per-face overflow allocation is exercised too.
    std::string text;
    for (int i = 0; i < 300; i++) {
      text += "v " + std::to_string(i) + " 0 0\n";
    }
    text += "g one\nusemtl red\n";
    text += "f 1 2 3 4 5 6 7 8\n";
    for (int i = 1; i + 2 <= 300; i += 3) {
      text += "f " + std::to_string(i) + " " + std::to_string(i + 1) + " " +
          std::to_string(i + 2) + "\n";
    }

    MemStream stream(text);
    GMDL_Obj * obj = nullptr;
    ASSERT_EQ(gmdl_obj_load(stream.get(), nullptr, &allocator, &obj), GMDL_OK);
    ASSERT_NE(obj, nullptr);
    EXPECT_GT(counting.allocations, 0u) << "nothing went through the allocator";
    EXPECT_GT(counting.live, 0u);
    gmdl_obj_free(obj);
  }

  EXPECT_EQ(counting.live, 0u) << "every allocation should have been returned";
}

TEST(Allocator, MtlParseUsesTheGivenAllocator) {
  Counting counting;
  GMDL_Allocator allocator = make_allocator(&counting);

  {
    std::string text;
    for (int i = 0; i < 100; i++) {
      text += "newmtl m" + std::to_string(i) + "\nKd 1 1 1\n";
    }
    MemStream stream(text);
    GMDL_Mtl * mtl = nullptr;
    ASSERT_EQ(gmdl_mtl_load(stream.get(), nullptr, &allocator, &mtl), GMDL_OK);
    ASSERT_NE(mtl, nullptr);
    EXPECT_EQ(mtl->material_count, 100u);
    gmdl_mtl_free(mtl);
  }

  EXPECT_EQ(counting.live, 0u);
}

// The error paths have to return everything too, which is where a parser that
// unwinds by hand usually leaks.
TEST(Allocator, FailedParseReturnsEverything) {
  Counting counting;
  GMDL_Allocator allocator = make_allocator(&counting);

  GMDL_Limits limits;
  gmdl_limits_default(&limits);
  limits.max_faces = 4;

  std::string text;
  for (int i = 0; i < 200; i++) {
    text += "v " + std::to_string(i) + " 0 0\n";
  }
  // An n-gon first, so a face with an overflow array is in the builder when
  // the cap is hit and the parse is abandoned.
  text += "f 1 2 3 4 5 6 7 8\n";
  for (int i = 0; i < 20; i++) {
    text += "f 1 2 3\n";
  }

  MemStream stream(text);
  GMDL_Obj * obj = nullptr;
  EXPECT_EQ(gmdl_obj_load(stream.get(), &limits, &allocator, &obj),
      GMDL_ERR_LIMIT);
  EXPECT_EQ(obj, nullptr);
  EXPECT_GT(counting.allocations, 0u);
  EXPECT_EQ(counting.live, 0u) << "the abandoned parse leaked";
}

TEST(Allocator, StreamUsesTheGivenAllocator) {
  Counting counting;
  GMDL_Allocator allocator = make_allocator(&counting);

  GMDL_Stream * stream = nullptr;
  ASSERT_EQ(gmdl_stream_create_memory_with_allocator(
                "v 0 0 0\n", 8, &allocator, &stream),
      GMDL_OK);
  EXPECT_EQ(counting.allocations, 1u);
  gmdl_stream_destroy(stream);
  EXPECT_EQ(counting.live, 0u);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
