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
// from glibc's locale-path parsing, which nothing in this library calls.
//
// **It was checked rather than assumed, and re-checking it is harder than it
// looks.** Two natural ways to plant a leak are invisible to LeakSanitizer
// for reasons that have nothing to do with any suppression, and both look
// exactly like a suppression that is too wide:
//
//   volatile void * p = malloc(1234);                  // NOT reported:
//   (void)p;                                           // the conservative
//                                                      // stack scan finds it
//
//   void ** h = malloc(8); h[0] = malloc(1234);        // NOT reported: ASan's
//   free(h);                                           // quarantine still
//                                                      // holds the contents
//
//   void ** h = malloc(8); h[0] = malloc(1234);        // reported
//   h[0] = nullptr; free(h);
//
// So the plant has to be genuinely unreachable, and the control is to run it
// with the suppression turned OFF: if a plant is invisible either way, its
// invisibility says nothing about the suppression. Measured both ways - the
// third shape is reported with the suppression on and with it off, and the
// first is invisible with it on and with it off - so the template narrows to
// what it names.
//
// One more trap in the checking: do not grep the output for "detected memory
// leaks". The glibc leak raises that banner too whenever the suppression is
// off, so the test for whether the *planted* leak was seen has to name its
// own size. Getting this wrong made the suppression look guilty.
// Ghoti.io Image hit the same three shapes generating a locale for its own
// LC_NUMERIC test.
extern "C" const char * __lsan_default_suppressions(void) {
  return "leak:__argz_add_sep\n";
}

namespace {

#ifdef _WIN32

// The Windows CRT has no locale_t to hold, so "being in a locale" is a mode
// and a name: _configthreadlocale() gives the calling thread a locale of its
// own, and setlocale() then changes that one only. That is the mechanism the
// library's Windows pin uses as well, which is why these tests are what
// verify it - the pin has to win against a thread already in its own comma
// locale, and has to leave both the mode and the name as it found them.
//
// Where the CRT has no per-thread locale at all - MinGW against msvcrt.dll -
// _configthreadlocale() refuses and setlocale() moves the process. The
// library is then built with the process-wide pin, the thread-local tests
// skip themselves, and the global-locale tests are the ones that bite.
//
// Nothing is generated: Windows ships every locale. What differs is the
// names. The CRT takes BCP 47 tags ("de-DE") and the older
// Language_Country.codepage spelling, but not POSIX's "de_DE.UTF-8", so the
// list is tried in order and the positive control below decides - a name
// that is accepted but does not produce "0,5" is no use here.
const char * const kCommaLocaleNames[] = {"de-DE", "German_Germany.1252",
    "fr-FR", "French_France.1252", "deu", "german"};

/** Puts the calling thread in its own locale, with LC_NUMERIC set to @p name,
 *  and restores the thread's mode and LC_NUMERIC on destruction. */
class ThreadNumericLocale {
public:
  explicit ThreadNumericLocale(const char * name)
      : mode_(_configthreadlocale(_ENABLE_PER_THREAD_LOCALE)) {
    const char * current = setlocale(LC_NUMERIC, nullptr);
    saved_ = current ? current : "C";
    ok_ = setlocale(LC_NUMERIC, name) != nullptr;
  }
  ~ThreadNumericLocale() {
    setlocale(LC_NUMERIC, saved_.c_str());
    // -1 is msvcrt refusing: there was no mode change to undo, and handing
    // -1 back would be an invalid argument to a CRT that does implement it.
    if (mode_ != -1) {
      _configthreadlocale(mode_);
    }
  }
  ThreadNumericLocale(const ThreadNumericLocale &) = delete;
  ThreadNumericLocale & operator=(const ThreadNumericLocale &) = delete;

  bool ok() const { return ok_; }

private:
  int mode_;
  std::string saved_;
  bool ok_ = false;
};

/** Does snprintf, as the calling thread stands right now, write a comma? */
bool separator_is_comma_here() {
  char buffer[16];
  snprintf(buffer, sizeof buffer, "%.1f", 0.5);
  return std::string(buffer) == "0,5";
}

/** A comma-decimal locale name the CRT accepts, or none. */
class CommaLocale {
public:
  CommaLocale() {
    for (const char * name : kCommaLocaleNames) {
      ThreadNumericLocale probe(name);
      if (probe.ok() && separator_is_comma_here()) {
        name_ = name;
        return;
      }
    }
  }

  bool usable() const { return !name_.empty(); }
  const char * name() const { return name_.c_str(); }

private:
  std::string name_;
};

/** Held for the body of a test, so an assertion cannot leave it applied. */
class InComma {
public:
  explicit InComma(const CommaLocale & loc) : held_(loc.name()) {}

private:
  ThreadNumericLocale held_;
};

#else

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

#endif

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

// The same for the writer, and for what is left behind: the pin must put the
// caller's separator back when it ends. Under the process-wide arm that is
// the arm's whole restore path, and this is the only test that reaches it -
// the two thread-local tests below skip themselves there. It is where a
// platform without per-thread locales is checked at all.
TEST(Locale, AGloballySetCommaLocaleIsPinnedForWritingAndPutBack) {
  ASSERT_TRUE(comma().usable());
  const char * previous = setlocale(LC_NUMERIC, NULL);
  const std::string saved = previous ? previous : "C";
  struct Restore {
    const std::string & name;
    ~Restore() { setlocale(LC_NUMERIC, name.c_str()); }
  } restore{saved};
  if (!setlocale(LC_NUMERIC, comma().name())) {
    GTEST_SKIP() << "the generated locale is not reachable through setlocale";
  }
  char control[16];
  snprintf(control, sizeof control, "%.1f", 0.5);
  ASSERT_STREQ(control, "0,5") << "the global locale did not take effect";

  MemStream stream("v 0.5 0.25 0.125\n");
  GMDL_Obj * obj = nullptr;
  ASSERT_EQ(gmdl_obj_load(stream.get(), nullptr, nullptr, &obj), GMDL_OK);
  snprintf(control, sizeof control, "%.1f", 0.5);
  EXPECT_STREQ(control, "0,5") << "the load did not put the locale back";

  gmdltest::CapturedOutput sink;
  ASSERT_NE(sink.get(), nullptr);
  ASSERT_EQ(gmdl_obj_dump(obj, sink.get()), GMDL_OK);
  gmdl_obj_free(obj);
  const std::string written = sink.finish();
  EXPECT_NE(written.find("0.5"), std::string::npos) << "wrote: " << written;
  EXPECT_EQ(written.find(','), std::string::npos)
      << "a comma in the output is not OBJ: " << written;

  snprintf(control, sizeof control, "%.1f", 0.5);
  EXPECT_STREQ(control, "0,5") << "the dump did not put the locale back";
  EXPECT_STREQ(setlocale(LC_NUMERIC, NULL), comma().name());
}

#ifdef _WIN32
// The Windows pin changes two things to take effect - the thread's locale
// mode and its LC_NUMERIC - and has to put both back. A thread that was on
// the global locale must return to it rather than keep a private copy that
// no longer follows setlocale(); one that had its own locale must keep it.
// Neither is visible in the bytes a load produces, so it is checked directly.
TEST(Locale, ThePinPutsBackTheThreadsLocaleModeAndName) {
  ASSERT_TRUE(comma().usable());
  for (bool own_locale : {false, true}) {
    SCOPED_TRACE(own_locale ? "thread with its own locale"
                            : "thread on the global locale");
    ThreadNumericLocale held(comma().name());
    ASSERT_TRUE(held.ok());
    if (!own_locale) {
      // ThreadNumericLocale asked for a per-thread locale; go back to the
      // global one, which it has just set to the comma locale on a CRT that
      // cannot do otherwise, and set it here explicitly on one that can.
      _configthreadlocale(_DISABLE_PER_THREAD_LOCALE);
      ASSERT_NE(setlocale(LC_NUMERIC, comma().name()), nullptr);
    }
    const int mode_before = _configthreadlocale(0);
    ASSERT_TRUE(separator_is_comma_here());

    GMDL_Numeric_Scope scope;
    gmdl_numeric_scope_begin(&scope);
    EXPECT_NE(scope.applied, nullptr) << "the pin did not take";
    char inside[16];
    snprintf(inside, sizeof inside, "%.1f", 0.5);
    EXPECT_STREQ(inside, "0.5");
    gmdl_numeric_scope_end(&scope);

    EXPECT_EQ(_configthreadlocale(0), mode_before);
    EXPECT_TRUE(separator_is_comma_here());
    EXPECT_STREQ(setlocale(LC_NUMERIC, NULL), comma().name());
  }
}
#endif

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

  gmdltest::CapturedOutput sink;
  ASSERT_NE(sink.get(), nullptr);
  ASSERT_EQ(gmdl_obj_dump(obj, sink.get()), GMDL_OK);

  const std::string written = sink.finish();
  EXPECT_NE(written.find("0.5"), std::string::npos)
      << "wrote: " << written;
  EXPECT_EQ(written.find(','), std::string::npos)
      << "a comma in the output is not OBJ: " << written;
  gmdl_obj_free(obj);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
