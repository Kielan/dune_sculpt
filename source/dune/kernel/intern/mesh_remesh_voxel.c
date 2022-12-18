#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "MEM_guardedalloc.h"

#include "BLI_array.hh"
#include "BLI_index_range.hh"
#include "BLI_math_vec_types.hh"
#include "BLI_math_vector.h"
#include "BLI_span.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "BKE_bvhutils.h"
#include "BKE_customdata.h"
#include "BKE_editmesh.h"
#include "BKE_lib_id.h"
#include "BKE_mesh.h"
#include "BKE_mesh_remesh_voxel.h" /* own include */
#include "BKE_mesh_runtime.h"

#include "bmesh_tools.h"

#ifdef WITH_OPENVDB
#  include <openvdb/openvdb.h>
#  include <openvdb/tools/MeshToVolume.h>
#  include <openvdb/tools/VolumeToMesh.h>
#endif

#ifdef WITH_QUADRIFLOW
#  include "quadriflow_capi.hpp"
#endif

using blender::Array;
using blender::float3;
using blender::IndexRange;
using blender::MutableSpan;
using blender::Span;

#ifdef WITH_QUADRIFLOW
static Mesh *remesh_quadriflow(const Mesh *input_mesh,
                               int target_faces,
                               int seed,
                               bool preserve_sharp,
                               bool preserve_boundary,
                               bool adaptive_scale,
                               void (*update_cb)(void *, float progress, int *cancel),
                               void *update_cb_data)
{
  /* Ensure that the triangulated mesh data is up to data */
  const MLoopTri *looptri = BKE_mesh_runtime_looptri_ensure(input_mesh);

  /* Gather the required data for export to the internal quadriflow mesh format. */
  MVertTri *verttri = (MVertTri *)MEM_callocN(
      sizeof(*verttri) * BKE_mesh_runtime_looptri_len(input_mesh), "remesh_looptri");
  BKE_mesh_runtime_verttri_from_looptri(
      verttri, input_mesh->mloop, looptri, BKE_mesh_runtime_looptri_len(input_mesh));

  const int totfaces = BKE_mesh_runtime_looptri_len(input_mesh);
  const int totverts = input_mesh->totvert;
  Array<float3> verts(totverts);
  Array<int> faces(totfaces * 3);

  for (const int i : IndexRange(totverts)) {
    verts[i] = input_mesh->mvert[i].co;
  }

  for (const int i : IndexRange(totfaces)) {
    MVertTri &vt = verttri[i];
    faces[i * 3] = vt.tri[0];
    faces[i * 3 + 1] = vt.tri[1];
    faces[i * 3 + 2] = vt.tri[2];
  }

  /* Fill out the required input data */
  QuadriflowRemeshData qrd;

  qrd.totfaces = totfaces;
  qrd.totverts = totverts;
  qrd.verts = (float *)verts.data();
  qrd.faces = faces.data();
  qrd.target_faces = target_faces;

  qrd.preserve_sharp = preserve_sharp;
  qrd.preserve_boundary = preserve_boundary;
  qrd.adaptive_scale = adaptive_scale;
  qrd.minimum_cost_flow = false;
  qrd.aggresive_sat = false;
  qrd.rng_seed = seed;

  qrd.out_faces = nullptr;

  /* Run the remesher */
  QFLOW_quadriflow_remesh(&qrd, update_cb, update_cb_data);

  MEM_freeN(verttri);

  if (qrd.out_faces == nullptr) {
    /* The remeshing was canceled */
    return nullptr;
  }

  if (qrd.out_totfaces == 0) {
    /* Meshing failed */
    MEM_freeN(qrd.out_faces);
    MEM_freeN(qrd.out_verts);
    return nullptr;
  }

  /* Construct the new output mesh */
  Mesh *mesh = BKE_mesh_new_nomain(qrd.out_totverts, 0, 0, qrd.out_totfaces * 4, qrd.out_totfaces);

  for (const int i : IndexRange(qrd.out_totverts)) {
    copy_v3_v3(mesh->mvert[i].co, &qrd.out_verts[i * 3]);
  }

  for (const int i : IndexRange(qrd.out_totfaces)) {
    MPoly &poly = mesh->mpoly[i];
    const int loopstart = i * 4;
    poly.loopstart = loopstart;
    poly.totloop = 4;
    mesh->mloop[loopstart].v = qrd.out_faces[loopstart];
    mesh->mloop[loopstart + 1].v = qrd.out_faces[loopstart + 1];
    mesh->mloop[loopstart + 2].v = qrd.out_faces[loopstart + 2];
    mesh->mloop[loopstart + 3].v = qrd.out_faces[loopstart + 3];
  }

  BKE_mesh_calc_edges(mesh, false, false);
  BKE_mesh_calc_normals(mesh);

  MEM_freeN(qrd.out_faces);
  MEM_freeN(qrd.out_verts);

  return mesh;
}
#endif
