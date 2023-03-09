#include "testing/testing.h"

#include "lib_utildefines.h"

#include "mem_guardedalloc.h"
#include "guardedalloc_test_base.h"

#define CHECK_ALIGNMENT(ptr, align) EXPECT_EQ((size_t)ptr % align, 0)

namespace {

void DoBasicAlignmentChecks(const int alignment)
{
  int *foo, *bar;

  foo = (int *)mem_mallocn_aligned(sizeof(int) * 10, alignment, "test");
  CHECK_ALIGNMENT(foo, alignment);

  bar = (int *)mem_dupallocn(foo);
  CHECK_ALIGNMENT(bar, alignment);
  mem_freen(bar);

  foo = (int *)mem_reallocn(foo, sizeof(int) * 5);
  CHECK_ALIGNMENT(foo, alignment);

  foo = (int *)mem_recallocn(foo, sizeof(int) * 5);
  CHECK_ALIGNMENT(foo, alignment);

  mem_freen(foo);
}

}  // namespace

TEST_F(LockFreeAllocatorTest, mem_mallocn_aligned)
{
  DoBasicAlignmentChecks(1);
  DoBasicAlignmentChecks(2);
  DoBasicAlignmentChecks(4);
  DoBasicAlignmentChecks(8);
  DoBasicAlignmentChecks(16);
  DoBasicAlignmentChecks(32);
  DoBasicAlignmentChecks(256);
  DoBasicAlignmentChecks(512);
}

TEST_F(GuardedAllocatorTest, MEM_mallocN_aligned)
{
  DoBasicAlignmentChecks(1);
  DoBasicAlignmentChecks(2);
  DoBasicAlignmentChecks(4);
  DoBasicAlignmentChecks(8);
  DoBasicAlignmentChecks(16);
  DoBasicAlignmentChecks(32);
  DoBasicAlignmentChecks(256);
  DoBasicAlignmentChecks(512);
}
