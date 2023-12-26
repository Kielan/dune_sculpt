#include "lib_lazy_threading.hh"
#include "lib_stack.hh"
#include "lib_vector.hh"

namespace dune::lazy_threading {

/* This uses a "raw" stack and vector so that it can be destructed after Blender checks for memory
 * leaks. A new list of receivers is created whenever an isolated region is entered to avoid
 * deadlocks. */
using HintReceivers = RawStack<RawVector<FnRef<void()>, 0>, 0>;
static thread_local HintReceivers hint_receivers = []() {
  HintReceivers receivers;
  /* Make sure there is always at least one vector. */
  receivers.push_as();
  return receivers;
}();

void send_hint()
{
  for (const FnRef<void()> &fn : hint_receivers.peek()) {
    fn();
  }
}

HintReceiver::HintReceiver(const FnRef<void()> fn)
{
  hint_receivers.peek().append(fn);
}

HintReceiver::~HintReceiver()
{
  hint_receivers.peek().pop_last();
}

ReceiverIsolation::ReceiverIsolation()
{
  hint_receivers.push_as();
}

ReceiverIsolation::~ReceiverIsolation()
{
  lib_assert(hint_receivers.peek().is_empty());
  hint_receivers.pop();
}

}  // namespace dune::lazy_threading
