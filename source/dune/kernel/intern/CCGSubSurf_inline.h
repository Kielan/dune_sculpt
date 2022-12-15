#pragma once

LIB_INLINE int ccg_gridsize(int level)
{
  LIB_assert(level > 0);
  LIB_assert(level <= CCGSUBSURF_LEVEL_MAX + 1);
  return (1 << (level - 1)) + 1;
}

LIB_INLINE int ccg_edgesize(int level)
{
  LIB_assert(level > 0);
  LIB_assert(level <= CCGSUBSURF_LEVEL_MAX + 1);
  return 1 + (1 << level);
}

LIB_INLINE int ccg_spacing(int high_level, int low_level)
{
  LIB_assert(high_level > 0 && low_level > 0);
  LIB_assert(high_level >= low_level);
  LIB_assert((high_level - low_level) <= CCGSUBSURF_LEVEL_MAX);
  return 1 << (high_level - low_level);
}

LIB_INLINE int ccg_edgebase(int level)
{
  LIB_assert(level > 0);
  LIB_assert(level <= CCGSUBSURF_LEVEL_MAX + 1);
  return level + (1 << level) - 1;
}

/* **** */

LIB_INLINE byte *VERT_getLevelData(CCGVert *v)
{
  return (byte *)(&(v)[1]);
}

LIB_INLINE byte *EDGE_getLevelData(CCGEdge *e)
{
  return (byte *)(&(e)[1]);
}

LIB_INLINE CCGVert **FACE_getVerts(CCGFace *f)
{
  return (CCGVert **)(&f[1]);
}

LIB_INLINE CCGEdge **FACE_getEdges(CCGFace *f)
{
  return (CCGEdge **)(&(FACE_getVerts(f)[f->numVerts]));
}

LIB_INLINE byte *FACE_getCenterData(CCGFace *f)
{
  return (byte *)(&(FACE_getEdges(f)[(f)->numVerts]));
}

/* **** */

LIB_INLINE void *ccg_vert_getCo(CCGVert *v, int lvl, int dataSize)
{
  return &VERT_getLevelData(v)[lvl * dataSize];
}

LIB_INLINE float *ccg_vert_getNo(CCGVert *v, int lvl, int dataSize, int normalDataOffset)
{
  return (float *)&VERT_getLevelData(v)[lvl * dataSize + normalDataOffset];
}

LIB_INLINE void *ccg_edge_getCo(CCGEdge *e, int lvl, int x, int dataSize)
{
  int levelBase = ccg_edgebase(lvl);
  return &EDGE_getLevelData(e)[dataSize * (levelBase + x)];
}

LIB_INLINE float *ccg_edge_getNo(CCGEdge *e, int lvl, int x, int dataSize, int normalDataOffset)
{
  int levelBase = ccg_edgebase(lvl);
  return (float *)&EDGE_getLevelData(e)[dataSize * (levelBase + x) + normalDataOffset];
}

LIB_INLINE void *ccg_face_getIECo(CCGFace *f, int lvl, int S, int x, int levels, int dataSize)
{
  int maxGridSize = ccg_gridsize(levels);
  int spacing = ccg_spacing(levels, lvl);
  byte *gridBase = FACE_getCenterData(f) +
                   dataSize * (1 + S * (maxGridSize + maxGridSize * maxGridSize));
  return &gridBase[dataSize * x * spacing];
}

LIB_INLINE void *ccg_face_getIENo(
    CCGFace *f, int lvl, int S, int x, int levels, int dataSize, int normalDataOffset)
{
  int maxGridSize = ccg_gridsize(levels);
  int spacing = ccg_spacing(levels, lvl);
  byte *gridBase = FACE_getCenterData(f) +
                   dataSize * (1 + S * (maxGridSize + maxGridSize * maxGridSize));
  return &gridBase[dataSize * x * spacing + normalDataOffset];
}

LIB_INLINE void *ccg_face_getIFCo(
    CCGFace *f, int lvl, int S, int x, int y, int levels, int dataSize)
{
  int maxGridSize = ccg_gridsize(levels);
  int spacing = ccg_spacing(levels, lvl);
  byte *gridBase = FACE_getCenterData(f) +
                   dataSize * (1 + S * (maxGridSize + maxGridSize * maxGridSize));
  return &gridBase[dataSize * (maxGridSize + (y * maxGridSize + x) * spacing)];
}

LIB_INLINE float *ccg_face_getIFNo(
    CCGFace *f, int lvl, int S, int x, int y, int levels, int dataSize, int normalDataOffset)
{
  int maxGridSize = ccg_gridsize(levels);
  int spacing = ccg_spacing(levels, lvl);
  byte *gridBase = FACE_getCenterData(f) +
                   dataSize * (1 + S * (maxGridSize + maxGridSize * maxGridSize));
  return (float *)&gridBase[dataSize * (maxGridSize + (y * maxGridSize + x) * spacing) +
                            normalDataOffset];
}

LIB_INLINE int ccg_face_getVertIndex(CCGFace *f, CCGVert *v)
{
  for (int i = 0; i < f->numVerts; i++) {
    if (FACE_getVerts(f)[i] == v) {
      return i;
    }
  }
  return -1;
}

LIB_INLINE int ccg_face_getEdgeIndex(CCGFace *f, CCGEdge *e)
{
  for (int i = 0; i < f->numVerts; i++) {
    if (FACE_getEdges(f)[i] == e) {
      return i;
    }
  }
  return -1;
}

LIB_INLINE void *ccg_face_getIFCoEdge(
    CCGFace *f, CCGEdge *e, int f_ed_idx, int lvl, int eX, int eY, int levels, int dataSize)
{
  int maxGridSize = ccg_gridsize(levels);
  int spacing = ccg_spacing(levels, lvl);
  int x, y, cx, cy;

  LIB_assert(f_ed_idx == ccg_face_getEdgeIndex(f, e));

  eX = eX * spacing;
  eY = eY * spacing;
  if (e->v0 != FACE_getVerts(f)[f_ed_idx]) {
    eX = (maxGridSize * 2 - 1) - 1 - eX;
  }
  y = maxGridSize - 1 - eX;
  x = maxGridSize - 1 - eY;
  if (x < 0) {
    f_ed_idx = (f_ed_idx + f->numVerts - 1) % f->numVerts;
    cx = y;
    cy = -x;
  }
  else if (y < 0) {
    f_ed_idx = (f_ed_idx + 1) % f->numVerts;
    cx = -y;
    cy = x;
  }
  else {
    cx = x;
    cy = y;
  }
  return ccg_face_getIFCo(f, levels, f_ed_idx, cx, cy, levels, dataSize);
}

LIB_INLINE void Normalize(float no[3])
{
  const float length = sqrtf(no[0] * no[0] + no[1] * no[1] + no[2] * no[2]);

  if (length > EPSILON) {
    const float length_inv = 1.0f / length;

    no[0] *= length_inv;
    no[1] *= length_inv;
    no[2] *= length_inv;
  }
  else {
    NormZero(no);
  }
}

/* Data layers mathematics. */

LIB_INLINE bool VertDataEqual(const float a[], const float b[], const CCGSubSurf *ss)
{
  for (int i = 0; i < ss->meshIFC.numLayers; i++) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

LIB_INLINE void VertDataZero(float v[], const CCGSubSurf *ss)
{
  memset(v, 0, sizeof(float) * ss->meshIFC.numLayers);
}

LIB_INLINE void VertDataCopy(float dst[], const float src[], const CCGSubSurf *ss)
{
  for (int i = 0; i < ss->meshIFC.numLayers; i++) {
    dst[i] = src[i];
  }
}

LIB_INLINE void VertDataAdd(float a[], const float b[], const CCGSubSurf *ss)
{
  for (int i = 0; i < ss->meshIFC.numLayers; i++) {
    a[i] += b[i];
  }
}

LIB_INLINE void VertDataSub(float a[], const float b[], const CCGSubSurf *ss)
{
  for (int i = 0; i < ss->meshIFC.numLayers; i++) {
    a[i] -= b[i];
  }
}

LIB_INLINE void VertDataMulN(float v[], float f, const CCGSubSurf *ss)
{
  for (int i = 0; i < ss->meshIFC.numLayers; i++) {
    v[i] *= f;
  }
}

LIB_INLINE void VertDataAvg4(float v[],
                             const float a[],
                             const float b[],
                             const float c[],
                             const float d[],
                             const CCGSubSurf *ss)
{
  for (int i = 0; i < ss->meshIFC.numLayers; i++) {
    v[i] = (a[i] + b[i] + c[i] + d[i]) * 0.25f;
  }
}
