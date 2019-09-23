/*
 *  * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 *   */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>

#include <base/logging.h>
#include <oper/operdb_init.h>
#include <oper/route_common.h>
#include <oper/interface_common.h>
#include <oper/vrf.h>
#include <oper/agent_sandesh.h>
#include <oper/hbf.h>
#include <oper/agent_route_walker.h>


#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>

#include <string.h>

using namespace std;
using namespace boost::uuids;

SandeshTraceBufferPtr HBFTraceBuf(SandeshTraceBufferCreate("HBF", 2000));

HBFHandler::HBFHandler(Agent* agent) :
    agent_(agent),
    interface_listener_id_(DBTable::kInvalidId) {
    LOG(ERROR, "HBF: HBFHandler constructor called");
}

void HBFHandler::Register() {
    interface_listener_id_ = agent_->interface_table()->Register(
         boost::bind(&HBFHandler::ModifyVmInterface, this, _1, _2));
    HBFTRACE(Trace, "HBFHandler registered for interface table");
}

void HBFHandler::Terminate() {
    agent_->interface_table()->Unregister(interface_listener_id_);
    LOG(ERROR, "HBF: Terminate called");
}

bool HBFHandler::IsHBFLInterface(VmInterface *vm_itf) {
    return (vm_itf->hbs_intf_type() == VmInterface::HBS_INTF_LEFT);
}

bool HBFHandler::IsHBFRInterface(VmInterface *vm_itf) {
    return (vm_itf->hbs_intf_type() == VmInterface::HBS_INTF_RIGHT);
}

/* Registered call for VM */
void HBFHandler::ModifyVmInterface(DBTablePartBase *partition,
                                   DBEntryBase *e)
{
    Interface *intf = static_cast<Interface *>(e);
    VmInterface *vm_itf;

    if (intf->type() != Interface::VM_INTERFACE) {
        return;
    }

    LOG(ERROR, "HBF: ModifyVmInterface called");
    vm_itf = static_cast<VmInterface *>(intf);
    HBFIntfDBState *state = static_cast<HBFIntfDBState *>(
            vm_itf->GetState(partition->parent(), interface_listener_id_));

    // When this function is invoked for Delete, VMI's vn can be NULL. Use its
    // state to figure out if it is HBF VMI
    if (!IsHBFLInterface(vm_itf) && !IsHBFRInterface(vm_itf) && !state) {
        return;
    }

    LOG(ERROR, "HBF: hbf interface " << vm_itf->ToString());
    if (IsHBFLInterface(vm_itf))
        LOG(ERROR, "HBF: Left interface");
    if (IsHBFRInterface(vm_itf))
        LOG(ERROR, "HBF: Right interface");

    HBFTRACE(Trace, "vmi notification for " + vm_itf->ToString());
    if (intf->IsDeleted() || ((vm_itf->l2_active() == false) &&
                              (vm_itf->ipv4_active() == false) &&
                              (vm_itf->ipv6_active() == false))) {
        LOG(ERROR, "HBF: Interface Deleted");
        if (state) {
            vm_itf->ClearState(partition->parent(), interface_listener_id_);
            HBFVrfWalker *walker = static_cast<HBFVrfWalker*>
                (state->vrf_walker_.get());
            if (walker) {
               LOG(ERROR, "HBF Starting VRF walk for deletion");
               HBFTRACE(Trace, "Starting VRF walk for deletion of "
                        + vm_itf->ToString());
                walker->WalkDoneCallback(boost::bind
                        (&HBFVrfWalker::WalkDone,
                        static_cast<HBFVrfWalker *>( walker)));
                walker->Start(Interface::kInvalidIndex,
                      state->lintf_,
                      state->projname_);
            }
            delete state;
        }
        return;
    }

    if (state == NULL && vm_itf->vn()) {
        state = new HBFIntfDBState(IsHBFLInterface(vm_itf), vm_itf->vn()->GetProject());
        state->vrf_walker_.reset(new HBFVrfWalker("HBFVrfWalker", agent_));
        agent_->oper_db()->agent_route_walk_manager()->
            RegisterWalker(static_cast<AgentRouteWalker *>(state->vrf_walker_.get()));
        vm_itf->SetState(partition->parent(), interface_listener_id_, state);
    } else {
        // Dont have to process Change? TODO
        return;
    }

    if (state->vrf_walker_.get()) {
        LOG(ERROR, "HBF Starting VRF walk");
        HBFTRACE(Trace, "Starting VRF walk for addition of "
                  + vm_itf->ToString());
        HBFVrfWalker *walker = static_cast<HBFVrfWalker*>(state->vrf_walker_.get());
        if (walker) {
            walker->Start(vm_itf->id(),
                      state->lintf_,
                      state->projname_);
        }
    }
}

HBFVrfWalker::HBFVrfWalker(const std::string &name, Agent *agent) :
        AgentRouteWalker(name, agent) {
}

HBFVrfWalker::~HBFVrfWalker() {
}

bool HBFVrfWalker::VrfWalkNotify(DBTablePartBase *partition,
                                 DBEntryBase *e) {
    VrfEntry *vrf = dynamic_cast<VrfEntry*>(e);

    if (vrf && vrf->vn()) {
        if (projname_ == vrf->vn()->GetProject()) {
            if (hbf_lintf_) {
                vrf->set_hbf_lintf(hbf_intf_);
                LOG(ERROR, "HBF: setting lintf to " << hbf_intf_
                    << " for vrf " << vrf->vrf_id() << " name " << vrf->GetName());
                std::stringstream ss;
                ss << "Setting lintf to " << hbf_intf_
                   << " for vrf " << vrf->vrf_id() << " name " << vrf->GetName();
                HBFTRACE(Trace, ss.str());
            } else {
                vrf->set_hbf_rintf(hbf_intf_);
                LOG(ERROR, "HBF: setting rintf to " << hbf_intf_
                    << " for vrf " << vrf->vrf_id() << " name " << vrf->GetName());
                std::stringstream ss;
                ss << "Setting rintf to " << hbf_intf_
                   << " for vrf " << vrf->vrf_id() << " name " << vrf->GetName();
                HBFTRACE(Trace, ss.str());
            }
        }

        LOG(ERROR, "HBF: VrfWalkNotify for " << vrf->GetName() << " projname "
            << projname_ << " hbf_lintf_ " << hbf_lintf_ << " hbf_intf_ " << hbf_intf_);
        vrf->Notify();
    }
    return true;
}

void HBFVrfWalker::Start(uint32_t hbf_intf, bool hbf_lintf, std::string projname) {
    hbf_intf_ = hbf_intf;
    hbf_lintf_ = hbf_lintf;
    projname_ = projname;
    StartVrfWalk();
}
