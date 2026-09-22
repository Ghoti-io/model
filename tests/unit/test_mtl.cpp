/**
 * @file
 *
 * Unit tests for the Wavefront MTL parser.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <string>

using gmdltest::data;
using gmdltest::MemStream;
using gmdltest::TempFile;

namespace {

/** Parse an inline MTL document, or fail the test. */
GMDL_Mtl * load_text(const std::string & text) {
  MemStream stream(text);
  GMDL_Mtl * mtl = nullptr;
  GMDL_Result r = gmdl_mtl_load(stream.get(), nullptr, nullptr, &mtl);
  EXPECT_EQ(r, GMDL_OK) << gmdl_result_string(r);
  return mtl;
}

/** Parse an inline MTL document that is expected to fail. */
GMDL_Result load_text_expecting_failure(
    const std::string & text, const GMDL_Limits * limits = nullptr) {
  MemStream stream(text);
  GMDL_Mtl * mtl = nullptr;
  GMDL_Result r = gmdl_mtl_load(stream.get(), limits, nullptr, &mtl);
  EXPECT_EQ(mtl, nullptr) << "nothing should be handed back on failure";
  gmdl_mtl_free(mtl);
  return r;
}

} // namespace

//
// Argument handling
//

TEST(MtlLoad, NullArgumentsRejected) {
  MemStream stream("newmtl a\n");
  GMDL_Mtl * mtl = nullptr;
  EXPECT_EQ(gmdl_mtl_load(nullptr, nullptr, nullptr, &mtl), GMDL_ERR_INVALID);
  EXPECT_EQ(mtl, nullptr);
  EXPECT_EQ(gmdl_mtl_load(stream.get(), nullptr, nullptr, nullptr),
      GMDL_ERR_INVALID);
}

TEST(MtlLoad, MissingFileReportsIo) {
  GMDL_Mtl * mtl = nullptr;
  EXPECT_EQ(
      gmdl_mtl_load_file(gmdltest::missing_path(), nullptr, nullptr, &mtl),
      GMDL_ERR_IO);
  EXPECT_EQ(mtl, nullptr);
}

TEST(MtlLoad, LoadFileRejectsNullArguments) {
  GMDL_Mtl * mtl = nullptr;
  EXPECT_EQ(gmdl_mtl_load_file(nullptr, nullptr, nullptr, &mtl),
      GMDL_ERR_INVALID);
  EXPECT_EQ(gmdl_mtl_load_file("whatever.mtl", nullptr, nullptr, nullptr),
      GMDL_ERR_INVALID);
}

TEST(MtlFree, NullIsSafe) {
  gmdl_mtl_free(nullptr);
}

TEST(MtlLoad, EmptyInputYieldsNoMaterials) {
  GMDL_Mtl * mtl = load_text("");
  ASSERT_NE(mtl, nullptr);
  EXPECT_EQ(mtl->material_count, 0u);
  gmdl_mtl_free(mtl);
}

//
// Parsing
//

TEST(MtlParse, ReadsEverySupportedProperty) {
  GMDL_Mtl * mtl = load_text(
      "newmtl shiny\n"
      "Ka 0.1 0.2 0.3\n"
      "Kd 0.4 0.5 0.6\n"
      "Ks 0.7 0.8 0.9\n"
      "Ns 32.5\n"
      "d 0.75\n"
      "illum 2\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  const GMDL_Mtl_Material & m = mtl->materials[0];
  EXPECT_STREQ(m.name, "shiny");
  EXPECT_FLOAT_EQ(m.Ka[0], 0.1f);
  EXPECT_FLOAT_EQ(m.Ka[2], 0.3f);
  EXPECT_FLOAT_EQ(m.Kd[1], 0.5f);
  EXPECT_FLOAT_EQ(m.Ks[2], 0.9f);
  EXPECT_FLOAT_EQ(m.Ns, 32.5f);
  EXPECT_FLOAT_EQ(m.d, 0.75f);
  EXPECT_EQ(m.illum, 2);
  gmdl_mtl_free(mtl);
}

TEST(MtlParse, SeveralMaterials) {
  GMDL_Mtl * mtl = load_text(
      "newmtl red\nKd 1 0 0\n"
      "newmtl green\nKd 0 1 0\n"
      "newmtl blue\nKd 0 0 1\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 3u);
  EXPECT_STREQ(mtl->materials[1].name, "green");
  EXPECT_FLOAT_EQ(mtl->materials[2].Kd[2], 1.0f);
  gmdl_mtl_free(mtl);
}

// Growth past the preallocated capacity.
TEST(MtlParse, GrowsBeyondInitialCapacity) {
  std::string text;
  const int kCount = 200;
  for (int i = 0; i < kCount; i++) {
    text += "newmtl m" + std::to_string(i) + "\n";
    text += "Ns " + std::to_string(i) + "\n";
  }
  GMDL_Mtl * mtl = load_text(text);
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, (size_t)kCount);
  EXPECT_STREQ(mtl->materials[kCount - 1].name, "m199");
  EXPECT_FLOAT_EQ(mtl->materials[kCount - 1].Ns, (float)(kCount - 1));
  gmdl_mtl_free(mtl);
}

TEST(MtlParse, CommentsAndBlankLinesIgnored) {
  GMDL_Mtl * mtl = load_text(
      "# comment\n"
      "\n"
      "newmtl only\n"
      "# another\n"
      "Kd 1 1 1\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  EXPECT_FLOAT_EQ(mtl->materials[0].Kd[0], 1.0f);
  gmdl_mtl_free(mtl);
}

TEST(MtlParse, PropertiesBeforeAnyMaterialAreIgnored) {
  GMDL_Mtl * mtl = load_text("Kd 1 0 0\nNs 5\nnewmtl later\nKd 0 1 0\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  EXPECT_STREQ(mtl->materials[0].name, "later");
  EXPECT_FLOAT_EQ(mtl->materials[0].Kd[1], 1.0f);
  gmdl_mtl_free(mtl);
}

TEST(MtlParse, UnknownPropertiesAreIgnored) {
  GMDL_Mtl * mtl = load_text(
      "newmtl m\n"
      "map_Kd texture.png\n"
      "Ni 1.45\n"
      "Tr 0.5\n"
      "Kd 1 1 1\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  EXPECT_FLOAT_EQ(mtl->materials[0].Kd[0], 1.0f);
  gmdl_mtl_free(mtl);
}

// A property name has to end at whitespace, so "Kd" does not also match a
// "Kdsomething" line and "d" does not match "dissolve".
TEST(MtlParse, PropertyNamesMustEndAtWhitespace) {
  GMDL_Mtl * mtl = load_text(
      "newmtl m\n"
      "Kdiffuse 9 9 9\n"
      "dissolve 9\n"
      "Kd 0.5 0.5 0.5\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  EXPECT_FLOAT_EQ(mtl->materials[0].Kd[0], 0.5f);
  EXPECT_FLOAT_EQ(mtl->materials[0].d, 0.0f) << "no 'd' line was present";
  gmdl_mtl_free(mtl);
}

TEST(MtlParse, MalformedPropertyIsAFormatError) {
  EXPECT_EQ(load_text_expecting_failure("newmtl m\nKd 1 2\n"),
      GMDL_ERR_FORMAT);
  EXPECT_EQ(load_text_expecting_failure("newmtl m\nNs\n"), GMDL_ERR_FORMAT);
  EXPECT_EQ(load_text_expecting_failure("newmtl m\nillum x\n"),
      GMDL_ERR_FORMAT);
  EXPECT_EQ(load_text_expecting_failure("newmtl\n"), GMDL_ERR_FORMAT);
}

//
// Lookup
//

TEST(MtlFind, FindsByName) {
  GMDL_Mtl * mtl = load_text("newmtl red\nKd 1 0 0\nnewmtl blue\nKd 0 0 1\n");
  ASSERT_NE(mtl, nullptr);
  const GMDL_Mtl_Material * blue = gmdl_mtl_find(mtl, "blue");
  ASSERT_NE(blue, nullptr);
  EXPECT_FLOAT_EQ(blue->Kd[2], 1.0f);
  EXPECT_EQ(gmdl_mtl_find(mtl, "green"), nullptr);
  EXPECT_EQ(gmdl_mtl_find(mtl, nullptr), nullptr);
  EXPECT_EQ(gmdl_mtl_find(nullptr, "red"), nullptr);
  gmdl_mtl_free(mtl);
}

//
// Limits
//

TEST(MtlLimits, MaterialCapIsEnforced) {
  GMDL_Limits limits;
  gmdl_limits_default(&limits);
  limits.max_materials = 2;
  EXPECT_EQ(
      load_text_expecting_failure("newmtl a\nnewmtl b\nnewmtl c\n", &limits),
      GMDL_ERR_LIMIT);
}

TEST(MtlLimits, LineLongerThanTheCapIsRejected) {
  GMDL_Limits limits;
  gmdl_limits_default(&limits);
  limits.max_line_length = 8;
  EXPECT_EQ(load_text_expecting_failure("newmtl a_very_long_name\n", &limits),
      GMDL_ERR_LIMIT);
}

//
// Checked-in fixture
//

TEST(MtlFixture, LoadsViolinCaseMaterials) {
  GMDL_Mtl * mtl = nullptr;
  ASSERT_EQ(gmdl_mtl_load_file(data("models/violin_case/vp.mtl").c_str(),
                nullptr, nullptr, &mtl),
      GMDL_OK);
  ASSERT_NE(mtl, nullptr);
  EXPECT_GT(mtl->material_count, 0u);
  for (size_t i = 0; i < mtl->material_count; i++) {
    EXPECT_GT(strlen(mtl->materials[i].name), 0u) << "material " << i;
  }
  gmdl_mtl_free(mtl);
}

//
// Dump
//

TEST(MtlDump, RejectsNullArguments) {
  GMDL_Mtl * mtl = load_text("newmtl m\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_EQ(gmdl_mtl_dump(nullptr, stdout), GMDL_ERR_INVALID);
  EXPECT_EQ(gmdl_mtl_dump(mtl, nullptr), GMDL_ERR_INVALID);
  gmdl_mtl_free(mtl);
}

TEST(MtlDump, RoundTripsThroughTheParser) {
  GMDL_Mtl * first = load_text(
      "newmtl red\nKa 0.1 0.1 0.1\nKd 1 0 0\nKs 0.5 0.5 0.5\n"
      "Ns 10\nd 1\nillum 2\n"
      "newmtl blue\nKd 0 0 1\n");
  ASSERT_NE(first, nullptr);

  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_mtl_dump(first, sink), GMDL_OK);
  fclose(sink);

  GMDL_Mtl * second = nullptr;
  ASSERT_EQ(gmdl_mtl_load_file(out.path(), nullptr, nullptr, &second), GMDL_OK);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->material_count, first->material_count);
  for (size_t i = 0; i < first->material_count; i++) {
    EXPECT_STREQ(second->materials[i].name, first->materials[i].name);
    EXPECT_FLOAT_EQ(second->materials[i].Kd[0], first->materials[i].Kd[0]);
    EXPECT_FLOAT_EQ(second->materials[i].Ns, first->materials[i].Ns);
    EXPECT_EQ(second->materials[i].illum, first->materials[i].illum);
  }

  gmdl_mtl_free(first);
  gmdl_mtl_free(second);
}

//
// The two formats together
//

TEST(ObjAndMtl, MtllibNamesAFileThatParses) {
  GMDL_Obj * obj = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(
                data("models/violin_case/violin_case.obj").c_str(), nullptr,
                nullptr, &obj),
      GMDL_OK);
  ASSERT_NE(obj, nullptr);
  ASSERT_STRNE(obj->mtllib, "");

  // The fixture names "./vp.mtl", relative to its own directory.
  GMDL_Mtl * mtl = nullptr;
  ASSERT_EQ(gmdl_mtl_load_file(data("models/violin_case/vp.mtl").c_str(),
                nullptr, nullptr, &mtl),
      GMDL_OK);
  ASSERT_NE(mtl, nullptr);

  // Every material the OBJ refers to should exist in the library.
  for (size_t i = 0; i < obj->material_mapping_count; i++) {
    EXPECT_NE(gmdl_mtl_find(mtl, obj->material_mappings[i].name), nullptr)
        << "usemtl " << obj->material_mappings[i].name;
  }

  gmdl_mtl_free(mtl);
  gmdl_obj_free(obj);
}

//
// Logical lines: the MTL parser reads through the same reader as the OBJ
// one, so the rules in section 2 hold for both.
//

TEST(MtlLine, ByteOrderMarkDoesNotSwallowTheFirstLine) {
  GMDL_Mtl * mtl = load_text("\xEF\xBB\xBFnewmtl a\nKd 1 0 0\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  EXPECT_STREQ(mtl->materials[0].name, "a");
  gmdl_mtl_free(mtl);
}

TEST(MtlLine, LeadingWhitespaceDoesNotHideADirective) {
  GMDL_Mtl * mtl = load_text("  newmtl a\n\tKd 1 0 0\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  EXPECT_FLOAT_EQ(mtl->materials[0].Kd[0], 1.0f);
  gmdl_mtl_free(mtl);
}

TEST(MtlLine, TrailingCommentIsNotAValue) {
  GMDL_Mtl * mtl = load_text("newmtl a\nKd 1 0 0 # red\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  EXPECT_FLOAT_EQ(mtl->materials[0].Kd[0], 1.0f);
  gmdl_mtl_free(mtl);
}

TEST(MtlLine, BackslashJoinsTheNextLine) {
  GMDL_Mtl * mtl = load_text("newmtl a\nKd 1 \\\n0 0\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  EXPECT_FLOAT_EQ(mtl->materials[0].Kd[0], 1.0f);
  EXPECT_FLOAT_EQ(mtl->materials[0].Kd[1], 0.0f);
  gmdl_mtl_free(mtl);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
