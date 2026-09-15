/**
 * @file
 *
 * Shared helpers for the Ghoti.io Model unit tests.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GMDL_TEST_HELPERS_H
#define GHOTI_IO_GMDL_TEST_HELPERS_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include <gtest/gtest.h>

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

/** Path to a checked-in fixture. */
inline std::string data(const std::string & relative) {
  return data_dir() + "/" + relative;
}

/** A path that is guaranteed not to exist. */
inline const char * missing_path() {
  return "/nonexistent/ghoti.io/definitely/not/here.dat";
}

/**
 * A file written to a temporary path and removed when the object goes out of
 * scope. Used only by the tests that exercise the file entry points; the rest
 * parse from memory, which is the point of the stream API.
 */
class TempFile {
public:
  explicit TempFile(const std::string & contents, const char * suffix = ".tmp") {
    char name[] = "/tmp/gmdl_test_XXXXXX";
    int fd = mkstemp(name);
    if (fd >= 0) {
      close(fd);
      ::remove(name);
    }
    path_ = std::string(name) + suffix;
    FILE * f = fopen(path_.c_str(), "wb");
    if (f) {
      if (!contents.empty()) {
        fwrite(contents.data(), 1, contents.size(), f);
      }
      fclose(f);
      valid_ = true;
    }
  }

  TempFile(const TempFile &) = delete;
  TempFile & operator=(const TempFile &) = delete;

  ~TempFile() {
    if (valid_) {
      ::remove(path_.c_str());
    }
  }

  const char * path() const { return path_.c_str(); }
  bool valid() const { return valid_; }

private:
  std::string path_;
  bool valid_ = false;
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
