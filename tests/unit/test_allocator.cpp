/**
 * @file
 *
 * Tests that the library really does route every allocation through the
 * allocator it is handed, rather than reaching for malloc() in places.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include <ghoti.io/cutil/allocator.h>

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

/**
 * How many of each record the sweep documents carry.
 *
 * Every array the loaders build starts with room for some number of records -
 * 128 for vertices, 16 for groups, 8 for materials, 4 for statements - and
 * allocates nothing more until that runs out. So a document has to be bigger
 * than the largest of those capacities before a single `gcu_array_append()`
 * inside the parse ever reallocates, and the arms that report *that* failing
 * are one per directive, which are the arms worth reaching.
 *
 * The first draft of this sweep used a document of about twenty lines. It
 * reported 28 allocations and 17 fatal refusals, which looked healthy; every
 * one of those refusals landed in obj_builder_init() or on the line buffer,
 * and the eleven per-directive arms were never visited at all. Coverage said
 * so; the sweep did not. A count that goes up is not a sweep that reaches
 * further - Ghoti.io Text found the same shape the same afternoon, where an
 * equivalence oracle run over 37,906 corpus files missed five deliberate
 * mutations that twelve hand-written documents caught in ten minutes.
 *
 * One number for every dimension, rather than one per array, so that the
 * requirement is a single sentence: **every initial capacity in the loaders
 * must be smaller than this.** `tools/check-lists.py` holds them to it, since
 * raising a capacity past this number would silently return the sweep to
 * measuring the setup, and nothing else here would notice.
 */
const size_t kGrow = 160;

/**
 * A document reaching every directive the OBJ parser handles, at a size that
 * makes every one of the builder's arrays grow while the parse is running.
 *
 * Every face carries five indices rather than three, so each one allocates an
 * overflow array. That is deliberate and it is not about the overflow arm: it
 * is what makes the face-array growth land on a face that *has* an overflow,
 * which is the one arm in the loader where forgetting a free on the error
 * path would leak rather than merely mis-report. Giving only some faces five
 * indices would leave which kind of face triggers the growth to arithmetic
 * nobody is checking.
 */
std::string rich_obj() {
  std::string t = "mtllib my library.mtl\n";
  // Plain vertices first and coloured ones after, so the colour array is
  // padded across a growth as well as appended to - obj_color_append() has an
  // allocation in each half.
  for (size_t i = 0; i < kGrow; i++) {
    t += "v " + std::to_string(i) + " 0 0\n";
  }
  for (size_t i = 0; i < 5; i++) {
    t += "v " + std::to_string(i) + " 1 0 0.5 0.25 0.125\n";
  }
  for (size_t i = 0; i < kGrow; i++) {
    t += "vt 0.5\n"; // The one-number form, which is legal (3.2).
  }
  for (size_t i = 0; i < kGrow; i++) {
    t += "vn 0 1 0\n";
  }
  t += "s 3\n";
  for (size_t i = 0; i < kGrow; i++) {
    if (i % 5 == 0) {
      // Objects and groups both, spelled with spaces, so the whole-line
      // reading of a name is in the sweep too.
      t += (i % 10 ? "g a group " : "o an object ") + std::to_string(i) + "\n";
    }
    if (i % 10 == 0) {
      t += "usemtl a material " + std::to_string(i) + "\n";
    }
    t += "f 1/1/1 2/2/1 3/1/1 4/1/1 5/1/1\n";
  }
  for (size_t i = 0; i < kGrow; i++) {
    t += "l 1 2 3\n";
  }
  for (size_t i = 0; i < kGrow; i++) {
    t += "p 1 2\n";
  }
  for (size_t i = 0; i < kGrow; i++) {
    t += (i % 2 ? "call something.obj " : "csh echo hello ") +
        std::to_string(i) + "\n";
  }
  return t;
}

/**
 * The same for MTL.
 *
 * The maps go on the first few materials rather than on all of them. A map
 * path is a plain copy, not an array, so the hundredth one reaches no arm the
 * third did not - it is only another parse for the sweep to sit through. The
 * material count is what has to clear the capacity, and it does.
 */
std::string rich_mtl() {
  std::string t;
  for (size_t i = 0; i < kGrow; i++) {
    std::string n = std::to_string(i);
    t += "newmtl a material " + n + "\n";
    t += "Ka 0.1 0.2 0.3\nKd 0.4 0.5 0.6\nKs 1 1 1\n";
    t += "Ns 32\nd 0.5\nillum 2\nKe 1 1 1\nPr 0.25\n";
    if (i < 3) {
      t += "map_Kd -s 1 1 1 -o 0 0 0 -bm 2 some texture " + n + ".png\n";
      t += "map_bump -bm 0.5 a bump map " + n + ".png\n";
      t += "refl -type sphere sky " + n + ".png\n";
    }
  }
  return t;
}

struct SweepMode {
  size_t run;        ///< Consecutive requests to refuse; 0 is "all of them".
  const char * name;
};

/// The order is the order the floors are given in.
const SweepMode kSweepModes[] = {
    {1, "one request"}, {2, "one append"}, {0, "exhausted"}};

/**
 * Walk an allocation failure across a whole parse, one site at a time, in
 * both of the two ways an allocation can fail.
 *
 * **No single way of failing reaches all of it, and that took measuring.**
 * Three settings, each added because a defect planted in the loader was
 * invisible to the ones before it; FailingAllocator's own comment has the
 * mechanism. In short: cutil retries a refused array growth at the exact size
 * needed, so one refused request never fails an append, and sustained
 * refusal can reach an append's failure arm but never survive it.
 *
 * The other thing that took measuring: not every allocation is load-bearing.
 * `obj_steal_into()` calls `gcu_array_shrink_to_fit()` and discards the
 * answer on purpose - a shrink that fails leaves the array as it was, so the
 * model carries the parser's spare capacity and nothing is wrong. The first
 * draft asserted that every refusal must produce GMDL_ERR_OOM and reported
 * eleven "swallowed failures" that were all that one call. So the question is
 * not whether a refusal was survived but whether surviving it changed the
 * answer, and the check for that is the dump: an optional allocation is
 * exactly one whose absence nothing downstream can observe, and the dump is
 * what downstream sees.
 *
 * @param label Which loader, for the failure messages.
 * @param least_fatal How many refusals each mode must find fatal, in the
 *   order the modes are swept. Without these the sweep passes against a
 *   loader that never fails at all, which is the state it was written to rule
 *   out. Each sits below what was measured, so they say "this still reaches
 *   the arms" rather than pinning numbers an ordinary edit would move.
 * @param parse Runs one parse against the given allocator and returns what
 *   the loader said, filling in the dump of whatever it produced - empty when
 *   it produced nothing - and freeing the model before it returns.
 */
template <typename Parse>
void sweep_allocation_failures(const char * label,
    const size_t (&least_fatal)[3], Parse parse) {
  // What the parse costs and what it produces, measured rather than guessed:
  // run it once with nothing refused.
  size_t requests = 0;
  std::string reference;
  {
    gmdltest::FailingAllocator generous((size_t)-1);
    ASSERT_EQ(parse(generous, &reference), GMDL_OK)
        << label << ": the document does not parse even with no failures, so "
                    "the sweep below would be measuring the document rather "
                    "than the loader";
    ASSERT_FALSE(reference.empty()) << label << ": parsed to nothing";
    ASSERT_EQ(generous.live(), 0u) << label << ": the successful parse leaked";
    requests = generous.requests();
  }
  for (size_t f : least_fatal) {
    ASSERT_GE(requests, f) << label << ": " << requests
                           << " allocations cannot contain " << f
                           << " fatal refusals";
  }

  for (size_t m = 0; m < 3; m++) {
    const char * mode = kSweepModes[m].name;
    size_t fatal = 0;
    size_t survived = 0;
    size_t mismatches = 0;
    size_t first_mismatch = 0;

    for (size_t nth = 0; nth < requests; nth++) {
      gmdltest::FailingAllocator allocator(nth, kSweepModes[m].run);
      std::string produced;
      GMDL_Result result = parse(allocator, &produced);

      // Every request before this one is served exactly as the reference run
      // served it, so request `nth` happens and is refused. If it did not,
      // the sweep is not walking what it thinks it is.
      ASSERT_TRUE(allocator.failed())
          << label << " (" << mode << "): request " << nth << " never "
          << "happened, though the unrefused parse made " << requests;
      ASSERT_EQ(allocator.live(), 0u)
          << label << " (" << mode << "): refusing request " << nth
          << " leaked";

      if (result == GMDL_ERR_OOM) {
        ASSERT_TRUE(produced.empty())
            << label << " (" << mode << "): refusing request " << nth
            << " reported GMDL_ERR_OOM and handed back a model anyway";
        fatal++;
        continue;
      }

      ASSERT_EQ(result, GMDL_OK)
          << label << " (" << mode << "): refusing request " << nth
          << " gave " << (int)result
          << ", which is neither GMDL_OK nor GMDL_ERR_OOM";
      if (produced != reference) {
        // Reported once per mode. A defect here is usually one arm hit at
        // hundreds of indices, and three hundred identical failures bury the
        // two modes that have not run yet.
        mismatches++;
        EXPECT_EQ(mismatches, 1u)
            << label << " (" << mode << "): " << mismatches
            << " survived refusals produced a model the unrefused parse did "
               "not; the first was request " << first_mismatch;
        if (mismatches == 1) {
          first_mismatch = nth;
          EXPECT_EQ(produced, reference)
              << label << " (" << mode << "): refusing request " << nth
              << " was survived, but the model it produced is not the one "
                 "the unrefused parse produced - something was lost quietly";
        }
      }
      survived++;
    }

    EXPECT_GE(fatal, least_fatal[m])
        << label << " (" << mode << "): only " << fatal
        << " refusals were fatal, so failure arms this sweep used to reach "
           "are no longer being reached";
    std::printf("[     INFO ] %s (%s): %zu allocations, %zu fatal, %zu "
                "survived unchanged\n",
        label, mode, requests, fatal, survived);
  }

  // Refusing the request after the last one refuses nothing, which confirms
  // the count the sweep just walked twice was the whole of it.
  gmdltest::FailingAllocator exact(requests);
  std::string produced;
  EXPECT_EQ(parse(exact, &produced), GMDL_OK);
  EXPECT_FALSE(exact.failed())
      << label << ": the parse asked for more than it asked for last time";
  EXPECT_EQ(produced, reference);
  EXPECT_EQ(exact.live(), 0u);
}

/** Render a model the way a consumer would see it, for comparison. */
template <typename Model, typename Dump>
std::string dump_to_string(Model * model, Dump dump) {
  char * buffer = nullptr;
  size_t size = 0;
  FILE * sink = open_memstream(&buffer, &size);
  if (!sink) {
    return std::string();
  }
  GMDL_Result result = dump(model, sink);
  fclose(sink);
  std::string text = result == GMDL_OK ? std::string(buffer, size)
                                       : std::string("<dump failed>");
  free(buffer);
  return text;
}

// Measured floors, each set below what the mode actually finds, in the order
// of kSweepModes: one request, one append, exhausted.
const size_t kObjFloors[3] = {160, 160, 500};
const size_t kMtlFloors[3] = {10, 10, 18};

// Every allocation the OBJ loader makes can fail, and before this sweep
// existed not one of those arms had ever run - the same gap FailingSink
// closed for the dumper. An arm that reports the failure but keeps what it
// had already built is the quiet half of it, so the sweep counts blocks as
// well as reading return codes, and does not need a sanitizer to be worth
// running.
TEST(Allocator, EveryObjAllocationFailureIsReported) {
  sweep_allocation_failures(
      "obj", kObjFloors, [](gmdltest::FailingAllocator & a, std::string * out) {
        MemStream stream(rich_obj());
        GMDL_Obj * obj = nullptr;
        GMDL_Result result = gmdl_obj_load(stream.get(), nullptr, a.get(), &obj);
        // The model holds this allocator, so everything below would be
        // refused too; the budget has done its work by now.
        a.stop_failing();
        *out = obj ? dump_to_string(obj, gmdl_obj_dump) : std::string();
        gmdl_obj_free(obj);
        return result;
      });
}

TEST(Allocator, EveryMtlAllocationFailureIsReported) {
  sweep_allocation_failures(
      "mtl", kMtlFloors, [](gmdltest::FailingAllocator & a, std::string * out) {
        MemStream stream(rich_mtl());
        GMDL_Mtl * mtl = nullptr;
        GMDL_Result result = gmdl_mtl_load(stream.get(), nullptr, a.get(), &mtl);
        a.stop_failing();
        *out = mtl ? dump_to_string(mtl, gmdl_mtl_dump) : std::string();
        gmdl_mtl_free(mtl);
        return result;
      });
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
