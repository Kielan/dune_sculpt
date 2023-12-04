#include "types_space.h"

#include "mem_guardedalloc.h"

#include "lib_math_matrix.h"
#include "lib_math_vector.h"
#include "lib_math_vector.hh"
#include "lib_rect.h"

#include "dune_cxt.hh"
#include "dune_node.hh"
#include "dune_node_runtime.hh"
#include "dune_node_tree_update.hh"
#include "dune_report.h"

#include "ed_node.hh"

#include "ui.hh"
#include "ui_view2d.hh"

#include "transform.hh"
#include "transform_convert.hh"
#include "transform_snap.hh"

#include "win_api.hh"

struct TransCustomDataNode {
  View2DEdgePanData edgepan_data;
  /* Cmp if the view has changed so we can update with `transformViewUpdate`. */
  rctf viewrect_prev;
};

/* Node Transform Creation */
static void create_transform_data_for_node(TransData &td,
                                           TransData2D &td2d,
                                           Node &node,
                                           const float dpi_fac)
{
  /* account for parents (nested nodes) */
  const dune::float2 node_offset = {node.offsetx, node.offsety};
  dune::float2 loc = dune::nodeToView(&node, dune::math::round(node_offset));
  loc *= dpi_fac;

  /* use top-left corner as the transform origin for nodes */
  /* Node sys is a mix of free 2d elems and DPI sensitive UI. */
  td2d.loc[0] = loc.x;
  td2d.loc[1] = loc.y;
  td2d.loc[2] = 0.0f;
  td2d.loc2d = td2d.loc; /* current location */

  td.loc = td2d.loc;
  copy_v3_v3(td.iloc, td.loc);
  /* use node center instead of origin (top-left corner) */
  td.center[0] = td2d.loc[0];
  td.center[1] = td2d.loc[1];
  td.center[2] = 0.0f;

  memset(td.axismtx, 0, sizeof(td.axismtx));
  td.axismtx[2][2] = 1.0f;

  td.ext = nullptr;
  td.val = nullptr;

  td.flag = TD_SELECTED;
  td.dist = 0.0f;

  unit_m3(td.mtx);
  unit_m3(td.smtx);

  td.extra = &node;
}

static bool is_node_parent_sel(const Node *node)
{
  while ((node = node->parent)) {
    if (node->flag & NODE_SEL) {
      return true;
    }
  }
  return false;
}

static void createTransNodeData(Cxt * /*C*/, TransInfo *t)
{
  using namespace dune;
  using namespace dune::ed;
  SpaceNode *snode = static_cast<SpaceNode *>(t->area->spacedata.first);
  NodeTree *node_tree = snode->edittree;
  if (!node_tree) {
    return;
  }

  /* Custom data to enable edge panning during the node transform */
  TransCustomDataNode *customdata = mem_cnew<TransCustomDataNode>(__func__);
  ui_view2d_edge_pan_init(t->cxt,
                          &customdata->edgepan_data,
                          NODE_EDGE_PAN_INSIDE_PAD,
                          NODE_EDGE_PAN_OUTSIDE_PAD,
                          NODE_EDGE_PAN_SPEED_RAMP,
                          NODE_EDGE_PAN_MAX_SPEED,
                          NODE_EDGE_PAN_DELAY,
                          NODE_EDGE_PAN_ZOOM_INFLUENCE);
  customdata->viewrect_prev = customdata->edgepan_data.init_rect;

  if (t->mods & MOD_NODE_ATTACH) {
    space_node::node_insert_on_link_flags_set(*snode, *t->rgn);
  }
  else {
    space_node::node_insert_on_link_flags_clear(*snode->edittree);
  }

  t->custom.type.data = customdata;
  t->custom.type.use_free = true;

  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);

  /* Nodes don't support proportional editing and probably never will. */
  t->flag = t->flag & ~T_PROP_EDIT_ALL;

  VectorSet<Node *> nodes = space_node::get_sel_nodes(*node_tree);
  nodes.remove_if([&](Node *node) { return is_node_parent_sel(node); });
  if (nodes.is_empty()) {
    return;
  }

  tc->data_len = nodes.size();
  tc->data = mem_cnew_array<TransData>(tc->data_len, __func__);
  tc->data_2d = mem_cnew_array<TransData2D>(tc->data_len, __func__);

  for (const int i : nodes.index_range()) {
    create_transform_data_for_node(tc->data[i], tc->data_2d[i], *nodes[i], UI_SCALE_FAC);
  }
}

/* Flush Transform Nodes */
static void node_snap_grid_apply(TransInfo *t)
{
  using namespace blender;

  if (!(transform_snap_is_active(t) &&
        (t->tsnap.mode & (SCE_SNAP_TO_INCREMENT | SCE_SNAP_TO_GRID)))) {
    return;
  }

  float2 grid_size = t->snap_spatial;
  if (t->mods & MOD_PRECISION) {
    grid_size *= t->snap_spatial_precision;
  }

  /* Early exit on unusable grid size. */
  if (math::is_zero(grid_size)) {
    return;
  }

  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    for (const int i : IndexRange(tc->data_len)) {
      TransData &td = tc->data[i];
      float iloc[2], loc[2], tvec[2];
      if (td.flag & TD_SKIP) {
        continue;
      }

      if ((t->flag & T_PROP_EDIT) && (td.factor == 0.0f)) {
        continue;
      }

      copy_v2_v2(iloc, td.loc);

      loc[0] = roundf(iloc[0] / grid_size[0]) * grid_size[0];
      loc[1] = roundf(iloc[1] / grid_size[1]) * grid_size[1];

      sub_v2_v2v2(tvec, loc, iloc);
      add_v2_v2(td.loc, tvec);
    }
  }
}

static void flushTransNodes(TransInfo *t)
{
  using namespace dune::ed;
  const float dpi_fac = UI_SCALE_FAC;
  SpaceNode *snode = static_cast<SpaceNode *>(t->area->spacedata.first);

  TransCustomDataNode *customdata = (TransCustomDataNode *)t->custom.type.data;

  if (t->options & CXT_VIEW2D_EDGE_PAN) {
    if (t->state == TRANS_CANCEL) {
      ui_view2d_edge_pan_cancel(t->cxt, &customdata->edgepan_data);
    }
    else {
      /* Edge panning fns expect win coords,
       * mval is rel to rgn */
      const int xy[2] = {
          t->rgn->winrct.xmin + int(t->mval[0]),
          t->rgn->winrct.ymin + int(t->mval[1]),
      };
      ui_view2d_edge_pan_apply(t->cxt, &customdata->edgepan_data, xy);
    }
  }

  float offset[2] = {0.0f, 0.0f};
  if (t->state != TRANS_CANCEL) {
    if (!lib_rctf_compare(&customdata->viewrect_prev, &t->rgn->v2d.cur, FLT_EPSILON)) {
      /* Additional offset due to change in view2D rect. */
      lin_rctf_transform_pt_v(&t->rgn->v2d.cur, &customdata->viewrect_prev, offset, offset);
      tranformViewUpdate(t);
      customdata->viewrect_prev = t->rgnn->v2d.cur;
    }
  }

  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    node_snap_grid_apply(t);

    /* flush to 2d vector from intern used 3d vector */
    for (int i = 0; i < tc->data_len; i++) {
      TransData *td = &tc->data[i];
      TransData2D *td2d = &tc->data_2d[i];
      Node *node = static_cast<Node *>(td->extra);

      dune::float2 loc;
      add_v2_v2v2(loc, td2d->loc, offset);

      /* The node sys is a mix of free 2d elems and DPI sensitive UI. */
      loc /= dpi_fac;

      /* account for parents (nested nodes) */
      const dune::float2 node_offset = {node->offsetx, node->offsety};
      const dune::float2 new_node_location = loc - dune::math::round(node_offset);
      const dune::float2 location = dune::nodeFromView(node->parent, new_node_location);
      node->locx = location.x;
      node->locy = location.y;
    }

    /* handle intersection with noodles */
    if (tc->data_len == 1) {
      if (t->mods & MOD_NODE_ATTACH) {
        space_node::node_insert_on_link_flags_set(*snode, *t->rgn);
      }
      else {
        space_node::node_insert_on_link_flags_clear(*snode->edittree);
      }
    }
  }
}

/* Special After Transform Node */
static void special_aftertrans_update_node(Cxt *C, TransInfo *t)
{
  using namespace dune::ed;
  Main *main = cxt_data_main(C);
  SpaceNode *snode = (SpaceNode *)t->area->spacedata.first;
  NodeTree *ntree = snode->edittree;

  const bool canceled = (t->state == TRANS_CANCEL);

  if (canceled && t->remove_on_cancel) {
    /* remove sel nodes on cancel */
    if (ntree) {
      LIST_FOREACH_MUTABLE (Node *, node, &ntree->nodes) {
        if (node->flag & NODE_SEL) {
          nodeRemoveNode(main, ntree, node, true);
        }
      }
      ed_node_tree_propagate_change(C, main, ntree);
    }
  }

  if (!canceled) {
    ed_node_post_apply_transform(C, snode->edittree);
    if (t->mods & MOD_NODE_ATTACH) {
      space_node::node_insert_on_link_flags(*main, *snode);
    }
  }

  space_node::node_insert_on_link_flags_clear(*ntree);

  WinOpType *ot = win_optype_find("NODE_OT_insert_offset", true);
  lib_assert(ot);
  ApiPtr ptr;
  win_op_props_create_ptr(&ptr, ot);
  win_op_name_call_ptr(C, ot, WIN_OP_INVOKE_DEFAULT, &ptr, nullptr);
  win_op_props_free(&ptr);
}

/** \} */

TransConvertTypeInfo TransConvertType_Node = {
    /*flags*/ (T_POINTS | T_2D_EDIT),
    /*create_trans_data*/ createTransNodeData,
    /*recalc_data*/ flushTransNodes,
    /*special_aftertrans_update*/ special_aftertrans_update__node,
};
