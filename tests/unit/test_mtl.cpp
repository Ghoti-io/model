/**
 * @file
 *
 * Unit tests for the Wavefront MTL parser.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using gmdltest::data;
using gmdltest::FailingSink;
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
  EXPECT_FLOAT_EQ(mtl->materials[0].d, 1.0f)
      << "'dissolve' is not 'd', so d keeps its opaque default";
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

//
// Names are refused when they do not fit, never truncated (3.9).
//

TEST(MtlNames, OverLongMaterialNameIsRefused) {
  std::string name(GMDL_MTL_MAX_NAME_LENGTH, 'a');
  EXPECT_EQ(load_text_expecting_failure("newmtl " + name + "\n"),
      GMDL_ERR_LIMIT);
}

TEST(MtlNames, ANameThatExactlyFitsIsAccepted) {
  std::string name(GMDL_MTL_MAX_NAME_LENGTH - 1, 'a');
  GMDL_Mtl * mtl = load_text("newmtl " + name + "\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  EXPECT_EQ(std::string(mtl->materials[0].name), name);
  gmdl_mtl_free(mtl);
}

TEST(MtlNames, ABareNewmtlIsStillAFormatError) {
  EXPECT_EQ(load_text_expecting_failure("newmtl\n"), GMDL_ERR_FORMAT);
}

//
// Colour forms: three are documented, one is implemented, and the other two
// are a different answer from "malformed" (4.2).
//

TEST(MtlColor, OneValueMeansGrey) {
  GMDL_Mtl * mtl = load_text("newmtl a\nKd 0.5\nKa 0.25\nKs 1\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  for (int i = 0; i < 3; i++) {
    EXPECT_FLOAT_EQ(mtl->materials[0].Kd[i], 0.5f) << "channel " << i;
    EXPECT_FLOAT_EQ(mtl->materials[0].Ka[i], 0.25f) << "channel " << i;
    EXPECT_FLOAT_EQ(mtl->materials[0].Ks[i], 1.0f) << "channel " << i;
  }
  gmdl_mtl_free(mtl);
}

TEST(MtlColor, ThreeValuesStillWork) {
  GMDL_Mtl * mtl = load_text("newmtl a\nKd 0.1 0.2 0.3\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_FLOAT_EQ(mtl->materials[0].Kd[0], 0.1f);
  EXPECT_FLOAT_EQ(mtl->materials[0].Kd[1], 0.2f);
  EXPECT_FLOAT_EQ(mtl->materials[0].Kd[2], 0.3f);
  gmdl_mtl_free(mtl);
}

TEST(MtlColor, TrailingTextAfterThreeValuesIsIgnored) {
  GMDL_Mtl * mtl = load_text("newmtl a\nKd 0.1 0.2 0.3 extra\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_FLOAT_EQ(mtl->materials[0].Kd[2], 0.3f);
  gmdl_mtl_free(mtl);
}

TEST(MtlColor, UnimplementedFormsAreUnsupportedNotMalformed) {
  // The distinction is the point: the file is fine, the reader is not, and
  // answering FORMAT rejected a good material library as corrupt.
  EXPECT_EQ(load_text_expecting_failure("newmtl a\nKd xyz 1 1 1\n"),
      GMDL_ERR_UNSUPPORTED);
  EXPECT_EQ(load_text_expecting_failure("newmtl a\nKd spectral f.rfl\n"),
      GMDL_ERR_UNSUPPORTED);
  EXPECT_EQ(load_text_expecting_failure("newmtl a\nd -halo 0.5\n"),
      GMDL_ERR_UNSUPPORTED);
}

TEST(MtlColor, AValueThatDoesNotParseIsStillMalformed) {
  EXPECT_EQ(load_text_expecting_failure("newmtl a\nKd 0.5 x\n"),
      GMDL_ERR_FORMAT);
  EXPECT_EQ(load_text_expecting_failure("newmtl a\nKd 0.1 0.2\n"),
      GMDL_ERR_FORMAT);
  EXPECT_EQ(load_text_expecting_failure("newmtl a\nKd nope\n"),
      GMDL_ERR_FORMAT);
}

TEST(MtlDissolve, AValueThatDoesNotParseIsMalformed) {
  EXPECT_EQ(load_text_expecting_failure("newmtl a\nd x\n"), GMDL_ERR_FORMAT);
}

TEST(MtlColor, ANameBeginningWithAKeywordIsNotTheKeyword) {
  // "xyzzy" is not the "xyz" form, so it falls through to being malformed
  // rather than unsupported.
  EXPECT_EQ(load_text_expecting_failure("newmtl a\nKd xyzzy\n"),
      GMDL_ERR_FORMAT);
}

TEST(MtlColor, PlainDissolveStillParses) {
  GMDL_Mtl * mtl = load_text("newmtl a\nd 0.25\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_FLOAT_EQ(mtl->materials[0].d, 0.25f);
  gmdl_mtl_free(mtl);
}

TEST(MtlDump, FloatsSurviveTheRoundTrip) {
  GMDL_Mtl * first = load_text("newmtl a\nKd 0.0000001 0.5 1\nNs 1234.5678\n");
  ASSERT_NE(first, nullptr);
  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_mtl_dump(first, sink), GMDL_OK);
  fclose(sink);

  GMDL_Mtl * second = nullptr;
  ASSERT_EQ(gmdl_mtl_load_file(out.path(), nullptr, nullptr, &second),
      GMDL_OK);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->material_count, 1u);
  EXPECT_FLOAT_EQ(second->materials[0].Kd[0], first->materials[0].Kd[0]);
  EXPECT_NE(second->materials[0].Kd[0], 0.0f);
  EXPECT_FLOAT_EQ(second->materials[0].Ns, first->materials[0].Ns);
  gmdl_mtl_free(first);
  gmdl_mtl_free(second);
}

TEST(MtlLine, ATrailingDoubleBackslashLeavesTheNameEndingInOne) {
  // The continuation takes exactly one backslash and joins what follows, so
  // the blank line here is what stops "Ka" being swallowed into the name.
  // The material is then called "a\", which is the one thing a dump cannot
  // write back: it lands last on its line, where a backslash continues.
  // The fuzz harness skips such a model, and this test is what keeps that
  // exemption from quietly covering a real defect.
  GMDL_Mtl * mtl = load_text("newmtl a\\\\\n\nKa 1 1 1\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  EXPECT_STREQ(mtl->materials[0].name, "a\\");
  EXPECT_FLOAT_EQ(mtl->materials[0].Ka[0], 1.0f);
  gmdl_mtl_free(mtl);
}

TEST(MtlLine, WithoutTheBlankLineTheNextLineJoinsTheName) {
  // Same input without the blank line: "Ka 1 1 1" is joined onto the name's
  // line, so the name is "a\Ka" and nothing sets Ka. Pinned because it is
  // surprising, and because it is what the continuation rule says.
  GMDL_Mtl * mtl = load_text("newmtl a\\\\\nKa 1 1 1\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  EXPECT_STREQ(mtl->materials[0].name, "a\\Ka");
  EXPECT_FLOAT_EQ(mtl->materials[0].Ka[0], 0.0f);
  gmdl_mtl_free(mtl);
}

//
// An absent "d" is opaque (4.2). Zero would mean invisible, which no source
// file says by saying nothing.
//

TEST(MtlDissolve, AbsentDissolveIsOpaque) {
  GMDL_Mtl * mtl = load_text("newmtl body\nKd 0.8 0.1 0.1\nNs 96\nillum 2\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  EXPECT_FLOAT_EQ(mtl->materials[0].d, 1.0f);
  gmdl_mtl_free(mtl);
}

TEST(MtlDissolve, EveryMaterialGetsItsOwnDefault) {
  // The default is set per material at newmtl, not once for the file.
  GMDL_Mtl * mtl = load_text(
      "newmtl a\nd 0.25\n"
      "newmtl b\nKd 1 1 1\n"
      "newmtl c\nd 0\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 3u);
  EXPECT_FLOAT_EQ(mtl->materials[0].d, 0.25f);
  EXPECT_FLOAT_EQ(mtl->materials[1].d, 1.0f) << "absent";
  EXPECT_FLOAT_EQ(mtl->materials[2].d, 0.0f) << "explicit zero is still zero";
  gmdl_mtl_free(mtl);
}

TEST(MtlDissolve, AnExplicitZeroSurvivesTheRoundTrip) {
  // The default must not swallow a material that really is invisible.
  GMDL_Mtl * first = load_text("newmtl ghost\nd 0\n");
  ASSERT_NE(first, nullptr);
  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_mtl_dump(first, sink), GMDL_OK);
  fclose(sink);
  GMDL_Mtl * second = nullptr;
  ASSERT_EQ(gmdl_mtl_load_file(out.path(), nullptr, nullptr, &second),
      GMDL_OK);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->material_count, 1u);
  EXPECT_FLOAT_EQ(second->materials[0].d, 0.0f);
  gmdl_mtl_free(first);
  gmdl_mtl_free(second);
}

namespace {

/** The text gmdl_mtl_dump() produces for a document. */
std::string dump_text(const GMDL_Mtl * mtl) {
  TempFile out("");
  EXPECT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  EXPECT_NE(sink, nullptr);
  EXPECT_EQ(gmdl_mtl_dump(mtl, sink), GMDL_OK);
  fclose(sink);
  FILE * back = fopen(out.path(), "rb");
  EXPECT_NE(back, nullptr);
  std::string text;
  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), back)) > 0) text.append(buf, n);
  fclose(back);
  return text;
}

} // namespace

//
// What a material states, and what the dump therefore says (4.2, 4.4).
//

TEST(MtlPresent, OnlyStatedPropertiesAreRecorded) {
  GMDL_Mtl * mtl = load_text("newmtl body\nKd 0.8 0.1 0.1\nillum 2\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  uint32_t p = mtl->materials[0].present;
  EXPECT_TRUE(p & GMDL_MTL_HAS_KD);
  EXPECT_TRUE(p & GMDL_MTL_HAS_ILLUM);
  EXPECT_FALSE(p & GMDL_MTL_HAS_KA);
  EXPECT_FALSE(p & GMDL_MTL_HAS_KS);
  EXPECT_FALSE(p & GMDL_MTL_HAS_NS);
  EXPECT_FALSE(p & GMDL_MTL_HAS_D);
  gmdl_mtl_free(mtl);
}

TEST(MtlPresent, AnExplicitZeroCountsAsStated) {
  // The whole point: "Kd 0 0 0" is black and must survive, while a material
  // that never said Kd must not acquire one.
  GMDL_Mtl * mtl = load_text("newmtl black\nKd 0 0 0\nd 0\nNs 0\nillum 0\n");
  ASSERT_NE(mtl, nullptr);
  uint32_t p = mtl->materials[0].present;
  EXPECT_TRUE(p & GMDL_MTL_HAS_KD);
  EXPECT_TRUE(p & GMDL_MTL_HAS_D);
  EXPECT_TRUE(p & GMDL_MTL_HAS_NS);
  EXPECT_TRUE(p & GMDL_MTL_HAS_ILLUM);
  gmdl_mtl_free(mtl);
}

TEST(MtlPresent, TheDumpOmitsWhatWasNeverStated) {
  // Writing "Kd 0 0 0" for a material with no Kd line made it black in both
  // Blender and VTK, where an absent Kd is a light default. The dump must
  // say nothing rather than say zero.
  GMDL_Mtl * mtl = load_text("newmtl body\nKa 0.2 0.2 0.2\nKs 1 1 1\nNs 96\n");
  ASSERT_NE(mtl, nullptr);
  std::string text = dump_text(mtl);
  EXPECT_EQ(text.find("\nKd "), std::string::npos)
      << "no Kd was stated, so none may be written:\n" << text;
  EXPECT_EQ(text.find("\nd "), std::string::npos) << text;
  EXPECT_EQ(text.find("\nillum "), std::string::npos) << text;
  EXPECT_NE(text.find("\nKa "), std::string::npos) << text;
  EXPECT_NE(text.find("\nKs "), std::string::npos) << text;
  EXPECT_NE(text.find("\nNs "), std::string::npos) << text;
  gmdl_mtl_free(mtl);
}

TEST(MtlPresent, TheDumpKeepsAnExplicitZero) {
  GMDL_Mtl * mtl = load_text("newmtl black\nKd 0 0 0\n");
  ASSERT_NE(mtl, nullptr);
  std::string text = dump_text(mtl);
  EXPECT_NE(text.find("\nKd 0 0 0\n"), std::string::npos) << text;
  gmdl_mtl_free(mtl);
}

TEST(MtlPresent, AMaterialStatingNothingDumpsAsABareNewmtl) {
  GMDL_Mtl * mtl = load_text("newmtl empty\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_EQ(mtl->materials[0].present, 0u);
  EXPECT_EQ(dump_text(mtl), "newmtl empty\n\n");
  gmdl_mtl_free(mtl);
}

TEST(MtlPresent, PresenceSurvivesTheRoundTrip) {
  GMDL_Mtl * first = load_text(
      "newmtl a\nKd 0.5 0.5 0.5\n"
      "newmtl b\nKa 1 1 1\nNs 3\nd 0\n"
      "newmtl c\n");
  ASSERT_NE(first, nullptr);
  ASSERT_EQ(first->material_count, 3u);
  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_mtl_dump(first, sink), GMDL_OK);
  fclose(sink);
  GMDL_Mtl * second = nullptr;
  ASSERT_EQ(gmdl_mtl_load_file(out.path(), nullptr, nullptr, &second),
      GMDL_OK);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->material_count, 3u);
  for (size_t i = 0; i < 3; i++) {
    EXPECT_EQ(first->materials[i].present, second->materials[i].present)
        << "material " << i;
    EXPECT_FLOAT_EQ(first->materials[i].d, second->materials[i].d)
        << "material " << i;
  }
  gmdl_mtl_free(first);
  gmdl_mtl_free(second);
}

//
// Texture maps (4.5).
//

TEST(MtlMap, APlainPathIsRead) {
  GMDL_Mtl * mtl = load_text("newmtl body\nmap_Kd brick.png\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  ASSERT_NE(mtl->materials[0].map_Kd, nullptr);
  EXPECT_STREQ(mtl->materials[0].map_Kd, "brick.png");
  gmdl_mtl_free(mtl);
}

TEST(MtlMap, AMapNobodyStatedIsNull) {
  // A pointer says this for itself, which is why the maps need no bit in
  // `present`: no file can ask for NULL.
  GMDL_Mtl * mtl = load_text("newmtl body\nKd 1 1 1\n");
  ASSERT_NE(mtl, nullptr);
  const GMDL_Mtl_Material & m = mtl->materials[0];
  EXPECT_EQ(m.map_Ka, nullptr);
  EXPECT_EQ(m.map_Kd, nullptr);
  EXPECT_EQ(m.map_Ks, nullptr);
  EXPECT_EQ(m.map_Ns, nullptr);
  EXPECT_EQ(m.map_d, nullptr);
  EXPECT_EQ(m.map_bump, nullptr);
  gmdl_mtl_free(mtl);
}

TEST(MtlMap, EveryKindIsRead) {
  GMDL_Mtl * mtl = load_text("newmtl all\n"
                             "map_Ka a.png\n"
                             "map_Kd d.png\n"
                             "map_Ks s.png\n"
                             "map_Ns n.png\n"
                             "map_d alpha.png\n"
                             "map_bump b.png\n");
  ASSERT_NE(mtl, nullptr);
  const GMDL_Mtl_Material & m = mtl->materials[0];
  EXPECT_STREQ(m.map_Ka, "a.png");
  EXPECT_STREQ(m.map_Kd, "d.png");
  EXPECT_STREQ(m.map_Ks, "s.png");
  EXPECT_STREQ(m.map_Ns, "n.png");
  EXPECT_STREQ(m.map_d, "alpha.png");
  EXPECT_STREQ(m.map_bump, "b.png");
  gmdl_mtl_free(mtl);
}

TEST(MtlMap, APathMayContainSpaces) {
  // Measured rather than assumed: Blender 4.3 and VTK 9.3 both take the whole
  // of the rest of the line, so "my tex.png" is one file and not two tokens.
  GMDL_Mtl * mtl = load_text("newmtl body\nmap_Kd my tex.png\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_STREQ(mtl->materials[0].map_Kd, "my tex.png");
  gmdl_mtl_free(mtl);
}

TEST(MtlMap, TrailingBlanksAreNotPartOfThePath) {
  // Blender strips them; VTK 9.3 keeps them and then cannot find the file it
  // just named, which is the behaviour of the two worth not copying.
  GMDL_Mtl * mtl = load_text("newmtl body\nmap_Kd tex.png \t \n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_STREQ(mtl->materials[0].map_Kd, "tex.png");
  gmdl_mtl_free(mtl);
}

TEST(MtlMap, SeparatorsAreNotTranslated) {
  // A path is kept as written. Both references keep the backslash too, and
  // only the caller knows what platform the path was written for.
  GMDL_Mtl * mtl = load_text("newmtl body\nmap_Kd sub\\tex.png\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_STREQ(mtl->materials[0].map_Kd, "sub\\tex.png");
  gmdl_mtl_free(mtl);
}

TEST(MtlMap, OptionsAreUnsupportedNotMalformed) {
  // The file is well-formed; this library is the one falling short. The two
  // references do not even agree on what the options are - Blender consumes
  // -clamp, VTK 9.3 folds it into the filename - so guessing would be taking
  // a side the caller cannot see.
  // Every kind, because each leaves the parser by its own arm.
  for (const char * line : {"map_Kd -o 1 1 1 tex.png\n",
           "map_Kd -s 2 2 2 tex.png\n", "map_Kd -clamp on tex.png\n",
           "map_Ka -o 1 a.png\n", "map_Ks -o 1 s.png\n",
           "map_Ns -o 1 n.png\n", "map_d -o 1 alpha.png\n",
           "map_bump -bm 0.5 b.png\n", "bump -bm 0.5 b.png\n"}) {
    EXPECT_EQ(load_text_expecting_failure(std::string("newmtl a\n") + line),
        GMDL_ERR_UNSUPPORTED)
        << line;
  }
}

TEST(MtlMap, ADirectiveWithNoPathIsMalformed) {
  // Both references ignore the line instead; 4.5 records that divergence.
  EXPECT_EQ(load_text_expecting_failure("newmtl a\nmap_Kd\n"), GMDL_ERR_FORMAT);
  EXPECT_EQ(
      load_text_expecting_failure("newmtl a\nmap_Kd   \n"), GMDL_ERR_FORMAT);
}

TEST(MtlMap, BumpAndMapBumpAreOneProperty) {
  GMDL_Mtl * mtl = load_text("newmtl a\nbump b.png\nnewmtl b\nmap_bump c.png\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 2u);
  EXPECT_STREQ(mtl->materials[0].map_bump, "b.png");
  EXPECT_STREQ(mtl->materials[1].map_bump, "c.png");
  // One property, so one spelling comes back out.
  EXPECT_NE(dump_text(mtl).find("map_bump b.png"), std::string::npos);
  gmdl_mtl_free(mtl);
}

TEST(MtlMap, ARepeatedDirectiveKeepsTheLast) {
  // The format cannot say two maps of one kind, so the first is released
  // rather than leaked.
  GMDL_Mtl * mtl = load_text("newmtl a\nmap_Kd first.png\nmap_Kd second.png\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_STREQ(mtl->materials[0].map_Kd, "second.png");
  gmdl_mtl_free(mtl);
}

TEST(MtlMap, ADirectiveBeginningWithAMapKeywordIsNotTheKeyword) {
  GMDL_Mtl * mtl = load_text("newmtl a\nmap_Kdx tex.png\nbumpy tex.png\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_EQ(mtl->materials[0].map_Kd, nullptr);
  EXPECT_EQ(mtl->materials[0].map_bump, nullptr);
  gmdl_mtl_free(mtl);
}

TEST(MtlMap, TheDumpWritesOnlyTheMapsStated) {
  GMDL_Mtl * mtl = load_text("newmtl body\nmap_Kd d.png\n");
  ASSERT_NE(mtl, nullptr);
  std::string text = dump_text(mtl);
  EXPECT_NE(text.find("map_Kd d.png\n"), std::string::npos) << text;
  EXPECT_EQ(text.find("map_Ka"), std::string::npos) << text;
  EXPECT_EQ(text.find("map_Ks"), std::string::npos) << text;
  EXPECT_EQ(text.find("map_bump"), std::string::npos) << text;
  gmdl_mtl_free(mtl);
}

TEST(MtlMap, AMaterialWithOnlyAMapIsNotABareNewmtl) {
  // `present` stays 0 - no scalar property was stated - and the map must
  // still be written, which is what would break if the dump keyed on it.
  GMDL_Mtl * mtl = load_text("newmtl body\nmap_Kd d.png\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_EQ(mtl->materials[0].present, 0u);
  EXPECT_EQ(dump_text(mtl), "newmtl body\nmap_Kd d.png\n\n");
  gmdl_mtl_free(mtl);
}

TEST(MtlMap, PathsSurviveTheRoundTrip) {
  GMDL_Mtl * first = load_text("newmtl a\nmap_Kd my tex.png\nmap_bump b.png\n"
                               "newmtl b\nmap_Ka sub\\a.png\nKd 1 0 0\n"
                               "newmtl c\n");
  ASSERT_NE(first, nullptr);
  ASSERT_EQ(first->material_count, 3u);
  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_mtl_dump(first, sink), GMDL_OK);
  fclose(sink);
  GMDL_Mtl * second = nullptr;
  ASSERT_EQ(
      gmdl_mtl_load_file(out.path(), nullptr, nullptr, &second), GMDL_OK);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->material_count, 3u);
  for (size_t i = 0; i < 3; i++) {
    const GMDL_Mtl_Material & a = first->materials[i];
    const GMDL_Mtl_Material & b = second->materials[i];
    ASSERT_EQ(a.map_Kd == nullptr, b.map_Kd == nullptr) << "material " << i;
    if (a.map_Kd) {
      EXPECT_STREQ(a.map_Kd, b.map_Kd) << "material " << i;
    }
    ASSERT_EQ(a.map_Ka == nullptr, b.map_Ka == nullptr) << "material " << i;
    if (a.map_Ka) {
      EXPECT_STREQ(a.map_Ka, b.map_Ka) << "material " << i;
    }
    ASSERT_EQ(a.map_bump == nullptr, b.map_bump == nullptr) << "material " << i;
    if (a.map_bump) {
      EXPECT_STREQ(a.map_bump, b.map_bump) << "material " << i;
    }
  }
  gmdl_mtl_free(first);
  gmdl_mtl_free(second);
}

TEST(MtlMap, TheDumpWritesKdAfterKa) {
  // Not cosmetic, and not to be tidied into alphabetical order. VTK 9.3
  // keeps one texture per material, which map_Ka and map_Kd both fill and
  // the last one wins; writing Kd second makes it settle on the diffuse
  // map, which is what a single-texture renderer wants. Measured, and
  // recorded in section 9.
  GMDL_Mtl * mtl = load_text("newmtl m\nmap_Kd d.png\nmap_Ka a.png\n");
  ASSERT_NE(mtl, nullptr);
  std::string text = dump_text(mtl);
  size_t ka = text.find("map_Ka ");
  size_t kd = text.find("map_Kd ");
  ASSERT_NE(ka, std::string::npos) << text;
  ASSERT_NE(kd, std::string::npos) << text;
  EXPECT_LT(ka, kd) << "map_Kd must be written after map_Ka:\n" << text;
  gmdl_mtl_free(mtl);
}

TEST(MtlMap, ATrailingDoubleBackslashLeavesThePathEndingInOne) {
  // The same hole the material name has, reached the same way: the
  // continuation takes one backslash and the blank line stops the join, so
  // the path is "a\" - which a dump writes last on its line, where a
  // backslash continues. The fuzz harness skips such a library, and this is
  // the test that stops that exemption from covering a real defect.
  GMDL_Mtl * mtl = load_text("newmtl m\nmap_Kd a\\\\\n\nKa 1 1 1\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 1u);
  EXPECT_STREQ(mtl->materials[0].map_Kd, "a\\");
  EXPECT_FLOAT_EQ(mtl->materials[0].Ka[0], 1.0f);
  gmdl_mtl_free(mtl);
}

TEST(MtlMap, AFailedLoadReleasesThePathsItHadAlreadyRead) {
  // Nothing is handed back, so the paths read before the bad line are the
  // library's to release. ASan and valgrind are what actually check this.
  EXPECT_EQ(load_text_expecting_failure("newmtl a\nmap_Kd one.png\n"
                                        "newmtl b\nmap_Ka two.png\n"
                                        "map_Kd -o 1 tex.png\n"),
      GMDL_ERR_UNSUPPORTED);
}

//
// Every write the dumper checks can fail (section 9).
//

TEST(MtlDump, EveryWriteFailureIsReported) {
  // Two materials between them reaching every line the dumper can write:
  // all six scalar properties and all six maps, plus one material that
  // states almost nothing, so the sweep crosses a material boundary.
  GMDL_Mtl * mtl = load_text("newmtl full\n"
                             "Ka 0.1 0.2 0.3\nKd 0.4 0.5 0.6\n"
                             "Ks 0.7 0.8 0.9\nKe 0.11 0.12 0.13\n"
                             "Tf 0.21 0.22 0.23\n"
                             "Ns 96\nNi 1.45\nd 0.5\nTr 0.25\nillum 2\n"
                             "sharpness 60\nPr 0.4\nPm 0.6\nPs 0.7\n"
                             "Pc 0.2\nPcr 0.3\naniso 0.1\nanisor 0.2\n"
                             "map_aat on\n"
                             "map_Ka a.png\nmap_Kd d.png\nmap_Ks s.png\n"
                             "map_Ns n.png\nmap_d alpha.png\nmap_bump b.png\n"
                             "map_Ke e.png\nmap_Pr pr.png\nmap_Pm pm.png\n"
                             "map_Ps ps.png\nnorm nm.png\ndisp dp.png\n"
                             "decal dc.png\nrefl sp.png\n"
                             "refl -type cube_top ct.png\n"
                             "newmtl bare\n");
  ASSERT_NE(mtl, nullptr);
  ASSERT_EQ(mtl->material_count, 2u);

  size_t failures = 0;
  for (size_t allow = 0; allow < 4096; allow++) {
    FailingSink sink(allow);
    ASSERT_NE(sink.get(), nullptr);
    GMDL_Result r = gmdl_mtl_dump(mtl, sink.get());
    if (r == GMDL_OK) {
      EXPECT_FALSE(sink.failed()) << "a dump that succeeded wrote past the "
                                     "budget it was given";
      break;
    }
    EXPECT_EQ(r, GMDL_ERR_IO) << "write " << allow << " failed and the dumper "
                              << "answered " << gmdl_result_string(r);
    EXPECT_TRUE(sink.failed())
        << "write " << allow << " reported I/O without the sink refusing";
    failures++;
  }
  // One line per property and per map, two newmtl lines and two blanks.
  // Every directive the material can carry is present, because a sweep over
  // a partial material leaves the arms of whatever it omitted unexecuted -
  // which is how this test silently stopped covering the writer when the
  // vocabulary grew.
  EXPECT_GT(failures, 35u) << "the sweep stopped far too early";
  gmdl_mtl_free(mtl);
}

//
// The rest of the MTL vocabulary (4.2, 4.5, 4.6).
//

namespace {

struct ScalarCase {
  const char * line;
  uint32_t bit;
  std::function<bool(const GMDL_Mtl_Material &)> holds;
};

} // namespace

TEST(MtlVocabulary, EachDirectiveSetsItsOwnFieldAndItsOwnBit) {
  // The dispatch is twenty near-identical blocks, and the mistake such code
  // invites is not a parse failure but a crossed wire: the right value in
  // the wrong field, or the wrong bit beside a right value. Both survive a
  // test that only checks the value it just wrote. Each case here states one
  // directive alone and demands that `present` equals exactly its own bit,
  // so a stray assignment to another property is a failure too.
  const std::vector<ScalarCase> cases = {
      {"Ka 0.1 0.2 0.3", GMDL_MTL_HAS_KA,
          [](const GMDL_Mtl_Material & m) { return m.Ka[0] == 0.1f && m.Ka[2] == 0.3f; }},
      {"Kd 0.1 0.2 0.3", GMDL_MTL_HAS_KD,
          [](const GMDL_Mtl_Material & m) { return m.Kd[0] == 0.1f && m.Kd[2] == 0.3f; }},
      {"Ks 0.1 0.2 0.3", GMDL_MTL_HAS_KS,
          [](const GMDL_Mtl_Material & m) { return m.Ks[0] == 0.1f && m.Ks[2] == 0.3f; }},
      {"Ke 0.1 0.2 0.3", GMDL_MTL_HAS_KE,
          [](const GMDL_Mtl_Material & m) { return m.Ke[0] == 0.1f && m.Ke[2] == 0.3f; }},
      {"Tf 0.1 0.2 0.3", GMDL_MTL_HAS_TF,
          [](const GMDL_Mtl_Material & m) { return m.Tf[0] == 0.1f && m.Tf[2] == 0.3f; }},
      {"Ns 96", GMDL_MTL_HAS_NS,
          [](const GMDL_Mtl_Material & m) { return m.Ns == 96.0f; }},
      {"Ni 1.45", GMDL_MTL_HAS_NI,
          [](const GMDL_Mtl_Material & m) { return m.Ni == 1.45f; }},
      {"d 0.5", GMDL_MTL_HAS_D,
          [](const GMDL_Mtl_Material & m) { return m.d == 0.5f; }},
      {"Tr 0.25", GMDL_MTL_HAS_TR,
          [](const GMDL_Mtl_Material & m) { return m.Tr == 0.25f; }},
      {"illum 2", GMDL_MTL_HAS_ILLUM,
          [](const GMDL_Mtl_Material & m) { return m.illum == 2; }},
      {"sharpness 60", GMDL_MTL_HAS_SHARPNESS,
          [](const GMDL_Mtl_Material & m) { return m.sharpness == 60; }},
      {"Pr 0.4", GMDL_MTL_HAS_PR,
          [](const GMDL_Mtl_Material & m) { return m.Pr == 0.4f; }},
      {"Pm 0.6", GMDL_MTL_HAS_PM,
          [](const GMDL_Mtl_Material & m) { return m.Pm == 0.6f; }},
      {"Ps 0.7", GMDL_MTL_HAS_PS,
          [](const GMDL_Mtl_Material & m) { return m.Ps == 0.7f; }},
      {"Pc 0.2", GMDL_MTL_HAS_PC,
          [](const GMDL_Mtl_Material & m) { return m.Pc == 0.2f; }},
      {"Pcr 0.3", GMDL_MTL_HAS_PCR,
          [](const GMDL_Mtl_Material & m) { return m.Pcr == 0.3f; }},
      {"aniso 0.1", GMDL_MTL_HAS_ANISO,
          [](const GMDL_Mtl_Material & m) { return m.aniso == 0.1f; }},
      {"anisor 0.2", GMDL_MTL_HAS_ANISOR,
          [](const GMDL_Mtl_Material & m) { return m.anisor == 0.2f; }},
      {"map_aat on", GMDL_MTL_HAS_MAP_AAT,
          [](const GMDL_Mtl_Material & m) { return m.map_aat; }},
      {"map_aat off", GMDL_MTL_HAS_MAP_AAT,
          [](const GMDL_Mtl_Material & m) { return !m.map_aat; }},
  };

  for (const ScalarCase & c : cases) {
    GMDL_Mtl * mtl = load_text(std::string("newmtl m\n") + c.line + "\n");
    ASSERT_NE(mtl, nullptr) << c.line;
    ASSERT_EQ(mtl->material_count, 1u) << c.line;
    const GMDL_Mtl_Material & m = mtl->materials[0];
    EXPECT_EQ(m.present, c.bit)
        << c.line << " set " << m.present << ", expected only " << c.bit;
    EXPECT_TRUE(c.holds(m)) << c.line << " did not reach its own field";
    gmdl_mtl_free(mtl);
  }
}

TEST(MtlVocabulary, EachMapDirectiveReachesItsOwnField) {
  // The same crossed-wire risk on the path side, where NULL is the marker
  // rather than a bit: exactly one path may be non-NULL.
  const std::vector<std::pair<const char *, size_t>> cases = {
      {"map_Ka", offsetof(GMDL_Mtl_Material, map_Ka)},
      {"map_Kd", offsetof(GMDL_Mtl_Material, map_Kd)},
      {"map_Ks", offsetof(GMDL_Mtl_Material, map_Ks)},
      {"map_Ns", offsetof(GMDL_Mtl_Material, map_Ns)},
      {"map_d", offsetof(GMDL_Mtl_Material, map_d)},
      {"map_bump", offsetof(GMDL_Mtl_Material, map_bump)},
      {"bump", offsetof(GMDL_Mtl_Material, map_bump)},
      {"map_Ke", offsetof(GMDL_Mtl_Material, map_Ke)},
      {"map_Pr", offsetof(GMDL_Mtl_Material, map_Pr)},
      {"map_Pm", offsetof(GMDL_Mtl_Material, map_Pm)},
      {"map_Ps", offsetof(GMDL_Mtl_Material, map_Ps)},
      {"norm", offsetof(GMDL_Mtl_Material, norm)},
      {"disp", offsetof(GMDL_Mtl_Material, disp)},
      {"decal", offsetof(GMDL_Mtl_Material, decal)},
  };

  for (const auto & c : cases) {
    GMDL_Mtl * mtl =
        load_text(std::string("newmtl m\n") + c.first + " t.png\n");
    ASSERT_NE(mtl, nullptr) << c.first;
    const GMDL_Mtl_Material & m = mtl->materials[0];
    const char * expected =
        *reinterpret_cast<const char * const *>(
            reinterpret_cast<const char *>(&m) + c.second);
    ASSERT_NE(expected, nullptr) << c.first << " did not reach its field";
    EXPECT_STREQ(expected, "t.png") << c.first;

    // ...and nothing else moved.
    size_t filled = 0;
    for (const char * p : {m.map_Ka, m.map_Kd, m.map_Ks, m.map_Ns, m.map_d,
             m.map_bump, m.map_Ke, m.map_Pr, m.map_Pm, m.map_Ps, m.norm,
             m.disp, m.decal}) {
      filled += p != nullptr;
    }
    for (size_t i = 0; i < GMDL_MTL_REFL_COUNT; i++) {
      filled += m.refl[i] != nullptr;
    }
    EXPECT_EQ(filled, 1u) << c.first << " filled in more than its own path";
    EXPECT_EQ(m.present, 0u) << c.first << " set a scalar's bit";
    gmdl_mtl_free(mtl);
  }
}

TEST(MtlTr, IsRecordedAndNotFoldedIntoDissolve) {
  // 4.2's open question, settled by measurement rather than by the format's
  // "Tr is 1 - d": neither reference derives one from the other. Blender 4.3
  // ignores Tr outright, VTK 9.3 likewise, and both take d whichever order
  // the pair appears in. So both are recorded and neither is synthesised.
  GMDL_Mtl * mtl = load_text("newmtl only_tr\nTr 0.25\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_FLOAT_EQ(mtl->materials[0].Tr, 0.25f);
  EXPECT_FLOAT_EQ(mtl->materials[0].d, 1.0f) << "d must keep its own default";
  EXPECT_FALSE(mtl->materials[0].present & GMDL_MTL_HAS_D);
  EXPECT_TRUE(mtl->materials[0].present & GMDL_MTL_HAS_TR);
  gmdl_mtl_free(mtl);
}

TEST(MtlTr, AContradictoryPairIsKeptAsWritten) {
  // d says three-quarters opaque, Tr says nine-tenths transparent. They
  // cannot both be describing the same surface, and it is not the parser's
  // place to pick; `present` says the file stated both.
  GMDL_Mtl * mtl = load_text("newmtl both\nd 0.75\nTr 0.9\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_FLOAT_EQ(mtl->materials[0].d, 0.75f);
  EXPECT_FLOAT_EQ(mtl->materials[0].Tr, 0.9f);
  uint32_t p = mtl->materials[0].present;
  EXPECT_TRUE(p & GMDL_MTL_HAS_D);
  EXPECT_TRUE(p & GMDL_MTL_HAS_TR);
  gmdl_mtl_free(mtl);
}

TEST(MtlRefl, EveryTypeReachesItsOwnSlot) {
  GMDL_Mtl * mtl = load_text("newmtl m\n"
                             "refl -type sphere s.png\n"
                             "refl -type cube_top top.png\n"
                             "refl -type cube_bottom bottom.png\n"
                             "refl -type cube_front front.png\n"
                             "refl -type cube_back back.png\n"
                             "refl -type cube_left left.png\n"
                             "refl -type cube_right right.png\n");
  ASSERT_NE(mtl, nullptr);
  const GMDL_Mtl_Material & m = mtl->materials[0];
  EXPECT_STREQ(m.refl[GMDL_MTL_REFL_SPHERE], "s.png");
  EXPECT_STREQ(m.refl[GMDL_MTL_REFL_CUBE_TOP], "top.png");
  EXPECT_STREQ(m.refl[GMDL_MTL_REFL_CUBE_BOTTOM], "bottom.png");
  EXPECT_STREQ(m.refl[GMDL_MTL_REFL_CUBE_FRONT], "front.png");
  EXPECT_STREQ(m.refl[GMDL_MTL_REFL_CUBE_BACK], "back.png");
  EXPECT_STREQ(m.refl[GMDL_MTL_REFL_CUBE_LEFT], "left.png");
  EXPECT_STREQ(m.refl[GMDL_MTL_REFL_CUBE_RIGHT], "right.png");
  EXPECT_EQ(m.refl[GMDL_MTL_REFL_UNTYPED], nullptr);

  // And each comes back out under the same -type. Written as one comparison
  // so that a slot dumped under the wrong name is a failure rather than a
  // line nobody looks at.
  EXPECT_EQ(dump_text(mtl),
      "newmtl m\n"
      "refl -type sphere s.png\n"
      "refl -type cube_top top.png\n"
      "refl -type cube_bottom bottom.png\n"
      "refl -type cube_front front.png\n"
      "refl -type cube_back back.png\n"
      "refl -type cube_left left.png\n"
      "refl -type cube_right right.png\n\n");
  gmdl_mtl_free(mtl);
}

TEST(MtlRefl, ALineWithNoTypeKeepsItsOwnSlot) {
  // The format wants -type. Both references accept the line without it, so
  // refusing would reject files that exist; guessing a surface would invent
  // one. It gets a slot of its own and comes back out the way it went in.
  GMDL_Mtl * mtl = load_text("newmtl m\nrefl t.png\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_STREQ(mtl->materials[0].refl[GMDL_MTL_REFL_UNTYPED], "t.png");
  EXPECT_EQ(mtl->materials[0].refl[GMDL_MTL_REFL_SPHERE], nullptr);
  EXPECT_EQ(dump_text(mtl), "newmtl m\nrefl t.png\n\n");
  gmdl_mtl_free(mtl);
}

TEST(MtlRefl, TypedSlotsAreWrittenWithTheirType) {
  GMDL_Mtl * mtl = load_text("newmtl m\nrefl -type cube_left l.png\n");
  ASSERT_NE(mtl, nullptr);
  EXPECT_EQ(dump_text(mtl), "newmtl m\nrefl -type cube_left l.png\n\n");
  gmdl_mtl_free(mtl);
}

TEST(MtlRefl, AnUnknownTypeIsMalformed) {
  EXPECT_EQ(load_text_expecting_failure("newmtl m\nrefl -type wedge t.png\n"),
      GMDL_ERR_FORMAT);
  EXPECT_EQ(
      load_text_expecting_failure("newmtl m\nrefl -type\n"), GMDL_ERR_FORMAT);
}

TEST(MtlRefl, AnyOtherOptionIsStillUnsupported) {
  // -type is read because it names the slot rather than adjusting sampling.
  // That is not a general opening of the option syntax.
  EXPECT_EQ(load_text_expecting_failure("newmtl m\nrefl -s 2 2 2 t.png\n"),
      GMDL_ERR_UNSUPPORTED);
  EXPECT_EQ(load_text_expecting_failure(
                "newmtl m\nrefl -type sphere -s 2 2 2 t.png\n"),
      GMDL_ERR_UNSUPPORTED);
}

TEST(MtlRefl, ATypedLineWithNoPathIsMalformed) {
  EXPECT_EQ(load_text_expecting_failure("newmtl m\nrefl -type sphere\n"),
      GMDL_ERR_FORMAT);
}

TEST(MtlVocabulary, AToggleThatIsNeitherOnNorOffIsMalformed) {
  EXPECT_EQ(
      load_text_expecting_failure("newmtl m\nmap_aat yes\n"), GMDL_ERR_FORMAT);
  EXPECT_EQ(
      load_text_expecting_failure("newmtl m\nmap_aat\n"), GMDL_ERR_FORMAT);
}

TEST(MtlVocabulary, UnimplementedColourFormsStillApplyToTheNewColours) {
  EXPECT_EQ(load_text_expecting_failure("newmtl m\nKe spectral f.rfl\n"),
      GMDL_ERR_UNSUPPORTED);
  EXPECT_EQ(load_text_expecting_failure("newmtl m\nTf xyz 1 2 3\n"),
      GMDL_ERR_UNSUPPORTED);
}

TEST(MtlVocabulary, EveryDirectiveRefusesWhatItCannotRead) {
  // The refusal arm of each directive, which is a separate branch per
  // directive and therefore a separate way to get one wrong. A colour that
  // names an unimplemented form is UNSUPPORTED; a value that is not a value
  // at all is FORMAT; a map carrying options is UNSUPPORTED (4.5).
  const std::vector<std::pair<const char *, GMDL_Result>> cases = {
      {"Ka spectral f.rfl", GMDL_ERR_UNSUPPORTED},
      {"Kd xyz 1 2 3", GMDL_ERR_UNSUPPORTED},
      {"Ks spectral f.rfl", GMDL_ERR_UNSUPPORTED},
      {"Ke xyz 1 2 3", GMDL_ERR_UNSUPPORTED},
      {"Tf spectral f.rfl", GMDL_ERR_UNSUPPORTED},
      {"Ka 0.5 x", GMDL_ERR_FORMAT},
      {"Ke 0.5 x", GMDL_ERR_FORMAT},
      {"Tf 0.5 x", GMDL_ERR_FORMAT},
      {"Ns x", GMDL_ERR_FORMAT},
      {"Ni x", GMDL_ERR_FORMAT},
      {"d x", GMDL_ERR_FORMAT},
      {"Tr x", GMDL_ERR_FORMAT},
      {"illum x", GMDL_ERR_FORMAT},
      {"sharpness x", GMDL_ERR_FORMAT},
      {"Pr x", GMDL_ERR_FORMAT},
      {"Pm x", GMDL_ERR_FORMAT},
      {"Ps x", GMDL_ERR_FORMAT},
      {"Pc x", GMDL_ERR_FORMAT},
      {"Pcr x", GMDL_ERR_FORMAT},
      {"aniso x", GMDL_ERR_FORMAT},
      {"anisor x", GMDL_ERR_FORMAT},
      {"map_aat maybe", GMDL_ERR_FORMAT},
      {"map_Ka -o 1 a.png", GMDL_ERR_UNSUPPORTED},
      {"map_Kd -o 1 d.png", GMDL_ERR_UNSUPPORTED},
      {"map_Ks -o 1 s.png", GMDL_ERR_UNSUPPORTED},
      {"map_Ns -o 1 n.png", GMDL_ERR_UNSUPPORTED},
      {"map_d -o 1 a.png", GMDL_ERR_UNSUPPORTED},
      {"map_bump -bm 1 b.png", GMDL_ERR_UNSUPPORTED},
      {"bump -bm 1 b.png", GMDL_ERR_UNSUPPORTED},
      {"map_Ke -o 1 e.png", GMDL_ERR_UNSUPPORTED},
      {"map_Pr -o 1 p.png", GMDL_ERR_UNSUPPORTED},
      {"map_Pm -o 1 p.png", GMDL_ERR_UNSUPPORTED},
      {"map_Ps -o 1 p.png", GMDL_ERR_UNSUPPORTED},
      {"norm -o 1 n.png", GMDL_ERR_UNSUPPORTED},
      {"disp -o 1 d.png", GMDL_ERR_UNSUPPORTED},
      {"decal -o 1 d.png", GMDL_ERR_UNSUPPORTED},
      {"refl -s 2 2 2 r.png", GMDL_ERR_UNSUPPORTED},
      {"map_Ka", GMDL_ERR_FORMAT},
      {"norm", GMDL_ERR_FORMAT},
      {"decal   ", GMDL_ERR_FORMAT},
  };

  for (const auto & c : cases) {
    EXPECT_EQ(load_text_expecting_failure(
                  std::string("newmtl m\n") + c.first + "\n"),
        c.second)
        << c.first;
  }
}

TEST(MtlVocabulary, TheWholeVocabularySurvivesTheRoundTrip) {
  GMDL_Mtl * first = load_text("newmtl everything\n"
                               "Ka 0.1 0.2 0.3\nKd 0.4 0.5 0.6\n"
                               "Ks 0.7 0.8 0.9\nKe 0.11 0.12 0.13\n"
                               "Tf 0.21 0.22 0.23\n"
                               "Ns 96\nNi 1.45\nd 0.5\nTr 0.25\nillum 2\n"
                               "sharpness 60\n"
                               "Pr 0.4\nPm 0.6\nPs 0.7\nPc 0.2\nPcr 0.3\n"
                               "aniso 0.1\nanisor 0.2\nmap_aat on\n"
                               "map_Ka a.png\nmap_Kd d.png\nmap_Ks s.png\n"
                               "map_Ns n.png\nmap_d alpha.png\n"
                               "map_bump b.png\nmap_Ke e.png\nmap_Pr pr.png\n"
                               "map_Pm pm.png\nmap_Ps ps.png\nnorm nm.png\n"
                               "disp dp.png\ndecal dc.png\n"
                               "refl -type sphere sp.png\n"
                               "refl -type cube_back cb.png\n");
  ASSERT_NE(first, nullptr);
  ASSERT_EQ(first->material_count, 1u);

  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_mtl_dump(first, sink), GMDL_OK);
  fclose(sink);

  GMDL_Mtl * second = nullptr;
  ASSERT_EQ(
      gmdl_mtl_load_file(out.path(), nullptr, nullptr, &second), GMDL_OK);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->material_count, 1u);
  const GMDL_Mtl_Material & a = first->materials[0];
  const GMDL_Mtl_Material & b = second->materials[0];

  EXPECT_EQ(a.present, b.present);
  for (int c = 0; c < 3; c++) {
    EXPECT_FLOAT_EQ(a.Ke[c], b.Ke[c]) << "Ke " << c;
    EXPECT_FLOAT_EQ(a.Tf[c], b.Tf[c]) << "Tf " << c;
  }
  EXPECT_FLOAT_EQ(a.Ni, b.Ni);
  EXPECT_FLOAT_EQ(a.Tr, b.Tr);
  EXPECT_EQ(a.sharpness, b.sharpness);
  EXPECT_FLOAT_EQ(a.Pr, b.Pr);
  EXPECT_FLOAT_EQ(a.Pm, b.Pm);
  EXPECT_FLOAT_EQ(a.Ps, b.Ps);
  EXPECT_FLOAT_EQ(a.Pc, b.Pc);
  EXPECT_FLOAT_EQ(a.Pcr, b.Pcr);
  EXPECT_FLOAT_EQ(a.aniso, b.aniso);
  EXPECT_FLOAT_EQ(a.anisor, b.anisor);
  EXPECT_EQ(a.map_aat, b.map_aat);
  EXPECT_STREQ(a.map_Ke, b.map_Ke);
  EXPECT_STREQ(a.map_Pr, b.map_Pr);
  EXPECT_STREQ(a.map_Pm, b.map_Pm);
  EXPECT_STREQ(a.map_Ps, b.map_Ps);
  EXPECT_STREQ(a.norm, b.norm);
  EXPECT_STREQ(a.disp, b.disp);
  EXPECT_STREQ(a.decal, b.decal);
  for (size_t i = 0; i < GMDL_MTL_REFL_COUNT; i++) {
    ASSERT_EQ(a.refl[i] == nullptr, b.refl[i] == nullptr) << "refl slot " << i;
    if (a.refl[i]) {
      EXPECT_STREQ(a.refl[i], b.refl[i]) << "refl slot " << i;
    }
  }
  gmdl_mtl_free(first);
  gmdl_mtl_free(second);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
