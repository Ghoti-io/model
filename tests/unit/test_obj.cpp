/**
 * @file
 *
 * Unit tests for the Wavefront OBJ parser.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <string>

using gmdltest::data;
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
  EXPECT_EQ(load_text_expecting_failure("vt 1\n"), GMDL_ERR_FORMAT);
  EXPECT_EQ(load_text_expecting_failure("vn 1 2\n"), GMDL_ERR_FORMAT);
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

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
