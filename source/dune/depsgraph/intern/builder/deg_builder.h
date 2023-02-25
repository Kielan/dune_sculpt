#pragma once

struct Base;
struct ID;
struct Main;
struct Object;
struct DPoseChannel;

namespace dune {
namespace deg {

struct Depsgraph;
class DepsgraphBuilderCache;

class DepsgraphBuilder {
 public:
  virtual ~DepsgraphBuilder() = default;

  virtual bool need_pull_base_into_graph(Base *base);

  virtual bool check_pchan_has_bbone(Object *object, const DPoseChannel *pchan);
  virtual bool check_pchan_has_bbone_segments(Object *object, const DPoseChannel *pchan);
  virtual bool check_pchan_has_bbone_segments(Object *object, const char *bone_name);

 protected:
  /* NOTE: The builder does NOT take ownership over any of those resources. */
  DepsgraphBuilder(Main *dmain, Depsgraph *graph, DepsgraphBuilderCache *cache);

  /* State which never changes, same for the whole builder time. */
  Main *dmain_;
  Depsgraph *graph_;
  DepsgraphBuilderCache *cache_;
};

bool deg_check_id_in_depsgraph(const Depsgraph *graph, ID *id_orig);
bool deg_check_base_in_depsgraph(const Depsgraph *graph, Base *base);
void deg_graph_build_finalize(Main *dmain, Depsgraph *graph);

}  // namespace deg
}  // namespace dune
