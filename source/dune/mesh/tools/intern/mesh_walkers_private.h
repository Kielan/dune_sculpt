#pragma once

/** Mesh walker API. **/

extern MeshWalker *mesh_walker_types[];
extern const int mesh_totwalkers;

/* Pointer hiding */
typedef struct MeshWalkerGeneric {
  Link link;
  int depth;
} MeshWalkerGeneric;

typedef struct MeshWalkerShell {
  MeshWalkerGeneric header;
  MeshEdge *curedge;
} MeshWalkerShell;

typedef struct MeshWalkerLoopShell {
  MeshWalkerGeneric header;
  MeshLoop *curloop;
} MeshWalkerLoopShell;

typedef struct MeshWalkerLoopShellWire {
  MeshWalkerGeneric header;
  MeshElem *curelem;
} MeshWalkerLoopShellWire;

typedef struct MeshWalkerIslandbound {
  BMwGenericWalker header;
  BMLoop *base;
  BMVert *lastv;
  BMLoop *curloop;
} MeshWalkerIslandbound;

typedef struct MeshWalkerIsland {
  MeshWalkerGeneric header;
  MeshFace *cur;
} MeshWalkerIsland;

typedef struct BMwEdgeLoopWalker {
  BMwGenericWalker header;
  BMEdge *cur, *start;
  BMVert *lastv, *startv;
  BMFace *f_hub;
  bool is_boundary; /* boundary looping changes behavior */
  bool is_single;   /* single means the edge verts are only connected to 1 face */
} BMwEdgeLoopWalker;

typedef struct MeshWalkerFaceLoop {
  BMwGenericWalker header;
  BMLoop *l;
  bool no_calc;
} MeshWalkerFaceLoop;

typedef struct MeshWalkerEdgering {
  MeshWalkerGeneric header;
  MeshLoop *l;
  MeshEdge *wireedge;
} MeshWalkerEdgering;

typedef struct MeshWalkerEdgeboundary {
  MeshWalkerGeneric header;
  MeshEdge *e;
} MeshWalkerEdgeboundary;

typedef struct BMwNonManifoldEdgeLoopWalker {
  BMwGenericWalker header;
  BMEdge *start, *cur;
  BMVert *startv, *lastv;
  int face_count; /* face count around the edge. */
} BMwNonManifoldEdgeLoopWalker;

typedef struct BMwUVEdgeWalker {
  BMwGenericWalker header;
  BMLoop *l;
} BMwUVEdgeWalker;

typedef struct BMwConnectedVertexWalker {
  BMwGenericWalker header;
  BMVert *curvert;
} BMwConnectedVertexWalker;
