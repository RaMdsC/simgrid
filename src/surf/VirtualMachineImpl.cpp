/* Copyright (c) 2013-2015. The SimGrid Team.
 * All rights reserved.                                                     */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "src/surf/VirtualMachineImpl.hpp"
#include "simgrid/s4u/VirtualMachine.hpp"

#include <xbt/signal.hpp>

#include "cpu_cas01.hpp"

XBT_LOG_NEW_DEFAULT_SUBCATEGORY(surf_vm, surf, "Logging specific to the SURF VM module");

simgrid::surf::VMModel* surf_vm_model = nullptr;

void surf_vm_model_init_HL13()
{
  if (surf_cpu_model_vm) {
    surf_vm_model = new simgrid::surf::VMModel();
    all_existing_models->push_back(surf_vm_model);
  }
}

namespace simgrid {
namespace surf {

/*************
 * Callbacks *
 *************/

simgrid::xbt::signal<void(simgrid::surf::VirtualMachineImpl*)> onVmCreation;
simgrid::xbt::signal<void(simgrid::surf::VirtualMachineImpl*)> onVmDestruction;
simgrid::xbt::signal<void(simgrid::surf::VirtualMachineImpl*)> onVmStateChange;

/*********
 * Model *
 *********/

std::deque<VirtualMachineImpl*> VirtualMachineImpl::allVms_;

/* In the real world, processes on the guest operating system will be somewhat degraded due to virtualization overhead.
 * The total CPU share these processes get is smaller than that of the VM process gets on a host operating system. */
// const double virt_overhead = 0.95;
const double virt_overhead = 1;

double VMModel::nextOccuringEvent(double now)
{
  /* TODO: update action's cost with the total cost of processes on the VM. */

  /* 1. Now we know how many resource should be assigned to each virtual
   * machine. We update constraints of the virtual machine layer.
   *
   * If we have two virtual machine (VM1 and VM2) on a physical machine (PM1).
   *     X1 + X2 = C       (Equation 1)
   * where
   *    the resource share of VM1: X1
   *    the resource share of VM2: X2
   *    the capacity of PM1: C
   *
   * Then, if we have two process (P1 and P2) on VM1.
   *     X1_1 + X1_2 = X1  (Equation 2)
   * where
   *    the resource share of P1: X1_1
   *    the resource share of P2: X1_2
   *    the capacity of VM1: X1
   *
   * Equation 1 was solved in the physical machine layer.
   * Equation 2 is solved in the virtual machine layer (here).
   * X1 must be passed to the virtual machine laye as a constraint value.
   **/

  /* iterate for all virtual machines */
  for (VirtualMachineImpl* ws_vm : VirtualMachineImpl::allVms_) {
    Cpu* cpu = ws_vm->piface_->pimpl_cpu;
    xbt_assert(cpu, "cpu-less host");

    double solved_value = ws_vm->action_->getVariable()->value;
    XBT_DEBUG("assign %f to vm %s @ pm %s", solved_value, ws_vm->piface_->name().c_str(),
              ws_vm->getPm()->name().c_str());

    // TODO: check lmm_update_constraint_bound() works fine instead of the below manual substitution.
    // cpu_cas01->constraint->bound = solved_value;
    xbt_assert(cpu->getModel() == surf_cpu_model_vm);
    lmm_system_t vcpu_system = cpu->getModel()->getMaxminSystem();
    lmm_update_constraint_bound(vcpu_system, cpu->getConstraint(), virt_overhead * solved_value);
  }

  /* 2. Calculate resource share at the virtual machine layer. */
  adjustWeightOfDummyCpuActions();

  /* 3. Ready. Get the next occuring event */
  return surf_cpu_model_vm->nextOccuringEvent(now);
}

/************
 * Resource *
 ************/

VirtualMachineImpl::VirtualMachineImpl(simgrid::s4u::Host* piface, simgrid::s4u::Host* host_PM)
    : HostImpl(piface, nullptr /*storage*/), hostPM_(host_PM)
{
  /* Register this VM to the list of all VMs */
  allVms_.push_back(this);

  /* Currently, a VM uses the network resource of its physical host. In
   * host_lib, this network resource object is referred from two different keys.
   * When deregistering the reference that points the network resource object
   * from the VM name, we have to make sure that the system does not call the
   * free callback for the network resource object. The network resource object
   * is still used by the physical machine. */
  piface_->pimpl_netcard = host_PM->pimpl_netcard;

  // //// CPU  RELATED STUFF ////
  // Roughly, create a vcpu resource by using the values of the sub_cpu one.
  CpuCas01* sub_cpu = dynamic_cast<CpuCas01*>(host_PM->pimpl_cpu);

  piface_->pimpl_cpu = surf_cpu_model_vm->createCpu(piface_, sub_cpu->getSpeedPeakList(), 1 /*cores*/);
  if (sub_cpu->getPState() != 0)
    piface_->pimpl_cpu->setPState(sub_cpu->getPState());

  /* We create cpu_action corresponding to a VM process on the host operating system. */
  /* FIXME: TODO: we have to periodically input GUESTOS_NOISE to the system? how ? */
  action_ = sub_cpu->execution_start(0);

  XBT_VERB("Create VM(%s)@PM(%s) with %ld mounted disks", piface->name().c_str(), hostPM_->name().c_str(),
           xbt_dynar_length(storage_));
}

/*
 * A physical host does not disappear in the current SimGrid code, but a VM may disappear during a simulation.
 */
VirtualMachineImpl::~VirtualMachineImpl()
{
  onVmDestruction(this);
  allVms_.erase(find(allVms_.begin(), allVms_.end(), this));

  /* Free the cpu_action of the VM. */
  XBT_ATTRIB_UNUSED int ret = action_->unref();
  xbt_assert(ret == 1, "Bug: some resource still remains");

  delete piface_->pimpl_cpu;
}

e_surf_vm_state_t VirtualMachineImpl::getState()
{
  return vmState_;
}

void VirtualMachineImpl::setState(e_surf_vm_state_t state)
{
  vmState_ = state;
}
void VirtualMachineImpl::suspend()
{
  action_->suspend();
  vmState_ = SURF_VM_STATE_SUSPENDED;
}

void VirtualMachineImpl::resume()
{
  action_->resume();
  vmState_ = SURF_VM_STATE_RUNNING;
}

void VirtualMachineImpl::save()
{
  vmState_ = SURF_VM_STATE_SAVING;
  action_->suspend();
  vmState_ = SURF_VM_STATE_SAVED;
}

void VirtualMachineImpl::restore()
{
  vmState_ = SURF_VM_STATE_RESTORING;
  action_->resume();
  vmState_ = SURF_VM_STATE_RUNNING;
}

/** @brief returns the physical machine on which the VM is running **/
s4u::Host* VirtualMachineImpl::getPm()
{
  return hostPM_;
}

/* Update the physical host of the given VM */
void VirtualMachineImpl::migrate(s4u::Host* host_dest)
{
  const char* vm_name     = piface_->name().c_str();
  const char* pm_name_src = hostPM_->name().c_str();
  const char* pm_name_dst = host_dest->name().c_str();

  /* update net_elm with that of the destination physical host */
  piface_->pimpl_netcard = host_dest->pimpl_netcard;

  hostPM_ = host_dest;

  /* Update vcpu's action for the new pm */
  /* create a cpu action bound to the pm model at the destination. */
  CpuAction* new_cpu_action = static_cast<CpuAction*>(host_dest->pimpl_cpu->execution_start(0));

  Action::State state = action_->getState();
  if (state != Action::State::done)
    XBT_CRITICAL("FIXME: may need a proper handling, %d", static_cast<int>(state));
  if (action_->getRemainsNoUpdate() > 0)
    XBT_CRITICAL("FIXME: need copy the state(?), %f", action_->getRemainsNoUpdate());

  /* keep the bound value of the cpu action of the VM. */
  double old_bound = action_->getBound();
  if (old_bound != 0) {
    XBT_DEBUG("migrate VM(%s): set bound (%f) at %s", vm_name, old_bound, pm_name_dst);
    new_cpu_action->setBound(old_bound);
  }

  XBT_ATTRIB_UNUSED int ret = action_->unref();
  xbt_assert(ret == 1, "Bug: some resource still remains");

  action_ = new_cpu_action;

  XBT_DEBUG("migrate VM(%s): change PM (%s to %s)", vm_name, pm_name_src, pm_name_dst);
}

void VirtualMachineImpl::setBound(double bound)
{
  action_->setBound(bound);
}
}
}