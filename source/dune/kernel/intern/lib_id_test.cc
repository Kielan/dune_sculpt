#include "testing/testing.h"

#include "MEM_guardedalloc.h"

#include "LIB_listbase.h"
#include "LIB_string.h"

#include "KERNEL_idtype.h"
#include "KERNEL_lib_id.h"
#include "KERNEL_main.h"

#include "structs_ID.h"
#include "structs_mesh_types.h"
#include "structs_object_types.h"

namespace dune::kernel::tests {

struct LibIDMainSortTestContext {
  Main *dunemain;
};

static void test_lib_id_main_sort_init(LibIDMainSortTestContext *ctx)
{
  KERNEL_idtype_init();
  ctx->bmain = KERNEL_main_new();
}

static void test_lib_id_main_sort_free(LibIDMainSortTestContext *ctx)
{
  KERNEL_main_free(ctx->bmain);
}

static void test_lib_id_main_sort_check_order(std::initializer_list<ID *> list)
{
  ID *prev_id = nullptr;
  for (ID *id : list) {
    EXPECT_EQ(id->prev, prev_id);
    if (prev_id != nullptr) {
      EXPECT_EQ(prev_id->next, id);
    }
    prev_id = id;
  }
  EXPECT_EQ(prev_id->next, nullptr);
}

TEST(lib_id_main_sort, local_ids_1)
{
  LibIDMainSortTestContext ctx = {nullptr};
  test_lib_id_main_sort_init(&ctx);
  EXPECT_TRUE(BLI_listbase_is_empty(&ctx.dunemain->libraries));

  ID *id_c = static_cast<ID *>(KE_id_new(ctx.bmain, ID_OB, "OB_C"));
  ID *id_a = static_cast<ID *>(KE_id_new(ctx.bmain, ID_OB, "OB_A"));
  ID *id_b = static_cast<ID *>(KE_id_new(ctx.bmain, ID_OB, "OB_B"));
  EXPECT_TRUE(ctx.dunemain->objects.first == id_a);
  EXPECT_TRUE(ctx.dunemain->objects.last == id_c);
  test_lib_id_main_sort_check_order({id_a, id_b, id_c});

  test_lib_id_main_sort_free(&ctx);
}

TEST(lib_id_main_sort, linked_ids_1)
{
  LibIDMainSortTestContext ctx = {nullptr};
  test_lib_id_main_sort_init(&ctx);
  EXPECT_TRUE(LIB_listbase_is_empty(&ctx.dunemain->libraries));

  Library *lib_a = static_cast<Library *>(KERNEL_id_new(ctx.dunemain, ID_LI, "LI_A"));
  Library *lib_b = static_cast<Library *>(BKE_id_new(ctx.dunemain, ID_LI, "LI_B"));
  ID *id_c = static_cast<ID *>(KERNEL_id_new(ctx.dunemain, ID_OB, "OB_C"));
  ID *id_a = static_cast<ID *>(KERNEL_id_new(ctx.dunemain, ID_OB, "OB_A"));
  ID *id_b = static_cast<ID *>(KERNEL_id_new(ctx.dunemain, ID_OB, "OB_B"));

  id_a->lib = lib_a;
  id_sort_by_name(&ctx.dunemain->objects, id_a, nullptr);
  id_b->lib = lib_a;
  id_sort_by_name(&ctx.dunemain->objects, id_b, nullptr);
  EXPECT_TRUE(ctx.dunemain->objects.first == id_c);
  EXPECT_TRUE(ctx.dunemain->objects.last == id_b);
  test_lib_id_main_sort_check_order({id_c, id_a, id_b});

  id_a->lib = lib_b;
  id_sort_by_name(&ctx.dunemain->objects, id_a, nullptr);
  EXPECT_TRUE(ctx.dunemain->objects.first == id_c);
  EXPECT_TRUE(ctx.dunemain->objects.last == id_a);
  test_lib_id_main_sort_check_order({id_c, id_b, id_a});

  id_b->lib = lib_b;
  id_sort_by_name(&ctx.dunemain->objects, id_b, nullptr);
  EXPECT_TRUE(ctx.dunemain->objects.first == id_c);
  EXPECT_TRUE(ctx.dunemain->objects.last == id_b);
  test_lib_id_main_sort_check_order({id_c, id_a, id_b});

  test_lib_id_main_sort_free(&ctx);
}

TEST(lib_id_main_unique_name, local_ids_1)
{
  LibIDMainSortTestContext ctx = {nullptr};
  test_lib_id_main_sort_init(&ctx);
  EXPECT_TRUE(LIB_listbase_is_empty(&ctx.dunemain->libraries));

  ID *id_c = static_cast<ID *>(KERNEL_id_new(ctx.dunemain, ID_OB, "OB_C"));
  ID *id_a = static_cast<ID *>(KERNEL_id_new(ctx.dunemain, ID_OB, "OB_A"));
  ID *id_b = static_cast<ID *>(KERNEL_id_new(ctx.dunemain, ID_OB, "OB_B"));
  test_lib_id_main_sort_check_order({id_a, id_b, id_c});

  LIB_strncpy(id_c->name, id_a->name, sizeof(id_c->name));
  KERNEL_id_new_name_validate(&ctx.dunemain->objects, id_c, nullptr, false);
  EXPECT_TRUE(strcmp(id_c->name + 2, "OB_A.001") == 0);
  EXPECT_TRUE(strcmp(id_a->name + 2, "OB_A") == 0);
  EXPECT_TRUE(ctx.dunemain->objects.first == id_a);
  EXPECT_TRUE(ctx.dunemain->objects.last == id_b);
  test_lib_id_main_sort_check_order({id_a, id_c, id_b});

  test_lib_id_main_sort_free(&ctx);
}

TEST(lib_id_main_unique_name, linked_ids_1)
{
  LibIDMainSortTestContext ctx = {nullptr};
  test_lib_id_main_sort_init(&ctx);
  EXPECT_TRUE(LIB_listbase_is_empty(&ctx.dunemain->libraries));

  Library *lib_a = static_cast<Library *>(KERNEL_id_new(ctx.dunemain, ID_LI, "LI_A"));
  Library *lib_b = static_cast<Library *>(KERNEL_id_new(ctx.dunemain, ID_LI, "LI_B"));
  ID *id_c = static_cast<ID *>(KE_id_new(ctx.dunemain, ID_OB, "OB_C"));
  ID *id_a = static_cast<ID *>(KE_id_new(ctx.dunemain, ID_OB, "OB_A"));
  ID *id_b = static_cast<ID *>(KE_id_new(ctx.dunemain, ID_OB, "OB_B"));

  id_a->lib = lib_a;
  id_sort_by_name(&ctx.dunemain->objects, id_a, nullptr);
  id_b->lib = lib_a;
  id_sort_by_name(&ctx.bmain->objects, id_b, nullptr);
  LIB_strncpy(id_b->name, id_a->name, sizeof(id_b->name));
  KERNEL_id_new_name_validate(&ctx.bmain->objects, id_b, nullptr, true);
  EXPECT_TRUE(strcmp(id_b->name + 2, "OB_A.001") == 0);
  EXPECT_TRUE(strcmp(id_a->name + 2, "OB_A") == 0);
  EXPECT_TRUE(ctx.dunemain->objects.first == id_c);
  EXPECT_TRUE(ctx.dunemain->objects.last == id_b);
  test_lib_id_main_sort_check_order({id_c, id_a, id_b});

  id_b->lib = lib_b;
  id_sort_by_name(&ctx.bmain->objects, id_b, nullptr);
  LIB_strncpy(id_b->name, id_a->name, sizeof(id_b->name));
  KERNEL_id_new_name_validate(&ctx.bmain->objects, id_b, nullptr, true);
  EXPECT_TRUE(strcmp(id_b->name + 2, "OB_A") == 0);
  EXPECT_TRUE(strcmp(id_a->name + 2, "OB_A") == 0);
  EXPECT_TRUE(ctx.bmain->objects.first == id_c);
  EXPECT_TRUE(ctx.bmain->objects.last == id_b);
  test_lib_id_main_sort_check_order({id_c, id_a, id_b});

  test_lib_id_main_sort_free(&ctx);
}

}  // namespace dune::kernel::tests
