#pragma once

struct Base;
struct Id;
struct DMain;
struct DObject;
struct DPoseChannel;

namespace dune {
namespace dgraph {

struct DGraph;
class DGraphBuilderCache;

class DGraphBuilder {
 public:
  virtual ~DGraphBuilder() = default;

  virtual bool need_pull_base_into_graph(Base *base);

  virtual bool check_pchan_has_bbone(Object *object, const DPoseChannel *pchan);
  virtual bool check_pchan_has_bbone_segments(Object *object, const DPoseChannel *pchan);
  virtual bool check_pchan_has_bbone_segments(Object *object, const char *bone_name);

 protected:
  /* NOTE: The builder does NOT take ownership over any of those resources. */
  DGraphBuilder(Main *dmain, DGraph *graph, DGraphBuilderCache *cache);

  /* State which never changes, same for the whole builder time. */
  Main *dmain_;
  DGraph *graph_;
  DGraphBuilderCache *cache_;
};

bool dgraph_check_id_in_dgraph(const DGraph *graph, Id *id_orig);
bool dgraph_check_base_in_dgraph(const DGraph *graph, Base *base);
void dgraph_build_finalize(Main *dmain, DGraph *graph);

}  // namespace dgraph
}  // namespace dune
