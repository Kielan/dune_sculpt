#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_meta_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_memarena.h"
#include "BLI_string_utils.h"
#include "BLI_utildefines.h"

#include "BKE_global.h"

#include "BKE_displist.h"
#include "BKE_mball_tessellate.h" /* own include */
#include "BKE_object.h"
#include "BKE_scene.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "BLI_strict_flags.h"

/* experimental (faster) normal calculation */
// #define USE_ACCUM_NORMAL

#define MBALL_ARRAY_LEN_INIT 4096

/* Data types */

typedef struct corner { /* corner of a cube */
  int i, j, k;          /* (i, j, k) is index within lattice */
  float co[3], value;   /* location and function value */
  struct corner *next;
} CORNER;

typedef struct cube { /* partitioning cell (cube) */
  int i, j, k;        /* lattice location of cube */
  CORNER *corners[8]; /* eight corners */
} CUBE;

typedef struct cubes { /* linked list of cubes acting as stack */
  CUBE cube;           /* a single cube */
  struct cubes *next;  /* remaining elements */
} CUBES;

typedef struct centerlist { /* list of cube locations */
  int i, j, k;              /* cube location */
  struct centerlist *next;  /* remaining elements */
} CENTERLIST;

typedef struct edgelist {     /* list of edges */
  int i1, j1, k1, i2, j2, k2; /* edge corner ids */
  int vid;                    /* vertex id */
  struct edgelist *next;      /* remaining elements */
} EDGELIST;

typedef struct intlist { /* list of integers */
  int i;                 /* an integer */
  struct intlist *next;  /* remaining elements */
} INTLIST;

typedef struct intlists { /* list of list of integers */
  INTLIST *list;          /* a list of integers */
  struct intlists *next;  /* remaining elements */
} INTLISTS;

typedef struct Box { /* an AABB with pointer to metalelem */
  float min[3], max[3];
  const MetaElem *ml;
} Box;

typedef struct MetaballBVHNode { /* BVH node */
  Box bb[2];                     /* AABB of children */
  struct MetaballBVHNode *child[2];
} MetaballBVHNode;

typedef struct process {     /* parameters, storage */
  float thresh, size;        /* mball threshold, single cube size */
  float delta;               /* small delta for calculating normals */
  unsigned int converge_res; /* converge procedure resolution (more = slower) */

  MetaElem **mainb;          /* array of all metaelems */
  unsigned int totelem, mem; /* number of metaelems */

  MetaballBVHNode metaball_bvh; /* The simplest bvh */
  Box allbb;                    /* Bounding box of all metaelems */

  MetaballBVHNode **bvh_queue; /* Queue used during bvh traversal */
  unsigned int bvh_queue_size;

  CUBES *cubes;         /* stack of cubes waiting for polygonization */
  CENTERLIST **centers; /* cube center hash table */
  CORNER **corners;     /* corner value hash table */
  EDGELIST **edges;     /* edge and vertex id hash table */

  int (*indices)[4];     /* output indices */
  unsigned int totindex; /* size of memory allocated for indices */
  unsigned int curindex; /* number of currently added indices */

  float (*co)[3], (*no)[3]; /* surface vertices - positions and normals */
  unsigned int totvertex;   /* memory size */
  unsigned int curvertex;   /* currently added vertices */

  /* memory allocation from common pool */
  MemArena *pgn_elements;
} PROCESS;

/* Forward declarations */
static int vertid(PROCESS *process, const CORNER *c1, const CORNER *c2);
static void add_cube(PROCESS *process, int i, int j, int k);
static void make_face(PROCESS *process, int i1, int i2, int i3, int i4);
static void converge(PROCESS *process, const CORNER *c1, const CORNER *c2, float r_p[3]);

/* ******************* SIMPLE BVH ********************* */

static void make_box_union(const BoundBox *a, const Box *b, Box *r_out)
{
  r_out->min[0] = min_ff(a->vec[0][0], b->min[0]);
  r_out->min[1] = min_ff(a->vec[0][1], b->min[1]);
  r_out->min[2] = min_ff(a->vec[0][2], b->min[2]);

  r_out->max[0] = max_ff(a->vec[6][0], b->max[0]);
  r_out->max[1] = max_ff(a->vec[6][1], b->max[1]);
  r_out->max[2] = max_ff(a->vec[6][2], b->max[2]);
}

static void make_box_from_metaelem(Box *r, const MetaElem *ml)
{
  copy_v3_v3(r->max, ml->bb->vec[6]);
  copy_v3_v3(r->min, ml->bb->vec[0]);
  r->ml = ml;
}

/**
 * Partitions part of #process.mainb array [start, end) along axis s. Returns i,
 * where centroids of elements in the [start, i) segment lie "on the right side" of div,
 * and elements in the [i, end) segment lie "on the left"
 */
static unsigned int partition_mainb(
    MetaElem **mainb, unsigned int start, unsigned int end, unsigned int s, float div)
{
  unsigned int i = start, j = end - 1;
  div *= 2.0f;

  while (1) {
    while (i < j && div > (mainb[i]->bb->vec[6][s] + mainb[i]->bb->vec[0][s])) {
      i++;
    }
    while (j > i && div < (mainb[j]->bb->vec[6][s] + mainb[j]->bb->vec[0][s])) {
      j--;
    }

    if (i >= j) {
      break;
    }

    SWAP(MetaElem *, mainb[i], mainb[j]);
    i++;
    j--;
  }

  if (i == start) {
    i++;
  }

  return i;
}

/**
 * Recursively builds a BVH, dividing elements along the middle of the longest axis of allbox.
 */
static void build_bvh_spatial(PROCESS *process,
                              MetaballBVHNode *node,
                              unsigned int start,
                              unsigned int end,
                              const Box *allbox)
{
  unsigned int part, j, s;
  float dim[3], div;

  /* Maximum bvh queue size is number of nodes which are made, equals calls to this function. */
  process->bvh_queue_size++;

  dim[0] = allbox->max[0] - allbox->min[0];
  dim[1] = allbox->max[1] - allbox->min[1];
  dim[2] = allbox->max[2] - allbox->min[2];

  s = 0;
  if (dim[1] > dim[0] && dim[1] > dim[2]) {
    s = 1;
  }
  else if (dim[2] > dim[1] && dim[2] > dim[0]) {
    s = 2;
  }

  div = allbox->min[s] + (dim[s] / 2.0f);

  part = partition_mainb(process->mainb, start, end, s, div);

  make_box_from_metaelem(&node->bb[0], process->mainb[start]);
  node->child[0] = NULL;

  if (part > start + 1) {
    for (j = start; j < part; j++) {
      make_box_union(process->mainb[j]->bb, &node->bb[0], &node->bb[0]);
    }

    node->child[0] = BLI_memarena_alloc(process->pgn_elements, sizeof(MetaballBVHNode));
    build_bvh_spatial(process, node->child[0], start, part, &node->bb[0]);
  }

  node->child[1] = NULL;
  if (part < end) {
    make_box_from_metaelem(&node->bb[1], process->mainb[part]);

    if (part < end - 1) {
      for (j = part; j < end; j++) {
        make_box_union(process->mainb[j]->bb, &node->bb[1], &node->bb[1]);
      }

      node->child[1] = BLI_memarena_alloc(process->pgn_elements, sizeof(MetaballBVHNode));
      build_bvh_spatial(process, node->child[1], part, end, &node->bb[1]);
    }
  }
  else {
    INIT_MINMAX(node->bb[1].min, node->bb[1].max);
  }
}

/* ******************** ARITH ************************* */

/**
 * BASED AT CODE (but mostly rewritten) :
 * C code from the article
 * "An Implicit Surface Polygonizer"
 * by Jules Bloomenthal <jbloom@beauty.gmu.edu>
 * in "Graphics Gems IV", Academic Press, 1994
 *
 * Authored by Jules Bloomenthal, Xerox PARC.
 * Copyright (c) Xerox Corporation, 1991.  All rights reserved.
 * Permission is granted to reproduce, use and distribute this code for
 * any and all purposes, provided that this notice appears in all copies.
 */
