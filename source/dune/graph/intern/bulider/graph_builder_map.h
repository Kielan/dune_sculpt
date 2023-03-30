#pragma once

#include "intern/graph_type.h"

struct Id;

namespace dune {
namespace graph {

class BuilderMap {
 public:
  enum {
    TAG_ANIMATION = (1 << 0),
    TAG_PARAMS = (1 << 1),
    TAG_TRANSFORM = (1 << 2),
    TAG_GEOMETRY = (1 << 3),

    TAG_SCENE_COMPOSITOR = (1 << 4),
    TAG_SCENE_SEQUENCER = (1 << 5),
    TAG_SCENE_AUDIO = (1 << 6),

    /* All ID components has been built. */
    TAG_COMPLETE = (TAG_ANIMATION | TAG_PARAMS | TAG_TRANSFORM | TAG_GEOMETRY |
                    TAG_SCENE_COMPOSITOR | TAG_SCENE_SEQUENCER | TAG_SCENE_AUDIO),
  };

  /* Check whether given ID is already handled by builder (or if it's being handled). */
  bool checkIsBuilt(Id *id, int tag = TAG_COMPLETE) const;

  /* Tag given Id as handled/built. */
  void tagBuild(Id *id, int tag = TAG_COMPLETE);

  /* Combination of previous two functions, returns truth if ID was already handled, or tags is
   * handled otherwise and return false. */
  bool checkIsBuiltAndTag(Id *id, int tag = TAG_COMPLETE);

  template<typename T> bool checkIsBuilt(T *datablock, int tag = TAG_COMPLETE) const
  {
    return checkIsBuilt(&datablock->id, tag);
  }
  template<typename T> void tagBuild(T *datablock, int tag = TAG_COMPLETE)
  {
    tagBuild(&datablock->id, tag);
  }
  template<typename T> bool checkIsBuiltAndTag(T *datablock, int tag = TAG_COMPLETE)
  {
    return checkIsBuiltAndTag(&datablock->id, tag);
  }

 protected:
  int getIdTag(Id *id) const;

  Map<Id *, int> id_tags_;
};

}  // namespace graph
}  // namespace dune
