#include <cmath>

#include "lib_math_vector_types.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"

#include "gpu_shader.h"

#include "COM_node_operation.hh"
#include "COM_utilities.hh"

#include "node_composite_util.hh"

/* SCALAR MATH ******************** */
namespace dune::nodes::node_composite_boxmask_cc {

NODE_STORAGE_FUNCS(NodeBoxMask)

static void cmp_node_boxmask_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Mask")
      .default_val(0.0f)
      .min(0.0f)
      .max(1.0f)
      .compositor_domain_priority(0);
  b.add_input<decl::Float>("Val")
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .compositor_domain_priority(1);
  b.add_output<decl::Float>("Mask");
}

static void node_composit_init_boxmask(NodeTree * /*ntree*/, bNode *node)
{
  NodeBoxMask *data = mem_cnew<NodeBoxMask>(__func__);
  data->x = 0.5;
  data->y = 0.5;
  data->width = 0.2;
  data->height = 0.1;
  data->rotation = 0.0;
  node->storage = data;
}

static void node_composit_buts_boxmask(uiLayout *layout, bContext * /*C*/, PointerRNA *ptr)
{
  uiLayout *row;

  row = uiLayoutRow(layout, true);
  uiItemR(row, ptr, "x", UI_ITEM_R_SPLIT_EMPTY_NAME, nullptr, ICON_NONE);
  uiItemR(row, ptr, "y", UI_ITEM_R_SPLIT_EMPTY_NAME, nullptr, ICON_NONE);

  row = uiLayoutRow(layout, true);
  uiItemR(row, ptr, "width", UI_ITEM_R_SPLIT_EMPTY_NAME | UI_ITEM_R_SLIDER, nullptr, ICON_NONE);
  uiItemR(row, ptr, "height", UI_ITEM_R_SPLIT_EMPTY_NAME | UI_ITEM_R_SLIDER, nullptr, ICON_NONE);

  uiItemR(layout, ptr, "rotation", UI_ITEM_R_SPLIT_EMPTY_NAME, nullptr, ICON_NONE);
  uiItemR(layout, ptr, "mask_type", UI_ITEM_R_SPLIT_EMPTY_NAME, nullptr, ICON_NONE);
}

using namespace dune::realtime_compositor;

class BoxMaskOp : public NodeOp {
 public:
  using NodeOp::NodeOp;

  void execute() override
  {
    GPUShader *shader = context().get_shader(get_shader_name());
    gpu_shader_bind(shader);

    const Domain domain = compute_domain();

    gpu_shader_uniform_2iv(shader, "domain_size", domain.size);

    GPU_shader_uniform_2fv(shader, "location", get_location());
    GPU_shader_uniform_2fv(shader, "size", get_size() / 2.0f);
    GPU_shader_uniform_1f(shader, "cos_angle", std::cos(get_angle()));
    GPU_shader_uniform_1f(shader, "sin_angle", std::sin(get_angle()));

    const Result &input_mask = get_input("Mask");
    input_mask.bind_as_texture(shader, "base_mask_tx");

    const Result &value = get_input("Value");
    value.bind_as_texture(shader, "mask_value_tx");

    Result &output_mask = get_result("Mask");
    output_mask.alloc_texture(domain);
    output_mask.bind_as_image(shader, "output_mask_img");

    compute_dispatch_threads_at_least(shader, domain.size);

    input_mask.unbind_as_texture();
    value.unbind_as_texture();
    output_mask.unbind_as_image();
    GPU_shader_unbind();
  }

  Domain compute_domain() override
  {
    if (get_input("Mask").is_single_value()) {
      return Domain(context().get_compositing_region_size());
    }
    return get_input("Mask").domain();
  }

  CMPNodeMaskType get_mask_type()
  {
    return (CMPNodeMaskType)bnode().custom1;
  }

  const char *get_shader_name()
  {
    switch (get_mask_type()) {
      default:
      case CMP_NODE_MASKTYPE_ADD:
        return "compositor_box_mask_add";
      case CMP_NODE_MASKTYPE_SUBTRACT:
        return "compositor_box_mask_subtract";
      case CMP_NODE_MASKTYPE_MULTIPLY:
        return "compositor_box_mask_multiply";
      case CMP_NODE_MASKTYPE_NOT:
        return "compositor_box_mask_not";
    }
  }

  float2 get_location()
  {
    return float2(node_storage(bnode()).x, node_storage(bnode()).y);
  }

  float2 get_size()
  {
    return float2(node_storage(bnode()).width, node_storage(bnode()).height);
  }

  float get_angle()
  {
    return node_storage(bnode()).rotation;
  }
};

static NodeOperation *get_compositor_operation(Context &context, DNode node)
{
  return new BoxMaskOperation(context, node);
}

}  // namespace dune::nodes::node_composite_boxmask_cc

void register_node_type_cmp_boxmask()
{
  namespace file_ns = dune::nodes::node_composite_boxmask_cc;

  static NodeType ntype;

  cmp_node_type_base(&ntype, CMP_NODE_MASK_BOX, "Box Mask", NODE_CLASS_MATTE);
  ntype.declare = file_ns::cmp_node_boxmask_declare;
  ntype.draw_btns = file_ns::node_composit_buts_boxmask;
  ntype.initfn = file_ns::node_composit_init_boxmask;
  node_type_storage(&ntype, "NodeBoxMask", node_free_standard_storage, node_copy_standard_storage);
  ntype.get_compositor_op = file_ns::get_compositor_op;

  nodeRegisterType(&ntype);
}
