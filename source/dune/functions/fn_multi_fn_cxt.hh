#pragma once

/* MFCxt is passed w every call to a multi-fn. Presently it does nothig
 * but it can be used for the following purposes:
 * - Pass debug info up and down the fn call stack.
 * - Pass reusable mem bufs to sub-fns to increase performance.
 * - Pass cached data to called fns.*/
#include "lib_utildefines.h"
#include "lib_map.hh"

namespace dune::fn {

class MFCxt

class MFCxtBuilder {
 private:
  Map<std::string, const void *> global_cxts_;

  friend MFCxt;

 public:
  template<typename T> void add_global_cxt(std::string name, const T *cxt)
  {
    global_cxts_.add_new(std::move(name), static_cast<const void *>(cxt));
  }
};

class MFCxt {
 private:
  MFCxtBuilder &builder_;

 public:
  MFContext(MFCxtBuilder &builder) : builder_(builder)
  {
  }

  template<typename T> const T *get_global_cxt(StringRef name) const
  {
    const void *cxt = builder_.global_cxts_.lookup_default_as(name, nullptr);
    /* TODO: Implement type checking. */
    return static_cast<const T *>(cxt);
  }
};

}  // namespace dune::fn
