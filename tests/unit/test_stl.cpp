/**
 * @file
 *
 * Unit tests for the STL parser and dumper.
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

const char * kAsciiTriangle =
    "solid tri\n"
    "  facet normal 0 0 1\n"
    "    outer loop\n"
    "      vertex 0 0 0\n"
    "      vertex 1 0 0\n"
    "      vertex 0 1 0\n"
    "    endloop\n"
    "  endfacet\n"
    "endsolid tri\n";

GMDL_Stl * load_bytes(const void * data, size_t size,
    const GMDL_Stl_Options * options = nullptr) {
  MemStream stream(std::string(static_cast<const char *>(data), size));
  GMDL_Stl * stl = nullptr;
  GMDL_Result r = gmdl_stl_load(stream.get(), options, nullptr, &stl);
  EXPECT_EQ(r, GMDL_OK) << gmdl_result_string(r);
  return stl;
}

GMDL_Stl * load_text(const std::string & text,
    const GMDL_Stl_Options * options = nullptr) {
  return load_bytes(text.data(), text.size(), options);
}

GMDL_Result load_text_expecting_failure(const std::string & text,
    const GMDL_Stl_Options * options = nullptr) {
  MemStream stream(text);
  GMDL_Stl * stl = nullptr;
  GMDL_Result r = gmdl_stl_load(stream.get(), options, nullptr, &stl);
  EXPECT_EQ(stl, nullptr);
  gmdl_stl_free(stl);
  return r;
}

std::string dump_bytes(const GMDL_Stl * stl,
    const GMDL_Stl_Options * options = nullptr) {
  gmdltest::CapturedOutput sink;
  EXPECT_EQ(gmdl_stl_dump(stl, options, sink.get()), GMDL_OK);
  return sink.finish();
}

void write_f32_le(std::vector<uint8_t> & out, float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  out.push_back((uint8_t)(bits & 0xffu));
  out.push_back((uint8_t)((bits >> 8) & 0xffu));
  out.push_back((uint8_t)((bits >> 16) & 0xffu));
  out.push_back((uint8_t)((bits >> 24) & 0xffu));
}

void write_u16_le(std::vector<uint8_t> & out, uint16_t value) {
  out.push_back((uint8_t)(value & 0xffu));
  out.push_back((uint8_t)((value >> 8) & 0xffu));
}

void write_u32_le(std::vector<uint8_t> & out, uint32_t value) {
  out.push_back((uint8_t)(value & 0xffu));
  out.push_back((uint8_t)((value >> 8) & 0xffu));
  out.push_back((uint8_t)((value >> 16) & 0xffu));
  out.push_back((uint8_t)((value >> 24) & 0xffu));
}

std::vector<uint8_t> make_binary_triangle(uint16_t attribute = 0) {
  std::vector<uint8_t> bytes;
  bytes.resize(80, 0);
  write_u32_le(bytes, 1);
  write_f32_le(bytes, 0.0f);
  write_f32_le(bytes, 0.0f);
  write_f32_le(bytes, 1.0f);
  write_f32_le(bytes, 0.0f);
  write_f32_le(bytes, 0.0f);
  write_f32_le(bytes, 0.0f);
  write_f32_le(bytes, 1.0f);
  write_f32_le(bytes, 0.0f);
  write_f32_le(bytes, 0.0f);
  write_f32_le(bytes, 0.0f);
  write_f32_le(bytes, 1.0f);
  write_f32_le(bytes, 0.0f);
  write_u16_le(bytes, attribute);
  return bytes;
}

} // namespace

TEST(StlParse, AnAsciiTriangleLoads) {
  GMDL_Stl * stl = load_text(kAsciiTriangle);
  ASSERT_NE(stl, nullptr);
  EXPECT_EQ(stl->form, GMDL_STL_FORM_ASCII);
  EXPECT_STREQ(stl->solid_name, "tri");
  ASSERT_EQ(stl->triangle_count, 1u);
  EXPECT_FLOAT_EQ(stl->triangles[0].normal[2], 1.0f);
  EXPECT_FLOAT_EQ(stl->triangles[0].vertex[1][0], 1.0f);
  EXPECT_FLOAT_EQ(stl->triangles[0].vertex[2][1], 1.0f);
  gmdl_stl_free(stl);
}

TEST(StlParse, AsciiKeywordsAreCaseInsensitive) {
  GMDL_Stl * stl = load_text(
      "SOLID Box\n"
      "  FACET NORMAL 0 0 1\n"
      "    OUTER LOOP\n"
      "      VERTEX 0 0 0\n"
      "      VERTEX 1 0 0\n"
      "      VERTEX 0 1 0\n"
      "    ENDLOOP\n"
      "  ENDFACET\n"
      "ENDSOLID Box\n");
  ASSERT_NE(stl, nullptr);
  EXPECT_STREQ(stl->solid_name, "Box");
  EXPECT_EQ(stl->triangle_count, 1u);
  gmdl_stl_free(stl);
}

TEST(StlParse, ABinaryTriangleLoads) {
  auto bytes = make_binary_triangle();
  GMDL_Stl * stl = load_bytes(bytes.data(), bytes.size());
  ASSERT_NE(stl, nullptr);
  EXPECT_EQ(stl->form, GMDL_STL_FORM_BINARY);
  ASSERT_EQ(stl->triangle_count, 1u);
  EXPECT_FLOAT_EQ(stl->triangles[0].normal[2], 1.0f);
  EXPECT_FLOAT_EQ(stl->triangles[0].vertex[2][1], 1.0f);
  EXPECT_EQ(stl->triangles[0].attribute, 0u);
  gmdl_stl_free(stl);
}

TEST(StlParse, BinaryRoundTripPreservesGeometry) {
  GMDL_Stl * first = load_text(kAsciiTriangle);
  ASSERT_NE(first, nullptr);
  std::string binary = dump_bytes(first);
  gmdl_stl_free(first);

  GMDL_Stl * second = load_bytes(binary.data(), binary.size());
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->form, GMDL_STL_FORM_BINARY);
  ASSERT_EQ(second->triangle_count, 1u);
  EXPECT_FLOAT_EQ(second->triangles[0].vertex[0][0], 0.0f);
  EXPECT_FLOAT_EQ(second->triangles[0].vertex[1][0], 1.0f);
  gmdl_stl_free(second);
}

TEST(StlParse, AsciiRoundTripPreservesTheSolidName) {
  GMDL_Stl * first = load_text(kAsciiTriangle);
  ASSERT_NE(first, nullptr);
  GMDL_Stl_Options options;
  gmdl_stl_options_default(&options);
  options.write_ascii = true;
  std::string ascii = dump_bytes(first, &options);
  gmdl_stl_free(first);

  GMDL_Stl * second = load_text(ascii);
  ASSERT_NE(second, nullptr);
  EXPECT_STREQ(second->solid_name, "tri");
  EXPECT_EQ(second->triangle_count, 1u);
  gmdl_stl_free(second);
}

TEST(StlParse, ForceAsciiReadsText) {
  GMDL_Stl_Options options;
  gmdl_stl_options_default(&options);
  options.force_ascii = true;
  GMDL_Stl * stl = load_text(kAsciiTriangle, &options);
  ASSERT_NE(stl, nullptr);
  EXPECT_EQ(stl->form, GMDL_STL_FORM_ASCII);
  EXPECT_EQ(stl->triangle_count, 1u);
  gmdl_stl_free(stl);
}

TEST(StlParse, ForceBinaryWinsOverForceAscii) {
  auto bytes = make_binary_triangle();
  GMDL_Stl_Options options;
  gmdl_stl_options_default(&options);
  options.force_ascii = true;
  options.force_binary = true;
  GMDL_Stl * stl = load_bytes(bytes.data(), bytes.size(), &options);
  ASSERT_NE(stl, nullptr);
  EXPECT_EQ(stl->form, GMDL_STL_FORM_BINARY);
  gmdl_stl_free(stl);
}

TEST(StlParse, SizeMatchSelectsBinaryEvenWhenHeaderSaysSolid) {
  auto bytes = make_binary_triangle();
  std::memcpy(bytes.data(), "solid   ", 8);
  GMDL_Stl * stl = load_bytes(bytes.data(), bytes.size());
  ASSERT_NE(stl, nullptr);
  EXPECT_EQ(stl->form, GMDL_STL_FORM_BINARY);
  EXPECT_EQ(stl->triangle_count, 1u);
  gmdl_stl_free(stl);
}

TEST(StlParse, MaxTrianglesIsEnforced) {
  GMDL_Stl_Options options;
  gmdl_stl_options_default(&options);
  options.max_triangles = 0; // no cap first
  GMDL_Stl * ok = load_text(kAsciiTriangle, &options);
  ASSERT_NE(ok, nullptr);
  gmdl_stl_free(ok);

  options.max_triangles = 0;
  // Cap of one is fine for one triangle; cap of zero means unlimited.
  options.max_triangles = 1;
  GMDL_Stl * one = load_text(kAsciiTriangle, &options);
  ASSERT_NE(one, nullptr);
  gmdl_stl_free(one);

  std::string two = kAsciiTriangle;
  // Duplicate the facet before endsolid.
  size_t end = two.find("endsolid");
  ASSERT_NE(end, std::string::npos);
  two.insert(end,
      "  facet normal 0 0 1\n"
      "    outer loop\n"
      "      vertex 0 0 0\n"
      "      vertex 1 0 0\n"
      "      vertex 0 1 0\n"
      "    endloop\n"
      "  endfacet\n");
  options.max_triangles = 1;
  EXPECT_EQ(load_text_expecting_failure(two, &options), GMDL_ERR_LIMIT);
}

TEST(StlParse, NonFiniteCanBeRejected) {
  GMDL_Stl_Options options;
  gmdl_stl_options_default(&options);
  options.reject_non_finite = true;
  EXPECT_EQ(load_text_expecting_failure(
                "solid x\n"
                "  facet normal nan 0 1\n"
                "    outer loop\n"
                "      vertex 0 0 0\n"
                "      vertex 1 0 0\n"
                "      vertex 0 1 0\n"
                "    endloop\n"
                "  endfacet\n"
                "endsolid x\n",
                &options),
      GMDL_ERR_FORMAT);
}

TEST(StlParse, ViscamColorIsDecodedWhenAsked) {
  // Bit 15 set, R=31, G=0, B=0 -> bright red under VisCAM.
  uint16_t word = (uint16_t)((1u << 15) | (31u << 10));
  auto bytes = make_binary_triangle(word);
  GMDL_Stl_Options options;
  gmdl_stl_options_default(&options);
  options.color_convention = GMDL_STL_COLOR_VISCAM;
  GMDL_Stl * stl = load_bytes(bytes.data(), bytes.size(), &options);
  ASSERT_NE(stl, nullptr);
  ASSERT_EQ(stl->triangle_count, 1u);
  EXPECT_TRUE(stl->triangles[0].present & GMDL_STL_TRI_HAS_COLOR);
  EXPECT_NEAR(stl->triangles[0].r, 1.0f, 0.02f);
  EXPECT_NEAR(stl->triangles[0].g, 0.0f, 0.02f);
  EXPECT_NEAR(stl->triangles[0].b, 0.0f, 0.02f);
  gmdl_stl_free(stl);
}

TEST(StlParse, MagicsColorUsesInvertedValidAndBgr) {
  // Bit 15 clear, low 5 bits = 31 -> red under Magics (BGR packing).
  uint16_t word = 31u;
  auto bytes = make_binary_triangle(word);
  GMDL_Stl_Options options;
  gmdl_stl_options_default(&options);
  options.color_convention = GMDL_STL_COLOR_MAGICS;
  GMDL_Stl * stl = load_bytes(bytes.data(), bytes.size(), &options);
  ASSERT_NE(stl, nullptr);
  EXPECT_TRUE(stl->triangles[0].present & GMDL_STL_TRI_HAS_COLOR);
  EXPECT_NEAR(stl->triangles[0].r, 1.0f, 0.02f);
  EXPECT_NEAR(stl->triangles[0].b, 0.0f, 0.02f);
  gmdl_stl_free(stl);
}

TEST(StlParse, MagicsHeaderColorIsRecorded) {
  auto bytes = make_binary_triangle();
  bytes[0] = 'C';
  bytes[1] = 'O';
  bytes[2] = 'L';
  bytes[3] = 'O';
  bytes[4] = 'R';
  bytes[5] = '=';
  bytes[6] = 255;
  bytes[7] = 0;
  bytes[8] = 0;
  bytes[9] = 255;
  GMDL_Stl * stl = load_bytes(bytes.data(), bytes.size());
  ASSERT_NE(stl, nullptr);
  EXPECT_TRUE(stl->present & GMDL_STL_HAS_DEFAULT_COLOR);
  EXPECT_NEAR(stl->default_color[0], 1.0f, 0.01f);
  EXPECT_NEAR(stl->default_color[3], 1.0f, 0.01f);
  gmdl_stl_free(stl);
}

TEST(StlParse, MagicsColorRoundTripsThroughBinaryDump) {
  auto bytes = make_binary_triangle(31u);
  bytes[0] = 'C';
  bytes[1] = 'O';
  bytes[2] = 'L';
  bytes[3] = 'O';
  bytes[4] = 'R';
  bytes[5] = '=';
  bytes[6] = 10;
  bytes[7] = 20;
  bytes[8] = 30;
  bytes[9] = 40;
  GMDL_Stl_Options options;
  gmdl_stl_options_default(&options);
  options.color_convention = GMDL_STL_COLOR_MAGICS;
  GMDL_Stl * first = load_bytes(bytes.data(), bytes.size(), &options);
  ASSERT_NE(first, nullptr);
  std::string out = dump_bytes(first, &options);
  gmdl_stl_free(first);

  GMDL_Stl * second = load_bytes(out.data(), out.size(), &options);
  ASSERT_NE(second, nullptr);
  EXPECT_TRUE(second->present & GMDL_STL_HAS_DEFAULT_COLOR);
  EXPECT_TRUE(second->triangles[0].present & GMDL_STL_TRI_HAS_COLOR);
  EXPECT_NEAR(second->triangles[0].r, 1.0f, 0.02f);
  gmdl_stl_free(second);
}

TEST(StlParse, ColorIsNotInventedWithoutAConvention) {
  auto bytes = make_binary_triangle((uint16_t)((1u << 15) | (31u << 10)));
  GMDL_Stl * stl = load_bytes(bytes.data(), bytes.size());
  ASSERT_NE(stl, nullptr);
  EXPECT_FALSE(stl->triangles[0].present & GMDL_STL_TRI_HAS_COLOR);
  EXPECT_EQ(stl->triangles[0].attribute, (uint16_t)((1u << 15) | (31u << 10)));
  gmdl_stl_free(stl);
}

TEST(StlParse, TruncatedBinaryIsFormat) {
  auto bytes = make_binary_triangle();
  bytes.resize(bytes.size() - 10);
  EXPECT_EQ(load_text_expecting_failure(
                std::string(bytes.begin(), bytes.end())),
      GMDL_ERR_FORMAT);
}

TEST(StlParse, MalformedAsciiIsFormat) {
  EXPECT_EQ(load_text_expecting_failure("solid x\nfacet normal 0 0 1\n"),
      GMDL_ERR_FORMAT);
}

TEST(StlParse, ZeroNormalIsKeptByDefault) {
  const char * text =
      "solid zn\n"
      "  facet normal 0 0 0\n"
      "    outer loop\n"
      "      vertex 0 0 0\n"
      "      vertex 2 0 0\n"
      "      vertex 0 2 0\n"
      "    endloop\n"
      "  endfacet\n"
      "endsolid zn\n";
  GMDL_Stl * stl = load_text(text);
  ASSERT_NE(stl, nullptr);
  EXPECT_FLOAT_EQ(stl->triangles[0].normal[0], 0.0f);
  EXPECT_FLOAT_EQ(stl->triangles[0].normal[1], 0.0f);
  EXPECT_FLOAT_EQ(stl->triangles[0].normal[2], 0.0f);
  gmdl_stl_free(stl);
}

TEST(StlParse, RecomputeZeroNormalsMatchesGeometry) {
  const char * text =
      "solid zn\n"
      "  facet normal 0 0 0\n"
      "    outer loop\n"
      "      vertex 0 0 0\n"
      "      vertex 2 0 0\n"
      "      vertex 0 2 0\n"
      "    endloop\n"
      "  endfacet\n"
      "endsolid zn\n";
  GMDL_Stl_Options options;
  gmdl_stl_options_default(&options);
  options.recompute_zero_normals = true;
  GMDL_Stl * stl = load_text(text, &options);
  ASSERT_NE(stl, nullptr);
  EXPECT_NEAR(stl->triangles[0].normal[0], 0.0f, 1e-5f);
  EXPECT_NEAR(stl->triangles[0].normal[1], 0.0f, 1e-5f);
  EXPECT_NEAR(stl->triangles[0].normal[2], 1.0f, 1e-5f);
  gmdl_stl_free(stl);
}

TEST(StlParse, RequireLowercaseRejectsMixedCase) {
  const char * text =
      "SOLID Box\n"
      "  FACET NORMAL 0 0 1\n"
      "    OUTER LOOP\n"
      "      VERTEX 0 0 0\n"
      "      VERTEX 1 0 0\n"
      "      VERTEX 0 1 0\n"
      "    ENDLOOP\n"
      "  ENDFACET\n"
      "ENDSOLID Box\n";
  GMDL_Stl_Options options;
  gmdl_stl_options_default(&options);
  options.require_lowercase_keywords = true;
  EXPECT_EQ(load_text_expecting_failure(text, &options), GMDL_ERR_FORMAT);
}

TEST(StlParse, BlenderPresetSetsMeasuredReadings) {
  GMDL_Stl_Options options;
  gmdl_stl_options_blender(&options);
  EXPECT_TRUE(options.recompute_zero_normals);
  EXPECT_TRUE(options.require_lowercase_keywords);
}

TEST(StlParse, FreecadPresetRecomputesNormalsOnly) {
  GMDL_Stl_Options options;
  gmdl_stl_options_freecad(&options);
  EXPECT_TRUE(options.recompute_zero_normals);
  EXPECT_FALSE(options.require_lowercase_keywords);
}

TEST(StlParse, OpenscadPresetMatchesBlenderReadings) {
  GMDL_Stl_Options options;
  gmdl_stl_options_openscad(&options);
  EXPECT_TRUE(options.recompute_zero_normals);
  EXPECT_TRUE(options.require_lowercase_keywords);
}

TEST(StlParse, NullFreeIsSafe) {
  gmdl_stl_free(nullptr);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
