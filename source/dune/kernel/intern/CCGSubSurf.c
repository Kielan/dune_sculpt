#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "LIB_sys_types.h" /* for intptr_t support */
#include "MEM_guardedalloc.h"

#include "LIB_utildefines.h" /* for LIB_assert */

#include "KERNEL_ccg.h"
#include "KERNEL_subsurf.h"
#include "CCGSubSurf.h"
#include "CCGSubSurf_intern.h"

int KERNEL_ccg_gridsize(int level)
{
  return ccg_gridsize(level);
}

int KERNEL_ccg_factor(int low_level, int high_level)
{
  LIB_assert(low_level > 0 && high_level > 0);
  LIB_assert(low_level <= high_level);

  return 1 << (high_level - low_level);
}

/***/

static CCGVert *_vert_new(CCGVertHDL vHDL, CCGSubSurf *ss)
{
  int num_vert_data = ss->subdivLevels + 1;
  CCGVert *v = CCGSUBSURF_alloc(
      ss, sizeof(CCGVert) + ss->meshIFC.vertDataSize * num_vert_data + ss->meshIFC.vertUserSize);
  byte *userData;

  v->vHDL = vHDL;
  v->edges = NULL;
  v->faces = NULL;
  v->numEdges = v->numFaces = 0;
  v->flags = 0;

  userData = ccgSubSurf_getVertUserData(ss, v);
  memset(userData, 0, ss->meshIFC.vertUserSize);
  if (ss->useAgeCounts) {
    *((int *)&userData[ss->vertUserAgeOffset]) = ss->currentAge;
  }

  return v;
}
static void _vert_remEdge(CCGVert *v, CCGEdge *e)
{
  for (int i = 0; i < v->numEdges; i++) {
    if (v->edges[i] == e) {
      v->edges[i] = v->edges[--v->numEdges];
      break;
    }
  }
}
static void _vert_remFace(CCGVert *v, CCGFace *f)
{
  for (int i = 0; i < v->numFaces; i++) {
    if (v->faces[i] == f) {
      v->faces[i] = v->faces[--v->numFaces];
      break;
    }
  }
}
static void _vert_addEdge(CCGVert *v, CCGEdge *e, CCGSubSurf *ss)
{
  v->edges = CCGSUBSURF_realloc(
      ss, v->edges, (v->numEdges + 1) * sizeof(*v->edges), v->numEdges * sizeof(*v->edges));
  v->edges[v->numEdges++] = e;
}
static void _vert_addFace(CCGVert *v, CCGFace *f, CCGSubSurf *ss)
{
  v->faces = CCGSUBSURF_realloc(
      ss, v->faces, (v->numFaces + 1) * sizeof(*v->faces), v->numFaces * sizeof(*v->faces));
  v->faces[v->numFaces++] = f;
}
static CCGEdge *_vert_findEdgeTo(const CCGVert *v, const CCGVert *vQ)
{
  for (int i = 0; i < v->numEdges; i++) {
    CCGEdge *e = v->edges[v->numEdges - 1 - i];  // XXX, note reverse
    if ((e->v0 == v && e->v1 == vQ) || (e->v1 == v && e->v0 == vQ)) {
      return e;
    }
  }
  return NULL;
}
static void _vert_free(CCGVert *v, CCGSubSurf *ss)
{
  if (v->edges) {
    CCGSUBSURF_free(ss, v->edges);
  }

  if (v->faces) {
    CCGSUBSURF_free(ss, v->faces);
  }

  CCGSUBSURF_free(ss, v);
}

/***/

static CCGEdge *_edge_new(CCGEdgeHDL eHDL, CCGVert *v0, CCGVert *v1, float crease, CCGSubSurf *ss)
{
  int num_edge_data = ccg_edgebase(ss->subdivLevels + 1);
  CCGEdge *e = CCGSUBSURF_alloc(
      ss, sizeof(CCGEdge) + ss->meshIFC.vertDataSize * num_edge_data + ss->meshIFC.edgeUserSize);
  byte *userData;

  e->eHDL = eHDL;
  e->v0 = v0;
  e->v1 = v1;
  e->crease = crease;
  e->faces = NULL;
  e->numFaces = 0;
  e->flags = 0;
  _vert_addEdge(v0, e, ss);
  _vert_addEdge(v1, e, ss);

  userData = ccgSubSurf_getEdgeUserData(ss, e);
  memset(userData, 0, ss->meshIFC.edgeUserSize);
  if (ss->useAgeCounts) {
    *((int *)&userData[ss->edgeUserAgeOffset]) = ss->currentAge;
  }

  return e;
}
static void _edge_remFace(CCGEdge *e, CCGFace *f)
{
  for (int i = 0; i < e->numFaces; i++) {
    if (e->faces[i] == f) {
      e->faces[i] = e->faces[--e->numFaces];
      break;
    }
  }
}
static void _edge_addFace(CCGEdge *e, CCGFace *f, CCGSubSurf *ss)
{
  e->faces = CCGSUBSURF_realloc(
      ss, e->faces, (e->numFaces + 1) * sizeof(*e->faces), e->numFaces * sizeof(*e->faces));
  e->faces[e->numFaces++] = f;
}
static void *_edge_getCoVert(CCGEdge *e, CCGVert *v, int lvl, int x, int dataSize)
{
  int levelBase = ccg_edgebase(lvl);
  if (v == e->v0) {
    return &EDGE_getLevelData(e)[dataSize * (levelBase + x)];
  }
  return &EDGE_getLevelData(e)[dataSize * (levelBase + (1 << lvl) - x)];
}

static void _edge_free(CCGEdge *e, CCGSubSurf *ss)
{
  if (e->faces) {
    CCGSUBSURF_free(ss, e->faces);
  }

  CCGSUBSURF_free(ss, e);
}
static void _edge_unlinkMarkAndFree(CCGEdge *e, CCGSubSurf *ss)
{
  _vert_remEdge(e->v0, e);
  _vert_remEdge(e->v1, e);
  e->v0->flags |= Vert_eEffected;
  e->v1->flags |= Vert_eEffected;
  _edge_free(e, ss);
}

static CCGFace *_face_new(
    CCGFaceHDL fHDL, CCGVert **verts, CCGEdge **edges, int numVerts, CCGSubSurf *ss)
{
  int maxGridSize = ccg_gridsize(ss->subdivLevels);
  int num_face_data = (numVerts * maxGridSize + numVerts * maxGridSize * maxGridSize + 1);
  CCGFace *f = CCGSUBSURF_alloc(
      ss,
      sizeof(CCGFace) + sizeof(CCGVert *) * numVerts + sizeof(CCGEdge *) * numVerts +
          ss->meshIFC.vertDataSize * num_face_data + ss->meshIFC.faceUserSize);
  byte *userData;

  f->numVerts = numVerts;
  f->fHDL = fHDL;
  f->flags = 0;

  for (int i = 0; i < numVerts; i++) {
    FACE_getVerts(f)[i] = verts[i];
    FACE_getEdges(f)[i] = edges[i];
    _vert_addFace(verts[i], f, ss);
    _edge_addFace(edges[i], f, ss);
  }

  userData = ccgSubSurf_getFaceUserData(ss, f);
  memset(userData, 0, ss->meshIFC.faceUserSize);
  if (ss->useAgeCounts) {
    *((int *)&userData[ss->faceUserAgeOffset]) = ss->currentAge;
  }

  return f;
}
static void _face_free(CCGFace *f, CCGSubSurf *ss)
{
  CCGSUBSURF_free(ss, f);
}
static void _face_unlinkMarkAndFree(CCGFace *f, CCGSubSurf *ss)
{
  int j;
  for (j = 0; j < f->numVerts; j++) {
    _vert_remFace(FACE_getVerts(f)[j], f);
    _edge_remFace(FACE_getEdges(f)[j], f);
    FACE_getVerts(f)[j]->flags |= Vert_eEffected;
  }
  _face_free(f, ss);
}

/***/

CCGSubSurf *ccgSubSurf_new(CCGMeshIFC *ifc,
                           int subdivLevels,
                           CCGAllocatorIFC *allocatorIFC,
                           CCGAllocatorHDL allocator)
{
  if (!allocatorIFC) {
    allocatorIFC = ccg_getStandardAllocatorIFC();
    allocator = NULL;
  }

  if (subdivLevels < 1) {
    return NULL;
  }

  CCGSubSurf *ss = allocatorIFC->alloc(allocator, sizeof(*ss));

  ss->allocatorIFC = *allocatorIFC;
  ss->allocator = allocator;

  ss->vMap = ccg_ehash_new(0, &ss->allocatorIFC, ss->allocator);
  ss->eMap = ccg_ehash_new(0, &ss->allocatorIFC, ss->allocator);
  ss->fMap = ccg_ehash_new(0, &ss->allocatorIFC, ss->allocator);

  ss->meshIFC = *ifc;

  ss->subdivLevels = subdivLevels;
  ss->numGrids = 0;
  ss->allowEdgeCreation = 0;
  ss->defaultCreaseValue = 0;
  ss->defaultEdgeUserData = NULL;

  ss->useAgeCounts = 0;
  ss->vertUserAgeOffset = ss->edgeUserAgeOffset = ss->faceUserAgeOffset = 0;

  ss->calcVertNormals = 0;
  ss->normalDataOffset = 0;

  ss->allocMask = 0;

  ss->q = CCGSUBSURF_alloc(ss, ss->meshIFC.vertDataSize);
  ss->r = CCGSUBSURF_alloc(ss, ss->meshIFC.vertDataSize);

  ss->currentAge = 0;

  ss->syncState = eSyncState_None;

  ss->oldVMap = ss->oldEMap = ss->oldFMap = NULL;
  ss->lenTempArrays = 0;
  ss->tempVerts = NULL;
  ss->tempEdges = NULL;

  return ss;
}

void ccgSubSurf_free(CCGSubSurf *ss)
{
  CCGAllocatorIFC allocatorIFC = ss->allocatorIFC;
  CCGAllocatorHDL allocator = ss->allocator;

  if (ss->syncState) {
    ccg_ehash_free(ss->oldFMap, (EHEntryFreeFP)_face_free, ss);
    ccg_ehash_free(ss->oldEMap, (EHEntryFreeFP)_edge_free, ss);
    ccg_ehash_free(ss->oldVMap, (EHEntryFreeFP)_vert_free, ss);

    MEM_freeN(ss->tempVerts);
    MEM_freeN(ss->tempEdges);
  }

  CCGSUBSURF_free(ss, ss->r);
  CCGSUBSURF_free(ss, ss->q);
  if (ss->defaultEdgeUserData) {
    CCGSUBSURF_free(ss, ss->defaultEdgeUserData);
  }

  ccg_ehash_free(ss->fMap, (EHEntryFreeFP)_face_free, ss);
  ccg_ehash_free(ss->eMap, (EHEntryFreeFP)_edge_free, ss);
  ccg_ehash_free(ss->vMap, (EHEntryFreeFP)_vert_free, ss);

  CCGSUBSURF_free(ss, ss);

  if (allocatorIFC.release) {
    allocatorIFC.release(allocator);
  }
}

CCGError ccgSubSurf_setAllowEdgeCreation(CCGSubSurf *ss,
                                         int allowEdgeCreation,
                                         float defaultCreaseValue,
                                         void *defaultUserData)
{
  if (ss->defaultEdgeUserData) {
    CCGSUBSURF_free(ss, ss->defaultEdgeUserData);
  }

  ss->allowEdgeCreation = !!allowEdgeCreation;
  ss->defaultCreaseValue = defaultCreaseValue;
  ss->defaultEdgeUserData = CCGSUBSURF_alloc(ss, ss->meshIFC.edgeUserSize);

  if (defaultUserData) {
    memcpy(ss->defaultEdgeUserData, defaultUserData, ss->meshIFC.edgeUserSize);
  }
  else {
    memset(ss->defaultEdgeUserData, 0, ss->meshIFC.edgeUserSize);
  }

  return eCCGError_None;
}
void ccgSubSurf_getAllowEdgeCreation(CCGSubSurf *ss,
                                     int *allowEdgeCreation_r,
                                     float *defaultCreaseValue_r,
                                     void *defaultUserData_r)
{
  if (allowEdgeCreation_r) {
    *allowEdgeCreation_r = ss->allowEdgeCreation;
  }
  if (ss->allowEdgeCreation) {
    if (defaultCreaseValue_r) {
      *defaultCreaseValue_r = ss->defaultCreaseValue;
    }
    if (defaultUserData_r) {
      memcpy(defaultUserData_r, ss->defaultEdgeUserData, ss->meshIFC.edgeUserSize);
    }
  }
}

CCGError ccgSubSurf_setSubdivisionLevels(CCGSubSurf *ss, int subdivisionLevels)
{
  if (subdivisionLevels <= 0) {
    return eCCGError_InvalidValue;
  }
  if (subdivisionLevels != ss->subdivLevels) {
    ss->numGrids = 0;
    ss->subdivLevels = subdivisionLevels;
    ccg_ehash_free(ss->vMap, (EHEntryFreeFP)_vert_free, ss);
    ccg_ehash_free(ss->eMap, (EHEntryFreeFP)_edge_free, ss);
    ccg_ehash_free(ss->fMap, (EHEntryFreeFP)_face_free, ss);
    ss->vMap = ccg_ehash_new(0, &ss->allocatorIFC, ss->allocator);
    ss->eMap = ccg_ehash_new(0, &ss->allocatorIFC, ss->allocator);
    ss->fMap = ccg_ehash_new(0, &ss->allocatorIFC, ss->allocator);
  }

  return eCCGError_None;
}

void ccgSubSurf_getUseAgeCounts(CCGSubSurf *ss,
                                int *useAgeCounts_r,
                                int *vertUserOffset_r,
                                int *edgeUserOffset_r,
                                int *faceUserOffset_r)
{
  *useAgeCounts_r = ss->useAgeCounts;

  if (vertUserOffset_r) {
    *vertUserOffset_r = ss->vertUserAgeOffset;
  }
  if (edgeUserOffset_r) {
    *edgeUserOffset_r = ss->edgeUserAgeOffset;
  }
  if (faceUserOffset_r) {
    *faceUserOffset_r = ss->faceUserAgeOffset;
  }
}

CCGError ccgSubSurf_setUseAgeCounts(
    CCGSubSurf *ss, int useAgeCounts, int vertUserOffset, int edgeUserOffset, int faceUserOffset)
{
  if (useAgeCounts) {
    if ((vertUserOffset + 4 > ss->meshIFC.vertUserSize) ||
        (edgeUserOffset + 4 > ss->meshIFC.edgeUserSize) ||
        (faceUserOffset + 4 > ss->meshIFC.faceUserSize)) {
      return eCCGError_InvalidValue;
    }
    ss->useAgeCounts = 1;
    ss->vertUserAgeOffset = vertUserOffset;
    ss->edgeUserAgeOffset = edgeUserOffset;
    ss->faceUserAgeOffset = faceUserOffset;
  }
  else {
    ss->useAgeCounts = 0;
    ss->vertUserAgeOffset = ss->edgeUserAgeOffset = ss->faceUserAgeOffset = 0;
  }

  return eCCGError_None;
}

CCGError ccgSubSurf_setCalcVertexNormals(CCGSubSurf *ss, int useVertNormals, int normalDataOffset)
{
  if (useVertNormals) {
    if (normalDataOffset < 0 || normalDataOffset + 12 > ss->meshIFC.vertDataSize) {
      return eCCGError_InvalidValue;
    }
    ss->calcVertNormals = 1;
    ss->normalDataOffset = normalDataOffset;
  }
  else {
    ss->calcVertNormals = 0;
    ss->normalDataOffset = 0;
  }

  return eCCGError_None;
}

void ccgSubSurf_setAllocMask(CCGSubSurf *ss, int allocMask, int maskOffset)
{
  ss->allocMask = allocMask;
  ss->maskDataOffset = maskOffset;
}

void ccgSubSurf_setNumLayers(CCGSubSurf *ss, int numLayers)
{
  ss->meshIFC.numLayers = numLayers;
}

/***/

CCGError ccgSubSurf_initFullSync(CCGSubSurf *ss)
{
  if (ss->syncState != eSyncState_None) {
    return eCCGError_InvalidSyncState;
  }

  ss->currentAge++;

  ss->oldVMap = ss->vMap;
  ss->oldEMap = ss->eMap;
  ss->oldFMap = ss->fMap;

  ss->vMap = ccg_ehash_new(0, &ss->allocatorIFC, ss->allocator);
  ss->eMap = ccg_ehash_new(0, &ss->allocatorIFC, ss->allocator);
  ss->fMap = ccg_ehash_new(0, &ss->allocatorIFC, ss->allocator);

  ss->numGrids = 0;

  ss->lenTempArrays = 12;
  ss->tempVerts = MEM_mallocN(sizeof(*ss->tempVerts) * ss->lenTempArrays, "CCGSubsurf tempVerts");
  ss->tempEdges = MEM_mallocN(sizeof(*ss->tempEdges) * ss->lenTempArrays, "CCGSubsurf tempEdges");

  ss->syncState = eSyncState_Vert;

  return eCCGError_None;
}

CCGError ccgSubSurf_initPartialSync(CCGSubSurf *ss)
{
  if (ss->syncState != eSyncState_None) {
    return eCCGError_InvalidSyncState;
  }

  ss->currentAge++;

  ss->syncState = eSyncState_Partial;

  return eCCGError_None;
}

CCGError ccgSubSurf_syncVertDel(CCGSubSurf *ss, CCGVertHDL vHDL)
{
  if (ss->syncState != eSyncState_Partial) {
    return eCCGError_InvalidSyncState;
  }

  void **prevp;
  CCGVert *v = ccg_ehash_lookupWithPrev(ss->vMap, vHDL, &prevp);

  if (!v || v->numFaces || v->numEdges) {
    return eCCGError_InvalidValue;
  }

  *prevp = v->next;
  _vert_free(v, ss);

  return eCCGError_None;
}

CCGError ccgSubSurf_syncEdgeDel(CCGSubSurf *ss, CCGEdgeHDL eHDL)
{
  if (ss->syncState != eSyncState_Partial) {
    return eCCGError_InvalidSyncState;
  }

  void **prevp;
  CCGEdge *e = ccg_ehash_lookupWithPrev(ss->eMap, eHDL, &prevp);

  if (!e || e->numFaces) {
    return eCCGError_InvalidValue;
  }

  *prevp = e->next;
  _edge_unlinkMarkAndFree(e, ss);

  return eCCGError_None;
}

CCGError ccgSubSurf_syncFaceDel(CCGSubSurf *ss, CCGFaceHDL fHDL)
{
  if (ss->syncState != eSyncState_Partial) {
    return eCCGError_InvalidSyncState;
  }

  void **prevp;
  CCGFace *f = ccg_ehash_lookupWithPrev(ss->fMap, fHDL, &prevp);

  if (!f) {
    return eCCGError_InvalidValue;
  }

  *prevp = f->next;
  _face_unlinkMarkAndFree(f, ss);

  return eCCGError_None;
}

CCGError ccgSubSurf_syncVert(
    CCGSubSurf *ss, CCGVertHDL vHDL, const void *vertData, int seam, CCGVert **v_r)
{
  void **prevp;
  CCGVert *v = NULL;
  short seamflag = (seam) ? Vert_eSeam : 0;

  if (ss->syncState == eSyncState_Partial) {
    v = ccg_ehash_lookupWithPrev(ss->vMap, vHDL, &prevp);
    if (!v) {
      v = _vert_new(vHDL, ss);
      VertDataCopy(ccg_vert_getCo(v, 0, ss->meshIFC.vertDataSize), vertData, ss);
      ccg_ehash_insert(ss->vMap, (EHEntry *)v);
      v->flags = Vert_eEffected | seamflag;
    }
    else if (!VertDataEqual(vertData, ccg_vert_getCo(v, 0, ss->meshIFC.vertDataSize), ss) ||
             ((v->flags & Vert_eSeam) != seamflag)) {
      int i, j;

      VertDataCopy(ccg_vert_getCo(v, 0, ss->meshIFC.vertDataSize), vertData, ss);
      v->flags = Vert_eEffected | seamflag;

      for (i = 0; i < v->numEdges; i++) {
        CCGEdge *e = v->edges[i];
        e->v0->flags |= Vert_eEffected;
        e->v1->flags |= Vert_eEffected;
      }
      for (i = 0; i < v->numFaces; i++) {
        CCGFace *f = v->faces[i];
        for (j = 0; j < f->numVerts; j++) {
          FACE_getVerts(f)[j]->flags |= Vert_eEffected;
        }
      }
    }
  }
  else {
    if (ss->syncState != eSyncState_Vert) {
      return eCCGError_InvalidSyncState;
    }

    v = ccg_ehash_lookupWithPrev(ss->oldVMap, vHDL, &prevp);
    if (!v) {
      v = _vert_new(vHDL, ss);
      VertDataCopy(ccg_vert_getCo(v, 0, ss->meshIFC.vertDataSize), vertData, ss);
      ccg_ehash_insert(ss->vMap, (EHEntry *)v);
      v->flags = Vert_eEffected | seamflag;
    }
    else if (!VertDataEqual(vertData, ccg_vert_getCo(v, 0, ss->meshIFC.vertDataSize), ss) ||
             ((v->flags & Vert_eSeam) != seamflag)) {
      *prevp = v->next;
      ccg_ehash_insert(ss->vMap, (EHEntry *)v);
      VertDataCopy(ccg_vert_getCo(v, 0, ss->meshIFC.vertDataSize), vertData, ss);
      v->flags = Vert_eEffected | Vert_eChanged | seamflag;
    }
    else {
      *prevp = v->next;
      ccg_ehash_insert(ss->vMap, (EHEntry *)v);
      v->flags = 0;
    }
  }

  if (v_r) {
    *v_r = v;
  }
  return eCCGError_None;
}

CCGError ccgSubSurf_syncEdge(CCGSubSurf *ss,
                             CCGEdgeHDL eHDL,
                             CCGVertHDL e_vHDL0,
                             CCGVertHDL e_vHDL1,
                             float crease,
                             CCGEdge **e_r)
{
  void **prevp;
  CCGEdge *e = NULL, *eNew;

  if (ss->syncState == eSyncState_Partial) {
    e = ccg_ehash_lookupWithPrev(ss->eMap, eHDL, &prevp);
    if (!e || e->v0->vHDL != e_vHDL0 || e->v1->vHDL != e_vHDL1 || crease != e->crease) {
      CCGVert *v0 = ccg_ehash_lookup(ss->vMap, e_vHDL0);
      CCGVert *v1 = ccg_ehash_lookup(ss->vMap, e_vHDL1);

      eNew = _edge_new(eHDL, v0, v1, crease, ss);

      if (e) {
        *prevp = eNew;
        eNew->next = e->next;

        _edge_unlinkMarkAndFree(e, ss);
      }
      else {
        ccg_ehash_insert(ss->eMap, (EHEntry *)eNew);
      }

      eNew->v0->flags |= Vert_eEffected;
      eNew->v1->flags |= Vert_eEffected;
    }
  }
  else {
    if (ss->syncState == eSyncState_Vert) {
      ss->syncState = eSyncState_Edge;
    }
    else if (ss->syncState != eSyncState_Edge) {
      return eCCGError_InvalidSyncState;
    }

    e = ccg_ehash_lookupWithPrev(ss->oldEMap, eHDL, &prevp);
    if (!e || e->v0->vHDL != e_vHDL0 || e->v1->vHDL != e_vHDL1 || e->crease != crease) {
      CCGVert *v0 = ccg_ehash_lookup(ss->vMap, e_vHDL0);
      CCGVert *v1 = ccg_ehash_lookup(ss->vMap, e_vHDL1);
      e = _edge_new(eHDL, v0, v1, crease, ss);
      ccg_ehash_insert(ss->eMap, (EHEntry *)e);
      e->v0->flags |= Vert_eEffected;
      e->v1->flags |= Vert_eEffected;
    }
    else {
      *prevp = e->next;
      ccg_ehash_insert(ss->eMap, (EHEntry *)e);
      e->flags = 0;
      if ((e->v0->flags | e->v1->flags) & Vert_eChanged) {
        e->v0->flags |= Vert_eEffected;
        e->v1->flags |= Vert_eEffected;
      }
    }
  }

  if (e_r) {
    *e_r = e;
  }
  return eCCGError_None;
}

CCGError ccgSubSurf_syncFace(
    CCGSubSurf *ss, CCGFaceHDL fHDL, int numVerts, CCGVertHDL *vHDLs, CCGFace **f_r)
{
  void **prevp;
  CCGFace *f = NULL, *fNew;
  int j, k, topologyChanged = 0;

  if (UNLIKELY(numVerts > ss->lenTempArrays)) {
    ss->lenTempArrays = (numVerts < ss->lenTempArrays * 2) ? ss->lenTempArrays * 2 : numVerts;
    ss->tempVerts = MEM_reallocN(ss->tempVerts, sizeof(*ss->tempVerts) * ss->lenTempArrays);
    ss->tempEdges = MEM_reallocN(ss->tempEdges, sizeof(*ss->tempEdges) * ss->lenTempArrays);
  }

  if (ss->syncState == eSyncState_Partial) {
    f = ccg_ehash_lookupWithPrev(ss->fMap, fHDL, &prevp);

    for (k = 0; k < numVerts; k++) {
      ss->tempVerts[k] = ccg_ehash_lookup(ss->vMap, vHDLs[k]);
    }
    for (k = 0; k < numVerts; k++) {
      ss->tempEdges[k] = _vert_findEdgeTo(ss->tempVerts[k], ss->tempVerts[(k + 1) % numVerts]);
    }

    if (f) {
      if (f->numVerts != numVerts ||
          memcmp(FACE_getVerts(f), ss->tempVerts, sizeof(*ss->tempVerts) * numVerts) != 0 ||
          memcmp(FACE_getEdges(f), ss->tempEdges, sizeof(*ss->tempEdges) * numVerts) != 0) {
        topologyChanged = 1;
      }
    }

    if (!f || topologyChanged) {
      fNew = _face_new(fHDL, ss->tempVerts, ss->tempEdges, numVerts, ss);

      if (f) {
        ss->numGrids += numVerts - f->numVerts;

        *prevp = fNew;
        fNew->next = f->next;

        _face_unlinkMarkAndFree(f, ss);
      }
      else {
        ss->numGrids += numVerts;
        ccg_ehash_insert(ss->fMap, (EHEntry *)fNew);
      }

      for (k = 0; k < numVerts; k++) {
        FACE_getVerts(fNew)[k]->flags |= Vert_eEffected;
      }
    }
  }
  else {
    if (ELEM(ss->syncState, eSyncState_Vert, eSyncState_Edge)) {
      ss->syncState = eSyncState_Face;
    }
    else if (ss->syncState != eSyncState_Face) {
      return eCCGError_InvalidSyncState;
    }

    f = ccg_ehash_lookupWithPrev(ss->oldFMap, fHDL, &prevp);

    for (k = 0; k < numVerts; k++) {
      ss->tempVerts[k] = ccg_ehash_lookup(ss->vMap, vHDLs[k]);

      if (!ss->tempVerts[k]) {
        return eCCGError_InvalidValue;
      }
    }
    for (k = 0; k < numVerts; k++) {
      ss->tempEdges[k] = _vert_findEdgeTo(ss->tempVerts[k], ss->tempVerts[(k + 1) % numVerts]);

      if (!ss->tempEdges[k]) {
        if (ss->allowEdgeCreation) {
          CCGEdge *e = ss->tempEdges[k] = _edge_new((CCGEdgeHDL)-1,
                                                    ss->tempVerts[k],
                                                    ss->tempVerts[(k + 1) % numVerts],
                                                    ss->defaultCreaseValue,
                                                    ss);
          ccg_ehash_insert(ss->eMap, (EHEntry *)e);
          e->v0->flags |= Vert_eEffected;
          e->v1->flags |= Vert_eEffected;
          if (ss->meshIFC.edgeUserSize) {
            memcpy(ccgSubSurf_getEdgeUserData(ss, e),
                   ss->defaultEdgeUserData,
                   ss->meshIFC.edgeUserSize);
          }
        }
        else {
          return eCCGError_InvalidValue;
        }
      }
    }

    if (f) {
      if (f->numVerts != numVerts ||
          memcmp(FACE_getVerts(f), ss->tempVerts, sizeof(*ss->tempVerts) * numVerts) != 0 ||
          memcmp(FACE_getEdges(f), ss->tempEdges, sizeof(*ss->tempEdges) * numVerts) != 0) {
        topologyChanged = 1;
      }
    }

    if (!f || topologyChanged) {
      f = _face_new(fHDL, ss->tempVerts, ss->tempEdges, numVerts, ss);
      ccg_ehash_insert(ss->fMap, (EHEntry *)f);
      ss->numGrids += numVerts;

      for (k = 0; k < numVerts; k++) {
        FACE_getVerts(f)[k]->flags |= Vert_eEffected;
      }
    }
    else {
      *prevp = f->next;
      ccg_ehash_insert(ss->fMap, (EHEntry *)f);
      f->flags = 0;
      ss->numGrids += f->numVerts;

      for (j = 0; j < f->numVerts; j++) {
        if (FACE_getVerts(f)[j]->flags & Vert_eChanged) {
          for (k = 0; k < f->numVerts; k++) {
            FACE_getVerts(f)[k]->flags |= Vert_eEffected;
          }
          break;
        }
      }
    }
  }

  if (f_r) {
    *f_r = f;
  }
  return eCCGError_None;
}

static void ccgSubSurf__sync(CCGSubSurf *ss)
{
  ccgSubSurf__sync_legacy(ss);
}

CCGError ccgSubSurf_processSync(CCGSubSurf *ss)
{
  if (ss->syncState == eSyncState_Partial) {
    ss->syncState = eSyncState_None;

    ccgSubSurf__sync(ss);
  }
  else if (ss->syncState) {
    ccg_ehash_free(ss->oldFMap, (EHEntryFreeFP)_face_unlinkMarkAndFree, ss);
    ccg_ehash_free(ss->oldEMap, (EHEntryFreeFP)_edge_unlinkMarkAndFree, ss);
    ccg_ehash_free(ss->oldVMap, (EHEntryFreeFP)_vert_free, ss);
    MEM_freeN(ss->tempEdges);
    MEM_freeN(ss->tempVerts);

    ss->lenTempArrays = 0;

    ss->oldFMap = ss->oldEMap = ss->oldVMap = NULL;
    ss->tempVerts = NULL;
    ss->tempEdges = NULL;

    ss->syncState = eSyncState_None;

    ccgSubSurf__sync(ss);
  }
  else {
    return eCCGError_InvalidSyncState;
  }

  return eCCGError_None;
}

void ccgSubSurf__allFaces(CCGSubSurf *ss, CCGFace ***faces, int *numFaces, int *freeFaces)
{
  CCGFace **array;
  int i, num;

  if (*faces == NULL) {
    array = MEM_mallocN(sizeof(*array) * ss->fMap->numEntries, "CCGSubsurf allFaces");
    num = 0;
    for (i = 0; i < ss->fMap->curSize; i++) {
      CCGFace *f = (CCGFace *)ss->fMap->buckets[i];

      for (; f; f = f->next) {
        array[num++] = f;
      }
    }

    *faces = array;
    *numFaces = num;
    *freeFaces = 1;
  }
  else {
    *freeFaces = 0;
  }
}
