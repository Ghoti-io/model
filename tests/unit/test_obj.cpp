/**
 * @file
 *
 * Unit tests for the Wavefront OBJ parser.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

// Reached directly, the way tests/unit/test_locale.cpp reaches the numeric
// scope. The line-reading helpers have a contract wider than any caller uses
// - they accept text that has not had its leading blanks removed, because a
// contract that says "provided somebody else trimmed it first" is a
// precondition nothing checks - and only a direct call exercises that.
#include "../../src/obj/obj_internal.h"

#include <cstddef>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>
#include <iterator>

#include <string>

using gmdltest::data;
using gmdltest::FailingSink;
using gmdltest::MemStream;
using gmdltest::TempFile;

namespace {

/** Parse an inline OBJ document, or fail the test. */
GMDL_Obj * load_text(const std::string & text) {
  MemStream stream(text);
  GMDL_Obj * obj = nullptr;
  GMDL_Result r = gmdl_obj_load(stream.get(), nullptr, nullptr, &obj);
  EXPECT_EQ(r, GMDL_OK) << gmdl_result_string(r);
  return obj;
}

/** Parse an inline OBJ document that is expected to fail. */
GMDL_Result load_text_expecting_failure(
    const std::string & text, const GMDL_Limits * limits = nullptr) {
  MemStream stream(text);
  GMDL_Obj * obj = nullptr;
  GMDL_Result r = gmdl_obj_load(stream.get(), limits, nullptr, &obj);
  EXPECT_EQ(obj, nullptr) << "nothing should be handed back on failure";
  gmdl_obj_free(obj);
  return r;
}

} // namespace

//
// The internal line helpers, called directly
//

TEST(LineHelpers, LeadingBlanksAreSkipped) {
  char out[32];
  EXPECT_EQ(gmdl_first_token("   \t name rest", out, sizeof(out)), GMDL_OK);
  EXPECT_STREQ(out, "name");

  EXPECT_EQ(gmdl_rest_of_line("  \t two words  \t", out, sizeof(out)), GMDL_OK);
  EXPECT_STREQ(out, "two words");

  int32_t value = -7;
  EXPECT_EQ(gmdl_parse_int32("  \t 42abc", &value), GMDL_OK);
  EXPECT_EQ(value, 42);
}

// The last line in obj_common.c no caller reaches. Reaching it here is
// cheaper than leaving an uncovered line for the next person to triage, and
// it pins the guard as part of the contract rather than as insurance nobody
// has ever collected on.
TEST(LineHelpers, TheReaderRejectsNullArguments) {
  const char * line = nullptr;
  EXPECT_EQ(gmdl_line_next(nullptr, &line), GMDL_ERR_INVALID);
  GMDL_Line_Reader reader;
  EXPECT_EQ(gmdl_line_next(&reader, nullptr), GMDL_ERR_INVALID);
}

TEST(LineHelpers, NothingThereIsAFormatErrorAndNoFitIsALimit) {
  char out[8];
  EXPECT_EQ(gmdl_first_token("   ", out, sizeof(out)), GMDL_ERR_FORMAT);
  EXPECT_EQ(gmdl_rest_of_line("  \t ", out, sizeof(out)), GMDL_ERR_FORMAT);
  EXPECT_EQ(gmdl_first_token("toolongforthis", out, sizeof(out)),
      GMDL_ERR_LIMIT);
  EXPECT_EQ(gmdl_rest_of_line("toolongforthis", out, sizeof(out)),
      GMDL_ERR_LIMIT);

  int32_t value = -7;
  EXPECT_EQ(gmdl_parse_int32("   ", &value), GMDL_ERR_FORMAT);
  EXPECT_EQ(gmdl_parse_int32("abc", &value), GMDL_ERR_FORMAT);
  EXPECT_EQ(gmdl_parse_int32("99999999999999999999", &value), GMDL_ERR_LIMIT);
  EXPECT_EQ(value, -7) << "a refused value must not have been written";
}

//
// Argument handling
//

TEST(ObjLoad, NullArgumentsRejected) {
  MemStream stream("v 0 0 0\n");
  GMDL_Obj * obj = nullptr;
  EXPECT_EQ(gmdl_obj_load(nullptr, nullptr, nullptr, &obj), GMDL_ERR_INVALID);
  EXPECT_EQ(obj, nullptr);
  EXPECT_EQ(gmdl_obj_load(stream.get(), nullptr, nullptr, nullptr),
      GMDL_ERR_INVALID);
}

TEST(ObjLoad, MissingFileReportsIo) {
  GMDL_Obj * obj = nullptr;
  EXPECT_EQ(
      gmdl_obj_load_file(gmdltest::missing_path(), nullptr, nullptr, &obj),
      GMDL_ERR_IO);
  EXPECT_EQ(obj, nullptr);
}

TEST(ObjLoad, LoadFileRejectsNullArguments) {
  GMDL_Obj * obj = nullptr;
  EXPECT_EQ(gmdl_obj_load_file(nullptr, nullptr, nullptr, &obj),
      GMDL_ERR_INVALID);
  EXPECT_EQ(gmdl_obj_load_file("whatever.obj", nullptr, nullptr, nullptr),
      GMDL_ERR_INVALID);
}

TEST(ObjLoad, LoadFileParsesTheSameAsAStream) {
  TempFile f("v 1 2 3\nv 4 5 6\n");
  ASSERT_TRUE(f.valid());
  GMDL_Obj * obj = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(f.path(), nullptr, nullptr, &obj), GMDL_OK);
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->vertex_count, 2u);
  EXPECT_FLOAT_EQ(obj->vertices[1].z, 6.0f);
  gmdl_obj_free(obj);
}

TEST(ObjFree, NullIsSafe) {
  gmdl_obj_free(nullptr);
}

TEST(ObjLoad, EmptyInputYieldsEmptyModel) {
  GMDL_Obj * obj = load_text("");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->vertex_count, 0u);
  EXPECT_EQ(obj->face_count, 0u);
  gmdl_obj_free(obj);
}

//
// Geometry
//

TEST(ObjParse, VerticesTexcoordsAndNormals) {
  GMDL_Obj * obj = load_text(
      "v 1.0 2.0 3.0\n"
      "v -4.5 0.0 6.25\n"
      "vt 0.25 0.75\n"
      "vn 0.0 1.0 0.0\n");
  ASSERT_NE(obj, nullptr);

  ASSERT_EQ(obj->vertex_count, 2u);
  EXPECT_FLOAT_EQ(obj->vertices[0].x, 1.0f);
  EXPECT_FLOAT_EQ(obj->vertices[0].y, 2.0f);
  EXPECT_FLOAT_EQ(obj->vertices[0].z, 3.0f);
  EXPECT_FLOAT_EQ(obj->vertices[1].x, -4.5f);
  EXPECT_FLOAT_EQ(obj->vertices[1].z, 6.25f);

  ASSERT_EQ(obj->texcoord_count, 1u);
  EXPECT_FLOAT_EQ(obj->texcoords[0].u, 0.25f);
  EXPECT_FLOAT_EQ(obj->texcoords[0].v, 0.75f);

  ASSERT_EQ(obj->normal_count, 1u);
  EXPECT_FLOAT_EQ(obj->normals[0].y, 1.0f);

  gmdl_obj_free(obj);
}

TEST(ObjParse, IncompleteVertexIsAFormatError) {
  EXPECT_EQ(load_text_expecting_failure("v 1 2\n"), GMDL_ERR_FORMAT);
  EXPECT_EQ(load_text_expecting_failure("vn 1 2\n"), GMDL_ERR_FORMAT);
  // "vt" is the exception: only u is required (3.2).
  EXPECT_EQ(load_text_expecting_failure("vt\n"), GMDL_ERR_FORMAT);
}

// `vt u [v] [w]` - v and w are optional and default to 0, which is what the
// specification says and what Blender reads. VTK calls "vt 0.5" an error;
// this follows the more permissive of the two references deliberately, so the
// test says which behaviour is intended rather than which one happens.
TEST(ObjParse, ATextureCoordinateNeedsOnlyItsFirstNumber) {
  GMDL_Obj * obj = load_text("vt 0.5\nvt 0.25 0.75\nvt 1 2 3\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->texcoord_count, 3u);
  EXPECT_FLOAT_EQ(obj->texcoords[0].u, 0.5f);
  EXPECT_FLOAT_EQ(obj->texcoords[0].v, 0.0f);
  EXPECT_FLOAT_EQ(obj->texcoords[1].u, 0.25f);
  EXPECT_FLOAT_EQ(obj->texcoords[1].v, 0.75f);
  // A third number is read and dropped, not a format error.
  EXPECT_FLOAT_EQ(obj->texcoords[2].u, 1.0f);
  EXPECT_FLOAT_EQ(obj->texcoords[2].v, 2.0f);
  gmdl_obj_free(obj);
}

//
// Vertex colours - the `v x y z r g b` extension (3.1)
//

// The three extra numbers used to be discarded without a word, which is the
// same shape of defect as dropping a texture map option: the file said
// something and the model did not carry it.
TEST(ObjParse, AVertexCarriesItsColour) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0 1 0 0\n"
      "v 1 0 0 0 1 0\n"
      "v 0 1 0 0 0 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->vertex_count, 3u);
  ASSERT_NE(obj->colors, nullptr);
  ASSERT_EQ(obj->color_count, 3u);
  EXPECT_TRUE(obj->colors[0].present);
  EXPECT_FLOAT_EQ(obj->colors[0].r, 1.0f);
  EXPECT_FLOAT_EQ(obj->colors[0].g, 0.0f);
  EXPECT_FLOAT_EQ(obj->colors[0].b, 0.0f);
  EXPECT_FLOAT_EQ(obj->colors[1].g, 1.0f);
  EXPECT_FLOAT_EQ(obj->colors[2].b, 1.0f);
  // The position is unaffected by the colour sharing the line.
  EXPECT_FLOAT_EQ(obj->vertices[2].y, 1.0f);
  gmdl_obj_free(obj);
}

// A file that never mentions a colour must not pay for the array. This is
// also the check that "has colours" is answerable at all: without it the
// caller would have to scan every entry's `present` flag to find out.
TEST(ObjParse, AFileWithNoColoursAllocatesNone) {
  GMDL_Obj * obj = load_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->colors, nullptr);
  EXPECT_EQ(obj->color_count, 0u);
  gmdl_obj_free(obj);
}

// Four numbers is `w`, not the first third of a colour; five is a vertex with
// junk after it. Measured against Blender, which colours at six and not
// before. Sampling only three and six would have left the boundary untested,
// and the boundary is the whole rule.
TEST(ObjParse, ColourNeedsAllSixNumbers) {
  for (const char * line : {"v 1 2 3\n", "v 1 2 3 4\n", "v 1 2 3 4 5\n"}) {
    GMDL_Obj * obj = load_text(line);
    ASSERT_NE(obj, nullptr) << line;
    EXPECT_EQ(obj->colors, nullptr) << line;
    EXPECT_EQ(obj->vertex_count, 1u) << line;
    EXPECT_FLOAT_EQ(obj->vertices[0].x, 1.0f) << line;
    gmdl_obj_free(obj);
  }
  GMDL_Obj * six = load_text("v 1 2 3 4 5 6\n");
  ASSERT_NE(six, nullptr);
  ASSERT_NE(six->colors, nullptr);
  EXPECT_TRUE(six->colors[0].present);
  EXPECT_FLOAT_EQ(six->colors[0].r, 4.0f);
  gmdl_obj_free(six);
}

// Seven numbers might have been meant as `x y z w r g b`, but Blender reads
// the colour from fields four to six regardless and so does this. Recorded
// because it is a guess either way and the next person should see which guess
// was made and on what evidence.
TEST(ObjParse, ASeventhNumberDoesNotMoveTheColour) {
  GMDL_Obj * obj = load_text("v 1 2 3 4 5 6 7\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_NE(obj->colors, nullptr);
  EXPECT_FLOAT_EQ(obj->colors[0].r, 4.0f);
  EXPECT_FLOAT_EQ(obj->colors[0].g, 5.0f);
  EXPECT_FLOAT_EQ(obj->colors[0].b, 6.0f);
  gmdl_obj_free(obj);
}

// Colours are recorded as written. Blender throws away every colour in a file
// containing a negative component and converts anything above 1 out of sRGB;
// both are decisions for whoever renders this, and a parser that made them
// would leave no way back to what the file said.
TEST(ObjParse, AColourOutsideZeroToOneIsKeptAsWritten) {
  GMDL_Obj * obj = load_text("v 0 0 0 -0.5 255 1.5\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_NE(obj->colors, nullptr);
  EXPECT_FLOAT_EQ(obj->colors[0].r, -0.5f);
  EXPECT_FLOAT_EQ(obj->colors[0].g, 255.0f);
  EXPECT_FLOAT_EQ(obj->colors[0].b, 1.5f);
  gmdl_obj_free(obj);
}

// A file may colour some vertices and not others. The array is indexed by
// vertex number, so the uncoloured ones need an entry too - and the entry has
// to say it is not a colour, or "white" and "no colour" become the same
// thing. Blender's answer is to discard every colour in such a file; this
// keeps them, because discarding is something the caller can still do.
TEST(ObjParse, ColoursMayBeMissingFromSomeVertices) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\n"      // before the first colour: backfilled
      "v 1 0 0 1 0 0\n"
      "v 2 0 0\n"      // after one: appended as absent
      "v 3 0 0 0 0 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->vertex_count, 4u);
  ASSERT_NE(obj->colors, nullptr);
  ASSERT_EQ(obj->color_count, 4u);
  EXPECT_FALSE(obj->colors[0].present);
  EXPECT_TRUE(obj->colors[1].present);
  EXPECT_FALSE(obj->colors[2].present);
  EXPECT_TRUE(obj->colors[3].present);
  // An absent entry holds white, so a consumer that ignores `present` and
  // multiplies straight through still gets the unmodified vertex.
  EXPECT_FLOAT_EQ(obj->colors[0].r, 1.0f);
  EXPECT_FLOAT_EQ(obj->colors[0].g, 1.0f);
  EXPECT_FLOAT_EQ(obj->colors[0].b, 1.0f);
  gmdl_obj_free(obj);
}

// The backfill has to reach an arbitrary distance back, not just one vertex.
// A single-vertex prefix would pass against a loop that ran once.
TEST(ObjParse, TheBackfillReachesEveryEarlierVertex) {
  std::string text;
  for (int i = 0; i < 300; i++) {
    text += "v " + std::to_string(i) + " 0 0\n";
  }
  text += "v 999 0 0 0.25 0.5 0.75\n";
  GMDL_Obj * obj = load_text(text);
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->vertex_count, 301u);
  ASSERT_EQ(obj->color_count, 301u);
  for (size_t i = 0; i < 300; i++) {
    ASSERT_FALSE(obj->colors[i].present) << "vertex " << i;
  }
  EXPECT_TRUE(obj->colors[300].present);
  EXPECT_FLOAT_EQ(obj->colors[300].g, 0.5f);
  gmdl_obj_free(obj);
}

// Six tokens where three of them are not numbers is three numbers, so no
// colour - the count is of numbers, not of tokens.
TEST(ObjParse, WordsWhereAColourWouldBeAreNotAColour) {
  GMDL_Obj * obj = load_text("v 1 2 3 red green blue\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->colors, nullptr);
  EXPECT_FLOAT_EQ(obj->vertices[0].z, 3.0f);
  gmdl_obj_free(obj);
}

// OBJ indices are 1-based in the file and stored 0-based.
TEST(ObjParse, FaceIndicesAreZeroBased) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "f 1 2 3\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  EXPECT_EQ(obj->faces[0].count, 3u);
  EXPECT_EQ(obj->faces[0].vertex[0], 0);
  EXPECT_EQ(obj->faces[0].vertex[1], 1);
  EXPECT_EQ(obj->faces[0].vertex[2], 2);
  // No texture or normal given: both absent.
  EXPECT_EQ(obj->faces[0].texcoord[0], -1);
  EXPECT_EQ(obj->faces[0].normal[0], -1);
  gmdl_obj_free(obj);
}

TEST(ObjParse, FaceWithVertexTexcoordNormal) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "vt 0 0\nvt 1 0\nvt 0 1\n"
      "vn 0 0 1\n"
      "f 1/1/1 2/2/1 3/3/1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  const GMDL_Obj_Face & f = obj->faces[0];
  ASSERT_EQ(f.count, 3u);
  EXPECT_EQ(f.vertex[1], 1);
  EXPECT_EQ(f.texcoord[1], 1);
  EXPECT_EQ(f.normal[1], 0);
  gmdl_obj_free(obj);
}

TEST(ObjParse, FaceWithVertexAndTexcoordOnly) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "vt 0 0\nvt 1 0\nvt 0 1\n"
      "f 1/1 2/2 3/3\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  const GMDL_Obj_Face & f = obj->faces[0];
  ASSERT_EQ(f.count, 3u);
  EXPECT_EQ(f.vertex[2], 2);
  EXPECT_EQ(f.texcoord[2], 2);
  EXPECT_EQ(f.normal[2], -1);
  gmdl_obj_free(obj);
}

// "v//vn" omits the texture coordinate. The normal still has to survive.
TEST(ObjParse, FaceWithVertexAndNormalOnly) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "vn 0 0 1\nvn 0 1 0\n"
      "f 1//2 2//2 3//2\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  const GMDL_Obj_Face & f = obj->faces[0];
  ASSERT_EQ(f.count, 3u);
  EXPECT_EQ(f.vertex[0], 0);
  EXPECT_EQ(f.texcoord[0], -1) << "no texture coordinate was given";
  EXPECT_EQ(f.normal[0], 1) << "vn index 2 should map to normal 1";
  gmdl_obj_free(obj);
}

// A negative OBJ index is relative: -1 is the most recently declared element.
// The specification measures that from the current position in the file, so
// the same "-1" means different vertices at different points in the document.
TEST(ObjParse, NegativeIndicesAreRelativeToWhatCameBefore) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "f -3 -2 -1\n"
      "v 2 0 0\n"
      "f -3 -2 -1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 2u);

  // The first face sees three vertices, so -1 is vertex 2.
  EXPECT_EQ(obj->faces[0].vertex[0], 0);
  EXPECT_EQ(obj->faces[0].vertex[1], 1);
  EXPECT_EQ(obj->faces[0].vertex[2], 2);

  // The second sees four, so the same text means one vertex later.
  EXPECT_EQ(obj->faces[1].vertex[0], 1);
  EXPECT_EQ(obj->faces[1].vertex[1], 2);
  EXPECT_EQ(obj->faces[1].vertex[2], 3);

  gmdl_obj_free(obj);
}

TEST(ObjParse, NegativeTexcoordAndNormalIndicesAreRelativeToo) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "vt 0 0\nvt 1 0\n"
      "vn 0 0 1\nvn 0 1 0\n"
      "f -3/-2/-2 -2/-1/-1 -1/-1/-1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  const GMDL_Obj_Face & f = obj->faces[0];
  EXPECT_EQ(f.texcoord[0], 0) << "-2 of two texture coordinates is the first";
  EXPECT_EQ(f.texcoord[1], 1);
  EXPECT_EQ(f.normal[0], 0) << "-2 of two normals is the first";
  EXPECT_EQ(f.normal[2], 1);
  gmdl_obj_free(obj);
}

TEST(ObjParse, RelativeIndicesSurviveADumpAndReload) {
  // The dumper writes absolute indices, because the parser resolved them.
  GMDL_Obj * first = load_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n");
  ASSERT_NE(first, nullptr);

  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_obj_dump(first, sink), GMDL_OK);
  fclose(sink);

  GMDL_Obj * second = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(out.path(), nullptr, nullptr, &second), GMDL_OK);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->face_count, 1u);
  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(second->faces[i == 0 ? 0 : 0].vertex[i], first->faces[0].vertex[i])
        << "index " << i;
  }
  gmdl_obj_free(first);
  gmdl_obj_free(second);
}

TEST(ObjParse, QuadFaceKeepsFourVertices) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
      "f 1 2 3 4\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  ASSERT_EQ(obj->faces[0].count, 4u);
  EXPECT_EQ(obj->faces[0].vertex[3], 3);
  EXPECT_EQ(obj->faces[0].overflow, nullptr);
  gmdl_obj_free(obj);
}

// Faces beyond four vertices spill into the overflow array.
TEST(ObjParse, NgonFaceUsesOverflow) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 2 0 0\nv 3 0 0\nv 4 0 0\nv 5 0 0\nv 6 0 0\n"
      "f 1 2 3 4 5 6 7\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  const GMDL_Obj_Face & f = obj->faces[0];
  ASSERT_EQ(f.count, 7u);
  ASSERT_NE(f.overflow, nullptr);
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(f.vertex[i], i);
  }
  EXPECT_EQ(f.overflow[0].vertex, 4);
  EXPECT_EQ(f.overflow[1].vertex, 5);
  EXPECT_EQ(f.overflow[2].vertex, 6);
  gmdl_obj_free(obj);
}

// Growing past the initial overflow allocation.
TEST(ObjParse, LargeNgonGrowsOverflow) {
  std::string text;
  for (int i = 0; i < 12; i++) {
    text += "v " + std::to_string(i) + " 0 0\n";
  }
  text += "f";
  for (int i = 1; i <= 12; i++) {
    text += " " + std::to_string(i);
  }
  text += "\n";

  GMDL_Obj * obj = load_text(text);
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  const GMDL_Obj_Face & f = obj->faces[0];
  ASSERT_EQ(f.count, 12u);
  ASSERT_NE(f.overflow, nullptr);
  for (int i = 0; i < 8; i++) {
    EXPECT_EQ(f.overflow[i].vertex, i + 4) << "overflow entry " << i;
  }
  gmdl_obj_free(obj);
}

TEST(ObjParse, TabsSeparateFaceTokens) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "f\t1\t2\t3\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  EXPECT_EQ(obj->faces[0].count, 3u);
  EXPECT_EQ(obj->faces[0].vertex[2], 2);
  gmdl_obj_free(obj);
}

//
// Growth past the preallocated capacities
//

TEST(ObjParse, GrowsBeyondInitialCapacity) {
  // The parser preallocates 128 vertices and faces; push well past that.
  const int kCount = 500;
  std::string text;
  for (int i = 0; i < kCount; i++) {
    text += "v " + std::to_string(i) + " 0 0\n";
  }
  for (int i = 1; i + 2 <= kCount; i += 3) {
    text += "f " + std::to_string(i) + " " + std::to_string(i + 1) + " " +
        std::to_string(i + 2) + "\n";
  }

  GMDL_Obj * obj = load_text(text);
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->vertex_count, (size_t)kCount);
  EXPECT_FLOAT_EQ(obj->vertices[kCount - 1].x, (float)(kCount - 1));
  EXPECT_GT(obj->face_count, 128u);
  gmdl_obj_free(obj);
}

//
// Groups, materials and comments
//

TEST(ObjParse, CommentsAndBlankLinesIgnored) {
  GMDL_Obj * obj = load_text(
      "# a comment\n"
      "\n"
      "v 1 2 3\n"
      "   \n"
      "# another\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->vertex_count, 1u);
  EXPECT_EQ(obj->face_count, 0u);
  gmdl_obj_free(obj);
}

TEST(ObjParse, UnknownDirectivesAreIgnored) {
  // The specification asks readers to skip what they do not understand.
  GMDL_Obj * obj = load_text(
      "s off\n"
      "vp 0 0 0\n"
      "cstype bezier\n"
      "v 1 2 3\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->vertex_count, 1u);
  gmdl_obj_free(obj);
}

// A directive has to end at whitespace. "vertex 1 2 3" is not a "v" line, and
// "usemtlish red" is not a "usemtl" line.
TEST(ObjParse, DirectivesMustEndAtWhitespace) {
  GMDL_Obj * obj = load_text(
      "vertex 1 2 3\n"
      "usemtlish red\n"
      "mtllibrary nope.mtl\n"
      "v 4 5 6\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->vertex_count, 1u) << "only the real 'v' line counts";
  EXPECT_FLOAT_EQ(obj->vertices[0].x, 4.0f);
  EXPECT_EQ(obj->material_mapping_count, 0u);
  EXPECT_STREQ(obj->mtllib, "");
  gmdl_obj_free(obj);
}

TEST(ObjParse, MtllibRecorded) {
  GMDL_Obj * obj = load_text("mtllib materials.mtl\nv 0 0 0\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_STREQ(obj->mtllib, "materials.mtl");
  ASSERT_EQ(obj->mtllib_count, 1u);
  EXPECT_STREQ(obj->mtllibs[0].path, "materials.mtl");
  gmdl_obj_free(obj);
}

// A document may name several libraries and both references load every one.
// This used to keep the last line only, so a file asking for two got one and
// nothing said so.
TEST(ObjParse, EveryMtllibLineIsKeptInOrder) {
  GMDL_Obj * obj = load_text(
      "mtllib first.mtl\nmtllib second one.mtl\nmtllib third.mtl\nv 0 0 0\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->mtllib_count, 3u);
  EXPECT_STREQ(obj->mtllibs[0].path, "first.mtl");
  EXPECT_STREQ(obj->mtllibs[1].path, "second one.mtl");
  EXPECT_STREQ(obj->mtllibs[2].path, "third.mtl");
  // The compatibility field is the first, not the last.
  EXPECT_STREQ(obj->mtllib, "first.mtl");
  gmdl_obj_free(obj);
}

// A bare `mtllib` names no library (3.8). It must not append an empty entry,
// or the dump writes back a `mtllib ` line the document never had - and it
// must not clear the libraries already named, which is what it used to do.
//
// Clearing made the compatibility field disagree with the list it is
// documented to be the first entry of, and the disagreement was not
// academic: the dump writes the list, so a document with this shape reloaded
// carrying a path the original had blanked. The fuzzer found it against the
// corpus. Measured 2026-09-23: Blender 4.3.2 reads a bare `mtllib` as an
// unrecognized element and still applies a material from a library an
// earlier line named, so not clearing is also the reference's reading.
TEST(ObjParse, BareMtllibAddsNoEntryAndClearsNothing) {
  GMDL_Obj * obj = load_text("mtllib a.mtl\nmtllib\nv 0 0 0\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->mtllib_count, 1u);
  EXPECT_STREQ(obj->mtllibs[0].path, "a.mtl");
  EXPECT_STREQ(obj->mtllib, "a.mtl")
      << "the compatibility field is mtllibs[0].path, always";
  gmdl_obj_free(obj);
}

// A bare `mtllib` with no real one before it still names nothing.
TEST(ObjParse, BareMtllibAloneLeavesNoLibrary) {
  GMDL_Obj * obj = load_text("mtllib\nv 0 0 0\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->mtllib_count, 0u);
  EXPECT_STREQ(obj->mtllib, "");
  gmdl_obj_free(obj);
}

// The round trip the fuzzer broke, as a unit test so it stays broken-proof
// without a fuzz run.
TEST(ObjDump, ABareMtllibDoesNotChangeWhatReloads) {
  GMDL_Obj * obj = load_text("mtllib a.mtl\nmtllib\nv 0 0 0\n");
  ASSERT_NE(obj, nullptr);

  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_obj_dump(obj, sink), GMDL_OK);
  fclose(sink);

  GMDL_Obj * again = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(out.path(), nullptr, nullptr, &again), GMDL_OK);
  ASSERT_NE(again, nullptr);
  EXPECT_EQ(again->mtllib_count, obj->mtllib_count);
  EXPECT_STREQ(again->mtllib, obj->mtllib);
  gmdl_obj_free(again);
  gmdl_obj_free(obj);
}

// The round trip is the half that the loader alone cannot show: keeping
// three paths is no use if the dump writes one.
TEST(ObjDump, EveryMtllibLineIsWrittenBack) {
  const char * text =
      "mtllib first.mtl\nmtllib second one.mtl\nmtllib third.mtl\nv 0 0 0\n";
  GMDL_Obj * obj = load_text(text);
  ASSERT_NE(obj, nullptr);

  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_obj_dump(obj, sink), GMDL_OK);
  fclose(sink);

  // Reparsing the dump gives the same three, in the same order - which is
  // the check that the dump wrote all of them and in order.
  GMDL_Obj * again = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(out.path(), nullptr, nullptr, &again), GMDL_OK);
  ASSERT_NE(again, nullptr);
  ASSERT_EQ(again->mtllib_count, 3u);
  EXPECT_STREQ(again->mtllibs[0].path, "first.mtl");
  EXPECT_STREQ(again->mtllibs[1].path, "second one.mtl");
  EXPECT_STREQ(again->mtllibs[2].path, "third.mtl");
  gmdl_obj_free(again);
  gmdl_obj_free(obj);
}

// Blender 4.3.2 exports `mtllib my model.mtl` for a document saved under that
// name - no unusual settings, no hand editing - and the first-token reading
// of that line named the file "my". Measured against the exporter, not
// reasoned about: this is what the reference actually writes by default.
TEST(ObjParse, AnMtllibPathKeepsItsSpaces) {
  GMDL_Obj * obj = load_text("mtllib my model.mtl\nv 0 0 0\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_STREQ(obj->mtllib, "my model.mtl");
  gmdl_obj_free(obj);
}

// Whitespace the writer left after the path is not part of it. A comment is
// cut before the directive is matched, so the same applies to `mtllib a.mtl
// # note`, which is why that case is here too.
TEST(ObjParse, TrailingBlanksAreNotPartOfThePath) {
  GMDL_Obj * obj = load_text("mtllib a.mtl  \t\nv 0 0 0\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_STREQ(obj->mtllib, "a.mtl");
  gmdl_obj_free(obj);

  GMDL_Obj * commented = load_text("mtllib a.mtl # the materials\nv 0 0 0\n");
  ASSERT_NE(commented, nullptr);
  EXPECT_STREQ(commented->mtllib, "a.mtl");
  gmdl_obj_free(commented);
}

// A material name may contain spaces, and both halves of the pair have to
// agree about that or an OBJ stops finding its own materials. Blender reads
// `usemtl two words` as one name; it writes underscores instead, so a file
// like this comes from some other exporter and used to arrive here as "two".
TEST(ObjParse, AMaterialNameKeepsItsSpaces) {
  GMDL_Obj * obj = load_text(
      "usemtl two words\n"
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "f 1 2 3\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->material_mapping_count, 1u);
  EXPECT_STREQ(obj->material_mappings[0].name, "two words");
  gmdl_obj_free(obj);
}

// `g` was left stopping at the first blank on the strength of the
// specification describing `g a b` as two group names. Measured afterwards:
// neither reference implements that - Blender reads the line as one group
// called "alpha beta" - and taking the first token is wrong under *both*
// readings, renaming the group under one and discarding a name under the
// other. The whole-line reading keeps every byte, so it is the one that does
// not foreclose the open question.
TEST(ObjParse, AGroupNameKeepsItsSpaces) {
  GMDL_Obj * obj = load_text(
      "g alpha beta\n"
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "f 1 2 3\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->group_count, 1u);
  EXPECT_STREQ(obj->groups[0].name, "alpha beta");
  gmdl_obj_free(obj);
}

// `o` and `g` behave identically here and do not mean the same thing to the
// tools that write them: Blender makes an object of one and a vertex group
// of the other. Flattening them lost that, and nothing could put it back.
TEST(ObjParse, ObjectAndGroupAreToldApart) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 2 0 0\n"
      "o thing\nf 1 2 3\n"
      "g part\nf 1 2 4\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->group_count, 2u);
  EXPECT_TRUE(obj->groups[0].is_object);
  EXPECT_STREQ(obj->groups[0].name, "thing");
  EXPECT_FALSE(obj->groups[1].is_object);
  EXPECT_STREQ(obj->groups[1].name, "part");
  gmdl_obj_free(obj);
}

// A `g` with no name is the default group, and it is a group rather than an
// object - the flag has to come from the directive, not from whatever the
// name turned out to be.
TEST(ObjParse, ABareDirectiveKeepsItsOwnSpelling) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "o\nf 1 2 3\n"
      "g\nf 1 2 3\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->group_count, 2u);
  EXPECT_STREQ(obj->groups[0].name, "default");
  EXPECT_TRUE(obj->groups[0].is_object);
  EXPECT_STREQ(obj->groups[1].name, "default");
  EXPECT_FALSE(obj->groups[1].is_object);
  gmdl_obj_free(obj);
}

// Reading the distinction is only half of it: writing `g` for an `o` would
// move the loss one file along.
TEST(ObjDump, ObjectsAreWrittenAsObjects) {
  GMDL_Obj * first = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 2 0 0\n"
      "o two words\nf 1 2 3\n"
      "g part\nf 1 2 4\n");
  ASSERT_NE(first, nullptr);
  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_obj_dump(first, sink), GMDL_OK);
  fclose(sink);

  std::ifstream in(out.path());
  std::string text((std::istreambuf_iterator<char>(in)),
      std::istreambuf_iterator<char>());
  EXPECT_NE(text.find("o two words\n"), std::string::npos) << text;
  EXPECT_NE(text.find("g part\n"), std::string::npos) << text;

  GMDL_Obj * second = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(out.path(), nullptr, nullptr, &second), GMDL_OK);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->group_count, 2u);
  EXPECT_TRUE(second->groups[0].is_object);
  EXPECT_STREQ(second->groups[0].name, "two words");
  EXPECT_FALSE(second->groups[1].is_object);
  gmdl_obj_free(second);
  gmdl_obj_free(first);
}

// `sscanf("%d")` is undefined behaviour when the value does not fit, and
// glibc resolved it by wrapping: `s 2147483648` arrived as -2147483648 and
// `s 99999999999999999999` as -1. A wrapped group number is worse than a
// refused one, because it is a *valid* group number that two different files
// now share - the same shape as the face index that wrapped into range.
TEST(ObjParse, ASmoothingGroupTooLargeForItsFieldIsRefused) {
  EXPECT_EQ(load_text_expecting_failure("s 2147483648\n"), GMDL_ERR_LIMIT);
  EXPECT_EQ(
      load_text_expecting_failure("s 99999999999999999999\n"), GMDL_ERR_LIMIT);
  EXPECT_EQ(load_text_expecting_failure("s -2147483649\n"), GMDL_ERR_LIMIT);
  // No number at all stays a format error: a different question, a different
  // answer, and the helper has to keep them apart.
  EXPECT_EQ(load_text_expecting_failure("s\n"), GMDL_ERR_FORMAT);
  EXPECT_EQ(load_text_expecting_failure("s abc\n"), GMDL_ERR_FORMAT);
}

// Both extremes fit and are kept, and trailing text is still ignored the way
// sscanf ignored it - the point of the change was the overflow, not a new
// strictness about what may follow a number.
TEST(ObjParse, ASmoothingGroupAtTheLimitIsKept) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "s 2147483647\nf 1 2 3\n"
      "s -2147483648\nf 1 2 3\n"
      "s 4abc\nf 1 2 3\n"
      "s off\nf 1 2 3\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 4u);
  EXPECT_EQ(obj->faces[0].smoothing_group, 2147483647);
  EXPECT_EQ(obj->faces[1].smoothing_group, -2147483648);
  EXPECT_EQ(obj->faces[2].smoothing_group, 4);
  EXPECT_EQ(obj->faces[3].smoothing_group, 0);
  gmdl_obj_free(obj);
}

TEST(ObjParse, GroupsRecorded) {
  GMDL_Obj * obj = load_text(
      "g first\n"
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "f 1 2 3\n"
      "g second\n"
      "f 1 2 3\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->group_count, 2u);
  EXPECT_STREQ(obj->groups[0].name, "first");
  EXPECT_STREQ(obj->groups[1].name, "second");
  EXPECT_EQ(obj->groups[0].start_face, 0u);
  EXPECT_EQ(obj->groups[0].face_count, 1u);
  EXPECT_EQ(obj->groups[1].start_face, 1u);
  EXPECT_EQ(obj->groups[1].face_count, 1u);
  gmdl_obj_free(obj);
}

// A "g" with no name is the default group, which the specification allows and
// the violin_case fixture actually contains on its sixth line.
TEST(ObjParse, BareGroupLineNamesTheDefaultGroup) {
  GMDL_Obj * obj = load_text("g\nv 0 0 0\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->group_count, 1u);
  EXPECT_STREQ(obj->groups[0].name, "default");
  gmdl_obj_free(obj);
}

TEST(ObjParse, ObjectLinesActLikeGroups) {
  GMDL_Obj * obj = load_text("o thing\nv 0 0 0\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->group_count, 1u);
  EXPECT_STREQ(obj->groups[0].name, "thing");
  gmdl_obj_free(obj);
}

TEST(ObjParse, UsemtlAssignsMaterialToFollowingFaces) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "f 1 2 3\n"
      "usemtl red\n"
      "f 1 2 3\n"
      "usemtl blue\n"
      "f 1 2 3\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 3u);
  EXPECT_EQ(obj->faces[0].material_index, -1) << "before any usemtl";
  EXPECT_NE(obj->faces[1].material_index, obj->faces[0].material_index);
  EXPECT_NE(obj->faces[2].material_index, obj->faces[1].material_index);
  EXPECT_EQ(obj->material_mapping_count, 2u);
  gmdl_obj_free(obj);
}

TEST(ObjParse, RepeatedUsemtlReusesTheSameMapping) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "usemtl red\nf 1 2 3\n"
      "usemtl blue\nf 1 2 3\n"
      "usemtl red\nf 1 2 3\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 3u);
  EXPECT_EQ(obj->material_mapping_count, 2u) << "red is mapped once";
  EXPECT_EQ(obj->faces[0].material_index, obj->faces[2].material_index);
  gmdl_obj_free(obj);
}

//
// Limits
//

TEST(ObjLimits, LineLongerThanTheCapIsRejected) {
  GMDL_Limits limits;
  gmdl_limits_default(&limits);
  limits.max_line_length = 8;
  EXPECT_EQ(load_text_expecting_failure(
                "v 1.000000 2.000000 3.000000\n", &limits),
      GMDL_ERR_LIMIT);
}

TEST(ObjLimits, VertexCapIsEnforced) {
  GMDL_Limits limits;
  gmdl_limits_default(&limits);
  limits.max_vertices = 2;
  EXPECT_EQ(
      load_text_expecting_failure("v 0 0 0\nv 1 1 1\nv 2 2 2\n", &limits),
      GMDL_ERR_LIMIT);
}

TEST(ObjLimits, FaceCapIsEnforced) {
  GMDL_Limits limits;
  gmdl_limits_default(&limits);
  limits.max_faces = 1;
  EXPECT_EQ(load_text_expecting_failure(
                "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\nf 1 2 3\n", &limits),
      GMDL_ERR_LIMIT);
}

TEST(ObjLimits, FaceIndexCapIsEnforced) {
  GMDL_Limits limits;
  gmdl_limits_default(&limits);
  limits.max_face_indices = 4;
  EXPECT_EQ(load_text_expecting_failure("f 1 2 3 4 5\n", &limits),
      GMDL_ERR_LIMIT);
}

TEST(ObjLimits, GroupAndMaterialCapsAreEnforced) {
  GMDL_Limits limits;
  gmdl_limits_default(&limits);
  limits.max_groups = 1;
  EXPECT_EQ(load_text_expecting_failure("g a\ng b\n", &limits),
      GMDL_ERR_LIMIT);

  gmdl_limits_default(&limits);
  limits.max_materials = 1;
  EXPECT_EQ(load_text_expecting_failure("usemtl a\nusemtl b\n", &limits),
      GMDL_ERR_LIMIT);
}

//
// Checked-in fixtures
//

TEST(ObjFixture, LoadsViolinCase) {
  GMDL_Obj * obj = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(
                data("models/violin_case/violin_case.obj").c_str(), nullptr,
                nullptr, &obj),
      GMDL_OK);
  ASSERT_NE(obj, nullptr);
  EXPECT_GT(obj->vertex_count, 0u);
  EXPECT_EQ(obj->face_count, 944u) << "the fixture has 944 'f' lines";
  EXPECT_STREQ(obj->mtllib, "./vp.mtl") << "as written in the fixture";

  // Every index the parser produced must be in range; an off-by-one here
  // would read out of bounds downstream.
  for (size_t i = 0; i < obj->face_count; i++) {
    const GMDL_Obj_Face & f = obj->faces[i];
    ASSERT_GE(f.count, 3u) << "face " << i;
    size_t inline_count = f.count < 4 ? f.count : 4;
    for (size_t j = 0; j < inline_count; j++) {
      EXPECT_GE(f.vertex[j], 0) << "face " << i << " vertex " << j;
      EXPECT_LT((size_t)f.vertex[j], obj->vertex_count) << "face " << i;
    }
  }
  gmdl_obj_free(obj);
}

TEST(ObjFixture, LoadsStanfordBunny) {
  GMDL_Obj * obj = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(
                data("models/stanford-bunny/stanford-bunny.obj").c_str(),
                nullptr, nullptr, &obj),
      GMDL_OK);
  ASSERT_NE(obj, nullptr);
  EXPECT_GT(obj->vertex_count, 30000u);
  EXPECT_GT(obj->face_count, 60000u);
  for (size_t i = 0; i < obj->face_count; i++) {
    const GMDL_Obj_Face & f = obj->faces[i];
    for (size_t j = 0; j < (f.count < 4 ? f.count : 4); j++) {
      ASSERT_GE(f.vertex[j], 0) << "face " << i;
      ASSERT_LT((size_t)f.vertex[j], obj->vertex_count) << "face " << i;
    }
  }
  gmdl_obj_free(obj);
}

//
// Dump
//

TEST(ObjDump, RejectsNullArguments) {
  GMDL_Obj * obj = load_text("v 0 0 0\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(gmdl_obj_dump(nullptr, stdout), GMDL_ERR_INVALID);
  EXPECT_EQ(gmdl_obj_dump(obj, nullptr), GMDL_ERR_INVALID);
  gmdl_obj_free(obj);
}

TEST(ObjDump, WritesSomethingForALoadedModel) {
  GMDL_Obj * obj = load_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  ASSERT_NE(obj, nullptr);
  FILE * sink = fopen("/dev/null", "w");
  ASSERT_NE(sink, nullptr);
  EXPECT_EQ(gmdl_obj_dump(obj, sink), GMDL_OK);
  fclose(sink);
  gmdl_obj_free(obj);
}

// What the dumper writes has to parse back to the same geometry.
TEST(ObjDump, RoundTripsThroughTheParser) {
  const std::string source =
      "mtllib m.mtl\n"
      "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\nv 2 2 2\nv 3 3 3\n"
      "vt 0 0\nvt 1 0\nvt 0 1\n"
      "vn 0 0 1\n"
      "g only\n"
      "usemtl red\n"
      "f 1/1/1 2/2/1 3/3/1\n"
      "f 1 2 3 4 5 6\n";
  GMDL_Obj * first = load_text(source);
  ASSERT_NE(first, nullptr);

  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_obj_dump(first, sink), GMDL_OK);
  fclose(sink);

  GMDL_Obj * second = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(out.path(), nullptr, nullptr, &second), GMDL_OK);
  ASSERT_NE(second, nullptr);

  EXPECT_EQ(second->vertex_count, first->vertex_count);
  EXPECT_EQ(second->texcoord_count, first->texcoord_count);
  EXPECT_EQ(second->normal_count, first->normal_count);
  ASSERT_EQ(second->face_count, first->face_count);
  EXPECT_STREQ(second->mtllib, first->mtllib);
  for (size_t i = 0; i < first->face_count; i++) {
    EXPECT_EQ(second->faces[i].count, first->faces[i].count) << "face " << i;
    for (size_t j = 0; j < (first->faces[i].count < 4 ? first->faces[i].count : 4);
         j++) {
      EXPECT_EQ(second->faces[i].vertex[j], first->faces[i].vertex[j])
          << "face " << i << " vertex " << j;
      EXPECT_EQ(second->faces[i].texcoord[j], first->faces[i].texcoord[j]);
      EXPECT_EQ(second->faces[i].normal[j], first->faces[i].normal[j]);
    }
  }

  gmdl_obj_free(first);
  gmdl_obj_free(second);
}

//
// Logical lines: the four rules in section 2 that separate a physical line
// from one the parser may dispatch on.
//

TEST(ObjLine, ByteOrderMarkDoesNotSwallowTheFirstLine) {
  // A file saved by a Windows editor begins with EF BB BF. Before the reader
  // skipped it, the first directive did not match at byte 0 and the whole
  // line vanished silently.
  GMDL_Obj * obj = load_text("\xEF\xBB\xBFv 1 2 3\nv 4 5 6\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->vertex_count, 2u);
  EXPECT_FLOAT_EQ(obj->vertices[0].x, 1.0f);
  gmdl_obj_free(obj);
}

TEST(ObjLine, ByteOrderMarkOnlyCountsAtTheStart) {
  // Those three bytes in the middle of a file are ordinary content, and a
  // line beginning with them is just an unrecognised directive.
  GMDL_Obj * obj = load_text("v 1 2 3\n\xEF\xBB\xBFv 4 5 6\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->vertex_count, 1u);
  gmdl_obj_free(obj);
}

TEST(ObjLine, TrailingCommentIsNotFaceData) {
  // "f 1 2 3 # c" used to yield a five-vertex face, the last two of them -1,
  // because the comment was never cut off.
  GMDL_Obj * obj = load_text("v 1 2 3\nv 1 2 3\nv 1 2 3\nf 1 2 3 # three\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  EXPECT_EQ(obj->faces[0].count, 3u);
  gmdl_obj_free(obj);
}

TEST(ObjLine, CommentNeedsNoLeadingSpace) {
  GMDL_Obj * obj = load_text("v 1 2 3\nv 1 2 3\nv 1 2 3\nf 1 2 3# three\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  EXPECT_EQ(obj->faces[0].count, 3u);
  gmdl_obj_free(obj);
}

TEST(ObjLine, LeadingWhitespaceDoesNotHideADirective) {
  GMDL_Obj * obj = load_text("  v 1 2 3\n\tv 4 5 6\n \t f 1 2 1\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->vertex_count, 2u);
  EXPECT_EQ(obj->face_count, 1u);
  gmdl_obj_free(obj);
}

TEST(ObjLine, BackslashJoinsTheNextLine) {
  // The fourth vertex has to come from the continued line. Checking only the
  // count cannot tell the two behaviours apart: the unjoined parse also
  // yields four, with -1 in the last slot because the backslash was a token.
  GMDL_Obj * obj = load_text("v 1 2 3\nv 1 2 3\nv 1 2 3\nf 1 2 3 \\\n 2\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  ASSERT_EQ(obj->faces[0].count, 4u);
  EXPECT_EQ(obj->faces[0].vertex[3], 1);
  gmdl_obj_free(obj);
}

TEST(ObjLine, ContinuationAllowsTrailingBlanksAfterTheBackslash) {
  GMDL_Obj * obj = load_text("v 1 2 3\nv 1 2 3\nv 1 2 3\nf 1 2 3 \\  \n 2\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  ASSERT_EQ(obj->faces[0].count, 4u);
  EXPECT_EQ(obj->faces[0].vertex[3], 1);
  gmdl_obj_free(obj);
}

TEST(ObjLine, BackslashInsideACommentDoesNotContinue) {
  // The comment is removed first, so the backslash is never the last
  // non-blank character of anything.
  GMDL_Obj * obj = load_text("v 1 2 3 # keep \\\nv 4 5 6\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->vertex_count, 2u);
  gmdl_obj_free(obj);
}

TEST(ObjLine, ContinuationAtEndOfStreamEndsTheLine) {
  GMDL_Obj * obj = load_text("v 1 2 3\\\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->vertex_count, 1u);
  gmdl_obj_free(obj);
}

TEST(ObjLine, TheCapAppliesToTheJoinedLine) {
  // Section 2.6: max_line_length bounds the join, not each physical piece.
  // Neither half alone exceeds the cap; together they do.
  GMDL_Limits limits;
  gmdl_limits_default(&limits);
  limits.max_line_length = 16;
  EXPECT_EQ(load_text_expecting_failure("v 1 2 3 4 5 6 \\\n7 8 9 10 11 12\n",
                &limits),
      GMDL_ERR_LIMIT);
}

//
// Face tokens are references, not "whatever begins with a digit".
//

TEST(ObjFaceToken, NonNumericTokenIsRefused) {
  EXPECT_EQ(load_text_expecting_failure("v 1 2 3\nf a b c\n"), GMDL_ERR_FORMAT);
}

TEST(ObjFaceToken, TrailingJunkAfterTheIndexIsRefused) {
  // These used to parse as vertex 1 with the junk discarded, so a typo
  // became a face pointing somewhere real.
  EXPECT_EQ(load_text_expecting_failure("v 1 2 3\nf 1x 1y 1z\n"),
      GMDL_ERR_FORMAT);
  EXPECT_EQ(load_text_expecting_failure("v 1 2 3\nf 1.5 1 1\n"),
      GMDL_ERR_FORMAT);
  EXPECT_EQ(load_text_expecting_failure("v 1 2 3\nf 1/2/3x 1 1\n"),
      GMDL_ERR_FORMAT);
}

TEST(ObjFaceToken, ATokenThatStartsWithASlashIsRefused) {
  EXPECT_EQ(load_text_expecting_failure("v 1 2 3\nf /1 1 1\n"),
      GMDL_ERR_FORMAT);
}

TEST(ObjFaceToken, EveryWellFormedShapeIsStillAccepted) {
  GMDL_Obj * obj = load_text(
      "v 1 2 3\nv 1 2 3\nv 1 2 3\nvt 0 0\nvn 0 1 0\n"
      "f 1 2/1 3//1\n"
      "f 1/1/1 2/1/1 3/1/1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 2u);
  // "1" alone: no texture coordinate, no normal.
  EXPECT_EQ(obj->faces[0].vertex[0], 0);
  EXPECT_EQ(obj->faces[0].texcoord[0], -1);
  EXPECT_EQ(obj->faces[0].normal[0], -1);
  // "2/1": texture coordinate, still no normal.
  EXPECT_EQ(obj->faces[0].texcoord[1], 0);
  EXPECT_EQ(obj->faces[0].normal[1], -1);
  // "3//1": normal, no texture coordinate.
  EXPECT_EQ(obj->faces[0].texcoord[2], -1);
  EXPECT_EQ(obj->faces[0].normal[2], 0);
  gmdl_obj_free(obj);
}

TEST(ObjFaceToken, AFourthFieldIsIgnoredRatherThanRefused) {
  // Section 3.5 says the fourth '/'-separated field is ignored. It is a
  // decision, not an oversight, so the strict token reader has to keep it.
  GMDL_Obj * obj = load_text("v 1 2 3\nvt 0 0\nvn 0 1 0\nf 1/1/1/9 1 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  EXPECT_EQ(obj->faces[0].vertex[0], 0);
  EXPECT_EQ(obj->faces[0].texcoord[0], 0);
  EXPECT_EQ(obj->faces[0].normal[0], 0);
  gmdl_obj_free(obj);
}

//
// Names and paths: refused when they do not fit, never truncated (3.9).
//

TEST(ObjNames, OverLongGroupNameIsRefused) {
  std::string name(GMDL_OBJ_MAX_NAME_LENGTH, 'a');
  EXPECT_EQ(load_text_expecting_failure("g " + name + "\n"), GMDL_ERR_LIMIT);
}

TEST(ObjNames, AGroupNameThatExactlyFitsIsAccepted) {
  // The field holds the terminator too, so the longest legal name is one
  // byte shorter than the field. Both sides of that boundary are checked,
  // because an off-by-one here refuses valid files.
  std::string name(GMDL_OBJ_MAX_NAME_LENGTH - 1, 'a');
  GMDL_Obj * obj = load_text("g " + name + "\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->group_count, 1u);
  EXPECT_EQ(std::string(obj->groups[0].name), name);
  gmdl_obj_free(obj);
}

TEST(ObjNames, OverLongMaterialNameIsRefused) {
  std::string name(GMDL_OBJ_MAX_NAME_LENGTH, 'm');
  EXPECT_EQ(load_text_expecting_failure("usemtl " + name + "\n"),
      GMDL_ERR_LIMIT);
}

TEST(ObjNames, ABareUsemtlIsStillAFormatError) {
  // A missing name and an over-long one are different answers, because the
  // caller's next step differs.
  EXPECT_EQ(load_text_expecting_failure("usemtl\n"), GMDL_ERR_FORMAT);
}

TEST(ObjNames, OverLongMtllibPathIsRefused) {
  std::string path(GMDL_OBJ_MAX_PATH_LENGTH, 'p');
  EXPECT_EQ(load_text_expecting_failure("mtllib " + path + "\n"),
      GMDL_ERR_LIMIT);
}

TEST(ObjNames, AMaplibPathTooLongIsRefused) {
  std::string path(GMDL_OBJ_MAX_PATH_LENGTH, 'p');
  EXPECT_EQ(load_text_expecting_failure("maplib " + path + "\n"),
      GMDL_ERR_LIMIT);
}

TEST(ObjNames, AMtllibPathThatExactlyFitsIsAccepted) {
  std::string path(GMDL_OBJ_MAX_PATH_LENGTH - 1, 'p');
  GMDL_Obj * obj = load_text("mtllib " + path + "\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(std::string(obj->mtllib), path);
  gmdl_obj_free(obj);
}

//
// Dump: section 9 promises a structural round-trip, and floats that survive.
//

namespace {

/** Dump a model and parse the result back. */
GMDL_Obj * dump_and_reload(const GMDL_Obj * obj) {
  TempFile out("");
  EXPECT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  EXPECT_NE(sink, nullptr);
  EXPECT_EQ(gmdl_obj_dump(obj, sink), GMDL_OK);
  fclose(sink);
  GMDL_Obj * reloaded = nullptr;
  GMDL_Result r = gmdl_obj_load_file(out.path(), nullptr, nullptr, &reloaded);
  EXPECT_EQ(r, GMDL_OK) << gmdl_result_string(r);
  return reloaded;
}

} // namespace

TEST(ObjDump, FloatsSurviveTheRoundTrip) {
  // "%f" wrote six decimals, so this vertex came back as the origin. The
  // value is compared rather than the text, because the promise is about
  // the model and not about the spelling.
  GMDL_Obj * first = load_text("v 0.0000001 -3.4028235e38 12345.678\n");
  ASSERT_NE(first, nullptr);
  GMDL_Obj * second = dump_and_reload(first);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->vertex_count, 1u);
  EXPECT_FLOAT_EQ(second->vertices[0].x, first->vertices[0].x);
  EXPECT_FLOAT_EQ(second->vertices[0].y, first->vertices[0].y);
  EXPECT_FLOAT_EQ(second->vertices[0].z, first->vertices[0].z);
  EXPECT_NE(second->vertices[0].x, 0.0f) << "the old %f wrote this as zero";
  gmdl_obj_free(first);
  gmdl_obj_free(second);
}

// A colour that was read has to be written, or the library quietly loses it
// on the way back out - which is the same defect as never reading it, moved
// one file along.
TEST(ObjDump, ColoursSurviveTheRoundTrip) {
  const std::string source =
      "v 0 0 0 1 0 0\n"
      "v 1 0 0\n" // no colour, in a file that has them
      "v 0 1 0 0.25 0.5 0.75\n"
      "f 1 2 3\n";
  GMDL_Obj * first = load_text(source);
  ASSERT_NE(first, nullptr);

  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_obj_dump(first, sink), GMDL_OK);
  fclose(sink);

  GMDL_Obj * second = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(out.path(), nullptr, nullptr, &second), GMDL_OK);
  ASSERT_NE(second, nullptr);
  // Against the source, not just against each other: a library that dropped
  // colours entirely would give both sides a count of zero, the loop below
  // would compare nothing, and the round trip would agree about nothing at
  // all. Measured - removing the steal left this test green.
  ASSERT_EQ(first->color_count, 3u);
  ASSERT_EQ(second->color_count, first->color_count);
  for (size_t i = 0; i < first->color_count; i++) {
    EXPECT_EQ(second->colors[i].present, first->colors[i].present) << i;
    EXPECT_FLOAT_EQ(second->colors[i].r, first->colors[i].r) << i;
    EXPECT_FLOAT_EQ(second->colors[i].g, first->colors[i].g) << i;
    EXPECT_FLOAT_EQ(second->colors[i].b, first->colors[i].b) << i;
  }
  gmdl_obj_free(second);
  gmdl_obj_free(first);
}

// The round trip above would still pass if the writer emitted white for the
// uncoloured vertex, because white reads back as a colour that happens to
// equal the default. Read the bytes instead: the line must have three numbers
// on it and not six.
TEST(ObjDump, AnUncolouredVertexIsWrittenWithoutAColour) {
  GMDL_Obj * obj = load_text("v 0 0 0 1 0 0\nv 1 2 3\n");
  ASSERT_NE(obj, nullptr);
  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_obj_dump(obj, sink), GMDL_OK);
  fclose(sink);

  std::ifstream in(out.path());
  std::string text((std::istreambuf_iterator<char>(in)),
      std::istreambuf_iterator<char>());
  EXPECT_NE(text.find("v 0 0 0 1 0 0\n"), std::string::npos) << text;
  EXPECT_NE(text.find("v 1 2 3\n"), std::string::npos) << text;
  gmdl_obj_free(obj);
}

TEST(ObjDump, FacesBeforeTheFirstGroupAreWritten) {
  // Face 0 belongs to no group, so the group loop never reached it and the
  // dump lost it. The reload succeeded with one face fewer, which is why
  // nothing noticed.
  GMDL_Obj * first = load_text("v 1 2 3\nf 1 1 1\ng later\nf 1 1 1\n");
  ASSERT_NE(first, nullptr);
  ASSERT_EQ(first->face_count, 2u);
  ASSERT_EQ(first->group_count, 1u);
  ASSERT_EQ(first->groups[0].start_face, 1u);

  GMDL_Obj * second = dump_and_reload(first);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->face_count, 2u);
  EXPECT_EQ(second->group_count, 1u);
  EXPECT_EQ(second->groups[0].start_face, 1u);
  EXPECT_EQ(second->groups[0].face_count, 1u);
  gmdl_obj_free(first);
  gmdl_obj_free(second);
}

TEST(ObjDump, AModelWithNoLeadingOrphansIsUnchanged) {
  GMDL_Obj * first = load_text("v 1 2 3\ng only\nf 1 1 1\n");
  ASSERT_NE(first, nullptr);
  GMDL_Obj * second = dump_and_reload(first);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->face_count, 1u);
  EXPECT_EQ(second->group_count, 1u);
  EXPECT_EQ(second->groups[0].start_face, 0u);
  gmdl_obj_free(first);
  gmdl_obj_free(second);
}

TEST(ObjLine, ATrailingDoubleBackslashLeavesTheNameEndingInOne) {
  // The continuation rule takes exactly one backslash, so "g \\" names the
  // group "\". Pinned because it is the one thing a dump cannot write back:
  // the name lands last on its line, where a backslash reads as a
  // continuation. The fuzz harness skips such a model, and this test is
  // what stops that exemption from quietly covering a real defect.
  GMDL_Obj * obj = load_text("g \\\\\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->group_count, 1u);
  EXPECT_STREQ(obj->groups[0].name, "\\");
  gmdl_obj_free(obj);
}

TEST(ObjLine, ABackslashInsideANameIsJustACharacter) {
  // Only a trailing one continues, so an interior backslash survives and
  // round-trips like any other byte.
  GMDL_Obj * obj = load_text("g a\\b\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->group_count, 1u);
  EXPECT_STREQ(obj->groups[0].name, "a\\b");
  GMDL_Obj * again = dump_and_reload(obj);
  ASSERT_NE(again, nullptr);
  ASSERT_EQ(again->group_count, 1u);
  EXPECT_STREQ(again->groups[0].name, "a\\b");
  gmdl_obj_free(obj);
  gmdl_obj_free(again);
}

//
// Every refusal in a face reference (3.5). Each of these left the parser by
// a different return, and none of them had been reached.
//

TEST(ObjFace, ATextureFieldThatIsNotANumberIsRefused) {
  EXPECT_EQ(load_text_expecting_failure("v 0 0 0\nvt 0 0\nf 1/x 1 1\n"),
      GMDL_ERR_FORMAT);
}

TEST(ObjFace, JunkAfterTheTextureFieldIsRefused) {
  EXPECT_EQ(load_text_expecting_failure("v 0 0 0\nvt 0 0\nf 1/1x 1 1\n"),
      GMDL_ERR_FORMAT);
}

TEST(ObjFace, ANormalFieldThatIsNotANumberIsRefused) {
  EXPECT_EQ(load_text_expecting_failure("v 0 0 0\nvn 0 0 1\nf 1/1/x 1 1\n"),
      GMDL_ERR_FORMAT);
}

//
// Every write the dumper checks can fail, and until this swept them none of
// those arms had ever executed (section 9).
//

namespace {

/** A model reaching every kind of line and every reference shape the dumper
 *  writes: an mtllib, all three coordinate kinds, a face before the first
 *  group, two groups, a material change, all four reference spellings, a
 *  face long enough to spill into the overflow array, a smoothing group that
 *  is set and then turned off, polylines with and without texture
 *  references, points, and a material change at the polylines and again
 *  at the points, a polyline and a point declared before any material -
 *  which the dumper writes in a pass of its own - a maplib, a usemap, the
 *  usemap off that turns it back around, a map change at a polyline and at a
 *  point, and a recorded call and
 *  csh. A directive the model
 *  does not carry has its
 *  failure arm go unexecuted, which is how this sweep quietly stops covering
 *  the writer whenever the format grows. */
const char * kRichModel = "mtllib m.mtl\n"
                          "maplib maps.map\n"
                          "call parts.obj 1\n"
                          "csh -date\n"
                          "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\n"
                          "v 2 0 0\nv 2 1 0\n"
                          // One coloured vertex among uncoloured ones, so the
                          // sweep walks both arms of the vertex writer. The
                          // fifth time a new directive arrived without one.
                          "v 3 0 0 0.5 0.25 0.125\n"
                          "vt 0 0\nvt 1 0\n"
                          "vn 0 0 1\n"
                          "l 1 2\n"
                          "p 3\n"
                          "usemtl red\n"
                          "usemap chrome\n"
                          "f 1 2 3\n"
                          "usemap off\n"
                          "g first\n"
                          "usemtl blue\n"
                          "f 1/1 2/2 3/1\n"
                          "f 1//1 2//1 3//1\n"
                          "f 1/1/1 2/2/1 3/1/1\n"
                          "g second\n"
                          "s 4\n"
                          "f 1 2 3 4 5 6\n"
                          "s off\n"
                          "f 1 2 3\n"
                          "usemtl green\n"
                          // A map change at a polyline and again at a point.
                          // The dumper writes each in a pass of its own, and
                          // a map that never changes there leaves the write
                          // failure arm of each unreachable.
                          "usemap etched\n"
                          "l 1 2 3\n"
                          "l 1/1 2/2\n"
                          "usemtl red\n"
                          "usemap brushed\n"
                          "p 1 2\n";

/** The same lines with no group, so the dumper writes every face in one
 *  range rather than walking groups. */
const char * kGrouplessModel = "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                               "vt 0 0\n"
                               "usemtl red\n"
                               "s 2\n"
                               "f 1/1 2/1 3/1\n"
                               "usemtl blue\n"
                               "f 1 2 3\n"
                               "usemtl green\n"
                               "l 1 2\n"
                               "usemtl red\n"
                               "p 1\n";

} // namespace

TEST(ObjDump, EveryWriteFailureIsReported) {
  // Both shapes, because they are written by different code: a model with
  // groups walks the group loop, and one without takes the branch that
  // writes every face in a single range. Sweeping only the first left that
  // second branch's failure arm the one line in the dumper no test reached.
  //
  // The third case is the groupless model with its compatibility `mtllib`
  // field set by hand. No parse reaches that state - the field is derived
  // from the list, so an empty list means an empty field - and the dumper
  // keeps a branch for a model built through the struct. Without this the
  // arm reporting THAT write failing is the one line in the dumper no test
  // executes, which is the failure this sweep's comment above warns about,
  // arriving from the other direction: not a directive the model lacks, but
  // a state a parse cannot produce.
  //
  struct SweepCase {
    const char * source;
    bool hand_set_mtllib;
  };
  const SweepCase cases[] = {
      {kRichModel, false},
      {kGrouplessModel, false},
      {kGrouplessModel, true},
  };
  for (const SweepCase & sweep : cases) {
    GMDL_Obj * obj = load_text(sweep.source);
    ASSERT_NE(obj, nullptr);
    if (sweep.hand_set_mtllib) {
      ASSERT_EQ(obj->mtllib_count, 0u);
      const char * path = "set by hand.mtl";
      memcpy(obj->mtllib, path, strlen(path) + 1);
    }

    // Walk the failure through the dump one write at a time. Each position
    // must be reported as I/O rather than swallowed; the sweep ends when the
    // budget is large enough for the whole dump to succeed.
    size_t failures = 0;
    for (size_t allow = 0; allow < 4096; allow++) {
      FailingSink sink(allow);
      ASSERT_NE(sink.get(), nullptr);
      GMDL_Result r = gmdl_obj_dump(obj, sink.get());
      if (r == GMDL_OK) {
        EXPECT_FALSE(sink.failed()) << "a dump that succeeded wrote past the "
                                       "budget it was given";
        break;
      }
      EXPECT_EQ(r, GMDL_ERR_IO)
          << "write " << allow << " failed and the dumper answered "
          << gmdl_result_string(r);
      EXPECT_TRUE(sink.failed())
          << "write " << allow << " reported I/O without the sink refusing";
      failures++;
    }
    // Guards the sweep itself: a model that dumped in two writes would make
    // this test pass while checking almost nothing.
    EXPECT_GT(failures, 10u) << "the sweep stopped far too early";
    gmdl_obj_free(obj);
  }
}

// The compatibility field without a list behind it. A parse cannot produce
// that state any more - the field is derived from the list, so an empty list
// means an empty field - but a model built through the struct can, and the
// dumper keeps a branch for it. Reached the only way it can be.
TEST(ObjDump, AHandSetMtllibFieldWithNoListIsStillWritten) {
  GMDL_Obj * obj = load_text("v 0 0 0\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->mtllib_count, 0u);
  ASSERT_STREQ(obj->mtllib, "");
  const char * path = "set by hand.mtl";
  memcpy(obj->mtllib, path, strlen(path) + 1);

  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_obj_dump(obj, sink), GMDL_OK);
  fclose(sink);

  GMDL_Obj * again = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(out.path(), nullptr, nullptr, &again), GMDL_OK);
  ASSERT_NE(again, nullptr);
  ASSERT_EQ(again->mtllib_count, 1u);
  EXPECT_STREQ(again->mtllibs[0].path, path);
  EXPECT_STREQ(again->mtllib, path);
  gmdl_obj_free(again);
  gmdl_obj_free(obj);
}

TEST(ObjDump, AFaceNamingAMapWithNoMappingWritesUsemapOff) {
  // The map fallback, reached the way the material one below is: by moving
  // the mapping out from under the face. It writes `usemap off` rather than
  // inventing a name, because "a map this model cannot name" and "no map"
  // read back identically and only one of them has a spelling - which is the
  // difference from the material fallback, where the format defines what an
  // unnamed material looks like.
  GMDL_Obj * obj = load_text("v 0 0 0\nusemap chrome\nf 1 1 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->map_mapping_count, 1u);
  ASSERT_EQ(obj->face_count, 1u);
  ASSERT_EQ(obj->faces[0].map_index, obj->map_mappings[0].index);
  obj->map_mappings[0].index += 100;

  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  EXPECT_EQ(gmdl_obj_dump(obj, sink), GMDL_OK);
  fclose(sink);

  std::string text;
  FILE * back = fopen(out.path(), "rb");
  ASSERT_NE(back, nullptr);
  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), back)) > 0) text.append(buf, n);
  fclose(back);
  EXPECT_NE(text.find("usemap off"), std::string::npos) << text;
  EXPECT_EQ(text.find("usemap chrome"), std::string::npos)
      << "the name belongs to a mapping the face no longer reaches:\n"
      << text;
  gmdl_obj_free(obj);
}

TEST(ObjDump, AFaceNamingAMaterialWithNoMappingWritesWhite) {
  // The dumper's fallback: a material index that matches no mapping is
  // written as "white", because OBJ cannot turn a material off and an
  // unnamed one renders white. No file produces that state - every index the
  // parser assigns comes from a mapping it made - so it is reached here by
  // moving the mapping out from under the face, which is the only way the
  // arm can be executed at all.
  GMDL_Obj * obj = load_text("v 0 0 0\nusemtl red\nf 1 1 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->material_mapping_count, 1u);
  ASSERT_EQ(obj->face_count, 1u);
  ASSERT_EQ(obj->faces[0].material_index, obj->material_mappings[0].index);
  obj->material_mappings[0].index += 100;

  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  EXPECT_EQ(gmdl_obj_dump(obj, sink), GMDL_OK);
  fclose(sink);

  GMDL_Obj * reloaded = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(out.path(), nullptr, nullptr, &reloaded),
      GMDL_OK);
  ASSERT_NE(reloaded, nullptr);
  ASSERT_EQ(reloaded->material_mapping_count, 1u);
  EXPECT_STREQ(reloaded->material_mappings[0].name, "white");
  gmdl_obj_free(reloaded);
  gmdl_obj_free(obj);
}

//
// Smoothing groups, polylines and points (3.11, 3.12).
//

TEST(ObjSmoothing, AppliesToTheFacesAfterIt) {
  GMDL_Obj * obj = load_text("v 0 0 0\n"
                             "f 1 1 1\n"
                             "s 3\n"
                             "f 1 1 1\n"
                             "f 1 1 1\n"
                             "s off\n"
                             "f 1 1 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 4u);
  EXPECT_EQ(obj->faces[0].smoothing_group, 0) << "no s yet, so none";
  EXPECT_EQ(obj->faces[1].smoothing_group, 3);
  EXPECT_EQ(obj->faces[2].smoothing_group, 3);
  EXPECT_EQ(obj->faces[3].smoothing_group, 0) << "s off is zero";
  gmdl_obj_free(obj);
}

TEST(ObjSmoothing, ZeroAndOffMeanTheSame) {
  GMDL_Obj * obj = load_text("v 0 0 0\ns 1\nf 1 1 1\ns 0\nf 1 1 1\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->faces[0].smoothing_group, 1);
  EXPECT_EQ(obj->faces[1].smoothing_group, 0);
  gmdl_obj_free(obj);
}

TEST(ObjSmoothing, AValueThatIsNeitherOffNorANumberIsRefused) {
  EXPECT_EQ(load_text_expecting_failure("v 0 0 0\ns wobble\nf 1 1 1\n"),
      GMDL_ERR_FORMAT);
}

TEST(ObjSmoothing, AModelWithNoSmoothingWritesNone) {
  GMDL_Obj * obj = load_text("v 0 0 0\nf 1 1 1\n");
  ASSERT_NE(obj, nullptr);
  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_obj_dump(obj, sink), GMDL_OK);
  fclose(sink);
  std::string text;
  FILE * back = fopen(out.path(), "rb");
  ASSERT_NE(back, nullptr);
  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), back)) > 0) text.append(buf, n);
  fclose(back);
  EXPECT_EQ(text.find("\ns "), std::string::npos)
      << "zero is the parser's starting state, so it needs no line:\n" << text;
  gmdl_obj_free(obj);
}

TEST(ObjSmoothing, SurvivesAcrossGroupBoundaries) {
  // The dumper writes faces in several runs - the orphans, then one per
  // group - and the smoothing group in force carries across them. Deriving
  // it per run instead would reset to zero at each "g", so the second
  // group's faces would come back unsmoothed with nothing to show for it.
  // The value has to *fall* across the boundary for this to bite. A group
  // that merely continues the previous one is written correctly either way -
  // a re-derived state emits a redundant "s 7" and reparses the same, which
  // is why the first version of this test passed against the bug.
  GMDL_Obj * first = load_text("v 0 0 0\n"
                               "s 7\n"
                               "f 1 1 1\n"
                               "g a\n"
                               "s 0\n"
                               "f 1 1 1\n"
                               "g b\n"
                               "s 7\n"
                               "f 1 1 1\n");
  ASSERT_NE(first, nullptr);
  ASSERT_EQ(first->face_count, 3u);
  ASSERT_EQ(first->faces[1].smoothing_group, 0) << "the fixture itself";
  GMDL_Obj * second = dump_and_reload(first);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->face_count, 3u);
  EXPECT_EQ(second->faces[0].smoothing_group, 7);
  EXPECT_EQ(second->faces[1].smoothing_group, 0)
      << "a group that turns smoothing off must say so";
  EXPECT_EQ(second->faces[2].smoothing_group, 7);
  gmdl_obj_free(first);
  gmdl_obj_free(second);
}

TEST(ObjLine, ReadsVertexAndTextureReferences) {
  GMDL_Obj * obj = load_text("v 0 0 0\nv 1 0 0\nv 2 0 0\n"
                             "vt 0 0\nvt 1 0\n"
                             "l 1 2 3\n"
                             "l 1/1 2/2\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->line_count, 2u);
  ASSERT_EQ(obj->line_vertex_count, 5u);

  EXPECT_EQ(obj->lines[0].start, 0u);
  EXPECT_EQ(obj->lines[0].count, 3u);
  EXPECT_EQ(obj->line_vertices[0].vertex, 0);
  EXPECT_EQ(obj->line_vertices[0].texcoord, -1);
  EXPECT_EQ(obj->line_vertices[2].vertex, 2);

  EXPECT_EQ(obj->lines[1].start, 3u);
  EXPECT_EQ(obj->lines[1].count, 2u);
  EXPECT_EQ(obj->line_vertices[3].vertex, 0);
  EXPECT_EQ(obj->line_vertices[3].texcoord, 0);
  EXPECT_EQ(obj->line_vertices[4].texcoord, 1);
  gmdl_obj_free(obj);
}

TEST(ObjLine, ResolvesRelativeIndices) {
  GMDL_Obj * obj = load_text("v 0 0 0\nv 1 0 0\nl -2 -1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->line_vertex_count, 2u);
  EXPECT_EQ(obj->line_vertices[0].vertex, 0);
  EXPECT_EQ(obj->line_vertices[1].vertex, 1);
  gmdl_obj_free(obj);
}

TEST(ObjLine, NamingNothingIsRefused) {
  EXPECT_EQ(load_text_expecting_failure("v 0 0 0\nl\n"), GMDL_ERR_FORMAT);
}

TEST(ObjLine, ARubbishReferenceIsRefused) {
  EXPECT_EQ(load_text_expecting_failure("v 0 0 0\nl 1 x\n"), GMDL_ERR_FORMAT);
}

TEST(ObjPoint, OneStatementDeclaresOnePointPerIndex) {
  GMDL_Obj * obj = load_text("v 0 0 0\nv 1 0 0\nv 2 0 0\np 1 2\np 3\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->point_count, 3u);
  EXPECT_EQ(obj->points[0].vertex, 0);
  EXPECT_EQ(obj->points[1].vertex, 1);
  EXPECT_EQ(obj->points[2].vertex, 2);
  gmdl_obj_free(obj);
}

TEST(ObjPoint, ASlashFormIsRefused) {
  // A point is a vertex index and nothing else.
  EXPECT_EQ(load_text_expecting_failure("v 0 0 0\nvt 0 0\np 1/1\n"),
      GMDL_ERR_FORMAT);
  EXPECT_EQ(load_text_expecting_failure("v 0 0 0\np\n"), GMDL_ERR_FORMAT);
}

TEST(ObjLine, LinesAndPointsSurviveTheRoundTrip) {
  GMDL_Obj * first = load_text("v 0 0 0\nv 1 0 0\nv 2 0 0\n"
                               "vt 0 0\nvt 1 0\n"
                               "f 1 2 3\n"
                               "l 1 2 3\n"
                               "l 1/1 2/2\n"
                               "p 1 2\n"
                               "p 3\n");
  ASSERT_NE(first, nullptr);
  GMDL_Obj * second = dump_and_reload(first);
  ASSERT_NE(second, nullptr);

  ASSERT_EQ(second->line_count, first->line_count);
  ASSERT_EQ(second->line_vertex_count, first->line_vertex_count);
  ASSERT_EQ(second->point_count, first->point_count);
  for (size_t i = 0; i < first->line_count; i++) {
    EXPECT_EQ(second->lines[i].start, first->lines[i].start) << "line " << i;
    EXPECT_EQ(second->lines[i].count, first->lines[i].count) << "line " << i;
  }
  for (size_t i = 0; i < first->line_vertex_count; i++) {
    EXPECT_EQ(second->line_vertices[i].vertex, first->line_vertices[i].vertex)
        << "entry " << i;
    EXPECT_EQ(
        second->line_vertices[i].texcoord, first->line_vertices[i].texcoord)
        << "entry " << i;
  }
  for (size_t i = 0; i < first->point_count; i++) {
    EXPECT_EQ(second->points[i].vertex, first->points[i].vertex)
        << "point " << i;
  }
  EXPECT_EQ(second->face_count, first->face_count);
  gmdl_obj_free(first);
  gmdl_obj_free(second);
}

TEST(ObjLine, AMaterialNamedBeforeALineIsRecorded) {
  // usemtl applies to l and p as it does to f, so all three carry one.
  GMDL_Obj * obj = load_text("v 0 0 0\nv 1 0 0\n"
                             "usemtl red\n"
                             "l 1 2\n"
                             "p 1\n"
                             "f 1 2 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->line_count, 1u);
  ASSERT_EQ(obj->point_count, 1u);
  ASSERT_EQ(obj->face_count, 1u);
  ASSERT_EQ(obj->material_mapping_count, 1u);
  int32_t red = obj->material_mappings[0].index;
  EXPECT_EQ(obj->lines[0].material_index, red);
  EXPECT_EQ(obj->points[0].material_index, red);
  EXPECT_EQ(obj->faces[0].material_index, red);
  gmdl_obj_free(obj);
}

TEST(ObjLine, AnElementBeforeAnyMaterialHasNone) {
  GMDL_Obj * obj = load_text("v 0 0 0\nv 1 0 0\nl 1 2\np 1\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->lines[0].material_index, -1);
  EXPECT_EQ(obj->points[0].material_index, -1);
  gmdl_obj_free(obj);
}

TEST(ObjLine, AMaterialThatChangesAfterTheFacesReachesTheLines) {
  // A material stated after the faces has to reach the polylines and the
  // points, which the dumper writes last. This does not need the carried
  // state to pass - a run starting afresh would also write "usemtl blue",
  // since blue differs from "none" too - so it is the plain statement that
  // the material arrives, and AMaterialAlreadyInForceIsNotRepeated is the
  // one the carrying is for.
  GMDL_Obj * first = load_text("v 0 0 0\nv 1 0 0\n"
                               "usemtl red\n"
                               "f 1 2 1\n"
                               "usemtl blue\n"
                               "l 1 2\n"
                               "p 2\n");
  ASSERT_NE(first, nullptr);
  ASSERT_EQ(first->material_mapping_count, 2u);
  GMDL_Obj * second = dump_and_reload(first);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->line_count, 1u);
  ASSERT_EQ(second->point_count, 1u);

  auto name_of = [](const GMDL_Obj * o, int32_t index) -> const char * {
    for (size_t i = 0; i < o->material_mapping_count; i++) {
      if (o->material_mappings[i].index == index) {
        return o->material_mappings[i].name;
      }
    }
    return "<none>";
  };
  EXPECT_STREQ(name_of(second, second->faces[0].material_index), "red");
  EXPECT_STREQ(name_of(second, second->lines[0].material_index), "blue");
  EXPECT_STREQ(name_of(second, second->points[0].material_index), "blue");
  gmdl_obj_free(first);
  gmdl_obj_free(second);
}

TEST(ObjLine, AnElementDeclaredBeforeAnyMaterialKeepsHavingNone) {
  // The fuzzer found this within ninety seconds of l and p gaining a
  // material. The dumper writes the faces first, so a polyline declared
  // before the file's only usemtl was emitted after it and reloaded
  // carrying the face's material - and OBJ cannot turn a material off, so
  // there was no way to write it in that position at all. The elements
  // holding no material now go out before the faces, while none is in
  // force.
  GMDL_Obj * first = load_text("v 0 0 0\nv 1 0 0\n"
                               "l 1 2\n"
                               "p 1\n"
                               "usemtl a\n"
                               "f 1 1 1\n"
                               "l 1 2\n"
                               "p 2\n");
  ASSERT_NE(first, nullptr);
  ASSERT_EQ(first->line_count, 2u);
  ASSERT_EQ(first->point_count, 2u);
  ASSERT_EQ(first->lines[0].material_index, -1) << "the fixture itself";
  ASSERT_NE(first->lines[1].material_index, -1);

  GMDL_Obj * second = dump_and_reload(first);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->line_count, 2u);
  ASSERT_EQ(second->point_count, 2u);
  EXPECT_EQ(second->lines[0].material_index, -1)
      << "a polyline before any usemtl still names no material";
  EXPECT_EQ(second->points[0].material_index, -1);
  EXPECT_NE(second->lines[1].material_index, -1);
  EXPECT_NE(second->points[1].material_index, -1);

  // And the arrays keep their own order across the split.
  EXPECT_EQ(second->line_vertices[second->lines[0].start].vertex, 0);
  gmdl_obj_free(first);
  gmdl_obj_free(second);
}

TEST(ObjDump, AnElementNamingNoMaterialKeepsThePreviousOne) {
  // The fourth thing section 9 says cannot be written back. OBJ can change
  // the material in force but not turn it off, so an element holding -1
  // after one is set has no spelling: nothing is written, and the reload
  // gives it the previous material. A parse never reaches this state -
  // material_index goes from -1 to a mapping and never back - so the model
  // is built by hand, which is the only way the arm executes at all.
  GMDL_Obj * obj = load_text("v 0 0 0\n"
                             "usemtl red\n"
                             "f 1 1 1\n"
                             "f 1 1 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 2u);
  ASSERT_NE(obj->faces[1].material_index, -1);
  obj->faces[1].material_index = -1;

  GMDL_Obj * reloaded = dump_and_reload(obj);
  ASSERT_NE(reloaded, nullptr);
  ASSERT_EQ(reloaded->face_count, 2u);
  EXPECT_EQ(reloaded->faces[1].material_index, reloaded->faces[0].material_index)
      << "with no way to say 'none', the file keeps saying the previous one";
  gmdl_obj_free(reloaded);
  gmdl_obj_free(obj);
}

TEST(ObjLine, AMaterialAlreadyInForceIsNotRepeated) {
  // This is the test the carried material is for. A run that began afresh
  // would write "usemtl red" again at the polylines, which reloads
  // correctly and says something the model does not. Confirmed by mutation:
  // re-deriving the state per run fails here and nowhere else.
  GMDL_Obj * obj = load_text("v 0 0 0\nv 1 0 0\n"
                             "usemtl red\n"
                             "f 1 2 1\n"
                             "l 1 2\n");
  ASSERT_NE(obj, nullptr);
  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_obj_dump(obj, sink), GMDL_OK);
  fclose(sink);
  std::string text;
  FILE * back = fopen(out.path(), "rb");
  ASSERT_NE(back, nullptr);
  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), back)) > 0) text.append(buf, n);
  fclose(back);
  size_t first = text.find("usemtl red");
  ASSERT_NE(first, std::string::npos) << text;
  EXPECT_EQ(text.find("usemtl red", first + 1), std::string::npos)
      << "the material was already in force:\n" << text;
  gmdl_obj_free(obj);
}

TEST(ObjDirectives, UnreadOnesAreSkippedRatherThanRefused) {
  // 3.14: the free-form sub-language and the render attributes. A file
  // carrying them still loads. `maplib` and `usemap` used to be here too and
  // are read now (3.15), which is why they are not in this list.
  GMDL_Obj * obj = load_text("v 0 0 0\n"
                             "vp 0.5\n"
                             "cstype bezier\n"
                             "deg 3\n"
                             "bevel on\n"
                             "c_interp on\n"
                             "lod 4\n"
                             "shadow_obj shadow.obj\n"
                             "trace_obj trace.obj\n"
                             "f 1 1 1\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->vertex_count, 1u);
  EXPECT_EQ(obj->face_count, 1u);
  EXPECT_EQ(obj->statement_count, 0u);
  gmdl_obj_free(obj);
}

//
// `maplib` and `usemap` (3.15). The texture-map pair, standing to texture
// maps as `mtllib` and `usemtl` do to materials. Measured 2026-09-23:
// Blender 4.3.2 implements neither, printing "OBJ element not recognized"
// and skipping the line, so nothing here follows a reference - it follows
// the readings this library already made for the material pair.
//

TEST(ObjMap, EveryMaplibLineIsKeptInOrder) {
  GMDL_Obj * obj = load_text(
      "maplib first.map\nmaplib second one.map\nv 0 0 0\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->maplib_count, 2u);
  EXPECT_STREQ(obj->maplibs[0].path, "first.map");
  // The whole line is the path, as it is for `mtllib` (3.8). No reference
  // settles the several-paths-per-line reading because no reference reads
  // the directive; this is the library's own rule applied consistently.
  EXPECT_STREQ(obj->maplibs[1].path, "second one.map");
  gmdl_obj_free(obj);
}

// A bare `maplib` names no library, so it must not append an empty entry -
// the dump would write back a `maplib ` line the document never had.
TEST(ObjMap, BareMaplibAddsNoEntryAndClearsNothing) {
  GMDL_Obj * obj = load_text("maplib a.map\nmaplib\nv 0 0 0\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->maplib_count, 1u);
  EXPECT_STREQ(obj->maplibs[0].path, "a.map");
  gmdl_obj_free(obj);
}

// Indices are assigned in order of first use and a repeated name reuses its
// own, which is what `usemtl` does (3.7).
TEST(ObjMap, NamesAreAssignedIndicesInOrderOfFirstUse) {
  GMDL_Obj * obj = load_text("v 0 0 0\n"
                             "usemap chrome\nf 1 1 1\n"
                             "usemap rust\nf 1 1 1\n"
                             "usemap chrome\nf 1 1 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->map_mapping_count, 2u);
  EXPECT_STREQ(obj->map_mappings[0].name, "chrome");
  EXPECT_STREQ(obj->map_mappings[1].name, "rust");
  ASSERT_EQ(obj->face_count, 3u);
  EXPECT_EQ(obj->faces[0].map_index, 0);
  EXPECT_EQ(obj->faces[1].map_index, 1);
  EXPECT_EQ(obj->faces[2].map_index, 0) << "a repeated name reuses its index";
  gmdl_obj_free(obj);
}

// The one thing `usemap` has that `usemtl` does not: a spelling for "none".
// A face before any `usemap` carries -1 as well, so the two are the same
// state and the dump cannot tell them apart - which is correct, because the
// format cannot either.
TEST(ObjMap, OffReturnsToNoMapAndAssignsNoIndex) {
  GMDL_Obj * obj = load_text("v 0 0 0\n"
                             "f 1 1 1\n"
                             "usemap chrome\nf 1 1 1\n"
                             "usemap off\nf 1 1 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 3u);
  EXPECT_EQ(obj->faces[0].map_index, -1) << "nothing is in force to begin";
  EXPECT_EQ(obj->faces[1].map_index, 0);
  EXPECT_EQ(obj->faces[2].map_index, -1);
  EXPECT_EQ(obj->map_mapping_count, 1u) << "\"off\" is not a map name";
  gmdl_obj_free(obj);
}

// `usemap` applies to every element `usemtl` applies to, so `l` and `p`
// carry it too. Faces alone would pass a test that only looked at faces.
TEST(ObjMap, AppliesToPolylinesAndPointsAsWell) {
  GMDL_Obj * obj = load_text("v 0 0 0\nv 1 0 0\n"
                             "usemap chrome\n"
                             "l 1 2\n"
                             "p 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->line_count, 1u);
  ASSERT_EQ(obj->point_count, 1u);
  EXPECT_EQ(obj->lines[0].map_index, 0);
  EXPECT_EQ(obj->points[0].map_index, 0);
  gmdl_obj_free(obj);
}

// A bare `usemap` names nothing, and there is nothing sensible to do with
// it: GMDL_ERR_FORMAT, the reading `usemtl` uses (3.7).
TEST(ObjMap, BareUsemapIsAFormatError) {
  EXPECT_EQ(load_text_expecting_failure("v 0 0 0\nusemap\nf 1 1 1\n"),
      GMDL_ERR_FORMAT);
}

// The round trip is the half the loader cannot show. Both directives and
// both states - a named map and the return to none - have to survive, and
// the "none" half is the one materials cannot do at all (9).
TEST(ObjMap, TheWholePairSurvivesARoundTrip) {
  GMDL_Obj * obj = load_text("maplib first.map\nmaplib second one.map\n"
                             "v 0 0 0\n"
                             "f 1 1 1\n"
                             "usemap chrome\nf 1 1 1\n"
                             "usemap off\nf 1 1 1\n");
  ASSERT_NE(obj, nullptr);

  TempFile out("");
  ASSERT_TRUE(out.valid());
  FILE * sink = fopen(out.path(), "wb");
  ASSERT_NE(sink, nullptr);
  ASSERT_EQ(gmdl_obj_dump(obj, sink), GMDL_OK);
  fclose(sink);

  GMDL_Obj * again = nullptr;
  ASSERT_EQ(gmdl_obj_load_file(out.path(), nullptr, nullptr, &again), GMDL_OK);
  ASSERT_NE(again, nullptr);
  ASSERT_EQ(again->maplib_count, 2u);
  EXPECT_STREQ(again->maplibs[0].path, "first.map");
  EXPECT_STREQ(again->maplibs[1].path, "second one.map");
  ASSERT_EQ(again->map_mapping_count, 1u);
  EXPECT_STREQ(again->map_mappings[0].name, "chrome");
  ASSERT_EQ(again->face_count, 3u);
  EXPECT_EQ(again->faces[0].map_index, -1);
  EXPECT_EQ(again->faces[1].map_index, 0);
  EXPECT_EQ(again->faces[2].map_index, -1)
      << "`usemap off` is the spelling that makes this writable";
  gmdl_obj_free(again);
  gmdl_obj_free(obj);
}

//
// "call" and "csh" are recorded, never executed (3.13).
//

TEST(ObjStatement, BothAreRecordedInFileOrder) {
  GMDL_Obj * obj = load_text("v 0 0 0\n"
                             "call parts/wheel.obj 3 4\n"
                             "csh -date\n"
                             "csh echo hello\n"
                             "f 1 1 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->statement_count, 3u);
  EXPECT_EQ(obj->statements[0].kind, GMDL_OBJ_STATEMENT_CALL);
  EXPECT_STREQ(obj->statements[0].text, "parts/wheel.obj 3 4")
      << "the filename and its arguments are kept together, unsplit";
  EXPECT_EQ(obj->statements[1].kind, GMDL_OBJ_STATEMENT_CSH);
  EXPECT_STREQ(obj->statements[1].text, "-date")
      << "the leading dash is part of what the file said";
  EXPECT_EQ(obj->statements[2].kind, GMDL_OBJ_STATEMENT_CSH);
  EXPECT_STREQ(obj->statements[2].text, "echo hello");
  EXPECT_EQ(obj->face_count, 1u) << "the geometry still parses";
  gmdl_obj_free(obj);
}

TEST(ObjStatement, NothingIsExecuted) {
  // The whole point. If the parser ran what it read, this would leave a
  // file behind; it does not, because it never spawns anything. Pinned so
  // that "record, do not execute" is a checked property rather than a
  // sentence in a comment.
  // The marker lives beside a temporary file, so nothing is written into the
  // source tree even if this ever starts failing.
  TempFile anchor("");
  ASSERT_TRUE(anchor.valid());
  std::string marker = std::string(anchor.path()) + ".ran";

  // Positive control first: a test whose value is a negative has to be shown
  // capable of producing the positive, or a probe that could never see the
  // marker reads exactly like a parser that never made one.
  std::remove(marker.c_str());
  FILE * planted = fopen(marker.c_str(), "wb");
  ASSERT_NE(planted, nullptr) << "could not write the marker at all";
  fclose(planted);
  FILE * found = fopen(marker.c_str(), "rb");
  ASSERT_NE(found, nullptr) << "the probe cannot see a marker that exists";
  fclose(found);
  ASSERT_EQ(std::remove(marker.c_str()), 0);

  GMDL_Obj * obj =
      load_text("v 0 0 0\ncsh touch " + marker + "\nf 1 1 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->statement_count, 1u);

  FILE * probe = fopen(marker.c_str(), "rb");
  EXPECT_EQ(probe, nullptr) << "the parser ran the command in the file";
  if (probe) {
    fclose(probe);
    std::remove(marker.c_str());
  }
  gmdl_obj_free(obj);
}

TEST(ObjStatement, NamingNothingIsRefused) {
  EXPECT_EQ(load_text_expecting_failure("v 0 0 0\ncsh\n"), GMDL_ERR_FORMAT);
  EXPECT_EQ(load_text_expecting_failure("v 0 0 0\ncall   \n"),
      GMDL_ERR_FORMAT);
}

TEST(ObjStatement, ADirectiveBeginningWithTheKeywordIsNotTheKeyword) {
  GMDL_Obj * obj = load_text("v 0 0 0\ncalling 1\ncshx 2\nf 1 1 1\n");
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->statement_count, 0u);
  gmdl_obj_free(obj);
}

TEST(ObjStatement, TheySurviveTheRoundTrip) {
  GMDL_Obj * first = load_text("v 0 0 0\n"
                               "call other.obj\n"
                               "csh -ls -la\n"
                               "f 1 1 1\n");
  ASSERT_NE(first, nullptr);
  GMDL_Obj * second = dump_and_reload(first);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->statement_count, first->statement_count);
  for (size_t i = 0; i < first->statement_count; i++) {
    EXPECT_EQ(second->statements[i].kind, first->statements[i].kind)
        << "statement " << i;
    EXPECT_STREQ(second->statements[i].text, first->statements[i].text)
        << "statement " << i;
  }
  gmdl_obj_free(first);
  gmdl_obj_free(second);
}

// --- Limits -----------------------------------------------------------

namespace {

/**
 * One field of GMDL_Limits, and every document shape that must exceed it.
 *
 * A list rather than a single document, because a field is not one gate: it
 * is read wherever the parser counts the thing it caps, and each of those is
 * a separate `if` that can be wrong on its own. `max_face_indices` guards a
 * face and a polyline; `max_statements` guards `call` and `csh`; `max_faces`
 * guards three element kinds. One document per field proved each *field*
 * refused something and said nothing about the other sites - and coverage
 * showed exactly which arms that left unexecuted.
 */
struct LimitCase {
  size_t offset;      ///< offsetof() of the field.
  const char * field; ///< Its name, for the failure message.
  std::vector<const char *> documents; ///< One per site that reads the field.
};

const LimitCase kLimitCases[] = {
    {offsetof(GMDL_Limits, max_line_length), "max_line_length",
        {"v 0 0 0\n"}},
    {offsetof(GMDL_Limits, max_vertices), "max_vertices",
        {"v 0 0 0\nv 1 0 0\nv 2 0 0\n"}},
    {offsetof(GMDL_Limits, max_texcoords), "max_texcoords",
        {"vt 0 0\nvt 1 0\nvt 2 0\n"}},
    {offsetof(GMDL_Limits, max_normals), "max_normals",
        {"vn 0 0 1\nvn 0 1 0\nvn 1 0 0\n"}},
    // Three element kinds, and the one written last is the one that decides.
    {offsetof(GMDL_Limits, max_faces), "max_faces",
        {"v 0 0 0\nf 1 1 1\nf 1 1 1\nf 1 1 1\n",
            "v 0 0 0\nv 1 0 0\nl 1 2\nl 1 2\nl 1 2\n",
            "v 0 0 0\np 1\np 1\np 1\n"}},
    // Read once for a face and once for a polyline; only the face was driven.
    {offsetof(GMDL_Limits, max_face_indices), "max_face_indices",
        {"v 0 0 0\nf 1 1 1\n", "v 0 0 0\nv 1 0 0\nl 1 2 1 2\n"}},
    {offsetof(GMDL_Limits, max_groups), "max_groups", {"g a\ng b\ng c\n"}},
    {offsetof(GMDL_Limits, max_materials), "max_materials",
        {"usemtl a\nusemtl b\nusemtl c\n"}},
    // Read once for `call` and once for `csh`; only `call` was driven.
    {offsetof(GMDL_Limits, max_statements), "max_statements",
        {"call a\ncall b\ncall c\n", "csh a\ncsh b\ncsh c\n"}},
    {offsetof(GMDL_Limits, max_mtllibs), "max_mtllibs",
        {"mtllib a.mtl\nmtllib b.mtl\nmtllib c.mtl\n"}},
    {offsetof(GMDL_Limits, max_maplibs), "max_maplibs",
        {"maplib a.map\nmaplib b.map\nmaplib c.map\n"}},
    {offsetof(GMDL_Limits, max_maps), "max_maps",
        {"usemap a\nusemap b\nusemap c\n"}},
};

} // namespace

// Every field GMDL_Limits offers must actually refuse something.
//
// max_statements was missing for exactly as long as nothing asserted the set
// was complete: `call` and `csh` allocated without bound while every other
// record honoured a cap, and a caller who set every field still could not
// stop it. The fuzzers cannot find that class of gap either - what they drive
// is the set of caps that EXIST, so a quantity with no field is invisible to
// them. This is the gate that catches the next one.
TEST(ObjLimits, EveryFieldRefusesSomething) {
  // If this fails, a size_t was added to GMDL_Limits without a row here.
  ASSERT_EQ(sizeof(kLimitCases) / sizeof(kLimitCases[0]),
      sizeof(GMDL_Limits) / sizeof(size_t))
      << "GMDL_Limits has a field this table does not cover";

  for (const LimitCase & c : kLimitCases) {
    ASSERT_FALSE(c.documents.empty()) << c.field << " has no document";
    for (const char * document : c.documents) {
      GMDL_Limits limits;
      memset(&limits, 0, sizeof(limits)); // 0 == unlimited, for every field.
      *reinterpret_cast<size_t *>(
          reinterpret_cast<char *>(&limits) + c.offset) = 2;

      MemStream stream(document);
      GMDL_Obj * obj = nullptr;
      EXPECT_EQ(gmdl_obj_load(stream.get(), &limits, nullptr, &obj),
          GMDL_ERR_LIMIT)
          << c.field << " did not refuse:\n"
          << document;
      gmdl_obj_free(obj);
    }
  }
}

// `max_faces` is one budget shared by faces, polylines and points, which is
// what section 5's table has always said. It used to be three separate
// checks against the same field, so a caller bounding memory from untrusted
// input got three times what they asked for - and the whole suite passed
// either way, because every existing case exercised one element kind at a
// time. A limit that spans several arrays needs a case that spans them.
TEST(ObjLimits, FacesLinesAndPointsShareOneBudget) {
  // Each element kind has its own check against the field, so each must be
  // the one that *decides*, and only the kind written last is. A single
  // ordering passes against two of the three checks still counting their own
  // array alone - measured: reverting the `f` check survived a case whose
  // third element was a `p`.
  const char * const kOrders[] = {
      "v 0 0 0\nv 1 0 0\nl 1 2\np 1\nf 1 1 1\n", // faces decide
      "v 0 0 0\nv 1 0 0\nf 1 1 1\np 1\nl 1 2\n", // polylines decide
      "v 0 0 0\nv 1 0 0\nf 1 1 1\nl 1 2\np 1\n", // points decide
  };
  for (const char * document : kOrders) {
    GMDL_Limits limits;
    memset(&limits, 0, sizeof(limits));
    limits.max_faces = 2;

    MemStream stream(document);
    GMDL_Obj * obj = nullptr;
    EXPECT_EQ(gmdl_obj_load(stream.get(), &limits, nullptr, &obj),
        GMDL_ERR_LIMIT)
        << "three elements of three kinds fitted under a cap of two:\n"
        << document;
    gmdl_obj_free(obj);
  }
}

// The budget still admits what it should, or the test above would pass
// against a cap that refuses everything.
TEST(ObjLimits, TwoElementsOfDifferentKindsFitUnderACapOfTwo) {
  GMDL_Limits limits;
  memset(&limits, 0, sizeof(limits));
  limits.max_faces = 2;

  MemStream stream("v 0 0 0\nv 1 0 0\nf 1 1 1\nl 1 2\n");
  GMDL_Obj * obj = nullptr;
  ASSERT_EQ(gmdl_obj_load(stream.get(), &limits, nullptr, &obj), GMDL_OK);
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->face_count, 1u);
  EXPECT_EQ(obj->line_count, 1u);
  gmdl_obj_free(obj);
}

// A `p` statement costs one per index it names, not one per line, because
// that is what the model stores (3.10). `p 1 2 3` is three elements.
TEST(ObjLimits, APointStatementCostsOnePerIndex) {
  GMDL_Limits limits;
  memset(&limits, 0, sizeof(limits));
  limits.max_faces = 2;

  MemStream stream("v 0 0 0\np 1 1 1\n");
  GMDL_Obj * obj = nullptr;
  EXPECT_EQ(gmdl_obj_load(stream.get(), &limits, nullptr, &obj),
      GMDL_ERR_LIMIT);
  gmdl_obj_free(obj);
}

namespace {

/** An allocator that remembers the high-water mark of live bytes. */
struct Peak {
  size_t live = 0;
  size_t peak = 0;
  std::map<void *, size_t> sizes;

  void note(void * p, size_t n) {
    if (!p) {
      return;
    }
    sizes[p] = n;
    live += n;
    if (live > peak) {
      peak = live;
    }
  }
  void forget(void * p) {
    auto it = sizes.find(p);
    if (it != sizes.end()) {
      live -= it->second;
      sizes.erase(it);
    }
  }
};

void * peak_malloc(void * ctx, size_t n) {
  Peak * s = (Peak *)ctx;
  void * p = malloc(n ? n : 1);
  s->note(p, n);
  return p;
}
void * peak_calloc(void * ctx, size_t a, size_t b) {
  Peak * s = (Peak *)ctx;
  size_t total = a * b;
  void * p = calloc(1, total ? total : 1);
  s->note(p, total);
  return p;
}
void * peak_realloc(void * ctx, void * q, size_t n) {
  Peak * s = (Peak *)ctx;
  s->forget(q);
  void * p = realloc(q, n ? n : 1);
  s->note(p, n);
  return p;
}
void peak_free(void * ctx, void * q) {
  Peak * s = (Peak *)ctx;
  s->forget(q);
  free(q);
}

/** Parse @p text under @p limits and report the high-water mark. */
size_t peak_bytes_to_parse(const std::string & text, const GMDL_Limits & limits) {
  Peak state;
  GMDL_Allocator allocator{};
  allocator.ctx = &state;
  allocator.malloc_fn = peak_malloc;
  allocator.calloc_fn = peak_calloc;
  allocator.realloc_fn = peak_realloc;
  allocator.free_fn = peak_free;

  MemStream stream(text);
  GMDL_Obj * obj = nullptr;
  gmdl_obj_load(stream.get(), &limits, &allocator, &obj);
  gmdl_obj_free(obj);
  return state.peak;
}

} // namespace

// A cap that stops parsing has to stop *allocating*, and a test keyed on the
// return code cannot tell the two apart: a parser that read the whole file
// into memory and then refused it answers GMDL_ERR_LIMIT exactly as one that
// stopped at the cap does. Same status, opposite memory behaviour, and
// memory is what a caller setting these fields is bounding.
//
// So this measures the bound rather than the status, and the property is not
// an arbitrary byte count - it is that the high-water mark does not move when
// the input grows. Four times the input, the same caps, the same peak.
TEST(ObjLimits, PeakMemoryDoesNotFollowTheInputSize) {
  GMDL_Limits limits;
  memset(&limits, 0, sizeof(limits));
  limits.max_line_length = 128;
  limits.max_vertices = 16;
  limits.max_faces = 16;
  limits.max_statements = 4;

  std::string small;
  for (int i = 0; i < 20000; i++) {
    small += "v 1 2 3\n";
  }
  std::string large;
  for (int i = 0; i < 80000; i++) {
    large += "v 1 2 3\n";
  }
  ASSERT_EQ(large.size(), small.size() * 4);

  size_t small_peak = peak_bytes_to_parse(small, limits);
  size_t large_peak = peak_bytes_to_parse(large, limits);
  EXPECT_EQ(small_peak, large_peak)
      << "the peak moved from " << small_peak << " to " << large_peak
      << " bytes when the input quadrupled under unchanged caps";

  // Invariance alone would also hold for a parser that allocated some huge
  // constant, so one magnitude check as well - but a principled one rather
  // than a chosen fraction. The claim a caller cares about is exactly "the
  // parser does not hold the file", and that is what this says.
  EXPECT_LT(large_peak, large.size())
      << "peak " << large_peak << " against an input of " << large.size();
}

// The same for one enormous line, where the record cap - not the line cap -
// is the thing that has to stop the allocation. A face naming a hundred
// thousand vertices must not build a hundred thousand of anything first.
TEST(ObjLimits, AnOverlongElementDoesNotAllocateBeforeItIsRefused) {
  // Invariance again, and for a second reason beyond avoiding a chosen
  // threshold: what a bound assertion prints is one number against a limit,
  // which says the peak is too big; what this prints is two peaks against
  // each other, which says the peak is *tracking the element* - and that is
  // the defect rather than a symptom of it. chron's observation, made while
  // taking the same form: an invariance assertion carries more in its
  // failure than a bound assertion, because it reports the trend.
  //
  // The line buffer is allocated at max_line_length + 1 whatever the line
  // turns out to hold, so holding the cap fixed and varying only the number
  // of indices leaves the buffer identical between the two runs. Anything
  // that differs is the face's own storage, which is what the cap is
  // supposed to stop.
  GMDL_Limits limits;
  memset(&limits, 0, sizeof(limits));
  limits.max_line_length = 1u << 20; // room for the longer line
  limits.max_face_indices = 8;
  limits.max_faces = 8;

  auto face_with = [](int indices) {
    std::string text = "v 1 2 3\nf";
    for (int i = 0; i < indices; i++) {
      text += " 1";
    }
    return text + "\n";
  };

  size_t few = peak_bytes_to_parse(face_with(10000), limits);
  size_t many = peak_bytes_to_parse(face_with(100000), limits);
  EXPECT_EQ(few, many)
      << "refusing a 10,000-index face peaked at " << few
      << " bytes and a 100,000-index one at " << many
      << ", against a cap of 8 that did not move";
}

// Four arms that no test reached, found by reading the uncovered lines
// rather than by suspecting anything. Each is ordinary input: blanks after
// the last token of an `l` or a `p`, blanks after a recorded statement, and
// a `p` naming something that is not a number.
TEST(ObjParse, TrailingBlanksAfterTheLastIndexAreNotAnIndex) {
  GMDL_Obj * obj = load_text(
      "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "l 1 2   \n"
      "p 1 2   \t\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->line_count, 1u);
  EXPECT_EQ(obj->lines[0].count, 2u) << "a blank run became a third vertex";
  EXPECT_EQ(obj->point_count, 2u) << "a blank run became a third point";
  gmdl_obj_free(obj);
}

// `p` takes bare integers and nothing else, so a word is a format error -
// the arm that says so had never run.
TEST(ObjParse, APointNamingSomethingThatIsNotANumberIsRefused) {
  EXPECT_EQ(load_text_expecting_failure("v 0 0 0\np one\n"), GMDL_ERR_FORMAT);
  EXPECT_EQ(load_text_expecting_failure("v 0 0 0\np 1 two\n"), GMDL_ERR_FORMAT);
}

// A recorded statement's text has its trailing blanks removed, which the
// header promises and nothing checked. It matters because the text is handed
// to a caller as a filename or a command.
TEST(ObjParse, ARecordedStatementLosesItsTrailingBlanks) {
  GMDL_Obj * obj = load_text("call parts.obj   \t\ncsh -date  \n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->statement_count, 2u);
  EXPECT_STREQ(obj->statements[0].text, "parts.obj");
  EXPECT_STREQ(obj->statements[1].text, "-date");
  gmdl_obj_free(obj);
}

TEST(ObjLimits, StatementsUnderTheCapAreKept) {
  GMDL_Limits limits;
  memset(&limits, 0, sizeof(limits));
  limits.max_statements = 2;

  MemStream stream("call a\ncsh b\n");
  GMDL_Obj * obj = nullptr;
  ASSERT_EQ(gmdl_obj_load(stream.get(), &limits, nullptr, &obj), GMDL_OK);
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->statement_count, 2u);
  gmdl_obj_free(obj);
}

// --- Index range ------------------------------------------------------

// An index too large to represent must stay OUT of range, never wrap into it.
// "f 4294967297" resolves to 4294967296, whose low 32 bits are zero, so
// narrowing pointed the face at vertex 0 - a real vertex the file never
// named, which the consumer's range check would happily accept. Section 1
// puts range checking on the consumer, and that only works while an
// unrepresentable index cannot arrive disguised as a valid one.
//
// The assertion is the PROPERTY - "not a valid index for this model" - and
// not a particular sentinel, because the contract promises only the former.
TEST(ObjFace, AnIndexTooLargeToRepresentStaysOutOfRange) {
  for (const char * huge : {"4294967297", "8589934593", "-4294967297"}) {
    GMDL_Obj * obj =
        load_text(std::string("v 0 0 0\nv 1 0 0\nf ") + huge + " 1 2\n");
    ASSERT_NE(obj, nullptr) << huge;
    ASSERT_EQ(obj->face_count, 1u) << huge;
    const int32_t resolved = obj->faces[0].vertex[0];
    EXPECT_FALSE(resolved >= 0
        && static_cast<size_t>(resolved) < obj->vertex_count)
        << huge << " resolved to " << resolved
        << ", which is a vertex this file never named";
    gmdl_obj_free(obj);
  }
}

// Guard against over-correcting: an absurd but representable index is still
// read as written, and is simply out of range for a two-vertex model.
TEST(ObjFace, AnAbsurdButRepresentableIndexIsStillRead) {
  GMDL_Obj * obj = load_text("v 0 0 0\nv 1 0 0\nf 2147483647 1 2\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  EXPECT_EQ(obj->faces[0].vertex[0], 2147483646);
  gmdl_obj_free(obj);
}

// Writing a maximal index must not overflow on the way out.
//
// "f 2147483648" resolves to INT32_MAX, which 3.5 says to record rather than
// reject; the dumper then writes it 1-based, and "INT32_MAX + 1" in a plain
// int is undefined behaviour. This went unseen because UBSan recovers by
// default - it printed the diagnostic and the suite still exited 0 - so the
// Makefile now passes -fno-sanitize-recover=undefined and this test gives it
// something to catch.
TEST(ObjDump, AMaximalIndexIsWrittenWithoutOverflowing) {
  GMDL_Obj * obj = load_text("v 0 0 0\nf 2147483648 1 1\n");
  ASSERT_NE(obj, nullptr);
  ASSERT_EQ(obj->face_count, 1u);
  ASSERT_EQ(obj->faces[0].vertex[0], 2147483647);

  char * buffer = nullptr;
  size_t size = 0;
  FILE * sink = open_memstream(&buffer, &size);
  ASSERT_NE(sink, nullptr);
  EXPECT_EQ(gmdl_obj_dump(obj, sink), GMDL_OK);
  fclose(sink);
  EXPECT_NE(std::string(buffer, size).find("2147483648"), std::string::npos)
      << "the index did not survive the round trip";
  free(buffer);
  gmdl_obj_free(obj);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
