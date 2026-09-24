/**
 * @file
 *
 * Unit tests for the byte-stream abstraction.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <string>
#include <vector>

using gmdltest::MemStream;
using gmdltest::TempFile;

namespace {

/** Read a whole stream one line at a time. */
std::vector<std::string> read_lines(GMDL_Stream * stream, size_t buffer_size) {
  std::vector<std::string> lines;
  std::vector<char> buffer(buffer_size);
  for (;;) {
    size_t length = 0;
    GMDL_Result r =
        gmdl_stream_read_line(stream, buffer.data(), buffer.size(), &length);
    if (r != GMDL_OK) {
      break;
    }
    lines.push_back(std::string(buffer.data(), length));
  }
  return lines;
}

} // namespace

TEST(Stream, CreateRejectsNullOutput) {
  EXPECT_EQ(gmdl_stream_create_memory("x", 1, nullptr), GMDL_ERR_INVALID);
}

TEST(Stream, CreateRejectsNullDataWithNonZeroSize) {
  GMDL_Stream * s = nullptr;
  EXPECT_EQ(gmdl_stream_create_memory(nullptr, 4, &s), GMDL_ERR_INVALID);
  EXPECT_EQ(s, nullptr);
}

TEST(Stream, EmptyStreamIsAllowed) {
  GMDL_Stream * s = nullptr;
  ASSERT_EQ(gmdl_stream_create_memory(nullptr, 0, &s), GMDL_OK);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(gmdl_stream_size(s), 0u);
  EXPECT_EQ(gmdl_stream_tell(s), 0u);
  EXPECT_NE(gmdl_stream_eof(s), 0);
  gmdl_stream_destroy(s);
}

TEST(Stream, DestroyNullIsSafe) {
  gmdl_stream_destroy(nullptr);
}

TEST(Stream, AccessorsTolerateNull) {
  EXPECT_EQ(gmdl_stream_tell(nullptr), 0u);
  EXPECT_EQ(gmdl_stream_size(nullptr), 0u);
  EXPECT_NE(gmdl_stream_eof(nullptr), 0);
  EXPECT_EQ(gmdl_stream_seek(nullptr, 0), GMDL_ERR_INVALID);
}

// Both answers, not only the one at the end. A stream with bytes left is the
// case every parse is in for all but its last read, and nothing asked for it:
// `gmdl_stream_eof()` was only ever called after a seek to the end.
TEST(Stream, EofIsFalseWhileBytesRemain) {
  MemStream s("abcdef");
  EXPECT_EQ(gmdl_stream_eof(s.get()), 0);
  ASSERT_EQ(gmdl_stream_seek(s.get(), 5), GMDL_OK);
  EXPECT_EQ(gmdl_stream_eof(s.get()), 0) << "one byte left is not the end";
  ASSERT_EQ(gmdl_stream_seek(s.get(), 6), GMDL_OK);
  EXPECT_NE(gmdl_stream_eof(s.get()), 0);
}

TEST(Stream, ReadRejectsNullArguments) {
  MemStream s("abcdef");
  char buffer[4] = {};
  size_t got = 0;
  EXPECT_EQ(gmdl_stream_read(nullptr, buffer, 4, &got), GMDL_ERR_INVALID);
  EXPECT_EQ(gmdl_stream_read(s.get(), buffer, 4, nullptr), GMDL_ERR_INVALID);
  EXPECT_EQ(gmdl_stream_read(s.get(), nullptr, 4, &got), GMDL_ERR_INVALID);
  // A null buffer with nothing to read into it is not an error.
  EXPECT_EQ(gmdl_stream_read(s.get(), nullptr, 0, &got), GMDL_OK);
}

TEST(Stream, ReadReturnsWhatIsThere) {
  MemStream s("abcdef");
  char buffer[4] = {};
  size_t got = 0;
  ASSERT_EQ(gmdl_stream_read(s.get(), buffer, 4, &got), GMDL_OK);
  EXPECT_EQ(got, 4u);
  EXPECT_EQ(std::string(buffer, 4), "abcd");
  EXPECT_EQ(gmdl_stream_tell(s.get()), 4u);

  // A short read at the end is not an error.
  ASSERT_EQ(gmdl_stream_read(s.get(), buffer, 4, &got), GMDL_OK);
  EXPECT_EQ(got, 2u);
  EXPECT_NE(gmdl_stream_eof(s.get()), 0);

  ASSERT_EQ(gmdl_stream_read(s.get(), buffer, 4, &got), GMDL_OK);
  EXPECT_EQ(got, 0u);
}

TEST(Stream, SeekMovesAndRejectsPastTheEnd) {
  MemStream s("abcdef");
  ASSERT_EQ(gmdl_stream_seek(s.get(), 6), GMDL_OK) << "seeking to the end is ok";
  EXPECT_NE(gmdl_stream_eof(s.get()), 0);
  EXPECT_EQ(gmdl_stream_seek(s.get(), 7), GMDL_ERR_INVALID);
  ASSERT_EQ(gmdl_stream_seek(s.get(), 2), GMDL_OK);
  char buffer[2] = {};
  size_t got = 0;
  ASSERT_EQ(gmdl_stream_read(s.get(), buffer, 2, &got), GMDL_OK);
  EXPECT_EQ(std::string(buffer, 2), "cd");
}

TEST(StreamReadLine, SplitsOnEveryLineEnding) {
  // "\n", "\r\n" and a bare "\r" all end a line, and "\r\n" counts once.
  MemStream s("unix\nwindows\r\nmac\rlast");
  std::vector<std::string> lines = read_lines(s.get(), 64);
  ASSERT_EQ(lines.size(), 4u);
  EXPECT_EQ(lines[0], "unix");
  EXPECT_EQ(lines[1], "windows");
  EXPECT_EQ(lines[2], "mac");
  EXPECT_EQ(lines[3], "last") << "a final line with no terminator still counts";
}

// A carriage return as the very last byte. The reader looks past a "\r" for
// the "\n" of a "\r\n" pair, and with the "\r" at the end there is nothing to
// look at - so the bounds test is the only thing standing between it and a
// read one byte past the buffer. "mac\rlast" reaches the other half of that
// test and this reaches this one.
TEST(StreamReadLine, ACarriageReturnAtTheEndEndsTheLine) {
  MemStream s("only\r");
  std::vector<std::string> lines = read_lines(s.get(), 64);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "only");
}

TEST(StreamReadLine, EmptyLinesAreKept) {
  MemStream s("a\n\nb\n");
  std::vector<std::string> lines = read_lines(s.get(), 64);
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[1], "");
}

TEST(StreamReadLine, EndOfStreamReportsIo) {
  MemStream s("");
  char buffer[8] = {'x'};
  EXPECT_EQ(gmdl_stream_read_line(s.get(), buffer, sizeof(buffer), nullptr),
      GMDL_ERR_IO);
  EXPECT_EQ(buffer[0], '\0');
}

// A line the caller's buffer cannot hold is reported rather than truncated:
// handing back a prefix would let the remainder be parsed as though it were a
// line of its own, which is what a fixed fgets() buffer used to do.
TEST(StreamReadLine, OverlongLineIsAnErrorNotATruncation) {
  MemStream s("0123456789\nshort\n");
  char buffer[8] = {};
  EXPECT_EQ(gmdl_stream_read_line(s.get(), buffer, sizeof(buffer), nullptr),
      GMDL_ERR_LIMIT);
  EXPECT_EQ(buffer[0], '\0');

  // The stream is positioned after the offending line either way.
  size_t length = 0;
  ASSERT_EQ(
      gmdl_stream_read_line(s.get(), buffer, sizeof(buffer), &length), GMDL_OK);
  EXPECT_EQ(std::string(buffer, length), "short");
}

TEST(StreamReadLine, LineExactlyFillingTheBufferIsAccepted) {
  MemStream s("1234567\n");
  char buffer[8] = {};
  size_t length = 0;
  ASSERT_EQ(
      gmdl_stream_read_line(s.get(), buffer, sizeof(buffer), &length), GMDL_OK);
  EXPECT_EQ(length, 7u);
  EXPECT_STREQ(buffer, "1234567");
}

TEST(StreamReadLine, RejectsBadArguments) {
  MemStream s("a\n");
  char buffer[8] = {};
  EXPECT_EQ(gmdl_stream_read_line(nullptr, buffer, sizeof(buffer), nullptr),
      GMDL_ERR_INVALID);
  EXPECT_EQ(
      gmdl_stream_read_line(s.get(), nullptr, 8, nullptr), GMDL_ERR_INVALID);
  EXPECT_EQ(
      gmdl_stream_read_line(s.get(), buffer, 0, nullptr), GMDL_ERR_INVALID);
}

TEST(StreamFile, ReadsAFile) {
  TempFile f("hello\nworld\n");
  ASSERT_TRUE(f.valid());
  GMDL_Stream * s = nullptr;
  ASSERT_EQ(gmdl_stream_create_file(f.path(), nullptr, &s), GMDL_OK);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(gmdl_stream_size(s), 12u);
  std::vector<std::string> lines = read_lines(s, 64);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0], "hello");
  gmdl_stream_destroy(s);
}

// The file is read in chunks rather than by a seek-to-end measurement, so a
// file larger than one chunk has to come back whole.
TEST(StreamFile, ReadsAFileLargerThanOneChunk) {
  std::string text;
  const size_t kLines = 20000;
  for (size_t i = 0; i < kLines; i++) {
    text += "line " + std::to_string(i) + "\n";
  }
  TempFile f(text);
  ASSERT_TRUE(f.valid());

  GMDL_Stream * s = nullptr;
  ASSERT_EQ(gmdl_stream_create_file(f.path(), nullptr, &s), GMDL_OK);
  EXPECT_EQ(gmdl_stream_size(s), text.size());
  std::vector<std::string> lines = read_lines(s, 64);
  EXPECT_EQ(lines.size(), kLines);
  EXPECT_EQ(lines.back(), "line " + std::to_string(kLines - 1));
  gmdl_stream_destroy(s);
}

TEST(StreamFile, MissingFileReportsIo) {
  GMDL_Stream * s = nullptr;
  EXPECT_EQ(
      gmdl_stream_create_file(gmdltest::missing_path(), nullptr, &s),
      GMDL_ERR_IO);
  EXPECT_EQ(s, nullptr);
}

TEST(StreamFile, RejectsBadArguments) {
  GMDL_Stream * s = nullptr;
  EXPECT_EQ(gmdl_stream_create_file(nullptr, nullptr, &s), GMDL_ERR_INVALID);
  EXPECT_EQ(gmdl_stream_create_file("x", nullptr, nullptr), GMDL_ERR_INVALID);
}

TEST(Result, StringCoversEveryCode) {
  for (int i = 0; i < GMDL_RESULT_COUNT; i++) {
    const char * message = gmdl_result_string((GMDL_Result)i);
    ASSERT_NE(message, nullptr) << "code " << i;
    EXPECT_GT(strlen(message), 0u) << "code " << i;
  }
  EXPECT_STREQ(gmdl_result_string((GMDL_Result)9999), "Unknown error");
}

TEST(Limits, DefaultsAreFilledIn) {
  GMDL_Limits limits;
  memset(&limits, 0xAA, sizeof(limits));
  gmdl_limits_default(&limits);
  EXPECT_GT(limits.max_line_length, 0u);
  gmdl_limits_default(nullptr); // Must not crash.
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
