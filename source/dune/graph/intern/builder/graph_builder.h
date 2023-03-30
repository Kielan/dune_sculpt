#pragma once

struct Base;
struct Id;
struct Main;
struct Object;
struct PoseChannel;

namespace dune {
namespace graph {

struct Graph;
class GraphBuilderCache;

class GraphBuilder {
 public:
  virtual ~GraphBuilder() = default;

  virtual bool need_pull_base_into_graph(Base *base);

  virtual bool check_pchan_has_bbone(Object *object, const PoseChannel *pchan);
  virtual bool check_pchan_has_bbone_segments(Object *object, const PoseChannel *pchan);
  virtual bool check_pchan_has_bbone_segments(Object *object, const char *bone_name);

 protected:
  /* NOTE: The builder does NOT take ownership over any of those resources. */
  GraphBuilder(Main *dmain, Graph *graph, GraphBuilderCache *cache);

  /* State which never changes, same for the whole builder time. */
  Main *dmain_;
  Graph *graph_;
  GraphBuilderCache *cache_;
};

bool graph_check_id_in_graph(const Graph *graph, Id *id_orig);
bool graph_check_base_in_graph(const Graph *graph, Base *base);
void graph_build_finalize(Main *dmain, Graph *graph);

}  // namespace graph
}  // namespace dune
