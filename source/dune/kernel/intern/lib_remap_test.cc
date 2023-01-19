#include "testing/testing.h"

#include "LIB_utildefines.h"

#include "CLG_log.h"

#include "structs_mesh_types.h"
#include "structs_node_types.h"
#include "structs_object_types.h"
#include "structs_scene_types.h"

#include "API_define.h"

#include "KERNEL_appdir.h"
#include "KERNEL_context.h"
#include "KERNEL_global.h"
#include "KERNEL_idtype.h"
#include "KERNEL_lib_id.h"
#include "KERNEL_lib_remap.h"
#include "KERNEL_main.h"
#include "KERNEL_mesh.h"
#include "KERNEL_node.h"
#include "KERNEL_object.h"
#include "KERNEL_scene.h"

#include "IMB_imbuf.h"

#include "ED_node.h"

#include "MEM_guardedalloc.h"

namespace dune::kernel::tests {

class TestData {
 public:
  Main *dunemain = nullptr;
  struct duneContext *C = nullptr;

  virtual void setup()
  {
    if (dunemain == nullptr) {
      dunemain = KERNEL_main_new();
      G.main = dunemain;
    }

    if (C == nullptr) {
      C = CTX_create();
      CTX_data_main_set(C, dunemain);
    }
  }

  virtual void teardown()
  {
    if (dunemain != nullptr) {
      KERNEL_main_free(dunemain);
      dunemain = nullptr;
      G.main = nullptr;
    }

    if (C != nullptr) {
      CTX_free(C);
      C = nullptr;
    }
  }
};

class SceneTestData : public TestData {
 public:
  Scene *scene = nullptr;
  void setup() override
  {
    TestData::setup();
    scene = KERNEL_scene_add(dunemain, "IDRemapScene");
    CTX_data_scene_set(C, scene);
  }
};

class CompositorTestData : public SceneTestData {
 public:
  bNodeTree *compositor_nodetree = nullptr;
  void setup() override
  {
    SceneTestData::setup();
    ED_node_composit_default(C, scene);
    compositor_nodetree = scene->nodetree;
  }
};

class MeshTestData : public TestData {
 public:
  Mesh *mesh = nullptr;

  void setup() override
  {
    TestData::setup();
    mesh = KERNEL_mesh_add(dunemain, nullptr);
  }
};

class TwoMeshesTestData : public MeshTestData {
 public:
  Mesh *other_mesh = nullptr;

  void setup() override
  {
    MeshTestData::setup();
    other_mesh = KERNEL_mesh_add(dunemain, nullptr);
  }
};

class MeshObjectTestData : public MeshTestData {
 public:
  Object *object;
  void setup() override
  {
    MeshTestData::setup();

    object = KERNEL_object_add_only_object(dunemain, OB_MESH, nullptr);
    object->data = mesh;
  }
};

template<typename TestData> class Context {
 public:
  TestData test_data;

  Context()
  {
    CLG_init();
    KERNEL_idtype_init();
    A_init();
    KERNEL_node_system_init();
    KERNEL_appdir_init();
    IMB_init();

    test_data.setup();
  }

  ~Context()
  {
    test_data.teardown();

    KERNEL_node_system_exit();
    API_exit();
    IMB_exit();
    KERNEL_appdir_exit();
    CLG_exit();
  }
};

/* -------------------------------------------------------------------- */
/** Embedded IDs **/

TEST(lib_remap, embedded_ids_can_not_be_remapped)
{
  Context<CompositorTestData> context;
  bNodeTree *other_tree = static_cast<bNodeTree *>(BKE_id_new_nomain(ID_NT, nullptr));

  EXPECT_NE(context.test_data.scene, nullptr);
  EXPECT_NE(context.test_data.compositor_nodetree, nullptr);
  EXPECT_EQ(context.test_data.compositor_nodetree, context.test_data.scene->nodetree);

  KERNEL_libblock_remap(
      context.test_data.dunemain, context.test_data.compositor_nodetree, other_tree, 0);

  EXPECT_EQ(context.test_data.compositor_nodetree, context.test_data.scene->nodetree);
  EXPECT_NE(context.test_data.scene->nodetree, other_tree);

  KERNEL_id_free(nullptr, other_tree);
}

TEST(lib_remap, embedded_ids_can_not_be_deleted)
{
  Context<CompositorTestData> context;

  EXPECT_NE(context.test_data.scene, nullptr);
  EXPECT_NE(context.test_data.compositor_nodetree, nullptr);
  EXPECT_EQ(context.test_data.compositor_nodetree, context.test_data.scene->nodetree);

  KERNEL_libblock_remap(context.test_data.dunemain,
                     context.test_data.compositor_nodetree,
                     nullptr,
                     ID_REMAP_SKIP_NEVER_NULL_USAGE);

  EXPECT_EQ(context.test_data.compositor_nodetree, context.test_data.scene->nodetree);
  EXPECT_NE(context.test_data.scene->nodetree, nullptr);
}

/* -------------------------------------------------------------------- */
/** Remap to self **/

TEST(lib_remap, delete_when_remap_to_self_not_allowed)
{
  Context<TwoMeshesTestData> context;

  EXPECT_NE(context.test_data.mesh, nullptr);
  EXPECT_NE(context.test_data.other_mesh, nullptr);
  context.test_data.mesh->texcomesh = context.test_data.other_mesh;

  KERNEL_libblock_remap(
      context.test_data.dunemain, context.test_data.other_mesh, context.test_data.mesh, 0);

  EXPECT_EQ(context.test_data.mesh->texcomesh, nullptr);
}

/* -------------------------------------------------------------------- */
/** User Reference Counting **/

TEST(lib_remap, users_are_decreased_when_not_skipping_never_null)
{
  Context<MeshObjectTestData> context;

  EXPECT_NE(context.test_data.object, nullptr);
  EXPECT_EQ(context.test_data.object->data, context.test_data.mesh);
  EXPECT_EQ(context.test_data.object->id.tag & LIB_TAG_DOIT, 0);
  EXPECT_EQ(context.test_data.mesh->id.us, 1);

  /* This is an invalid situation, test case tests this in between value until we have a better
   * solution. */
  KERNEL_libblock_remap(context.test_data.dunemain, context.test_data.mesh, nullptr, 0);
  EXPECT_EQ(context.test_data.mesh->id.us, 0);
  EXPECT_EQ(context.test_data.object->data, context.test_data.mesh);
  EXPECT_NE(context.test_data.object->data, nullptr);
  EXPECT_EQ(context.test_data.object->id.tag & LIB_TAG_DOIT, 0);
}

TEST(lib_remap, users_are_same_when_skipping_never_null)
{
  Context<MeshObjectTestData> context;

  EXPECT_NE(context.test_data.object, nullptr);
  EXPECT_EQ(context.test_data.object->data, context.test_data.mesh);
  EXPECT_EQ(context.test_data.object->id.tag & LIB_TAG_DOIT, 0);
  EXPECT_EQ(context.test_data.mesh->id.us, 1);

  KERNEL_libblock_remap(
      context.test_data.dunemain, context.test_data.mesh, nullptr, ID_REMAP_SKIP_NEVER_NULL_USAGE);
  EXPECT_EQ(context.test_data.mesh->id.us, 1);
  EXPECT_EQ(context.test_data.object->data, context.test_data.mesh);
  EXPECT_NE(context.test_data.object->data, nullptr);
  EXPECT_EQ(context.test_data.object->id.tag & LIB_TAG_DOIT, 0);
}

/* -------------------------------------------------------------------- */
/** Never Null **/

TEST(lib_remap, do_not_delete_when_cannot_unset)
{
  Context<MeshObjectTestData> context;

  EXPECT_NE(context.test_data.object, nullptr);
  EXPECT_EQ(context.test_data.object->data, context.test_data.mesh);

  KERNEL_libblock_remap(
      context.test_data.dunemain, context.test_data.mesh, nullptr, ID_REMAP_SKIP_NEVER_NULL_USAGE);
  EXPECT_EQ(context.test_data.object->data, context.test_data.mesh);
  EXPECT_NE(context.test_data.object->data, nullptr);
}

TEST(lib_remap, force_never_null_usage)
{
  Context<MeshObjectTestData> context;

  EXPECT_NE(context.test_data.object, nullptr);
  EXPECT_EQ(context.test_data.object->data, context.test_data.mesh);

  KERNEL_libblock_remap(
      context.test_data.dunemain, context.test_data.mesh, nullptr, ID_REMAP_FORCE_NEVER_NULL_USAGE);
  EXPECT_EQ(context.test_data.object->data, nullptr);
}

TEST(lib_remap, never_null_usage_flag_not_requested_on_delete)
{
  Context<MeshObjectTestData> context;

  EXPECT_NE(context.test_data.object, nullptr);
  EXPECT_EQ(context.test_data.object->data, context.test_data.mesh);
  EXPECT_EQ(context.test_data.object->id.tag & LIB_TAG_DOIT, 0);

  /* Never null usage isn't requested so the flag should not be set. */
  KERNEL_libblock_remap(
      context.test_data.dunemain, context.test_data.mesh, nullptr, ID_REMAP_SKIP_NEVER_NULL_USAGE);
  EXPECT_EQ(context.test_data.object->data, context.test_data.mesh);
  EXPECT_NE(context.test_data.object->data, nullptr);
  EXPECT_EQ(context.test_data.object->id.tag & LIB_TAG_DOIT, 0);
}

TEST(lib_remap, never_null_usage_flag_requested_on_delete)
{
  Context<MeshObjectTestData> context;

  EXPECT_NE(context.test_data.object, nullptr);
  EXPECT_EQ(context.test_data.object->data, context.test_data.mesh);
  EXPECT_EQ(context.test_data.object->id.tag & LIB_TAG_DOIT, 0);

  /* Never null usage is requested so the flag should be set. */
  KERNEL_libblock_remap(context.test_data.dunemain,
                     context.test_data.mesh,
                     nullptr,
                     ID_REMAP_SKIP_NEVER_NULL_USAGE | ID_REMAP_FLAG_NEVER_NULL_USAGE);
  EXPECT_EQ(context.test_data.object->data, context.test_data.mesh);
  EXPECT_NE(context.test_data.object->data, nullptr);
  EXPECT_EQ(context.test_data.object->id.tag & LIB_TAG_DOIT, LIB_TAG_DOIT);
}

TEST(lib_remap, never_null_usage_flag_not_requested_on_remap)
{
  Context<MeshObjectTestData> context;
  Mesh *other_mesh = KERNEL_mesh_add(context.test_data.bmain, nullptr);

  EXPECT_NE(context.test_data.object, nullptr);
  EXPECT_EQ(context.test_data.object->data, context.test_data.mesh);
  EXPECT_EQ(context.test_data.object->id.tag & LIB_TAG_DOIT, 0);

  /* Never null usage isn't requested so the flag should not be set. */
  KERNEL_libblock_remap(
      context.test_data.dunemain, context.test_data.mesh, other_mesh, ID_REMAP_SKIP_NEVER_NULL_USAGE);
  EXPECT_EQ(context.test_data.object->data, other_mesh);
  EXPECT_EQ(context.test_data.object->id.tag & LIB_TAG_DOIT, 0);
}

TEST(lib_remap, never_null_usage_flag_requested_on_remap)
{
  Context<MeshObjectTestData> context;
  Mesh *other_mesh = KERNEL_mesh_add(context.test_data.dunemain, nullptr);

  EXPECT_NE(context.test_data.object, nullptr);
  EXPECT_EQ(context.test_data.object->data, context.test_data.mesh);
  EXPECT_EQ(context.test_data.object->id.tag & LIB_TAG_DOIT, 0);

  /* Never null usage is requested so the flag should be set. */
  KERNEL_libblock_remap(context.test_data.dunemain,
                     context.test_data.mesh,
                     other_mesh,
                     ID_REMAP_SKIP_NEVER_NULL_USAGE | ID_REMAP_FLAG_NEVER_NULL_USAGE);
  EXPECT_EQ(context.test_data.object->data, other_mesh);
  EXPECT_EQ(context.test_data.object->id.tag & LIB_TAG_DOIT, LIB_TAG_DOIT);
}

}  // namespace dune::kernel::tests
