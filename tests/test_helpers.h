/**
 * @file
 *
 * Shared helpers for the Ghoti.io Model unit tests.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GMDL_TEST_HELPERS_H
#define GHOTI_IO_GMDL_TEST_HELPERS_H

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/cutil/file.h>
#include <ghoti.io/cutil/path.h>
#include <ghoti.io/model/model.h>

namespace gmdltest {

/**
 * Directory holding the checked-in fixtures. The Makefile bakes in
 * GMDL_TEST_DATA so the binaries can run from the build tree; the environment
 * variable wins when it is set, and there is a relative fallback for a manual
 * build.
 */
inline std::string data_dir() {
  const char * env = std::getenv("GMDL_TEST_DATA");
  if (env) {
    return std::string(env);
  }
#ifdef GMDL_TEST_DATA
  return std::string(GMDL_TEST_DATA);
#else
  return std::string("tests/data");
#endif
}

/** A path that is guaranteed not to exist. */
inline const char * missing_path() {
  return "/nonexistent/ghoti.io/definitely/not/here.dat";
}

/** Path to a checked-in fixture. */
inline std::string data(const std::string & relative) {
  char joined[1024];
  if (gcu_path_join(GCU_PATH_NATIVE, data_dir().c_str(), relative.c_str(),
          joined, sizeof(joined), nullptr)
      != GCU_PATH_OK) {
    // Only reachable if the data directory is absurdly long. Hand back
    // something that cannot open, rather than a truncated path that could.
    return std::string(missing_path());
  }
  return std::string(joined);
}

/**
 * A file written to a temporary path and removed when the object goes out of
 * scope. Used only by the tests that exercise the file entry points; the rest
 * parse from memory, which is the point of the stream API.
 *
 * The file comes from cutil, which creates it in whatever directory
 * gcu_path_temp_dir() names - so $TMPDIR is honoured rather than /tmp being
 * assumed - and which chooses the name and creates the file in one step that
 * fails if the name is taken. The helper this replaced invented a name,
 * deleted it, and reopened it with an extension appended, leaving a window in
 * which something else could take the path. The extension itself is gone with
 * it: nothing in this library decides anything from a file's name.
 *
 * The handle stays open for the object's lifetime. A test that reopens
 * path() to write to it is reopening the same file, which is what the dump
 * round-trip tests do.
 */
class TempFile {
public:
  explicit TempFile(const std::string & contents) {
    if (gcu_file_temp_create(&temp_, nullptr, "gmdl_test", nullptr)
        != GCU_FILE_OK) {
      return;
    }
    path_ = std::string(gcu_file_temp_path(&temp_));
    FILE * stream = gcu_file_temp_stream(&temp_);
    if (!contents.empty()
        && fwrite(contents.data(), 1, contents.size(), stream)
            != contents.size()) {
      gcu_file_temp_abort(&temp_);
      return;
    }
    if (fflush(stream) != 0) {
      gcu_file_temp_abort(&temp_);
      return;
    }
    valid_ = true;
  }

  TempFile(const TempFile &) = delete;
  TempFile & operator=(const TempFile &) = delete;

  /// Closes the file and deletes it. Accepts an already-spent handle, so it
  /// is correct whether or not the constructor got that far.
  ~TempFile() { gcu_file_temp_abort(&temp_); }

  const char * path() const { return path_.c_str(); }
  bool valid() const { return valid_; }

private:
  GCU_File_Temp temp_{};
  std::string path_;
  bool valid_ = false;
};

/**
 * A `FILE *` that accepts a set number of writes and fails every one after.
 *
 * The dumpers are almost all error handling by line count - every `fprintf`
 * is checked - and until this existed not one of those arms had ever run.
 * A test can only see them by handing over a stream that fails, and it has
 * to fail on demand rather than always, because failing on the first write
 * exercises exactly one of them.
 *
 * Buffering is off, so one `fprintf` is one write: constructed with `n`, the
 * sink serves n writes and fails the one after. Sweeping n from zero upwards
 * walks the failure through the whole of a dump.
 *
 * `fopencookie` is glibc's; the tests run there. Everything else in this
 * header is portable, and this is the one thing that cannot be.
 */
class FailingSink {
public:
  explicit FailingSink(size_t allow) : remaining_(allow) {
    cookie_io_functions_t fns = {};
    fns.write = &FailingSink::write_cb;
    file_ = fopencookie(this, "w", fns);
    if (file_) {
      setvbuf(file_, nullptr, _IONBF, 0);
    }
  }

  FailingSink(const FailingSink &) = delete;
  FailingSink & operator=(const FailingSink &) = delete;

  ~FailingSink() {
    if (file_) {
      fclose(file_);
    }
  }

  FILE * get() const { return file_; }

  /** Whether the write budget ran out, i.e. a failure was actually served. */
  bool failed() const { return failed_; }

private:
  static ssize_t write_cb(void * cookie, const char * buffer, size_t size) {
    (void)buffer;
    FailingSink * self = static_cast<FailingSink *>(cookie);
    if (self->remaining_ == 0) {
      self->failed_ = true;
      errno = ENOSPC;
      return -1;
    }
    self->remaining_--;
    return static_cast<ssize_t>(size);
  }

  size_t remaining_;
  bool failed_ = false;
  FILE * file_ = nullptr;
};

/**
 * An allocator that refuses one nominated request and serves every other.
 *
 * The counterpart of FailingSink, for the other half of the library. The
 * loaders are dense with allocation-failure arms - every array append, every
 * string copy - and none of them can run against an allocator that always
 * succeeds. Nor can they run against one that always fails: that reaches the
 * first arm and no other.
 *
 * So the refusal is a dial. Constructed with `n`, this serves every request
 * but the nth; sweeping n from zero upwards walks a single refusal through
 * the whole of a parse, one site at a time.
 *
 * **How many requests to refuse is a parameter, and all three settings are
 * needed.** Each was arrived at by planting a defect the previous setting
 * could not see:
 *
 * - `run = 0`, every request from the nth onwards. What genuine exhaustion
 *   looks like, and the only setting that reaches the loaders'
 *   `gcu_array_append() failed` arms at all - because cutil's reserve_n()
 *   answers a refused 1.5x growth by retrying at the exact size needed, on
 *   purpose, so refusing one request never fails an append.
 * - `run = 1`, the nth request alone. The only setting that can show a
 *   refusal being survived *and losing something*: under sustained refusal an
 *   arm that gives up quietly carries on, asks for the next allocation, is
 *   refused again, and the parse ends up failing for the right reason by
 *   accident.
 * - `run = 2`, the nth and the one after. What it takes to fail a single
 *   *append* rather than a single request, since the retry makes one logical
 *   append cost two. `run = 1` cannot fail an append at all and `run = 0`
 *   cannot survive one; a quiet give-up in an append arm is invisible to
 *   both. A planted one was.
 *
 * `run = 2` does not subsume `run = 1`: where two unrelated single-request
 * sites sit next to each other it refuses both, and a defect at the first can
 * be hidden by the parse dying correctly at the second.
 *
 * It also counts live blocks, because an abandoned parse has two ways to be
 * wrong and the interesting one is silent: reporting the failure and keeping
 * what it had already built. `live()` is the check for that, and it means the
 * sweep does not depend on running under a sanitizer to be worth anything.
 *
 * Refusal is spelled exactly as the allocator contract spells it - NULL, with
 * the caller's block untouched on a realloc, since a realloc() that fails
 * must leave the original allocation alone.
 */
class FailingAllocator {
public:
  /**
   * Refuse @p run requests starting at request number @p fail_at.
   *
   * @param fail_at The first request to refuse, counting from zero.
   *   `(size_t)-1` refuses none, which is how a parse's cost is measured
   *   before the sweep walks it.
   * @param run How many consecutive requests to refuse; 0 means every one
   *   from @p fail_at onwards. See the note above: the three settings see
   *   three different things and none of them sees all of it.
   */
  explicit FailingAllocator(size_t fail_at, size_t run = 1)
      : fail_at_(fail_at), run_(run) {
    allocator_.ctx = this;
    allocator_.malloc_fn = &FailingAllocator::malloc_cb;
    allocator_.calloc_fn = &FailingAllocator::calloc_cb;
    allocator_.realloc_fn = &FailingAllocator::realloc_cb;
    allocator_.free_fn = &FailingAllocator::free_cb;
  }

  // Neither copyable nor movable: the struct handed to the library holds a
  // pointer back to this object.
  FailingAllocator(const FailingAllocator &) = delete;
  FailingAllocator & operator=(const FailingAllocator &) = delete;

  const GMDL_Allocator * get() const { return &allocator_; }

  /** Whether a nominated request happened, i.e. a refusal was served. */
  bool failed() const { return failed_; }

  /** Blocks handed out and not yet given back. */
  size_t live() const { return live_; }

  /** Requests made, refused ones included. */
  size_t requests() const { return requests_; }

  /**
   * Stop refusing, without forgetting that a refusal was served.
   *
   * A model carries the allocator it was built with, so whatever a test does
   * with the model afterwards - dumping it, freeing it - comes back through
   * this object. Leaving the refusal armed would make those fail too, and a
   * sweep would end up measuring its own instrument. `failed()` still reports
   * what happened while it was armed.
   */
  void stop_failing() { fail_at_ = static_cast<size_t>(-1); }

private:
  /** Whether this request is refused, and record it if so. */
  bool refuse() {
    size_t index = requests_++;
    if (index < fail_at_) {
      return false;
    }
    // Subtraction rather than fail_at_ + run_, which overflows for the
    // (size_t)-1 that means "refuse nothing".
    if (run_ != 0 && index - fail_at_ >= run_) {
      return false;
    }
    failed_ = true;
    return true;
  }

  static void * malloc_cb(void * ctx, size_t size) {
    FailingAllocator * self = static_cast<FailingAllocator *>(ctx);
    if (self->refuse()) {
      return nullptr;
    }
    // A zero-size request must still yield a usable pointer, so that NULL
    // always means failure; see allocator.h.
    void * p = std::malloc(size ? size : 1);
    if (p) {
      self->live_++;
    }
    return p;
  }

  static void * calloc_cb(void * ctx, size_t nitems, size_t size) {
    FailingAllocator * self = static_cast<FailingAllocator *>(ctx);
    // Overflow is an allocation failure, not a truncated block (allocator.h).
    // It is not the nominated refusal either, so it does not consume it.
    if (nitems && size > static_cast<size_t>(-1) / nitems) {
      return nullptr;
    }
    if (self->refuse()) {
      return nullptr;
    }
    size_t total = nitems * size;
    void * p = std::calloc(1, total ? total : 1);
    if (p) {
      self->live_++;
    }
    return p;
  }

  static void * realloc_cb(void * ctx, void * ptr, size_t size) {
    FailingAllocator * self = static_cast<FailingAllocator *>(ctx);
    if (self->refuse()) {
      return nullptr; // The caller's block is still theirs and still valid.
    }
    void * p = std::realloc(ptr, size ? size : 1);
    if (p && !ptr) {
      self->live_++;
    }
    return p;
  }

  static void free_cb(void * ctx, void * ptr) {
    FailingAllocator * self = static_cast<FailingAllocator *>(ctx);
    if (ptr) {
      self->live_--;
    }
    std::free(ptr);
  }

  GMDL_Allocator allocator_{};
  size_t fail_at_;
  size_t run_;
  size_t requests_ = 0;
  size_t live_ = 0;
  bool failed_ = false;
};

/**
 * A stream over a string that lives as long as the holder, so that a test can
 * state its input inline.
 */
class MemStream {
public:
  explicit MemStream(std::string text) : text_(std::move(text)) {
    EXPECT_EQ(gmdl_stream_create_memory(text_.data(), text_.size(), &stream_),
        GMDL_OK);
  }

  MemStream(const MemStream &) = delete;
  MemStream & operator=(const MemStream &) = delete;

  ~MemStream() { gmdl_stream_destroy(stream_); }

  GMDL_Stream * get() const { return stream_; }

private:
  std::string text_;
  GMDL_Stream * stream_ = nullptr;
};

} // namespace gmdltest

#endif // GHOTI_IO_GMDL_TEST_HELPERS_H
