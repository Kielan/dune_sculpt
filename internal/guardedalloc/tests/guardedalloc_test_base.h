#ifndef __GUARDEDALLOC_TEST_UTIL_H__
#define __GUARDEDALLOC_TEST_UTIL_H__

#include "testing/testing.h"

#include "mem_guardedalloc.h"

class LockFreeAllocatorTest : public ::testing::Test {
 protected:
  virtual void SetUp()
  {
    mem_use_lockfree_allocator();
  }
};

class GuardedAllocatorTest : public ::testing::Test {
 protected:
  virtual void SetUp()
  {
    mem_use_guarded_allocator();
  }
};

#endif  // __GUARDEDALLOC_TEST_UTIL_H__
