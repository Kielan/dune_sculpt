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
  MeshWalkerGeneric header;
  MeshLoop *base;
  MeshVert *lastv;
  MeshLoop *curloop;
} MeshWalkerIslandbound;

typedef struct MeshWalkerIsland {
  MeshWalkerGeneric header;
  MeshFace *cur;
} MeshWalkerIsland;

typedef struct MeshWalkerEdgeLoop {
  MeshWalkerGenericWalker header;
  MeshEdge *cur, *start;
  MeshVert *lastv, *startv;
  MeshFace *f_hub;
  bool is_boundary; /* boundary looping changes behavior */
  bool is_single;   /* single means the edge verts are only connected to 1 face */
} MeshWalkerEdgeLoop;

typedef struct MeshWalkerFaceLoop {
  MeshWalkerGeneric header;
  MeshLoop *l;
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

typedef struct MeshWalkerNonManifoldEdgeLoop {
  MeshWalkerGeneric header;
  MeshEdge *start, *cur;
  MeshVert *startv, *lastv;
  int face_count; /* face count around the edge. */
} MeshWalkerNonManifoldEdgeLoop;

typedef struct MeshWalkerUVEdge {
  MeehWalkerGenericWalker header;
  MeshLoop *l;
} MeshWalkerUVEdge;

typedef struct MeshWalkerConnectedVertex {
  MeshWalkerGeneric header;
  MeshVert *curvert;
} MeshWalkerConnectedVertex;
