#pragma once

/* A `LazyFn` encapsulates a computation which has inputs, outputs and potentially side
 * effects. Most importantly aa `LazyFn` supports laziness in its inputs and outputs:
 * - Only outputs that are actually used have to be computed.
 * - Inputs can be requested lazily based on which 
 *   outputs are used or what side effects the fn has.
 *
 * A lazy-fn that uses laziness may be ex more than once. The most common example is
 * the geometry nodes switch node. Depending on a condition input, it decides which 1 of the
 * other inputs is actually used.
 * From the perspective of the switch node, its ex works as
 * follows:
 * 1. The switch node is 1st ex. It sees that the output is used. Now it requests the
 *    condition input from the caller and exits.
 * 2. Once the caller is able to provide the condition input the switch node is ex again.
 *    This time it retrieves the condition and requests  of the other inputs. Then the node
 *    exits again, giving back ctrl to the caller.
 * 3. When the caller computed the 2nd requested input the switch node ex's a last time.
 *    This time it retrieves the new input and forwards it to the output.
 *
 * abstract:, a lazy-fn can be thought of like a state machine.
 * Every time it is executed, it
 * advances its state until all required outputs are rdy.
 *
 * The lazy-fn interface is designed to support composition of many such fns into a new
 * lazy-fns, all while keeping the laziness working. Example, in geometry nodes a switch
 * node in a node group should still be able to decide whether a node in the parent group will be
 * ex or not. This is essential to avoid doing unnecessary work.
 *
 * The lazy-fn sys consists of multiple core components:
 * - The interface of a lazy-fn itself including its calling convention.
 * - A graph data struct allows composing many lazy-fns by connecting their inputs
 *   and outputs.
 * - An executor that allows multi-threaded ex or such a graph. */

#include "lib_cpp_type.hh"
#include "lib_fn_ref.hh"
#include "lib_generic_ptr.hh"
#include "lib_linear_allocator.hh"
#include "lib_vector.hh"

#include <atomic>
#include <thread>

#ifndef NDEBUG
#  define FN_LAZY_FN_DEBUG_THREADS
#endi
namespace dune::fn::lazy_fn {

enum class ValUsage : uint8_t {
  /* The val is definitely used and therefore has to be computed. */
  Used,
  /* It's unknown whether this val will be used or not. Computing it is ok but the result may be
   * discarded. */
  Maybe,
  /* The val will definitely not be used. It can still be computed but the result will be
   * discarded in all cases */
  Unused,
};

class LazyF!;

/* Extension of UserData that is thread-local. This avoids accessing e.g.
 * `EnumThreadSpecific.local()` in every nested lazy-fn bc the thread local
 * data is passed in by the caller. */
class LocalUserData {
 public:
  virtual ~LocalUserData() = default;
};

/* This allows passing arbitrary data into a lazy-fn during ex. For that, UserData
 * has to be subclassed. This mainly exists bc it's more type safe than passing a `void *
 * w no type info attached.
 *
 * Some lazy-fns may expect to find a certain type of user data when ex. */
class UserData {
 public:
  virtual ~UserData() = default;

  /* Get thread local data for this user-data and the current thread. */
  virtual destruct_ptr<LocalUserData> get_local(LinearAllocator<> &allocator);
};

/* Passed to the lazy-fn when it is ex. */
struct Context {
  /* If the lazy-fn has some state (which only makes sense when it is ex more than once
   * to finish its job), the state is stored here. This points to mem returned from
   * LazyFn::init_storage. */
  void *storage;
  /* Custom user data that can be used in the fn */
  UserData *user_data;
  /* Custom user data that is local to the thread that ex the lazy-fn. */
  LocalUserData *local_user_data;

  Context(void *storage, UserData *user_data, LocalUserData *local_user_data)
      : storage(storage), user_data(user_data), local_user_data(local_user_data)
  {
  }
};

/* Defines the calling convention for a lazy-fn. During ex, a lazy-fn retrieves
 * its inputs and sets the outputs through Params. */
class Params {
 public:
  /* The lazy-fn this Params has been prepared for. */
  const LazyFn &fn_;
#ifdef FN_LAZY_FN_DEBUG_THREADS
  std::thread::id main_thread_id_;
  std::atomic<bool> allow_multi_threading_;
#endif

 public:
  Params(const LazyFn &fn, bool allow_multi_threading_initially);

  /* Get a ptr to an input val if the value is available alrdy. Otherwise null is returned.
   * The LazyFn must leave returned ob in an initialized state, but can move from it. */
  void *try_get_input_data_ptr(int index) const;

  /* Same as try_get_input_data_ptr, but if the data is not yet available, request it. This makes
   * sure that the data will be available in a future ex of the LazyFn. */
  void *try_get_input_data_ptr_or_request(int index);

  /* Get a ptr to where the output val should be stored.
   * The val at the ptr is in an uninit state at first.
   * The LazyFn is responsible for init the val.
   * After the output has been init to its final val, output_set has to be called. */
  void *get_output_data_ptr(int index);

  /* Call this after the output val is init. After this is called the val must not be
   * touched anymore. It may be moved or destructed immediately. */
  void output_set(int 

  /* Allows the LazyFn to check whether an output was computed alrdy wo keeping
   * track of it itself. */
  bool output_was_set(int index) const;

  /* Can be used to detect which outputs have to be computed. */
  ValUsage get_output_usage(int index) const;

  /* Tell the caller of the LazyFn that a specific input will definitely not be used.
   * Only an input that was not ValUsage::Used can become unused. */
  void set_input_unused(int index);

  /* Typed util methods that wrap the methods above. */
  template<typename T> T extract_input(int index);
  template<typename T> T &get_input(int index) const;
  template<typename T> T *try_get_input_data_ptr(int index) const;
  template<typename T> T *try_get_input_data_ptr_or_request(int index);
  template<typename T> void set_output(int index, T &&val);

  /* Returns true when the lazy-fn is now allowed to use multi-threading when interacting
   * w this Params. That means, it is allowed to call non-const methods from diff threads. */
  bool try_enable_multi_threading();

 private:
  void assert_valid_thread() const;

  /* Methods that need to be implemented by subclasses. Those are separate from the non-virtual
   * methods above to make it easy to insert additional debugging logic on top of the
   * implementations. */
  virtual void *try_get_input_data_ptr_impl(int index) const = 0;
  virtual void *try_get_input_data_ptr_or_request_impl(int index) = 0;
  virtual void *get_output_data_ptr_impl(int index) = 0;
  virtual void output_set_impl(int index) = 0;
  virtual bool output_was_set_impl(int index) const = 0;
  virtual ValueUsage get_output_usage_impl(int index) const = 0;
  virtual void set_input_unused_impl(int index) = 0;
  virtual bool try_enable_multi_threading_impl();
};

/* Describes an input of a LazyFn. */
struct Input {
  /* Name used for debugging purposes. The string has to be static or has to be owned by something
   * else. */
  const char *debug_name;
  /* Data type of this input. */
  const CPPType *type;
  /* Can be used to indicate a caller or this fn if this input is used statically before
   * ex it the 1st time. Technically not needed but can improve efficiency bc
   * a round-trip through the `ex` method can be avoided.
   *
   * When this is ValUsage::Used, caller must ensure that the input is definitely
   * available when the ex method is first called. The ex method does not have to check
   * whether the val is actually available.  */
  ValUsage usage;

  Input(const char *debug_name, const CPPType &type, const ValUsage usage = ValueUsage::Used)
      : debug_name(debug_name), type(&type), usage(usage)
  {
  }
};

struct Output {
  /* Name used for debugging purposes. The string must be static or must be owned by something
   * else. */
  const char *debug_name;
  /* Data type of this output. */
  const CPPType *type = nullptr;

  Output(const char *debug_name, const CPPType &type) : debug_name(debug_name), type(&type) {}
};

/* A fn that can compute outputs and request inputs lazily. For more details see the comment
 * at the top of the file. */
class LazyFn {
 protected:
  const char *debug_name_ = "unknown";
  Vector<Input> inputs_;
  Vector<Output> outputs_;
  /* Allow ex the fn even if prev requested vals are not yet available. */
  bool allow_missing_requested_inputs_ = false;

 public:
  virtual ~LazyFn() = default;

  /* Get a name of the fn or an input or output. This is mainly used for debugging.
   * These are virtual fns because the names are often not used outside of debugging
   * workflows. This way the names are only generated when they are actually needed. */
  virtual std::string name() const;
  virtual std::string input_name(int index) const;
  virtual std::string output_name(int index) const;

  /* Alloc storage for this fn. The storage will be passed to every call to ex.
   * If the fn does not keep track of any state, this does not have to be implemented. */
  virtual void *init_storage(LinearAllocator<> &allocator) const;

  /* Destruct the storage created in init_storage. */
  virtual void destruct_storage(void *storage) const;

  /* Calls `fn` with the input indices that the given `output_index` may depend on. By default
   * every output depends on every input */
  virtual void possible_output_dependencies(int output_index,
                                            FnRef<void(Span<int>)> fn) const;

  /* Inputs of the fn */
  Span<Input> inputs() const;
  /* Outputs of the fn  */
  Span<Output> outputs() const;

  /* During ex the fn retrieves inputs and sets outputs in params. For some
   * fns, this method is called more than once. After ex, the fn either has
   * computed all required outputs or is waiting for more inputs. */
  void ex(Params &params, const Cxt &cxt) const;

  /* Util to check that the guarantee by Input::usage is followed. */
  bool always_used_inputs_available(const Params &params) const;

  /* If true, the fn can be ex even when some requested inputs are not available yet.
   * This allows the fn to make some progress and maybe to compute some outputs that are
   * passed into this fn again (lazy-fn graphs may contain cycles as long as there
   * aren't actually data deps). */
  bool allow_missing_requested_inputs() const
  {
    return allow_missing_requested_inputs_;
  }

 private:
  /* Must be implemented by subclasses. This is separate from ex so that additional
   * debugging logic can be implemented in ex. */
  virtual void ex_impl(Params &params, const Cxt &cxt) const = 0;
};

/* LazyFn Inline Methods */
inline Span<Input> LazyFn::inputs() const
{
  return inputs_;
}

inline Span<Output> LazyFn::outputs() const
{
  return outputs_;
}

inline void LazyFn::ex(Params &params, const Cxt &cxt) const
{
  lib_assert(this->always_used_inputs_available(params));
  this->ex_impl(params, cxt);
}

/* Params Inline Methods */
inline Params::Params(const LazyFn &fn,
                      [[maybe_unused]] bool allow_multi_threading_initially)
    : fn_(fn)
#ifdef FN_LAZY_DEBUG_THREADS
      ,
      main_thread_id_(std::this_thread::get_id()),
      allow_multi_threading_(allow_multi_threading_initially)
#endif
{
}

inline void *Params::try_get_input_data_ptr(const int index) const
{
  lib_assert(index >= 0 && index < fn_.inputs().size());
  return this->try_get_input_data_ptr_impl(index);
}

inline void *Params::try_get_input_data_ptr_or_request(const int index)
{
  lib_assert(index >= 0 && index < fn_.inputs().size());
  this->assert_valid_thread();
  return this->try_get_input_data_ptr_or_request_impl(index);
}

inline void *Params::get_output_data_ptr(const int index)
{
  lib_assert(index >= 0 && index < fn_.outputs().size());
  this->assert_valid_thread();
  return this->get_output_data_ptr_impl(index);
}

inline void Params::output_set(const int index)
{
  lib_assert(index >= 0 && index < fn_.outputs().size());
  this->assert_valid_thread();
  this->output_set_impl(index);
}

inline bool Params::output_was_set(const int index) const
{
  lib_assert(index >= 0 && index < fn_.outputs().size());
  return this->output_was_set_impl(index);
}

inline ValUsage Params::get_output_usage(const int index) const
{
  lib_assert(index >= 0 && index < fn_.outputs().size());
  return this->get_output_usage_impl(index);
}

inline void Params::set_input_unused(const int index)
{
  lib_assert(index >= 0 && index < fn_.inputs().size());
  this->assert_valid_thread();
  this->set_input_unused_impl(index);
}

template<typename T> inline T Params::extract_input(const int index)
{
  this->assert_valid_thread();
  void *data = this->try_get_input_data_ptr(index);
  lib_assert(data != nullptr);
  T return_val = std::move(*static_cast<T *>(data));
  return return_val;
}

template<typename T> inline T &Params::get_input(const int index) const
{
  void *data = this->try_get_input_data_ptr(index);
  lib_assert(data != nullptr);
  return *static_cast<T *>(data);
}

template<typename T> inline T *Params::try_get_input_data_ptr(const int index) const
{
  this->assert_valid_thread();
  return static_cast<T *>(this->try_get_input_data_ptr(index));
}

template<typename T> inline T *Params::try_get_input_data_ptr_or_request(const int index)
{
  this->assert_valid_thread();
  return static_cast<T *>(this->try_get_input_data_ptr_or_request(index));
}

template<typename T> inline void Params::set_output(const int index, T &&value)
{
  using DecayT = std::decay_t<T>;
  this->assert_valid_thread();
  void *data = this->get_output_data_ptr(index);
  new (data) DecayT(std::forward<T>(val));
  this->output_set(index);
}

inline bool Params::try_enable_multi_threading()
{
  this->assert_valid_thread();
  const bool success = this->try_enable_multi_threading_impl();
#ifdef FN_LAZY_DEBUG_THREADS
  if (success) {
    allow_multi_threading_ = true;
  }
#endif
  return success;
}

inline void Params::assert_valid_thread() const
{
#ifdef FN_LAZY_DEBUG_THREADS
  if (allow_multi_threading_) {
    return;
  }
  if (main_thread_id_ != std::this_thread::get_id()) {
    lib_assert_unreachable();
  }
#endif
}

}  // namespace dune::fn::lazy_fn

namespace dune {
namespace lf = fn::lazy_fn;
}
