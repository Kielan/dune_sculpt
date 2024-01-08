#pragma once

/* An MFCxt is passed along with every call to a multi-fn. Right now it does nothig
 * but it can be used for the following purposes:
 * - Pass debug infor up and down the fn call stack.
 * - Pass reusable mem bufs to sub-fns to increase performance.
 * - Pass cached data to called fns.*/

#include "BLI_utildefines.h"

#include "lib_map.hh"

namespace dune::fn {

class MFCxt

class MFCxtBuilder {
 private:
  Map<std::string, const void *> global_contexts_;

  friend MFContext;

 public:
  template<typename T> void add_global_context(std::string name, const T *context)
  {
    global_contexts_.add_new(std::move(name), static_cast<const void *>(context));
  }
};

class MFContext {
 private:
  MFContextBuilder &builder_;

 public:
  MFContext(MFContextBuilder &builder) : builder_(builder)
  {
  }

  template<typename T> const T *get_global_context(StringRef name) const
  {
    const void *context = builder_.global_contexts_.lookup_default_as(name, nullptr);
    /* TODO: Implement type checking. */
    return static_cast<const T *>(context);
  }
};

}  // namespace dune::fn
