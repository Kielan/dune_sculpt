/**
 * Implementation of CDDerivedMesh.
 * KERNEL_cdderivedmesh.h contains the function prototypes for this file.
 */

#include "atomic_ops.h"

#include "LIB_math.h"
#include "LIB_utildefines.h"

#include "KE_DerivedMesh.h"
#include "KE_cdderivedmesh.h"
#include "KE_curve.h"
#include "KE_editmesh.h"
#include "KE_mesh.h"
#include "KE_mesh_mapping.h"
#include "KE_object.h"
#include "KE_paint.h"
#include "KE_pbvh.h"

#include "structs_curve_types.h" /* for Curve */
#include "structs_mesh_types.h"
#include "structs_meshdata_types.h"
#include "structs_object_types.h"

#include "MEM_guardedalloc.h"

#include <limits.h>
#include <math.h>
#include <string.h>

typedef struct {
  DerivedMesh dm;

  /* these point to data in the DerivedMesh custom data layers,
   * they are only here for efficiency and convenience */
  MVert *mvert;
  const float (*vert_normals)[3];
  MEdge *medge;
  MFace *mface;
  MLoop *mloop;
  MPoly *mpoly;

  /* Cached */
  struct PBVH *pbvh;
  bool pbvh_draw;

  /* Mesh connectivity */
  MeshElemMap *pmap;
  int *pmap_mem;
} CDDerivedMesh;

/**************** DerivedMesh interface functions ****************/
static int cdDM_getNumVerts(DerivedMesh *dm)
{
  return dm->numVertData;
}

static int cdDM_getNumEdges(DerivedMesh *dm)
{
  return dm->numEdgeData;
}

static int cdDM_getNumTessFaces(DerivedMesh *dm)
{
  /* uncomment and add a breakpoint on the printf()
   * to help debug tessfaces issues since BMESH merge. */
#if 0
  if (dm->numTessFaceData == 0 && dm->numPolyData != 0) {
    printf("%s: has no faces!\n");
  }
#endif
  return dm->numTessFaceData;
}

static int cdDM_getNumLoops(DerivedMesh *dm)
{
  return dm->numLoopData;
}

static int cdDM_getNumPolys(DerivedMesh *dm)
{
  return dm->numPolyData;
}

static void cdDM_copyVertArray(DerivedMesh *dm, MVert *r_vert)
{
  CDDerivedMesh *cddm = (CDDerivedMesh *)dm;
  memcpy(r_vert, cddm->mvert, sizeof(*r_vert) * dm->numVertData);
}

static void cdDM_copyEdgeArray(DerivedMesh *dm, MEdge *r_edge)
{
  CDDerivedMesh *cddm = (CDDerivedMesh *)dm;
  memcpy(r_edge, cddm->medge, sizeof(*r_edge) * dm->numEdgeData);
}

static void cdDM_copyLoopArray(DerivedMesh *dm, MLoop *r_loop)
{
  CDDerivedMesh *cddm = (CDDerivedMesh *)dm;
  memcpy(r_loop, cddm->mloop, sizeof(*r_loop) * dm->numLoopData);
}

static void cdDM_copyPolyArray(DerivedMesh *dm, MPoly *r_poly)
{
  CDDerivedMesh *cddm = (CDDerivedMesh *)dm;
  memcpy(r_poly, cddm->mpoly, sizeof(*r_poly) * dm->numPolyData);
}

static void cdDM_getVertCo(DerivedMesh *dm, int index, float r_co[3])
{
  CDDerivedMesh *cddm = (CDDerivedMesh *)dm;

  copy_v3_v3(r_co, cddm->mvert[index].co);
}

static void cdDM_getVertNo(DerivedMesh *dm, int index, float r_no[3])
{
  CDDerivedMesh *cddm = (CDDerivedMesh *)dm;
  copy_v3_v3(r_no, cddm->vert_normals[index]);
}

static void cdDM_recalc_looptri(DerivedMesh *dm)
{
  CDDerivedMesh *cddm = (CDDerivedMesh *)dm;
  const unsigned int totpoly = dm->numPolyData;
  const unsigned int totloop = dm->numLoopData;

  DM_ensure_looptri_data(dm);
  LIB_assert(totpoly == 0 || cddm->dm.looptris.array_wip != NULL);

  KERNEL_mesh_recalc_looptri(
      cddm->mloop, cddm->mpoly, cddm->mvert, totloop, totpoly, cddm->dm.looptris.array_wip);

  LIB_assert(cddm->dm.looptris.array == NULL);
  atomic_cas_ptr(
      (void **)&cddm->dm.looptris.array, cddm->dm.looptris.array, cddm->dm.looptris.array_wip);
  cddm->dm.looptris.array_wip = NULL;
}

static void cdDM_free_internal(CDDerivedMesh *cddm)
{
  if (cddm->pmap) {
    MEM_freeN(cddm->pmap);
  }
  if (cddm->pmap_mem) {
    MEM_freeN(cddm->pmap_mem);
  }
}

static void cdDM_release(DerivedMesh *dm)
{
  CDDerivedMesh *cddm = (CDDerivedMesh *)dm;

  if (DM_release(dm)) {
    cdDM_free_internal(cddm);
    MEM_freeN(cddm);
  }
}

/**************** CDDM interface functions ****************/
static CDDerivedMesh *cdDM_create(const char *desc)
{
  CDDerivedMesh *cddm;
  DerivedMesh *dm;

  cddm = MEM_callocN(sizeof(*cddm), desc);
  dm = &cddm->dm;

  dm->getNumVerts = cdDM_getNumVerts;
  dm->getNumEdges = cdDM_getNumEdges;
  dm->getNumTessFaces = cdDM_getNumTessFaces;
  dm->getNumLoops = cdDM_getNumLoops;
  dm->getNumPolys = cdDM_getNumPolys;

  dm->copyVertArray = cdDM_copyVertArray;
  dm->copyEdgeArray = cdDM_copyEdgeArray;
  dm->copyLoopArray = cdDM_copyLoopArray;
  dm->copyPolyArray = cdDM_copyPolyArray;

  dm->getVertDataArray = DM_get_vert_data_layer;
  dm->getEdgeDataArray = DM_get_edge_data_layer;

  dm->recalcLoopTri = cdDM_recalc_looptri;

  dm->getVertCo = cdDM_getVertCo;
  dm->getVertNo = cdDM_getVertNo;

  dm->release = cdDM_release;

  return cddm;
}

static DerivedMesh *cdDM_from_mesh_ex(Mesh *mesh,
                                      eCDAllocType alloctype,
                                      const CustomData_MeshMasks *mask)
{
  CDDerivedMesh *cddm = cdDM_create(__func__);
  DerivedMesh *dm = &cddm->dm;
  CustomData_MeshMasks cddata_masks = *mask;

  cddata_masks.lmask &= ~CD_MASK_MDISPS;

  /* this does a referenced copy, with an exception for fluidsim */

  DM_init(dm,
          DM_TYPE_CDDM,
          mesh->totvert,
          mesh->totedge,
          0 /* mesh->totface */,
          mesh->totloop,
          mesh->totpoly);

  /* This should actually be dm->deformedOnly = mesh->runtime.deformed_only,
   * but only if the original mesh had its deformed_only flag correctly set
   * (which isn't generally the case). */
  dm->deformedOnly = 1;
  dm->cd_flag = mesh->cd_flag;

  CustomData_merge(&mesh->vdata, &dm->vertData, cddata_masks.vmask, alloctype, mesh->totvert);
  CustomData_merge(&mesh->edata, &dm->edgeData, cddata_masks.emask, alloctype, mesh->totedge);
  CustomData_merge(&mesh->fdata,
                   &dm->faceData,
                   cddata_masks.fmask | CD_MASK_ORIGINDEX,
                   alloctype,
                   0 /* mesh->totface */);
  CustomData_merge(&mesh->ldata, &dm->loopData, cddata_masks.lmask, alloctype, mesh->totloop);
  CustomData_merge(&mesh->pdata, &dm->polyData, cddata_masks.pmask, alloctype, mesh->totpoly);

  cddm->mvert = CustomData_get_layer(&dm->vertData, CD_MVERT);
  /* Though this may be an unnecessary calculation, simply retrieving the layer may return nothing
   * or dirty normals. */
  cddm->vert_normals = BKE_mesh_vertex_normals_ensure(mesh);
  cddm->medge = CustomData_get_layer(&dm->edgeData, CD_MEDGE);
  cddm->mloop = CustomData_get_layer(&dm->loopData, CD_MLOOP);
  cddm->mpoly = CustomData_get_layer(&dm->polyData, CD_MPOLY);
#if 0
  cddm->mface = CustomData_get_layer(&dm->faceData, CD_MFACE);
#else
  cddm->mface = NULL;
#endif

  /* commented since even when CD_ORIGINDEX was first added this line fails
   * on the default cube, (after editmode toggle too) - campbell */
#if 0
  LIB_assert(CustomData_has_layer(&cddm->dm.faceData, CD_ORIGINDEX));
#endif

  return dm;
}

DerivedMesh *CDDM_from_mesh(Mesh *mesh)
{
  return cdDM_from_mesh_ex(mesh, CD_REFERENCE, &CD_MASK_MESH);
}

DerivedMesh *CDDM_copy(DerivedMesh *source)
{
  CDDerivedMesh *cddm = cdDM_create("CDDM_copy cddm");
  DerivedMesh *dm = &cddm->dm;
  int numVerts = source->numVertData;
  int numEdges = source->numEdgeData;
  int numTessFaces = 0;
  int numLoops = source->numLoopData;
  int numPolys = source->numPolyData;

  /* NOTE: Don't copy tessellation faces if not requested explicitly. */

  /* ensure these are created if they are made on demand */
  source->getVertDataArray(source, CD_ORIGINDEX);
  source->getEdgeDataArray(source, CD_ORIGINDEX);
  source->getPolyDataArray(source, CD_ORIGINDEX);

  /* this initializes dm, and copies all non mvert/medge/mface layers */
  DM_from_template(dm, source, DM_TYPE_CDDM, numVerts, numEdges, numTessFaces, numLoops, numPolys);
  dm->deformedOnly = source->deformedOnly;
  dm->cd_flag = source->cd_flag;

  CustomData_copy_data(&source->vertData, &dm->vertData, 0, 0, numVerts);
  CustomData_copy_data(&source->edgeData, &dm->edgeData, 0, 0, numEdges);

  /* now add mvert/medge/mface layers */
  cddm->mvert = source->dupVertArray(source);
  cddm->medge = source->dupEdgeArray(source);

  CustomData_add_layer(&dm->vertData, CD_MVERT, CD_ASSIGN, cddm->mvert, numVerts);
  CustomData_add_layer(&dm->edgeData, CD_MEDGE, CD_ASSIGN, cddm->medge, numEdges);

  DM_DupPolys(source, dm);

  cddm->mloop = CustomData_get_layer(&dm->loopData, CD_MLOOP);
  cddm->mpoly = CustomData_get_layer(&dm->polyData, CD_MPOLY);

  return dm;
}
