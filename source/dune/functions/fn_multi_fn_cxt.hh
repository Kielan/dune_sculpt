#pragma once

/* MFCx is passed w every call to a multi-fn.
 * It does nothing; it could be used for the purposes:
 * - Pass debug info up and down the fn call stack.
 * - Pass reusable mem bufs to sub-fns to increase performance.
 * - Pass cached data to called fns.*/
#include "lib_utildefines.h"
#include "lib_map.hh"

namespace dune::fn {

class MFCx

class MFCxBuilder {
 private:
  Map<std::string, const void *> global_cxs_;

  friend MFCx;

 public:
  template<typename T> void add_global_cx(std::string name, const T *cx)
  {
    global_cxs_.add_new(std::move(name), static_cast<const void *>(cx));
  }
};

class MFCx {
 private:
  MFCxBuilder &builder_;

 public:
  MFCx(MFCxBuilder &builder) : builder_(builder)
  {
  }

  template<typename T> const T *get_global_cx(StringRef name) const
  {
    const void *cx = builder_.global_cxs_.lookup_default_as(name, nullptr);
    /* TODO: Implem type checking. */
    return static_cast<const T *>(cx);
  }
};

}  // namespace dune::fn
