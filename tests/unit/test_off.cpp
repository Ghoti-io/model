/**
 * @file
 *
 * Unit tests for the OFF parser and dumper.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using gmdltest::FailingSink;
using gmdltest::MemStream;
using gmdltest::TempFile;

namespace {

const char * kTriangle =
    "OFF\n"
    "3 1 0\n"
    "0 0 0\n"
    "1 0 0\n"
    "0 1 0\n"
    "3 0 1 2\n";

const char * kOneLineHeader =
    "OFF 3 1 0\n"
    "0 0 0\n"
    "1 0 0\n"
    "0 1 0\n"
    "3 0 1 2\n";

GMDL_Off * load_text(const std::string & text,
    const GMDL_Off_Options * options = nullptr) {
  MemStream stream(text);
  GMDL_Off * off = nullptr;
  GMDL_Result r = gmdl_off_load(stream.get(), options, nullptr, &off);
  EXPECT_EQ(r, GMDL_OK) << gmdl_result_string(r);
  return off;
}

GMDL_Result load_text_expecting_failure(const std::string & text,
    const GMDL_Off_Options * options = nullptr) {
  MemStream stream(text);
  GMDL_Off * off = nullptr;
  GMDL_Result r = gmdl_off_load(stream.get(), options, nullptr, &off);
  EXPECT_EQ(off, nullptr);
  gmdl_off_free(off);
  return r;
}

std::string dump_bytes(const GMDL_Off * off,
    const GMDL_Off_Options * options = nullptr) {
  char * buffer = nullptr;
  size_t size = 0;
  FILE * fd = open_memstream(&buffer, &size);
  EXPECT_NE(fd, nullptr);
  EXPECT_EQ(gmdl_off_dump(off, options, fd), GMDL_OK);
  fclose(fd);
  std::string out(buffer, size);
  free(buffer);
  return out;
}

} // namespace

TEST(OffParse, TriangleTwoLineHeader) {
  GMDL_Off * off = load_text(kTriangle);
  ASSERT_NE(off, nullptr);
  EXPECT_EQ(off->vertex_count, 3u);
  EXPECT_EQ(off->face_count, 1u);
  EXPECT_EQ(off->edge_count, 0u);
  EXPECT_EQ(off->present, 0u);
  EXPECT_EQ(off->faces[0].index_count, 3u);
  EXPECT_EQ(off->faces[0].indices[0], 0u);
  EXPECT_EQ(off->faces[0].indices[1], 1u);
  EXPECT_EQ(off->faces[0].indices[2], 2u);
  gmdl_off_free(off);
}

TEST(OffParse, TriangleOneLineHeader) {
  GMDL_Off * off = load_text(kOneLineHeader);
  ASSERT_NE(off, nullptr);
  EXPECT_EQ(off->vertex_count, 3u);
  EXPECT_EQ(off->face_count, 1u);
  gmdl_off_free(off);
}

TEST(OffParse, CommentsAndBlankLinesAreSkipped) {
  const char * text =
      "# comment\n"
      "\n"
      "OFF\n"
      "# counts next\n"
      "3 1 0\n"
      "0 0 0 # origin\n"
      "1 0 0\n"
      "0 1 0\n"
      "3 0 1 2 # face\n";
  GMDL_Off * off = load_text(text);
  ASSERT_NE(off, nullptr);
  EXPECT_EQ(off->vertex_count, 3u);
  gmdl_off_free(off);
}

TEST(OffParse, MissingKeywordIsFormat) {
  EXPECT_EQ(load_text_expecting_failure("3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n"),
      GMDL_ERR_FORMAT);
}

TEST(OffParse, BinaryKeywordIsUnsupported) {
  EXPECT_EQ(load_text_expecting_failure("OFF BINARY\n"), GMDL_ERR_UNSUPPORTED);
  EXPECT_EQ(load_text_expecting_failure("OFFBINARY\n"), GMDL_ERR_UNSUPPORTED);
  EXPECT_EQ(load_text_expecting_failure("4OFF\n3 1 0\n"), GMDL_ERR_UNSUPPORTED);
  EXPECT_EQ(load_text_expecting_failure("nOFF\n3 1 0\n"), GMDL_ERR_UNSUPPORTED);
  EXPECT_EQ(load_text_expecting_failure("STOFF\n3 1 0\n"), GMDL_ERR_UNSUPPORTED);
}

TEST(OffParse, CoffRecordsVertexColors) {
  const char * text =
      "COFF\n"
      "3 1 0\n"
      "0 0 0 1 0 0 1\n"
      "1 0 0 0 1 0 1\n"
      "0 1 0 0 0 1 1\n"
      "3 0 1 2\n";
  GMDL_Off * off = load_text(text);
  ASSERT_NE(off, nullptr);
  EXPECT_TRUE(off->present & GMDL_OFF_HAS_VERTEX_COLORS);
  EXPECT_TRUE(off->vertices[0].present & GMDL_OFF_VERTEX_HAS_COLOR);
  EXPECT_NEAR(off->vertices[0].r, 1.0f, 1e-6f);
  EXPECT_NEAR(off->vertices[1].g, 1.0f, 1e-6f);
  EXPECT_NEAR(off->vertices[2].b, 1.0f, 1e-6f);
  gmdl_off_free(off);
}

TEST(OffParse, NoffRecordsVertexNormals) {
  const char * text =
      "NOFF\n"
      "3 1 0\n"
      "0 0 0 0 0 1\n"
      "1 0 0 0 0 1\n"
      "0 1 0 0 0 1\n"
      "3 0 1 2\n";
  GMDL_Off * off = load_text(text);
  ASSERT_NE(off, nullptr);
  EXPECT_TRUE(off->present & GMDL_OFF_HAS_VERTEX_NORMALS);
  EXPECT_NEAR(off->vertices[0].normal[2], 1.0f, 1e-6f);
  gmdl_off_free(off);
}

TEST(OffParse, FaceColorAsBytes) {
  const char * text =
      "OFF\n"
      "3 1 0\n"
      "0 0 0\n"
      "1 0 0\n"
      "0 1 0\n"
      "3 0 1 2 255 0 0\n";
  GMDL_Off * off = load_text(text);
  ASSERT_NE(off, nullptr);
  EXPECT_TRUE(off->faces[0].present & GMDL_OFF_FACE_HAS_COLOR);
  EXPECT_NEAR(off->faces[0].r, 1.0f, 1e-6f);
  EXPECT_NEAR(off->faces[0].g, 0.0f, 1e-6f);
  EXPECT_NEAR(off->faces[0].a, 1.0f, 1e-6f);
  gmdl_off_free(off);
}

TEST(OffParse, FaceColorIndex) {
  const char * text =
      "OFF\n"
      "3 1 0\n"
      "0 0 0\n"
      "1 0 0\n"
      "0 1 0\n"
      "3 0 1 2 7\n";
  GMDL_Off * off = load_text(text);
  ASSERT_NE(off, nullptr);
  EXPECT_TRUE(off->faces[0].present & GMDL_OFF_FACE_HAS_COLOR_INDEX);
  EXPECT_EQ(off->faces[0].color_index, 7);
  gmdl_off_free(off);
}

TEST(OffParse, OutOfRangeIndexIsFormat) {
  EXPECT_EQ(load_text_expecting_failure(
                "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 9\n"),
      GMDL_ERR_FORMAT);
}

TEST(OffParse, DegenerateFaceIsFormat) {
  EXPECT_EQ(load_text_expecting_failure(
                "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n2 0 1\n"),
      GMDL_ERR_FORMAT);
}

TEST(OffParse, MaxVerticesIsEnforced) {
  GMDL_Off_Options options;
  gmdl_off_options_default(&options);
  options.max_vertices = 2;
  EXPECT_EQ(load_text_expecting_failure(kTriangle, &options), GMDL_ERR_LIMIT);
}

TEST(OffParse, MaxFacesIsEnforced) {
  GMDL_Off_Options options;
  gmdl_off_options_default(&options);
  options.max_faces = 1;
  GMDL_Off * ok = load_text(kTriangle, &options);
  ASSERT_NE(ok, nullptr);
  gmdl_off_free(ok);
  const char * two =
      "OFF\n"
      "3 2 0\n"
      "0 0 0\n"
      "1 0 0\n"
      "0 1 0\n"
      "3 0 1 2\n"
      "3 0 2 1\n";
  EXPECT_EQ(load_text_expecting_failure(two, &options), GMDL_ERR_LIMIT);
}

TEST(OffParse, MaxFaceCornersIsEnforced) {
  GMDL_Off_Options options;
  gmdl_off_options_default(&options);
  options.max_face_corners = 3;
  GMDL_Off * ok = load_text(kTriangle, &options);
  ASSERT_NE(ok, nullptr);
  gmdl_off_free(ok);
  options.max_face_corners = 2;
  EXPECT_EQ(load_text_expecting_failure(kTriangle, &options), GMDL_ERR_LIMIT);
}

TEST(OffParse, RejectNonFinite) {
  GMDL_Off_Options options;
  gmdl_off_options_default(&options);
  options.reject_non_finite = true;
  EXPECT_EQ(load_text_expecting_failure(
                "OFF\n1 0 0\nnan 0 0\n", &options),
      GMDL_ERR_FORMAT);
}

TEST(OffParse, HugeFaceCornerCountDoesNotAllocateUpFront) {
  // A forged Nv must not ask malloc for Nv * sizeof(uint32_t) before the
  // line has been shown to hold that many indices. Measured: libFuzzer
  // aborted on malloc(4990880016) from this shape.
  const char * text =
      "OFF\n"
      "3 1 0\n"
      "0 0 0\n"
      "1 0 0\n"
      "0 1 0\n"
      "1247720004 0 1 2\n";
  EXPECT_EQ(load_text_expecting_failure(text), GMDL_ERR_FORMAT);
}

TEST(OffParse, ColorIndexOutsideInt32IsFormat) {
  const char * text =
      "OFF\n"
      "3 1 0\n"
      "0 0 0\n"
      "1 0 0\n"
      "0 1 0\n"
      "3 0 1 2 3444444444\n";
  EXPECT_EQ(load_text_expecting_failure(text), GMDL_ERR_FORMAT);
}

TEST(OffParse, DumpRoundTrips) {
  GMDL_Off * first = load_text(kTriangle);
  ASSERT_NE(first, nullptr);
  std::string dumped = dump_bytes(first);
  gmdl_off_free(first);

  EXPECT_EQ(dumped.substr(0, 4), "OFF\n");
  EXPECT_NE(dumped.find("3 1 0\n"), std::string::npos);

  GMDL_Off * second = load_text(dumped);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->vertex_count, 3u);
  EXPECT_EQ(second->face_count, 1u);
  gmdl_off_free(second);
}

TEST(OffParse, CoffDumpUsesCoffKeyword) {
  const char * text =
      "COFF\n"
      "1 0 0\n"
      "0 0 0 0.5 0.25 0.125 1\n";
  GMDL_Off * off = load_text(text);
  ASSERT_NE(off, nullptr);
  std::string dumped = dump_bytes(off);
  gmdl_off_free(off);
  EXPECT_EQ(dumped.substr(0, 5), "COFF\n");
}

TEST(OffParse, LoadFileAndDumpIoFailure) {
  TempFile file(kTriangle);
  ASSERT_TRUE(file.valid());
  GMDL_Off * off = nullptr;
  ASSERT_EQ(gmdl_off_load_file(file.path(), nullptr, nullptr, &off), GMDL_OK);
  ASSERT_NE(off, nullptr);

  FailingSink sink(0);
  EXPECT_EQ(gmdl_off_dump(off, nullptr, sink.get()), GMDL_ERR_IO);
  EXPECT_TRUE(sink.failed());
  gmdl_off_free(off);
}

TEST(OffParse, NullArgsAreInvalid) {
  EXPECT_EQ(gmdl_off_load(nullptr, nullptr, nullptr, nullptr), GMDL_ERR_INVALID);
  MemStream stream(kTriangle);
  EXPECT_EQ(gmdl_off_load(stream.get(), nullptr, nullptr, nullptr),
      GMDL_ERR_INVALID);
  GMDL_Off * off = nullptr;
  EXPECT_EQ(gmdl_off_load_file(nullptr, nullptr, nullptr, &off),
      GMDL_ERR_INVALID);
  EXPECT_EQ(gmdl_off_dump(nullptr, nullptr, stdout), GMDL_ERR_INVALID);
  gmdl_off_free(nullptr);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
