#pragma once

struct GraphEditorUpdateCtx;
struct Id;

namespace dune {
namespace graph {

void graph_editors_id_update(const GraphEditorUpdateCtx *update_ctx, struct Id *id);

void graph_editors_scene_update(const GraphEditorUpdateCtx *update_ctx, bool updated);

}  // namespace graph
}  // namespace dune
