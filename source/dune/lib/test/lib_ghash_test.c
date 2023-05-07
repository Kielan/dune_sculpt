#include "testing/testing.h"

#define GHASH_INTERNAL_API

#include "lib_ghash.h"
#include "lib_rand.h"
#include "lib_utildefines.h"

#define TESTCASE_SIZE 10000

/* Only keeping this in case here, for now. */
#define PRINTF_GHASH_STATS(_gh) \
  { \
    double q, lf, var, pempty, poverloaded; \
    int bigb; \
    q = lib_ghash_calc_quality_ex((_gh), &lf, &var, &pempty, &poverloaded, &bigb); \
    printf( \
        "GHash stats (%d entries):\n\t" \
        "Quality (the lower the better): %f\n\tVariance (the lower the better): %f\n\tLoad: " \
        "%f\n\t" \
        "Empty buckets: %.2f%%\n\tOverloaded buckets: %.2f%% (biggest bucket: %d)\n", \
        lib_ghash_len(_gh), \
        q, \
        var, \
        lf, \
        pempty * 100.0, \
        poverloaded * 100.0, \
        bigb); \
  } \
  void(0)

/* NOTE: for pure-ghash testing, nature of the keys and data have absolutely no importance! So here
 * we just use mere random integers stored in pointers. */

static void init_keys(unsigned int keys[TESTCASE_SIZE], const int seed)
{
  RNG *rng = lib_rng_new(seed);
  unsigned int *k;
  int i;

  for (i = 0, k = keys; i < TESTCASE_SIZE;) {
    /* Risks of collision are low, but they do exist.
     * And we cannot use a GSet, since we test that here! */
    int j, t = lib_rng_get_uint(rng);
    for (j = i; j--;) {
      if (keys[j] == t) {
        continue;
      }
    }
    *k = t;
    i++;
    k++;
  }
  lib_rng_free(rng);
}

/* Here we simply insert and then lookup all keys, ensuring we do get back the expected stored
 * 'data'. */
TEST(ghash, InsertLookup)
{
  GHash *ghash = lib_ghash_new(lib_ghashutil_inthash_p, LIB_ghashutil_intcmp, __func__);
  unsigned int keys[TESTCASE_SIZE], *k;
  int i;

  init_keys(keys, 0);

  for (i = TESTCASE_SIZE, k = keys; i--; k++) {
    lib_ghash_insert(ghash, POINTER_FROM_UINT(*k), POINTER_FROM_UINT(*k));
  }

  EXPECT_EQ(LIB_ghash_len(ghash), TESTCASE_SIZE);

  for (i = TESTCASE_SIZE, k = keys; i--; k++) {
    void *v = LIB_ghash_lookup(ghash, POINTER_FROM_UINT(*k));
    EXPECT_EQ(POINTER_AS_UINT(v), *k);
  }

  lib_ghash_free(ghash, nullptr, nullptr);
}

/* Here we simply insert and then remove all keys, ensuring we do get an empty,
 * ghash that has not been shrunk. */
TEST(ghash, InsertRemove)
{
  GHash *ghash = lib_ghash_new(lib_ghashutil_inthash_p, LIB_ghashutil_intcmp, __func__);
  unsigned int keys[TESTCASE_SIZE], *k;
  int i, bkt_size;

  init_keys(keys, 10);

  for (i = TESTCASE_SIZE, k = keys; i--; k++) {
    lib_ghash_insert(ghash, POINTER_FROM_UINT(*k), POINTER_FROM_UINT(*k));
  }

  EXPECT_EQ(lib_ghash_len(ghash), TESTCASE_SIZE);
  bkt_size = lib_ghash_buckets_len(ghash);

  for (i = TESTCASE_SIZE, k = keys; i--; k++) {
    void *v = lib_ghash_popkey(ghash, POINTER_FROM_UINT(*k), nullptr);
    EXPECT_EQ(PTR_AS_UINT(v), *k);
  }

  EXPECT_EQ(lib_ghash_len(ghash), 0);
  EXPECT_EQ(lib_ghash_buckets_len(ghash), bkt_size);

  lib_ghash_free(ghash, nullptr, nullptr);
}

/* Same as above, but this time we allow ghash to shrink. */
TEST(ghash, InsertRemoveShrink)
{
  GHash *ghash = lib_ghash_new(lib_ghashutil_inthash_p, LIB_ghashutil_intcmp, __func__);
  unsigned int keys[TESTCASE_SIZE], *k;
  int i, bkt_size;

  lib_ghash_flag_set(ghash, GHASH_FLAG_ALLOW_SHRINK);
  init_keys(keys, 20);

  for (i = TESTCASE_SIZE, k = keys; i--; k++) {
    lib_ghash_insert(ghash, POINTER_FROM_UINT(*k), POINTER_FROM_UINT(*k));
  }

  EXPECT_EQ(lib_ghash_len(ghash), TESTCASE_SIZE);
  bkt_size = lib_ghash_buckets_len(ghash);

  for (i = TESTCASE_SIZE, k = keys; i--; k++) {
    void *v = lib_ghash_popkey(ghash, POINTER_FROM_UINT(*k), nullptr);
    EXPECT_EQ(PTR_AS_UINT(v), *k);
  }

  EXPECT_EQ(lib_ghash_len(ghash), 0);
  EXPECT_LT(lib_ghash_buckets_len(ghash), bkt_size);

  lib_ghash_free(ghash, nullptr, nullptr);
}

/* Check copy. */
TEST(ghash, Copy)
{
  GHash *ghash = lib_ghash_new(lib_ghashutil_inthash_p, LIB_ghashutil_intcmp, __func__);
  GHash *ghash_copy;
  unsigned int keys[TESTCASE_SIZE], *k;
  int i;

  init_keys(keys, 30);

  for (i = TESTCASE_SIZE, k = keys; i--; k++) {
    lib_ghash_insert(ghash, POINTER_FROM_UINT(*k), POINTER_FROM_UINT(*k));
  }

  EXPECT_EQ(lib_ghash_len(ghash), TESTCASE_SIZE);

  ghash_copy = lib_ghash_copy(ghash, nullptr, nullptr);

  EXPECT_EQ(lib_ghash_len(ghash_copy), TESTCASE_SIZE);
  EXPECT_EQ(lib_ghash_buckets_len(ghash_copy), LIB_ghash_buckets_len(ghash));

  for (i = TESTCASE_SIZE, k = keys; i--; k++) {
    void *v = lib_ghash_lookup(ghash_copy, POINTER_FROM_UINT(*k));
    EXPECT_EQ(PTR_AS_UINT(v), *k);
  }

  LIB_ghash_free(ghash, nullptr, nullptr);
  LIB_ghash_free(ghash_copy, nullptr, nullptr);
}

/* Check pop. */
TEST(ghash, Pop)
{
  GHash *ghash = lib_ghash_new(lib_ghashutil_inthash_p, LIB_ghashutil_intcmp, __func__);
  unsigned int keys[TESTCASE_SIZE], *k;
  int i;

  lib_ghash_flag_set(ghash, GHASH_FLAG_ALLOW_SHRINK);
  init_keys(keys, 30);

  for (i = TESTCASE_SIZE, k = keys; i--; k++) {
    lib_ghash_insert(ghash, PTR_FROM_UINT(*k), POINTER_FROM_UINT(*k));
  }

  EXPECT_EQ(LIB_ghash_len(ghash), TESTCASE_SIZE);

  GHashIterState pop_state = {0};

  for (i = TESTCASE_SIZE / 2; i--;) {
    void *k, *v;
    bool success = LIB_ghash_pop(ghash, &pop_state, &k, &v);
    EXPECT_EQ(k, v);
    EXPECT_TRUE(success);

    if (i % 2) {
      LIB_ghash_insert(ghash, POINTER_FROM_UINT(i * 4), POINTER_FROM_UINT(i * 4));
    }
  }

  EXPECT_EQ(LIB_ghash_len(ghash), (TESTCASE_SIZE - TESTCASE_SIZE / 2 + TESTCASE_SIZE / 4));

  {
    void *k, *v;
    while (LIB_ghash_pop(ghash, &pop_state, &k, &v)) {
      EXPECT_EQ(k, v);
    }
  }
  EXPECT_EQ(LIB_ghash_len(ghash), 0);

  LIB_ghash_free(ghash, nullptr, nullptr);
}
