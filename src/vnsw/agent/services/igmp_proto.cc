/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "oper/vn.h"
#include "oper/route_common.h"
#include "oper/multicast.h"
#include "services/igmp_proto.h"

IgmpProto::IgmpProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, "Agent::Services", PktHandler::IGMP, io),
    task_name_("Agent::Services"), io_(io) {

    itf_attach_count_ = 0;
    IgmpProtoInit();
}

IgmpProto::~IgmpProto() {
}

void IgmpProto::IgmpProtoInit(void) {

    // limit the number of entries in the workqueue
    work_queue_.SetSize(agent_->params()->services_queue_limit());
    work_queue_.SetBounded(true);

    gmp_proto_ = GmpProtoManager::CreateGmpProto(GmpType::IGMP, agent_,
                                    task_name_, PktHandler::IGMP, io_);
    if (gmp_proto_) {
        gmp_proto_->Start();
    }

    iid_ = agent_->interface_table()->Register(
            boost::bind(&IgmpProto::ItfNotify, this, _1, _2));
    vrfid_ = agent_->vrf_table()->Register(
            boost::bind(&IgmpProto::VrfNotify, this, _1, _2));

    ClearStats();
}

void IgmpProto::Shutdown() {
    agent_->interface_table()->Unregister(iid_);

    if (gmp_proto_) {
        gmp_proto_->Stop();
        GmpProtoManager::DeleteGmpProto(gmp_proto_);
    }
}

ProtoHandler *IgmpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                           boost::asio::io_service &io) {
    return new IgmpHandler(agent(), info, io);
}

void IgmpProto::ItfNotify(DBTablePartBase *part, DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (itf->type() != Interface::VM_INTERFACE) {
        return;
    }

    VmInterface *vm_itf = static_cast<VmInterface *>(itf);
    if (vm_itf->vmi_type() == VmInterface::VHOST) {
        return;
    }

    IgmpInfo::McastInterfaceState *mif_state =
                static_cast<IgmpInfo::McastInterfaceState *>
                        (entry->GetState(part->parent(), iid_));

    if (itf->IsDeleted()) {
        if (mif_state) {
            gmp_proto_->DeleteIntf(mif_state->gmp_intf_);
            MulticastHandler *mch = MulticastHandler::GetInstance();
            if (mch) {
                mch->DeleteVmInterfaceFromSourceGroup(
                                    agent_->fabric_policy_vrf_name(),
                                    mif_state->vrf_name_, vm_itf);
            }
            entry->ClearState(part->parent(), iid_);
            itf_attach_count_--;
            delete mif_state;
        }
        return;
    }

    if (mif_state == NULL) {
        mif_state = new IgmpInfo::McastInterfaceState();

        mif_state->gmp_intf_ = gmp_proto_->CreateIntf();
        itf_attach_count_++;
        entry->SetState(part->parent(), iid_, mif_state);
    }

    if (mif_state && mif_state->gmp_intf_) {
        mif_state->gmp_intf_->SetIpAddress(vm_itf->primary_ip_addr());
        if (vm_itf->vrf()) {
            mif_state->gmp_intf_->SetVrf(vm_itf->vrf()->GetName());
        }
    }

    if (vm_itf->vrf()) {                 
        mif_state->vrf_name_ = vm_itf->vrf()->GetName();
    }
}

void IgmpProto::VrfNotify(DBTablePartBase *part, DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    IgmpInfo::VrfState *state = static_cast<IgmpInfo::VrfState *>
        (vrf->GetState(part->parent(), vrfid_));

    if (vrf->IsDeleted()) {
        if (state) {
            AgentRouteTable *rt_table = static_cast<AgentRouteTable *>(vrf->
                          GetInet4MulticastRouteTable());
            rt_table->Unregister(state->mc_rt_id_);
            state->mc_rt_id_ = DBTableBase::kInvalidId;
            vrf->ClearState(part->parent(), vrfid_);
            delete state;
        }
        return;
    }

    if (state == NULL) {
        state = new IgmpInfo::VrfState();
        entry->SetState(part->parent(), vrfid_, state);
        AgentRouteTable *rt_table = static_cast<AgentRouteTable *>(vrf->
                          GetInet4MulticastRouteTable());
        if (state->mc_rt_id_ == DBTableBase::kInvalidId) {
            state->mc_rt_id_ = rt_table->Register(boost::bind(&IgmpProto::Inet4McRouteTableNotify,
                                           this, _1, _2));
        }
    }
}

void IgmpProto::Inet4McRouteTableNotify(DBTablePartBase *part,
                                    DBEntryBase *entry) {

    const Inet4MulticastRouteEntry *mc_rt = 
              static_cast<const Inet4MulticastRouteEntry *>(entry);
    if (mc_rt->IsDeleted()) {
        MulticastHandler *mch = MulticastHandler::GetInstance();
        if (!mch) {
            return;
        }
        mch->DeleteMulticastVrfSourceGroup(mc_rt->vrf()->GetName(),
                                    mc_rt->src_ip_addr(),
                                    mc_rt->dest_ip_addr());
    }

    return;
}

DBTableBase::ListenerId IgmpProto::ItfListenerId () {
    return iid_;
}

DBTableBase::ListenerId IgmpProto::VrfListenerId () {
    return vrfid_;
}

const IgmpInfo::McastInterfaceState::IgmpIfStats &IgmpProto::GetIfStats(
                            VmInterface *vm_itf) {

    IgmpInfo::McastInterfaceState *mif_state;

    mif_state = static_cast<IgmpInfo::McastInterfaceState *>(vm_itf->GetState(
                                vm_itf->get_table_partition()->parent(), iid_));

    return mif_state->GetIfStats();
}

