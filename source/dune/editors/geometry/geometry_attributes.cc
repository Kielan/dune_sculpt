#include "mem_guardedalloc.h"

#include "types_mesh.h"
#include "types_meshdata.h"
#include "types_scene.h"

#include "dune_attribute.h"
#include "dune_cxt.h"
#include "dune_deform.h"
#include "dune_geometry_set.hh"
#include "dune_object_deform.h"
#include "dune_report.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "graph.h"

#include "wm_api.h"
#include "wm_types.h"

#include "ui_interface.h"
#include "ui_resources.h"

#include "ed_object.h"

#include "geometry_intern.hh"

namespace dune::ed::geometry {

/* Attribute Operators */
static bool geometry_attributes_poll(Cxt *C)
{
  Object *ob = ed_object_context(C);
  Id *data = (ob) ? static_cast<Id *>(ob->data) : nullptr;
  return (ob && !ID_IS_LINKED(ob) && data && !ID_IS_LINKED(data)) &&
         dune_id_attributes_supported(data);
}

static bool geometry_attributes_remove_poll(Cxt *C)
{
  if (!geometry_attributes_poll(C)) {
    return false;
  }

  Object *ob = ed_object_cxt(C);
  Id *data = (ob) ? static_cast<Id *>(ob->data) : nullptr;
  if (dune_id_attributes_active_get(data) != nullptr) {
    return true;
  }

  return false;
}

static const EnumPropItem *geometry_attribute_domain_itemf(Cxt *C,
                                                           ApiPtr *UNUSED(ptr),
                                                           ApiProp *UNUSED(prop),
                                                           bool *r_free)
{
  if (C == nullptr) {
    return DummyApi_NULL_items;
  }

  Object *ob = ed_object_cxt(C);
  if (ob == nullptr) {
    return DummyApi_NULL_items;
  }

  return api_enum_attribute_domain_itemf(static_cast<Id *>(ob->data), false, r_free);
}

static int geometry_attribute_add_ex(Cxt *C, wmOp *op)
{
  Object *ob = ed_object_context(C);
  Id *id = static_cast<Id *>(ob->data);

  char name[MAX_NAME];
  api_string_get(op->ptr, "name", name);
  CustomDataType type = (CustomDataType)api_enum_get(op->ptr, "data_type");
  AttributeDomain domain = (AttributeDomain)api_enum_get(op->ptr, "domain");
  CustomDataLayer *layer = dune_id_attribute_new(id, name, type, domain, op->reports);

  if (layer == nullptr) {
    return OP_CANCELLED;
  }

  dune_id_attributes_active_set(id, layer);

  graph_id_tag_update(id, ID_RECALC_GEOMETRY);
  wm_main_add_notifier(NC_GEOM | ND_DATA, id);

  return OP_FINISHED;
}

void GEOMETRY_OT_attribute_add(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add Geometry Attribute";
  ot->description = "Add attribute to geometry";
  ot->idname = "GEOMETRY_OT_attribute_add";

  /* api cbs */
  ot->poll = geometry_attributes_poll;
  ot->exec = geometry_attribute_add_exec;
  ot->invoke = wm_op_props_popup_confirm;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ApiProp *prop;

  prop = api_def_string(ot->sapi, "name", "Attribute", MAX_NAME, "Name", "Name of new attribute");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);

  prop = api_def_enum(ot->sapi,
                      "domain",
                      rna_enum_attribute_domain_items,
                      ATTR_DOMAIN_POINT,
                      "Domain",
                      "Type of element that attribute is stored on");
  api_def_enum_fns(prop, geometry_attribute_domain_itemf);
  api_def_prop_flag(prop, PROP_SKIP_SAVE);

  prop = api_def_enum(ot->sapi,
                      "data_type",
                      api_enum_attribute_type_items,
                      CD_PROP_FLOAT,
                      "Data Type",
                      "Type of data stored in attribute");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

static int geometry_attribute_remove_ex(Cxt *C, wmOp *op)
{
  Object *ob = ed_object_cxt(C);
  Id *id = static_cast<Id *>(ob->data);
  CustomDataLayer *layer = dune_id_attributes_active_get(id);

  if (layer == nullptr) {
    return OP_CANCELLED;
  }

  if (!dune_id_attribute_remove(id, layer, op->reports)) {
    return OP_CANCELLED;
  }

  int *active_index = dune_id_attributes_active_index_p(id);
  if (*active_index > 0) {
    *active_index -= 1;
  }

  graoh_id_tag_update(id, ID_RECALC_GEOMETRY);
  wm_main_add_notifier(NC_GEOM | ND_DATA, id);

  return OP_FINISHED;
}

void GEOMETRY_OT_attribute_remove(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Remove Geometry Attribute";
  ot->description = "Remove attribute from geometry";
  ot->idname = "GEOMETRY_OT_attribute_remove";

  /* api callbacks */
  ot->exec = geometry_attribute_remove_ex;
  ot->poll = geometry_attributes_remove_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

enum class ConvertAttributeMode {
  Generic,
  UVMap,
  VertexGroup,
  VertexColor,
};

static bool geometry_attribute_convert_poll(Cxt *C)
{
  if (!geometry_attributes_poll(C)) {
    return false;
  }

  Object *ob = ed_object_cxt(C);
  Id *data = static_cast<Id *>(ob->data);
  if (GS(data->name) != ID_ME) {
    return false;
  }
  CustomDataLayer *layer = dune_id_attributes_active_get(data);
  if (layer == nullptr) {
    return false;
  }
  return true;
}

static int geometry_attribute_convert_exec(Cxt *C, wmOp *op)
{
  Object *ob = ed_object_cxt(C);
  Id *ob_data = static_cast<ID *>(ob->data);
  CustomDataLayer *layer = dune_id_attributes_active_get(ob_data);
  const std::string name = layer->name;

  const ConvertAttributeMode mode = static_cast<ConvertAttributeMode>(
      api_enum_get(op->ptr, "mode"));

  Mesh *mesh = reinterpret_cast<Mesh *>(ob_data);
  MeshComponent mesh_component;
  mesh_component.replace(mesh, GeometryOwnershipType::Editable);

  /* General conversion steps are always the same:
   * 1. Convert old data to right domain and data type.
   * 2. Copy the data into a new array so that it does not depend on the old attribute anymore.
   * 3. Delete the old attribute.
   * 4. Create a new attribute based on the previously copied data. */
  switch (mode) {
    case ConvertAttributeMode::Generic: {
      const AttributeDomain dst_domain = static_cast<AttributeDomain>(
          api_enum_get(op->ptr, "domain"));
      const CustomDataType dst_type = static_cast<CustomDataType>(
          api_enum_get(op->ptr, "data_type"));

      if (ELEM(dst_type, CD_PROP_STRING, CD_MLOOPCOL)) {
        dune_report(op->reports, RPT_ERROR, "Cannot convert to the selected type");
        return OP_CANCELLED;
      }

      GVArray src_varray = mesh_component.attribute_get_for_read(name, dst_domain, dst_type);
      const CPPType &cpp_type = src_varray.type();
      void *new_data = mem_malloc_arrayn(src_varray.size(), cpp_type.size(), __func__);
      src_varray.materialize_to_uninitialized(new_data);
      mesh_component.attribute_try_delete(name);
      mesh_component.attribute_try_create(name, dst_domain, dst_type, AttributeInitMove(new_data));
      break;
    }
    case ConvertAttributeMode::UVMap: {
      MLoopUV *dst_uvs = static_cast<MLoopUV *>(
          mem_calloc_arrayn(mesh->totloop, sizeof(MLoopUV), __func__));
      VArray<float2> src_varray = mesh_component.attribute_get_for_read<float2>(
          name, ATTR_DOMAIN_CORNER, {0.0f, 0.0f});
      for (const int i : IndexRange(mesh->totloop)) {
        copy_v2_v2(dst_uvs[i].uv, src_varray[i]);
      }
      mesh_component.attribute_try_delete(name);
      CustomData_add_layer_named(
          &mesh->ldata, CD_MLOOPUV, CD_ASSIGN, dst_uvs, mesh->totloop, name.c_str());
      break;
    }
    case ConvertAttributeMode::VertexColor: {
      MLoopCol *dst_colors = static_cast<MLoopCol *>(
          mem_calloc_arrayn(mesh->totloop, sizeof(MLoopCol), __func__));
      VArray<ColorGeometry4f> src_varray = mesh_component.attribute_get_for_read<ColorGeometry4f>(
          name, ATTR_DOMAIN_CORNER, ColorGeometry4f{0.0f, 0.0f, 0.0f, 1.0f});
      for (const int i : IndexRange(mesh->totloop)) {
        ColorGeometry4b encoded_color = src_varray[i].encode();
        copy_v4_v4_uchar(&dst_colors[i].r, &encoded_color.r);
      }
      mesh_component.attribute_try_delete(name);
      CustomData_add_layer_named(
          &mesh->ldata, CD_MLOOPCOL, CD_ASSIGN, dst_colors, mesh->totloop, name.c_str());
      break;
    }
    case ConvertAttributeMode::VertexGroup: {
      Array<float> src_weights(mesh->totvert);
      VArray<float> src_varray = mesh_component.attribute_get_for_read<float>(
          name, ATTR_DOMAIN_POINT, 0.0f);
      src_varray.materialize(src_weights);
      mesh_component.attribute_try_delete(name);

      DeformGroup *defgroup = dune_object_defgroup_new(ob, name.c_str());
      const int defgroup_index = lib_findindex(dune_id_defgroup_list_get(&mesh->id), defgroup);
      MDeformVert *dverts = dune_object_defgroup_data_create(&mesh->id);
      for (const int i : IndexRange(mesh->totvert)) {
        const float weight = src_weights[i];
        if (weight > 0.0f) {
          dune_defvert_add_index_notest(dverts + i, defgroup_index, weight);
        }
      }
      break;
    }
  }

  int *active_index = dune_id_attributes_active_index_p(&mesh->id);
  if (*active_index > 0) {
    *active_index -= 1;
  }

  graph_id_tag_update(&mesh->id, ID_RECALC_GEOMETRY);
  wm_main_add_notifier(NC_GEOM | ND_DATA, &mesh->id);

  return OPERATOR_FINISHED;
}

static void geometry_attribute_convert_ui(Cxt *UNUSED(C), wmOp *op)
{
  uiLayout *layout = op->layout;
  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  uiItemR(layout, op->ptr, "mode", 0, nullptr, ICON_NONE);

  const ConvertAttributeMode mode = static_cast<ConvertAttributeMode>(
      api_enum_get(op->ptr, "mode"));

  if (mode == ConvertAttributeMode::Generic) {
    uiItemR(layout, op->ptr, "domain", 0, nullptr, ICON_NONE);
    uiItemR(layout, op->ptr, "data_type", 0, nullptr, ICON_NONE);
  }
}

static int geometry_attribute_convert_invoke(Cxt *C,
                                             wmOp *op,
                                             const wmEvent *UNUSED(event))
{
  return wm_op_props_dialog_popup(C, op, 300);
}

void GEOMETRY_OT_attribute_convert(wmOpType *ot)
{
  ot->name = "Convert Attribute";
  ot->description = "Change how the attribute is stored";
  ot->idname = "GEOMETRY_OT_attribute_convert";

  ot->invoke = geometry_attribute_convert_invoke;
  ot->exec = geometry_attribute_convert_ex;
  ot->poll = geometry_attribute_convert_poll;
  ot->ui = geometry_attribute_convert_ui;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  static EnumPropItem mode_items[] = {
      {int(ConvertAttributeMode::Generic), "GENERIC", 0, "Generic", ""},
      {int(ConvertAttributeMode::UVMap), "UV_MAP", 0, "UV Map", ""},
      {int(ConvertAttributeMode::VertexGroup), "VERTEX_GROUP", 0, "Vertex Group", ""},
      {int(ConvertAttributeMode::VertexColor), "VERTEX_COLOR", 0, "Vertex Color", ""},
      {0, nullptr, 0, nullptr, nullptr},
  };

  ApiProp *prop;

  api_def_enum(
      ot->sapi, "mode", mode_items, static_cast<int>(ConvertAttributeMode::Generic), "Mode", "");

  prop = api_def_enum(ot->srna,
                      "domain",
                      api_enum_attribute_domain_items,
                      ATTR_DOMAIN_POINT,
                      "Domain",
                      "Which geometry element to move the attribute to");
  api_def_enum_fns(prop, geometry_attribute_domain_itemf);

  api_def_enum(
      ot->sapi, "data_type", api_enum_attribute_type_items, CD_PROP_FLOAT, "Data Type", "");
}

}  // namespace dune::ed::geometry
