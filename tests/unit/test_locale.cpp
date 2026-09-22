/**
 * @file
 *
 * The decimal separator belongs to the format, not to the caller's locale.
 *
 * OBJ and MTL always spell a decimal point ".". A program that links this
 * library may be running anywhere, and in a good many locales LC_NUMERIC's
 * separator is a comma - at which point an unguarded strtof() stops at the
 * "." and an unguarded printf("%.9g") writes "0,5", which is not OBJ.
 *
 * **These tests are worthless without a comma locale, so they refuse to pass
 * without one.** This machine has none installed - `locale -a` lists no
 * de_DE, and asking for it yields a decimal point of ".". A test that merely
 * set the locale and checked the library still worked would therefore pass
 * here while measuring nothing at all, which is the failure mode where a
 * broken harness reads as a green run. So each test below establishes a
 * positive control first: it asserts that plain snprintf, under the locale it
 * just installed, really does produce "0,5". Only once the hostile condition
 * is proven present does the assertion about the library mean anything.
 *
 * The locale is generated into a temporary directory with localedef and
 * reached through LOCPATH, which needs no root and leaves the system alone.
 */

#include "test_helpers.h"

#include "../../src/core/number_internal.h"

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <locale.h>
#include <string>
#include <vector>

using gmdltest::MemStream;


// glibc's newlocale() keeps one allocation for the LOCPATH search list, made
// in __argz_add_sep and never released - freelocale() does not take it back.
// Reaching a generated locale requires LOCPATH, so this test cannot avoid it.
//
// The suppression lives HERE, as LeakSanitizer's own per-binary hook, rather
// than in a shared suppressions file, and that is deliberate: it applies to
// this one executable, it sits next to the reason, and it cannot quietly
// widen to cover testObj or testMtl later. __argz_add_sep is reachable only
// from glibc's locale-path parsing, which nothing in this library calls, and
// and it was checked rather than assumed: with a deliberate 1234-byte leak
// planted in this file, LeakSanitizer still reported it while suppressing the
// glibc one, so the template narrows to what it names.
extern "C" const char * __lsan_default_suppressions(void) {
  return "leak:__argz_add_sep\n";
}

namespace {

/** A comma-decimal locale, generated on demand, or nullptr if impossible. */
class CommaLocale {
public:
  CommaLocale() {
    // Try what is already installed before generating anything.
    for (const char * name : {"de_DE.UTF-8", "fr_FR.UTF-8", "de_DE.utf8"}) {
      handle_ = newlocale(LC_NUMERIC_MASK, name, (locale_t)0);
      if (handle_ && separator_is_comma()) {
        name_ = name;
        return;
      }
      if (handle_) {
        freelocale(handle_);
        handle_ = (locale_t)0;
      }
    }

    // Generate one. localedef writes a directory LOCPATH can reach.
    dir_ = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp")
        + "/gmdl-locale-XXXXXX";
    std::vector<char> tmpl(dir_.begin(), dir_.end());
    tmpl.push_back('\0');
    if (!mkdtemp(tmpl.data())) {
      return;
    }
    dir_ = tmpl.data();
    // LD_PRELOAD is stripped for the child deliberately. Under the ASan
    // suite this process runs with the sanitizer runtime preloaded, and
    // system() hands that to localedef - which then trips LeakSanitizer on
    // leaks of its OWN, exits 1, and leaves us reporting "no locale could be
    // generated" for a reason that has nothing to do with locales. Measured:
    // exit 1 with the preload, exit 0 without.
    std::string cmd = "env -u LD_PRELOAD -u LD_LIBRARY_PATH "
                      "localedef -i de_DE -f UTF-8 '"
        + dir_ + "/de_DE.UTF-8' >/dev/null 2>&1";
    if (std::system(cmd.c_str()) != 0) {
      return;
    }
    setenv("LOCPATH", dir_.c_str(), 1);
    name_ = "de_DE.UTF-8";
    handle_ = newlocale(LC_NUMERIC_MASK, "de_DE.UTF-8", (locale_t)0);
    if (handle_ && !separator_is_comma()) {
      freelocale(handle_);
      handle_ = (locale_t)0;
    }
  }

  ~CommaLocale() {
    if (handle_) {
      freelocale(handle_);
    }
  }

  bool usable() const { return handle_ != (locale_t)0; }
  const char * name() const { return name_.c_str(); }
  locale_t get() const { return handle_; }

private:
  /** Does snprintf under this locale actually write a comma? */
  bool separator_is_comma() const {
    locale_t previous = uselocale(handle_);
    char buffer[16];
    snprintf(buffer, sizeof buffer, "%.1f", 0.5);
    uselocale(previous);
    return std::string(buffer) == "0,5";
  }

  locale_t handle_ = (locale_t)0;
  std::string dir_;
  std::string name_;
};

/** Held for the body of a test, so an assertion cannot leave it applied. */
class InComma {
public:
  explicit InComma(const CommaLocale & loc) : previous_(uselocale(loc.get())) {}
  ~InComma() { uselocale(previous_); }

private:
  locale_t previous_;
};

CommaLocale & comma() {
  static CommaLocale instance;
  return instance;
}

} // namespace

// The control, as its own test, so that a machine which cannot produce a
// comma locale reports THAT rather than silently passing the two below.
// Failing here is a broken harness, not a broken library, and the message
// says which.
TEST(Locale, TheHostileConditionCanBeProduced) {
  ASSERT_TRUE(comma().usable())
      << "no comma-decimal locale could be found or generated, so the two "
         "tests that follow would pass without exercising anything. Install "
         "a de_DE locale, or make localedef available, and re-run. This is a "
         "harness failure, not a library failure.";
}

// The pin must be the thread-local one on this platform. Nothing
// behavioural can check that - both arms read and write identical bytes, and
// they differ only while a conversion is in flight in another thread - so the
// check is structural or it does not exist. Ghoti.io Text shipped the
// process-wide arm for years with five behavioural tests passing over it.
TEST(Locale, ThePinIsThreadLocalOnThisPlatform) {
#ifdef GMDL_ALLOW_PROCESS_WIDE_LOCALE
  GTEST_SKIP() << "this build asked for the process-wide pin, so the "
                  "degraded arm is the intended one";
#else
  EXPECT_TRUE(gmdl_numeric_pin_is_thread_local())
      << "built against the process-wide fallback. That is correct only for a "
         "platform without uselocale(), and this is not one - check whether "
         "the feature test in src/core/number.c still selects the arm it "
         "means to.";
#endif
}

// A comma locale set the way a program without per-thread locales would set
// one. This holds under BOTH arms, which is what makes it the test worth
// having: the thread-local pin overrides the global for this thread, and the
// process-wide pin moves the global itself.
TEST(Locale, AGloballySetCommaLocaleIsAlsoPinned) {
  ASSERT_TRUE(comma().usable());
  const char * previous = setlocale(LC_NUMERIC, NULL);
  std::string saved = previous ? previous : "C";
  if (!setlocale(LC_NUMERIC, comma().name())) {
    GTEST_SKIP() << "the generated locale is not reachable through setlocale";
  }
  char control[16];
  snprintf(control, sizeof control, "%.1f", 0.5);
  ASSERT_STREQ(control, "0,5") << "the global locale did not take effect";

  MemStream stream("v 0.5 0.25 0.125\n");
  GMDL_Obj * obj = nullptr;
  ASSERT_EQ(gmdl_obj_load(stream.get(), nullptr, nullptr, &obj), GMDL_OK);
  ASSERT_NE(obj, nullptr);
  EXPECT_FLOAT_EQ(obj->vertices[0].x, 0.5f);
  gmdl_obj_free(obj);
  setlocale(LC_NUMERIC, saved.c_str());
}

TEST(Locale, ReadingIsUnaffectedByTheCallersLocale) {
  if (!gmdl_numeric_pin_is_thread_local()) {
    GTEST_SKIP() << "the process-wide arm cannot override a thread that has "
                    "its own locale, and is not claiming to";
  }
  ASSERT_TRUE(comma().usable());
  InComma held(comma());

  // Positive control: prove the hostile condition is actually in force for
  // THIS thread, right now, before asserting anything about the library.
  char control[16];
  snprintf(control, sizeof control, "%.1f", 0.5);
  ASSERT_STREQ(control, "0,5") << "the locale did not take effect";

  MemStream stream("v 0.5 0.25 0.125\n");
  GMDL_Obj * obj = nullptr;
  ASSERT_EQ(gmdl_obj_load(stream.get(), nullptr, nullptr, &obj), GMDL_OK);
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->vertex_count, 1u);
  EXPECT_FLOAT_EQ(obj->vertices[0].x, 0.5f) << "read as if the '.' were junk";
  EXPECT_FLOAT_EQ(obj->vertices[0].y, 0.25f);
  EXPECT_FLOAT_EQ(obj->vertices[0].z, 0.125f);
  gmdl_obj_free(obj);
}

TEST(Locale, WritingUsesTheFormatsSeparatorNotTheLocales) {
  if (!gmdl_numeric_pin_is_thread_local()) {
    GTEST_SKIP() << "the process-wide arm cannot override a thread that has "
                    "its own locale, and is not claiming to";
  }
  ASSERT_TRUE(comma().usable());
  InComma held(comma());

  char control[16];
  snprintf(control, sizeof control, "%.1f", 0.5);
  ASSERT_STREQ(control, "0,5") << "the locale did not take effect";

  MemStream stream("v 0.5 0.25 0.125\n");
  GMDL_Obj * obj = nullptr;
  ASSERT_EQ(gmdl_obj_load(stream.get(), nullptr, nullptr, &obj), GMDL_OK);

  char * buffer = nullptr;
  size_t size = 0;
  FILE * sink = open_memstream(&buffer, &size);
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_obj_dump(obj, sink), GMDL_OK);
  fclose(sink);

  const std::string written(buffer, size);
  EXPECT_NE(written.find("0.5"), std::string::npos)
      << "wrote: " << written;
  EXPECT_EQ(written.find(','), std::string::npos)
      << "a comma in the output is not OBJ: " << written;
  free(buffer);
  gmdl_obj_free(obj);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
