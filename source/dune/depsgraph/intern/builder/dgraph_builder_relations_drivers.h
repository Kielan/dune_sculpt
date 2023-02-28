#pragma once

#include "lib_string_ref.hh"

#include "api_types.h"

#include "intern/builder/dgraph_builder_relations.h"

struct FCurve;

namespace dune::dgraph {

/* Helper class for determining which relations are needed between driver evaluation nodes. */
class DriverDescriptor {
 public:
  /**
   * Drivers are grouped by their RNA prefix. The prefix is the part of the RNA
   * path up to the last dot, the suffix is the remainder of the RNA path:
   *
   * code'''
   * fcu->api_path                     rna_prefix              rna_suffix
   * -------------------------------   ----------------------  ----------
   * 'color'                           ''                      'color'
   * 'rigidbody_world.time_scale'      'rigidbody_world'       'time_scale'
   * 'pose.bones["master"].location'   'pose.bones["master"]'  'location'
   * '''
   */
  StringRef api_prefix;
  StringRef api_suffix;

 public:
  DriverDescriptor(ApiPtr *id_ptr, FCurve *fcu);

  bool driver_relations_needed() const;
  bool is_array() const;
  /** Assumes that 'other' comes from the same Api group, that is, has the same api path prefix. */
  bool is_same_array_as(const DriverDescriptor &other) const;
  OpKey dgraph_key() const;

 private:
  ApiPtr *id_ptr_;
  FCurve *fcu_;
  bool driver_relations_needed_;

  ApiPtr ptr_api_;
  ApiProp *prop_api_;
  bool is_array_;

  bool determine_relations_needed();
  void split_api_path();
  bool resolve_api();
};

}  // namespace dune::deg
