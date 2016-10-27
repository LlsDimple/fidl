// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERFACE_PTR_H_
#define LIB_FIDL_CPP_BINDINGS_INTERFACE_PTR_H_

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>

#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/internal/interface_ptr_internal.h"
#include "lib/fidl/cpp/bindings/macros.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"

namespace fidl {

// A pointer to a local proxy of a remote Interface implementation. Uses a
// message pipe to communicate with the remote implementation, and automatically
// closes the pipe and deletes the proxy on destruction. The pointer must be
// bound to a message pipe before the interface methods can be called.
//
// This class is thread hostile, as is the local proxy it manages. All calls to
// this class or the proxy should be from the same thread that created it. If
// you need to move the proxy to a different thread, extract the
// InterfaceHandle (containing just the message pipe and any version
// information) using PassInterfaceHandle(), pass it to a different thread, and
// create and bind a new InterfacePtr from that thread.
template <typename Interface>
class InterfacePtr {
 public:
  // Constructs an unbound InterfacePtr.
  InterfacePtr() {}
  InterfacePtr(std::nullptr_t) {}

  // Takes over the binding of another InterfacePtr.
  InterfacePtr(InterfacePtr&& other) {
    internal_state_.Swap(&other.internal_state_);
  }

  // Takes over the binding of another InterfacePtr, and closes any message pipe
  // already bound to this pointer.
  InterfacePtr& operator=(InterfacePtr&& other) {
    reset();
    internal_state_.Swap(&other.internal_state_);
    return *this;
  }

  // Assigning nullptr to this class causes it to close the currently bound
  // message pipe (if any) and returns the pointer to the unbound state.
  InterfacePtr& operator=(std::nullptr_t) {
    reset();
    return *this;
  }

  // Closes the bound message pipe (if any) on destruction.
  ~InterfacePtr() {}

  // If |info| is valid (containing a valid message pipe handle), returns an
  // InterfacePtr bound to it. Otherwise, returns an unbound InterfacePtr. The
  // specified |waiter| will be used as in the InterfacePtr::Bind() method.
  static InterfacePtr<Interface> Create(
      InterfaceHandle<Interface> info,
      const FidlAsyncWaiter* waiter = GetDefaultAsyncWaiter()) {
    InterfacePtr<Interface> ptr;
    if (info.is_valid())
      ptr.Bind(std::move(info), waiter);
    return ptr;
  }

  // Binds the InterfacePtr to a remote implementation of Interface. The
  // |waiter| is used for receiving notifications when there is data to read
  // from the message pipe. For most callers, the default |waiter| will be
  // sufficient.
  //
  // Calling with an invalid |info| (containing an invalid message pipe handle)
  // has the same effect as reset(). In this case, the InterfacePtr is not
  // considered as bound.
  void Bind(InterfaceHandle<Interface> info,
            const FidlAsyncWaiter* waiter = GetDefaultAsyncWaiter()) {
    reset();
    if (info.is_valid())
      internal_state_.Bind(std::move(info), waiter);
  }

  // Returns whether or not this InterfacePtr is bound to a message pipe.
  bool is_bound() const { return internal_state_.is_bound(); }

  // Returns a raw pointer to the local proxy. Caller does not take ownership.
  // Note that the local proxy is thread hostile, as stated above.
  Interface* get() const { return internal_state_.instance(); }

  // Functions like a pointer to Interface. Must already be bound.
  Interface* operator->() const { return get(); }
  Interface& operator*() const { return *get(); }

  // Returns the version number of the interface that the remote side supports.
  uint32_t version() const { return internal_state_.version(); }

  // If the remote side doesn't support the specified version, it will close its
  // end of the message pipe asynchronously. This does nothing if it's already
  // known that the remote side supports the specified version, i.e., if
  // |version <= this->version()|.
  //
  // After calling RequireVersion() with a version not supported by the remote
  // side, all subsequent calls to interface methods will be ignored.
  void RequireVersion(uint32_t version) {
    internal_state_.RequireVersion(version);
  }

  // Closes the bound message pipe (if any) and returns the pointer to the
  // unbound state.
  void reset() {
    State doomed;
    internal_state_.Swap(&doomed);
  }

  // Tests as true if bound, false if not.
  explicit operator bool() const { return internal_state_.is_bound(); }

  // Blocks the current thread until the next incoming response callback arrives
  // or an error occurs. Returns |true| if a response arrived, or |false| in
  // case of error.
  //
  // This method may only be called after the InterfacePtr has been bound to a
  // message pipe.
  bool WaitForIncomingResponse() {
    return internal_state_.WaitForIncomingResponse(MX_TIME_INFINITE);
  }

  // Blocks the current thread until the next incoming response callback
  // arrives, an error occurs, or the timeout exceeded. Returns |true| if a
  // response arrived, or |false| otherwise. Use |encountered_error| to know
  // if an error occurred, of if the timeout exceeded.
  //
  // This method may only be called after the InterfacePtr has been bound to a
  // message pipe.
  bool WaitForIncomingResponseWithTimeout(ftl::TimeDelta timeout) {
    return internal_state_.WaitForIncomingResponse(timeout);
  }

  // Indicates whether the message pipe has encountered an error. If true,
  // method calls made on this interface will be dropped (and may already have
  // been dropped).
  bool encountered_error() const { return internal_state_.encountered_error(); }

  // Registers a handler to receive error notifications. The handler will be
  // called from the thread that owns this InterfacePtr.
  //
  // This method may only be called after the InterfacePtr has been bound to a
  // message pipe.
  void set_connection_error_handler(const ftl::Closure& error_handler) {
    internal_state_.set_connection_error_handler(error_handler);
  }

  // Unbinds the InterfacePtr and returns the information which could be used
  // to setup an InterfacePtr again. This method may be used to move the proxy
  // to a different thread (see class comments for details).
  InterfaceHandle<Interface> PassInterfaceHandle() {
    State state;
    internal_state_.Swap(&state);

    return state.PassInterfaceHandle();
  }

  // DO NOT USE. Exposed only for internal use and for testing.
  internal::InterfacePtrState<Interface>* internal_state() {
    return &internal_state_;
  }

 private:
  typedef internal::InterfacePtrState<Interface> State;
  mutable State internal_state_;

  FIDL_MOVE_ONLY_TYPE(InterfacePtr);
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERFACE_PTR_H_