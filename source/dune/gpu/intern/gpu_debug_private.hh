/** This interface allow GPU to manage GL objects for multiple context and threads. **/

#pragma once

#include "lib_string_ref.hh"
#include "lib_vector.hh"

namespace dune::gpu {

typedef Vector<StringRef> DebugStack;

}  // namespace dune::gpu
