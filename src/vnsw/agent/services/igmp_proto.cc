/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "oper/vn.h"
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

    iid_ = agent_->interface_table()->Register(
            boost::bind(&IgmpProto::ItfNotify, this, _1, _2));

    gmp_proto_ = GmpProtoManager::CreateGmpProto(GmpType::IGMP, agent_,
                                    task_name_, PktHandler::IGMP, io_);
    if (gmp_proto_) {
        gmp_proto_->Start();
    }

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
    }
}

DBTableBase::ListenerId IgmpProto::ItfListenerId () {
    return iid_;
}

const IgmpInfo::McastInterfaceState::IgmpIfStats &IgmpProto::GetIfStats(
                            VmInterface *vm_itf) {

    IgmpInfo::McastInterfaceState *mif_state;

    mif_state = static_cast<IgmpInfo::McastInterfaceState *>(vm_itf->GetState(
                                vm_itf->get_table_partition()->parent(), iid_));

    return mif_state->GetIfStats();
}

