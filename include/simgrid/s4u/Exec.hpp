/* Copyright (c) 2017. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#ifndef SIMGRID_S4U_EXEC_HPP
#define SIMGRID_S4U_EXEC_HPP

#include <simgrid/forward.h>
#include <simgrid/s4u/Activity.hpp>
#include <simgrid/s4u/forward.hpp>

#include <atomic>

namespace simgrid {
namespace s4u {

XBT_PUBLIC_CLASS Exec : public Activity
{
  Exec() : Activity() {}
public:
  friend void intrusive_ptr_release(simgrid::s4u::Exec * e);
  friend void intrusive_ptr_add_ref(simgrid::s4u::Exec * e);
  friend ExecPtr this_actor::exec_init(double flops_amount);

  ~Exec() = default;

  void start() override;
  void wait() override;
  void wait(double timeout) override;
  bool test();

  ExecPtr setPriority(double priority);
  ExecPtr setHost(Host * host);

  double getRemains() override;
  double getRemainingRatio();

private:
  Host* host_          = nullptr;
  double flops_amount_ = 0.0;
  double priority_     = 1.0;

  std::atomic_int_fast32_t refcount_{0};
}; // class
}
}; // Namespace simgrid::s4u

#endif /* SIMGRID_S4U_EXEC_HPP */