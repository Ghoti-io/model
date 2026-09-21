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
