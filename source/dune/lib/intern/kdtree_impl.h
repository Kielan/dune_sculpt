#include "mem_guardedalloc.h"

#include "lib_kdtree_impl.h"
#include "lib_math_base.h"
#include "lib_strict_flags.h"
#include "lib_utildefines.h"

#include <string.h>

#define _LIB_KDTREE_CONCAT_AUX(MACRO_ARG1, MACRO_ARG2) MACRO_ARG1##MACRO_ARG2
#define _LIB_KDTREE_CONCAT(MACRO_ARG1, MACRO_ARG2) _BLI_KDTREE_CONCAT_AUX(MACRO_ARG1, MACRO_ARG2)
#define LIB_kdtree_nd_(id) _LIB_KDTREE_CONCAT(KDTREE_PREFIX_ID, _##id)

typedef struct KDTreeNode_head {
  uint left, right;
  float co[KD_DIMS];
  int index;
} KDTreeNode_head;

typedef struct KDTreeNode {
  uint left, right;
  float co[KD_DIMS];
  int index;
  uint d; /* range is only (0..KD_DIMS - 1) */
} KDTreeNode;

struct KDTree {
  KDTreeNode *nodes;
  uint nodes_len;
  uint root;
  int max_node_index;
#ifndef NDEBUG
  bool is_balanced;        /* ensure we call balance first */
  uint nodes_len_capacity; /* max size of the tree */
#endif
};

#define KD_STACK_INIT 100     /* initial size for array (on the stack) */
#define KD_NEAR_ALLOC_INC 100 /* alloc increment for collecting nearest */
#define KD_FOUND_ALLOC_INC 50 /* alloc increment for collecting nearest */

#define KD_NODE_UNSET ((uint)-1)

/* When set we know all vas are unbalanced,
 * otherwise clear them when re-balancing: see #62210. */
#define KD_NODE_ROOT_IS_INIT ((uint)-2)

/* Local Math API */
static void copy_vn_vn(float v0[KD_DIMS], const float v1[KD_DIMS])
{
  for (uint j = 0; j < KD_DIMS; j++) {
    v0[j] = v1[j];
  }
}

static float len_squared_vnvn(const float v0[KD_DIMS], const float v1[KD_DIMS])
{
  float d = 0.0f;
  for (uint j = 0; j < KD_DIMS; j++) {
    d += square_f(v0[j] - v1[j]);
  }
  return d;
}

static float len_squared_vnvn_cb(const float co_kdtree[KD_DIMS],
                                 const float co_search[KD_DIMS],
                                 const void *UNUSED(user_data))
{
  return len_squared_vnvn(co_kdtree, co_search);
}

/* Creates or free a kdtree */
KDTree *lib_kdtree_nd_(new)(uint nodes_len_capacity)
{
  KDTree *tree;

  tree = MEM_mallocN(sizeof(KDTree), "KDTree");
  tree->nodes = MEM_mallocN(sizeof(KDTreeNode) * nodes_len_capacity, "KDTreeNode");
  tree->nodes_len = 0;
  tree->root = KD_NODE_ROOT_IS_INIT;
  tree->max_node_index = -1;

#ifndef NDEBUG
  tree->is_balanced = false;
  tree->nodes_len_capacity = nodes_len_capacity;
#endif

  return tree;
}

void lib_kdtree_nd_(free)(KDTree *tree)
{
  if (tree) {
    mem_free(tree->nodes);
    mem_free(tree);
  }
}

/* Construction: first insert points, then call balance. Normal is optional. */
void lib_kdtree_nd_(insert)(KDTree *tree, int index, const float co[KD_DIMS])
{
  KDTreeNode *node = &tree->nodes[tree->nodes_len++];

#ifndef NDEBUG
  lib_assert(tree->nodes_len <= tree->nodes_len_capacity);
#endif

  /* Array isn't calloc'd,
   * need to init all struct mems */
  node->left = node->right = KD_NODE_UNSET;
  copy_vn_vn(node->co, co);
  node->index = index;
  node->d = 0;
  tree->max_node_index = MAX2(tree->max_node_index, index);

#ifndef NDEBUG
  tree->is_balanced = false;
#endif
}

static uint kdtree_balance(KDTreeNode *nodes, uint nodes_len, uint axis, const uint ofs)
{
  KDTreeNode *node;
  float co;
  uint left, right, median, i, j;

  if (nodes_len <= 0) {
    return KD_NODE_UNSET;
  }
  else if (nodes_len == 1) {
    return 0 + ofs;
  }

  /* Quick-sort style sorting around median. */
  left = 0;
  right = nodes_len - 1;
  median = nodes_len / 2;

  while (right > left) {
    co = nodes[right].co[axis];
    i = left - 1;
    j = right;

    while (1) {
      while (nodes[++i].co[axis] < co) { /* pass */
      }
      while (nodes[--j].co[axis] > co && j > left) { /* pass */
      }

      if (i >= j) {
        break;
      }

      SWAP(KDTreeNode_head, *(KDTreeNode_head *)&nodes[i], *(KDTreeNode_head *)&nodes[j]);
    }

    SWAP(KDTreeNode_head, *(KDTreeNode_head *)&nodes[i], *(KDTreeNode_head *)&nodes[right]);
    if (i >= median) {
      right = i - 1;
    }
    if (i <= median) {
      left = i + 1;
    }
  }

  /* Set node and sort sub-nodes. */
  node = &nodes[median];
  node->d = axis;
  axis = (axis + 1) % KD_DIMS;
  node->left = kdtree_balance(nodes, median, axis, ofs);
  node->right = kdtree_balance(
      nodes + median + 1, (nodes_len - (median + 1)), axis, (median + 1) + ofs);

  return median + ofs;
}

void lib_kdtree_nd_(balance)(KDTree *tree)
{
  if (tree->root != KD_NODE_ROOT_IS_INIT) {
    for (uint i = 0; i < tree->nodes_len; i++) {
      tree->nodes[i].left = KD_NODE_UNSET;
      tree->nodes[i].right = KD_NODE_UNSET;
    }
  }

  tree->root = kdtree_balance(tree->nodes, tree->nodes_len, 0, 0);

#ifndef NDEBUG
  tree->is_balanced = true;
#endif
}

static uint *realloc_nodes(uint *stack, uint *stack_len_capacity, const bool is_alloc)
{
  uint *stack_new = mem_malloc((*stack_len_capacity + KD_NEAR_ALLOC_INC) * sizeof(uint),
                                "KDTree.treestack");
  memcpy(stack_new, stack, *stack_len_capacity * sizeof(uint));
  // memset(stack_new + *stack_len_capacity, 0, sizeof(uint) * KD_NEAR_ALLOC_INC);
  if (is_alloc) {
    mem_free(stack);
  }
  *stack_len_capacity += KD_NEAR_ALLOC_INC;
  return stack_new;
}

/* Find nearest returns index, and -1 if no node is found. */
int lib_kdtree_nd_(find_nearest)(const KDTree *tree,
                                 const float co[KD_DIMS],
                                 KDTreeNearest *r_nearest)
{
  const KDTreeNode *nodes = tree->nodes;
  const KDTreeNode *root, *min_node;
  uint *stack, stack_default[KD_STACK_INIT];
  float min_dist, cur_dist;
  uint stack_len_capacity, cur = 0;

#ifndef NDEBUG
  lib_assert(tree->is_balanced == true);
#endif

  if (UNLIKELY(tree->root == KD_NODE_UNSET)) {
    return -1;
  }

  stack = stack_default;
  stack_len_capacity = KD_STACK_INIT;

  root = &nodes[tree->root];
  min_node = root;
  min_dist = len_squared_vnvn(root->co, co);

  if (co[root->d] < root->co[root->d]) {
    if (root->right != KD_NODE_UNSET) {
      stack[cur++] = root->right;
    }
    if (root->left != KD_NODE_UNSET) {
      stack[cur++] = root->left;
    }
  }
  else {
    if (root->left != KD_NODE_UNSET) {
      stack[cur++] = root->left;
    }
    if (root->right != KD_NODE_UNSET) {
      stack[cur++] = root->right;
    }
  }

  while (cur--) {
    const KDTreeNode *node = &nodes[stack[cur]];

    cur_dist = node->co[node->d] - co[node->d];

    if (cur_dist < 0.0f) {
      cur_dist = -cur_dist * cur_dist;

      if (-cur_dist < min_dist) {
        cur_dist = len_squared_vnvn(node->co, co);
        if (cur_dist < min_dist) {
          min_dist = cur_dist;
          min_node = node;
        }
        if (node->left != KD_NODE_UNSET) {
          stack[cur++] = node->left;
        }
      }
      if (node->right != KD_NODE_UNSET) {
        stack[cur++] = node->right;
      }
    }
    else {
      cur_dist = cur_dist * cur_dist;

      if (cur_dist < min_dist) {
        cur_dist = len_squared_vnvn(node->co, co);
        if (cur_dist < min_dist) {
          min_dist = cur_dist;
          min_node = node;
        }
        if (node->right != KD_NODE_UNSET) {
          stack[cur++] = node->right;
        }
      }
      if (node->left != KD_NODE_UNSET) {
        stack[cur++] = node->left;
      }
    }
    if (UNLIKELY(cur + KD_DIMS > stack_len_capacity)) {
      stack = realloc_nodes(stack, &stack_len_capacity, stack_default != stack);
    }
  }

  if (r_nearest) {
    r_nearest->index = min_node->index;
    r_nearest->dist = sqrtf(min_dist);
    copy_vn_vn(r_nearest->co, min_node->co);
  }

  if (stack != stack_default) {
    mem_free(stack);
  }

  return min_node->index;
}

/* A v. of lib_kdtree_3d_find_nearest which runs a cb
 * to filter out vals.
 * param filter_cb: Filter find results,
 * Return codes: (1: accept, 0: skip, -1: immediate exit). */
int lib_kdtree_nd_(find_nearest_cb)(
    const KDTree *tree,
    const float co[KD_DIMS],
    int (*filter_cb)(void *user_data, int index, const float co[KD_DIMS], float dist_sq),
    void *user_data,
    KDTreeNearest *r_nearest)
{
  const KDTreeNode *nodes = tree->nodes;
  const KDTreeNode *min_node = NULL;

  uint *stack, stack_default[KD_STACK_INIT];
  float min_dist = FLT_MAX, cur_dist;
  uint stack_len_capacity, cur = 0;

#ifndef NDEBUG
  lib_assert(tree->is_balanced == true);
#endif

  if (UNLIKELY(tree->root == KD_NODE_UNSET)) {
    return -1;
  }

  stack = stack_default;
  stack_len_capacity = ARRAY_SIZE(stack_default);

#define NODE_TEST_NEAREST(node) \
  { \
    const float dist_sq = len_squared_vnvn((node)->co, co); \
    if (dist_sq < min_dist) { \
      const int result = filter_cb(user_data, (node)->index, (node)->co, dist_sq); \
      if (result == 1) { \
        min_dist = dist_sq; \
        min_node = node; \
      } \
      else if (result == 0) { \
        /* pass */ \
      } \
      else { \
        lib_assert(result == -1); \
        goto finally; \
      } \
    } \
  } \
  ((void)0)

  stack[cur++] = tree->root;

  while (cur--) {
    const KDTreeNode *node = &nodes[stack[cur]];

    cur_dist = node->co[node->d] - co[node->d];

    if (cur_dist < 0.0f) {
      cur_dist = -cur_dist * cur_dist;

      if (-cur_dist < min_dist) {
        NODE_TEST_NEAREST(node);

        if (node->left != KD_NODE_UNSET) {
          stack[cur++] = node->left;
        }
      }
      if (node->right != KD_NODE_UNSET) {
        stack[cur++] = node->right;
      }
    }
    else {
      cur_dist = cur_dist * cur_dist;

      if (cur_dist < min_dist) {
        NODE_TEST_NEAREST(node);

        if (node->right != KD_NODE_UNSET) {
          stack[cur++] = node->right;
        }
      }
      if (node->left != KD_NODE_UNSET) {
        stack[cur++] = node->left;
      }
    }
    if (UNLIKELY(cur + KD_DIMS > stack_len_capacity)) {
      stack = realloc_nodes(stack, &stack_len_capacity, stack_default != stack);
    }
  }

#undef NODE_TEST_NEAREST

finally:
  if (stack != stack_default) {
    mem_free(stack);
  }

  if (min_node) {
    if (r_nearest) {
      r_nearest->index = min_node->index;
      r_nearest->dist = sqrtf(min_dist);
      copy_vn_vn(r_nearest->co, min_node->co);
    }

    return min_node->index;
  }
  else {
    return -1;
  }
}

static void nearest_ordered_insert(KDTreeNearest *nearest,
                                   uint *nearest_len,
                                   const uint nearest_len_capacity,
                                   const int index,
                                   const float dist,
                                   const float co[KD_DIMS])
{
  uint i;

  if (*nearest_len < nearest_len_capacity) {
    (*nearest_len)++;
  }

  for (i = *nearest_len - 1; i > 0; i--) {
    if (dist >= nearest[i - 1].dist) {
      break;
    }
    else {
      nearest[i] = nearest[i - 1];
    }
  }

  nearest[i].index = index;
  nearest[i].dist = dist;
  copy_vn_vn(nearest[i].co, co);
}

/* Find nearest_len_capacity nearest returns num of points found, w results in nearest.
 * param r_nearest: An arr of nearest, sized at least nearest_len_capacity. */
int lib_kdtree_nd_(find_nearest_n_with_len_squared_cb)(
    const KDTree *tree,
    const float co[KD_DIMS],
    KDTreeNearest r_nearest[],
    const uint nearest_len_capacity,
    float (*len_sq_fn)(const float co_search[KD_DIMS],
                       const float co_test[KD_DIMS],
                       const void *user_data),
    const void *user_data)
{
  const KDTreeNode *nodes = tree->nodes;
  const KDTreeNode *root;
  uint *stack, stack_default[KD_STACK_INIT];
  float cur_dist;
  uint stack_len_capacity, cur = 0;
  uint i, nearest_len = 0;

#ifndef NDEBUG
  lib_assert(tree->is_balanced == true);
#endif

  if (UNLIKELY((tree->root == KD_NODE_UNSET) || nearest_len_capacity == 0)) {
    return 0;
  }

  if (len_sq_fn == NULL) {
    len_sq_fn = len_squared_vnvn_cb;
    lib_assert(user_data == NULL);
  }

  stack = stack_default;
  stack_len_capacity = ARRAY_SIZE(stack_default);

  root = &nodes[tree->root];

  cur_dist = len_sq_fn(co, root->co, user_data);
  nearest_ordered_insert(
      r_nearest, &nearest_len, nearest_len_capacity, root->index, cur_dist, root->co);

  if (co[root->d] < root->co[root->d]) {
    if (root->right != KD_NODE_UNSET) {
      stack[cur++] = root->right;
    }
    if (root->left != KD_NODE_UNSET) {
      stack[cur++] = root->left;
    }
  }
  else {
    if (root->left != KD_NODE_UNSET) {
      stack[cur++] = root->left;
    }
    if (root->right != KD_NODE_UNSET) {
      stack[cur++] = root->right;
    }
  }

  while (cur--) {
    const KDTreeNode *node = &nodes[stack[cur]];

    cur_dist = node->co[node->d] - co[node->d];

    if (cur_dist < 0.0f) {
      cur_dist = -cur_dist * cur_dist;

      if (nearest_len < nearest_len_capacity || -cur_dist < r_nearest[nearest_len - 1].dist) {
        cur_dist = len_sq_fn(co, node->co, user_data);

        if (nearest_len < nearest_len_capacity || cur_dist < r_nearest[nearest_len - 1].dist) {
          nearest_ordered_insert(
              r_nearest, &nearest_len, nearest_len_capacity, node->index, cur_dist, node->co);
        }

        if (node->left != KD_NODE_UNSET) {
          stack[cur++] = node->left;
        }
      }
      if (node->right != KD_NODE_UNSET) {
        stack[cur++] = node->right;
      }
    }
    else {
      cur_dist = cur_dist * cur_dist;

      if (nearest_len < nearest_len_capacity || cur_dist < r_nearest[nearest_len - 1].dist) {
        cur_dist = len_sq_fn(co, node->co, user_data);
        if (nearest_len < nearest_len_capacity || cur_dist < r_nearest[nearest_len - 1].dist) {
          nearest_ordered_insert(
              r_nearest, &nearest_len, nearest_len_capacity, node->index, cur_dist, node->co);
        }

        if (node->right != KD_NODE_UNSET) {
          stack[cur++] = node->right;
        }
      }
      if (node->left != KD_NODE_UNSET) {
        stack[cur++] = node->left;
      }
    }
    if (UNLIKELY(cur + KD_DIMS > stack_len_capacity)) {
      stack = realloc_nodes(stack, &stack_len_capacity, stack_default != stack);
    }
  }

  for (i = 0; i < nearest_len; i++) {
    r_nearest[i].dist = sqrtf(r_nearest[i].dist);
  }

  if (stack != stack_default) {
    mem_free(stack);
  }

  return (int)nearest_len;
}

int lib_kdtree_nd_(find_nearest_n)(const KDTree *tree,
                                   const float co[KD_DIMS],
                                   KDTreeNearest r_nearest[],
                                   uint nearest_len_capacity)
{
  return lib_kdtree_nd_(find_nearest_n_with_len_squared_cb)(
      tree, co, r_nearest, nearest_len_capacity, NULL, NULL);
}

static int nearest_cmp_dist(const void *a, const void *b)
{
  const KDTreeNearest *kda = a;
  const KDTreeNearest *kdb = b;

  if (kda->dist < kdb->dist) {
    return -1;
  }
  else if (kda->dist > kdb->dist) {
    return 1;
  }
  else {
    return 0;
  }
}
static void nearest_add_in_range(KDTreeNearest **r_nearest,
                                 uint nearest_index,
                                 uint *nearest_len_capacity,
                                 const int index,
                                 const float dist,
                                 const float co[KD_DIMS])
{
  KDTreeNearest *to;

  if (UNLIKELY(nearest_index >= *nearest_len_capacity)) {
    *r_nearest = mem_realloc_id(
        *r_nearest, (*nearest_len_capacity += KD_FOUND_ALLOC_INC) * sizeof(KDTreeNode), __func__);
  }

  to = (*r_nearest) + nearest_index;

  to->index = index;
  to->dist = sqrtf(dist);
  copy_vn_vn(to->co, co);
}

/* Range search returns num of points nearest_len, with results in nearest
 * param r_nearest: Alloc'd array of nearest nearest_len (caller is responsible for freeing). */
int lib_kdtree_nd_(range_search_with_len_squared_cb)(
    const KDTree *tree,
    const float co[KD_DIMS],
    KDTreeNearest **r_nearest,
    const float range,
    float (*len_sq_fn)(const float co_search[KD_DIMS],
                       const float co_test[KD_DIMS],
                       const void *user_data),
    const void *user_data)
{
  const KDTreeNode *nodes = tree->nodes;
  uint *stack, stack_default[KD_STACK_INIT];
  KDTreeNearest *nearest = NULL;
  const float range_sq = range * range;
  float dist_sq;
  uint stack_len_capacity, cur = 0;
  uint nearest_len = 0, nearest_len_capacity = 0;

#ifndef NDEBUG
  lib_assert(tree->is_balanced == true);
#endif

  if (UNLIKELY(tree->root == KD_NODE_UNSET)) {
    return 0;
  }

  if (len_sq_fn == NULL) {
    len_sq_fn = len_squared_vnvn_cb;
    lib_assert(user_data == NULL);
  }

  stack = stack_default;
  stack_len_capacity = ARRAY_SIZE(stack_default);

  stack[cur++] = tree->root;

  while (cur--) {
    const KDTreeNode *node = &nodes[stack[cur]];

    if (co[node->d] + range < node->co[node->d]) {
      if (node->left != KD_NODE_UNSET) {
        stack[cur++] = node->left;
      }
    }
    else if (co[node->d] - range > node->co[node->d]) {
      if (node->right != KD_NODE_UNSET) {
        stack[cur++] = node->right;
      }
    }
    else {
      dist_sq = len_sq_fn(co, node->co, user_data);
      if (dist_sq <= range_sq) {
        nearest_add_in_range(
            &nearest, nearest_len++, &nearest_len_capacity, node->index, dist_sq, node->co);
      }

      if (node->left != KD_NODE_UNSET) {
        stack[cur++] = node->left;
      }
      if (node->right != KD_NODE_UNSET) {
        stack[cur++] = node->right;
      }
    }

    if (UNLIKELY(cur + KD_DIMS > stack_len_capacity)) {
      stack = realloc_nodes(stack, &stack_len_capacity, stack_default != stack);
    }
  }

  if (stack != stack_default) {
    mem_free(stack);
  }

  if (nearest_len) {
    qsort(nearest, nearest_len, sizeof(KDTreeNearest), nearest_cmp_dist);
  }

  *r_nearest = nearest;

  return (int)nearest_len;
}

int lib_kdtree_nd_(range_search)(const KDTree *tree,
                                 const float co[KD_DIMS],
                                 KDTreeNearest **r_nearest,
                                 float range)
{
  return lib_kdtree_nd_(range_search_with_len_squared_cb)(tree, co, r_nearest, range, NULL, NULL);
}

/* A version of lib_kdtree_3d_range_search which runs a cb
 * instead of alloc an array.
 * param search_cb: Called for every node found in range,
 * false return val performs an early exit.
 * the order of calls isn't sorted based on distance. */
void lib_kdtree_nd_(range_search_cb)(
    const KDTree *tree,
    const float co[KD_DIMS],
    float range,
    bool (*search_cb)(void *user_data, int index, const float co[KD_DIMS], float dist_sq),
    void *user_data)
{
  const KDTreeNode *nodes = tree->nodes;

  uint *stack, stack_default[KD_STACK_INIT];
  float range_sq = range * range, dist_sq;
  uint stack_len_capacity, cur = 0;

#ifndef NDEBUG
  lib_assert(tree->is_balanced == true);
#endif

  if (UNLIKELY(tree->root == KD_NODE_UNSET)) {
    return;
  }

  stack = stack_default;
  stack_len_capacity = ARRAY_SIZE(stack_default);

  stack[cur++] = tree->root;

  while (cur--) {
    const KDTreeNode *node = &nodes[stack[cur]];

    if (co[node->d] + range < node->co[node->d]) {
      if (node->left != KD_NODE_UNSET) {
        stack[cur++] = node->left;
      }
    }
    else if (co[node->d] - range > node->co[node->d]) {
      if (node->right != KD_NODE_UNSET) {
        stack[cur++] = node->right;
      }
    }
    else {
      dist_sq = len_squared_vnvn(node->co, co);
      if (dist_sq <= range_sq) {
        if (search_cb(user_data, node->index, node->co, dist_sq) == false) {
          goto finally;
        }
      }

      if (node->left != KD_NODE_UNSET) {
        stack[cur++] = node->left;
      }
      if (node->right != KD_NODE_UNSET) {
        stack[cur++] = node->right;
      }
    }

    if (UNLIKELY(cur + KD_DIMS > stack_len_capacity)) {
      stack = realloc_nodes(stack, &stack_len_capacity, stack_default != stack);
    }
  }

finally:
  if (stack != stack_default) {
    mem_free(stack);
  }
}

/* Use when: want to loop over nodes ordered by index.
 * Requires indices to be aligned w nodes. */
static int *kdtree_order(const KDTree *tree)
{
  const KDTreeNode *nodes = tree->nodes;
  const size_t bytes_num = sizeof(int) * (size_t)(tree->max_node_index + 1);
  int *order = mem_malloc(bytes_num, __func__);
  memset(order, -1, bytes_num);
  for (uint i = 0; i < tree->nodes_len; i++) {
    order[nodes[i].index] = (int)i;
  }
  return order;
}

/* lib_kdtree_3d_calc_dups_fast */
struct DeDupParams {
  /* Static */
  const KDTreeNode *nodes;
  float range;
  float range_sq;
  int *dups;
  int *dups_found;

  /* Per Search */
  float search_co[KD_DIMS];
  int search;
};

static void dedup_recursive(const struct DeDupParams *p, uint i)
{
  const KDTreeNode *node = &p->nodes[i];
  if (p->search_co[node->d] + p->range <= node->co[node->d]) {
    if (node->left != KD_NODE_UNSET) {
      dedup_recursive(p, node->left);
    }
  }
  else if (p->search_co[node->d] - p->range >= node->co[node->d]) {
    if (node->right != KD_NODE_UNSET) {
      dedup_recursive(p, node->right);
    }
  }
  else {
    if ((p->search != node->index) && (p->duplicates[node->index] == -1)) {
      if (len_squared_vnvn(node->co, p->search_co) <= p->range_sq) {
        p->dups[node->index] = (int)p->search;
        *p->dups_found += 1;
      }
    }
    if (node->left != KD_NODE_UNSET) {
      dedup_recursive(p, node->left);
    }
    if (node->right != KD_NODE_UNSET) {
      dedup_recursive(p, node->right);
    }
  }
}

/* Find dup points in range.
 * Favors speed over quality since it doesn't find the best target vert for merging.
 * Nodes are looped over, dups are added when found.
 * Nevertheless results are predictable.
 *
 * param range: Coords in this range are candidates to be merged.
 * param use_index_order: Loop over the coords ordered by KDTreeNode.index
 * At the expense of some performance, this ensures the layout of the tree doesn't influence
 * the iter order.
 * param dups: An array of int's the length of KDTree.nodes_len
 * Vals init to -1 are candidates to me merged.
 * Setting the index to its own position in the array prevents it from being touched,
 * although it can still be used as a target.
 * returns The num of merges found (includes any merges alrdy in the dups array).
 *
 * Merging is always a single step (target indices won't be marked for merging). */
int lib_kdtree_nd_(calc_dups_fast)(const KDTree *tree,
                                   const float range,
                                   bool use_index_order,
                                   int *dups)
{
  int found = 0;
  struct DeDupParams p = {
      .nodes = tree->nodes,
      .range = range,
      .range_sq = square_f(range),
      .dups = dups,
      .dups_found = &found,
  };

  if (use_index_order) {
    int *order = kdtree_order(tree);
    for (int i = 0; i < tree->max_node_index + 1; i++) {
      const int node_index = order[i];
      if (node_index == -1) {
        continue;
      }
      const int index = i;
      if (ELEM(dups[index], -1, index)) {
        p.search = index;
        copy_vn_vn(p.search_co, tree->nodes[node_index].co);
        int found_prev = found;
        dedup_recursive(&p, tree->root);
        if (found != found_prev) {
          /* Prevent chains of doubles. */
          dups[index] = index;
        }
      }
    }
    mem_free(order);
  }
  else {
    for (uint i = 0; i < tree->nodes_len; i++) {
      const uint node_index = i;
      const int index = p.nodes[node_index].index;
      if (ELEM(dups[index], -1, index)) {
        p.search = index;
        copy_vn_vn(p.search_co, tree->nodes[node_index].co);
        int found_prev = found;
        dedup_recursive(&p, tree->root);
        if (found != found_prev) {
          /* Prevent chains of doubles. */
          dups[index] = index;
        }
      }
    }
  }
  return found;
}

/* lib_kdtree_3d_dedup */
static int kdtree_cmp_bool(const bool a, const bool b)
{
  if (a == b) {
    return 0;
  }
  return b ? -1 : 1;
}

static int kdtree_node_cmp_dedup(const void *n0_p, const void *n1_p)
{
  const KDTreeNode *n0 = n0_p;
  const KDTreeNode *n1 = n1_p;
  for (uint j = 0; j < KD_DIMS; j++) {
    if (n0->co[j] < n1->co[j]) {
      return -1;
    }
    else if (n0->co[j] > n1->co[j]) {
      return 1;
    }
  }

  if (n0->d != KD_DIMS && n1->d != KD_DIMS) {
    /* Two nodes share identical `co`
     * Both are still valid.
     * Cast away `const` and tag one of them as invalid. */
    ((KDTreeNode *)n1)->d = KD_DIMS;
  }

  /* Keep sorting until each unique val has one and only one valid node. */
  return kdtree_cmp_bool(n0->d == KD_DIMS, n1->d == KD_DIMS);
}

/* Remove exact dups (run before balancing).
 *
 * Keep the first element added when dups are found. */
int lib_kdtree_nd_(dedup)(KDTree *tree)
{
#ifndef NDEBUG
  tree->is_balanced = false;
#endif
  qsort(tree->nodes, (size_t)tree->nodes_len, sizeof(*tree->nodes), kdtree_node_cmp_deduplicate);
  uint j = 0;
  for (uint i = 0; i < tree->nodes_len; i++) {
    if (tree->nodes[i].d != KD_DIMS) {
      if (i != j) {
        tree->nodes[j] = tree->nodes[i];
      }
      j++;
    }
  }
  tree->nodes_len = j;
  return (int)tree->nodes_len;
}

/** \} */
