#include "ui_interface.hh"
#include "ui_resources.hh"

#include "gpu_material.h"

#include "com_shader_node.hh"

#include "node_composite_util.hh"

/* ALPHAOVER */

namespace dune::nodes::node_composite_alpha_over_cc {

NODE_STORAGE_FNS(NodeTwoFloats)

static void cmp_node_alphaover_declare(NodeDeclBuilder &b)
{
  b.add_input<decl::Float>("Fac")
      .default_val(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .compositor_domain_priority(2);
  b.add_input<decl::Color>("Img")
      .default_val({1.0f, 1.0f, 1.0f, 1.0f})
      .compositor_domain_priority(0);
  b.add_input<decl::Color>("Img", "Img_001")
      .default_val({1.0f, 1.0f, 1.0f, 1.0f})
      .compositor_domain_priority(1);
  b.add_output<decl::Color>("Img");
}

static void node_alphaover_init(NodeTree * /*ntree*/, Node *node)
{
  node->storage = mem_cnew<NodeTwoFloats>(__func__);
}

static void node_composit_buts_alphaover(uiLayout *layout, Cxt * /*C*/, ApiPtr *ptr)
{
  uiLayout *col;

  col = uiLayoutColumn(layout, true);
  uiItemR(col, ptr, "use_premultiply", UI_ITEM_R_SPLIT_EMPTY_NAME, nullptr, ICON_NONE);
  uiItemR(col, ptr, "premul", UI_ITEM_R_SPLIT_EMPTY_NAME, nullptr, ICON_NONE);
}

using namespace dune::realtime_compositor;

class AlphaOverShaderNode : public ShaderNode {
 public:
  using ShaderNode::ShaderNode;

  void compile(GPUMaterial *material) override
  {
    GPUNodeStack *inputs = get_inputs_array();
    GPUNodeStack *outputs = get_outputs_array();

    const float premultiply_factor = get_premultiply_factor();
    if (premultiply_factor != 0.0f) {
      gpu_stack_link(material,
                     &bnode(),
                     "node_composite_alpha_over_mixed",
                     inputs,
                     outputs,
                     gpu_uniform(&premultiply_factor));
      return;
    }

    if (get_use_premultiply()) {
      gpu_stack_link(material, &bnode(), "node_composite_alpha_over_key", inputs, outputs);
      return;
    }

    gpu_stack_link(material, &bnode(), "node_composite_alpha_over_premultiply", inputs, outputs);
  }

  bool get_use_premultiply()
  {
    return node().custom1;
  }

  float get_premultiply_factor()
  {
    return node_storage(node()).x;
  }
};

static ShaderNode *get_compositor_shader_node(Node node)
{
  return new AlphaOverShaderNode(node);
}

}  // namespace dune::nodes::node_composite_alpha_over_cc

void register_node_type_cmp_alphaover()
{
  namespace file_ns = dune::nodes::node_composite_alpha_over_cc;

  static NodeType ntype;

  cmp_node_type_base(&ntype, CMP_NODE_ALPHAOVER, "Alpha Over", NODE_CLASS_OP_COLOR);
  ntype.decl = file_ns::cmp_node_alphaover_decl;
  ntype.draw_btns = file_ns::node_composit_buts_alphaover;
  ntype.initfn = file_ns::node_alphaover_init;
  node_type_storage(
      &ntype, "NodeTwoFloats", node_free_standard_storage, node_copy_standard_storage);
  ntype.get_compositor_shader_node = file_ns::get_compositor_shader_node;

  nodeRegisterType(&ntype);
}
