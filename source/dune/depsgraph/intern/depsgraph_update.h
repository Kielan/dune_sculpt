#pragma once

struct DGraphEditorUpdateContext;
struct Id;

namespace dune {
namespace dgraph {

void dgraph_editors_id_update(const DGraphEditorUpdateContext *update_ctx, struct Id *id);

void dgraph_editors_scene_update(const DGraphEditorUpdateContext *update_ctx, bool updated);

}  // namespace dgraph
}  // namespace dune
