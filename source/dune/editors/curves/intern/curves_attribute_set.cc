#include "lib_generic_ptr.hh"

#include "dune_attribute.h"
#include "dune_attribute_math.hh"
#include "dune_cxt.hh"
#include "dune_report.h"
#include "dune_type_conversions.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_curves.hh"
#include "ed_geometry.hh"
#include "ed_ob.hh"
#include "ed_screen.hh"
#include "ed_transform.hh"
#include "ed_view3d.hh"

#include "api_access.hh"

#include "lang.h"

#include "ui.hh"
#include "ui_resources.hh"

#include "types_ob.h"

#include "graph.hh"
#include "graph_query.hh"

/* Delete Op */

namespace dune::ed::curves {

static bool active_attribute_poll(Cxt *C)
{
  if (!editable_curves_in_edit_mode_poll(C)) {
    return false;
  }
  Ob *ob = cxt_data_active_ob(C);
  Curves &curves_id = *static_cast<Curves *>(ob->data);
  const CustomDataLayer *layer = dune_id_attributes_active_get(&const_cast<Id &>(curves_id.id));
  if (!layer) {
    cxt_win_op_poll_msg_set(C, "No active attribute");
    return false;
  }
  if (layer->type == CD_PROP_STRING) {
    cxt_win_op_poll_msg_set(C, "Active string attribute not supported");
    return false;
  }
  return true;
}

static IndexMask retrieve_sel_elements(const Curves &curves_id,
                                       const eAttrDomain domain,
                                       IndexMaskMemory &memory)
{
  switch (domain) {
    case ATTR_DOMAIN_POINT:
      return retrieve_sel_points(curves_id, memory);
    case ATTR_DOMAIN_CURVE:
      return retrieve_sel_curves(curves_id, memory);
    default:
      lib_assert_unreachable();
      return {};
  }
}

static void validate_val(const dune::AttributeAccessor attributes,
                         const StringRef name,
                         const CPPType &type,
                         void *buf)
{
  const dune::AttributeValidator validator = attributes.lookup_validator(name);
  if (!validator) {
    return;
  }
  BUFFER_FOR_CPP_TYPE_VAL(type, validated_buf);
  LIB_SCOPED_DEFER([&]() { type.destruct(validated_buf); });

  const IndexMask single_mask(1);
  mf::ParamsBuilder params(*validator.fn, &single_mask);
  params.add_readonly_single_input(GPtr(type, buf));
  params.add_uninitialized_single_output({type, validated_buf, 1});
  mf::CxtBuilder cxt;
  validator.fn->call(single_mask, params, cxt);

  type.copy_assign(validated_buf, buf);
}

static int set_attribute_ex(Cxt *C, WinOp *op)
{
  Ob *active_ob = cxt_data_active_ob(C);
  Curves &active_curves_id = *static_cast<Curves *>(active_ob->data);

  CustomDataLayer *active_attribute = dune_id_attributes_active_get(&active_curves_id.id);
  const eCustomDataType active_type = eCustomDataType(active_attribute->type);
  const CPPType &type = *dune::custom_data_type_to_cpp_type(active_type);

  BUF_FOR_CPP_TYPE_VAL(type, buf);
  LIB_SCOPED_DEFER([&]() { type.destruct(buf); });
  const GPtr val = geometry::api_prop_for_attribute_type_retrieve_val(
      *op->ptr, active_type, buffer);

  const dune::DataTypeConversions &conversions = dune::get_implicit_type_conversions();

  for (Curves *curves_id : get_unique_editable_curves(*C)) {
    dune::CurvesGeometry &curves = curves_id->geometry.wrap();
    CustomDataLayer *layer = dune_id_attributes_active_get(&curves_id->id);
    if (!layer) {
      continue;
    }
    dune::MutableAttributeAccessor attributes = curves.attributes_for_write();
    dune::GSpanAttributeWriter attribute = attributes.lookup_for_write_span(layer->name);

    /* Use implicit conversions to try to handle the case where the active attribute has a
     * different type on multiple obs. */
    const CPPType &dst_type = attribute.span.type();
    if (&type != &dst_type && !conversions.is_convertible(type, dst_type)) {
      continue;
    }
    BUF_FOR_CPP_TYPE_VAL(dst_type, dst_buf);
    LIB_SCOPED_DEFER([&]() { dst_type.destruct(dst_buf); });
    conversions.convert_to_uninitialized(type, dst_type, val.get(), dst_buf);

    validate_val(attributes, layer->name, dst_type, dst_buffer);
    const GPtr dst_value(type, dst_buffer);

    IndexMaskMemory memory;
    const IndexMask selection = retrieve_sel_elements(*curves_id, attribute.domain, memory);
    if (sel.is_empty()) {
      attribute.finish();
      continue;
    }
    dst_type.fill_assign_indices(dst_val.get(), attribute.span.data(), sel);
    attribute.finish();

    graph_id_tag_update(&curves_id->id, ID_RECALC_GEOMETRY);
    win_ev_add_notifier(C, NC_GEOM | ND_DATA, curves_id);
  }

  return OP_FINISHED;
}

static int set_attribute_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  Ob *active_ob = cxt_data_active_ob(C);
  Curves &active_curves_id = *static_cast<Curves *>(active_ob->data);

  CustomDataLayer *active_attribute = dune_id_attributes_active_get(&active_curves_id.id);
  const dune::CurvesGeometry &curves = active_curves_id.geometry.wrap();
  const dune::AttributeAccessor attributes = curves.attributes();
  const dund::GAttributeReader attribute = attributes.lookup(active_attribute->name);
  const eAttrDomain domain = attribute.domain;

  IndexMaskMemory memory;
  const IndexMask selection = retrieve_sel_elements(active_curves_id, domain, memory);

  const CPPType &type = attribute.varray.type();

  ApiProp *prop = geometry::api_prop_for_type(*op->ptr,
                                                      dune::cpp_type_to_custom_data_type(type));
  if (api_prop_is_set(op->ptr, prop)) {
    return win_op_props_popup(C, op, ev);
  }

  BUF_FOR_CPP_TYPE_VAL(type, buf);
  LIB_SCOPED_DEFER([&]() { type.destruct(buf); });

  dune::attribute_math::convert_to_static_type(type, [&](auto dummy) {
    using T = decltype(dummy);
    const VArray<T> values_typed = attribute.varray.typed<T>();
    dune::attribute_math::DefaultMixer<T> mixer{MutableSpan(static_cast<T *>(buf), 1)};
    sel.foreach_index([&](const int i) { mixer.mix_in(0, vals_typed[i]); });
    mixer.finalize();
  });

  geometry::win_prop_for_attribute_type_set_val(*op->ptr, *prop, GPtr(type, buf));

  return win_op_props_popup(C, op, ev);
}

static void set_attribute_ui(Cxt *C, WinOp *op)
{
  uiLayout *layout = uiLayoutColumn(op->layout, true);
  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  Ob *ob = cxt_data_active_ob(C);
  Curves &curves_id = *static_cast<Curves *>(ob->data);

  CustomDataLayer *active_attribute = dune_id_attributes_active_get(&curves_id.id);
  const eCustomDataType active_type = eCustomDataType(active_attribute->type);
  const StringRefNull prop_name = geometry::api_prop_name_for_type(active_type);
  const char *name = active_attribute->name;
  uiItemR(layout, op->ptr, prop_name.c_str(), UI_ITEM_NONE, name, ICON_NONE);
}

void CURVES_OT_attribute_set(WinOpType *ot)
{
  using namespace dune::ed;
  using namespace dune::ed::curves;
  ot->name = "Set Attribute";
  ot->description = "Set vals of the active attribute for sel elements";
  ot->idname = "CURVES_OT_attribute_set";

  ot->ex = set_attribute_ex;
  ot->invoke = set_attribute_invoke;
  ot->poll = active_attribute_poll;
  ot->ui = set_attribute_ui;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  geometry::register_api_props_for_attribute_types(*ot->sapi);
}

}  // namespace dune::ed::curves
