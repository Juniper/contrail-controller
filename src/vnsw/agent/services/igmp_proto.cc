/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/filesystem.hpp>
#include "base/timer.h"
#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "oper/vn.h"
#include "services/igmp_proto.h"
#include "services/services_types.h"
#include "services/services_init.h"
#include "pkt/pkt_init.h"

using namespace boost::asio;
using boost::asio::ip::udp;

IgmpProto::IgmpProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, "Agent::Services", PktHandler::IGMP, io),
    ip_fabric_interface_(NULL), ip_fabric_interface_index_(-1),
    pkt_interface_index_(-1), gateway_delete_seqno_(0) {
    // limit the number of entries in the workqueue
    work_queue_.SetSize(agent->params()->services_queue_limit());
    work_queue_.SetBounded(true);

    iid_ = agent->interface_table()->Register(
                  boost::bind(&IgmpProto::ItfNotify, this, _2));
    vnid_ = agent->vn_table()->Register(
                  boost::bind(&IgmpProto::VnNotify, this, _2));
}

IgmpProto::~IgmpProto() {
}

void IgmpProto::Shutdown() {
    agent_->interface_table()->Unregister(iid_);
    agent_->vn_table()->Unregister(vnid_);
}

ProtoHandler *IgmpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                           boost::asio::io_service &io) {
    return new IgmpHandler(agent(), info, io);
}

void IgmpProto::ItfNotify(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (entry->IsDeleted()) {
        if (itf->type() == Interface::PHYSICAL &&
            itf->name() == agent_->fabric_interface_name()) {
            set_ip_fabric_interface(NULL);
            set_ip_fabric_interface_index(-1);
        } else if (itf->type() == Interface::PACKET) {
            set_pkt_interface_index(-1);
        } else if (itf->type() == Interface::VM_INTERFACE) {
            VmInterface *vmi = static_cast<VmInterface *>(itf);
            if (gw_vmi_list_.erase(vmi)) {
                IGMP_PKT_TRACE(Trace, "Gateway interface deleted: " << itf->name());
            }
        }
    } else {
        if (itf->type() == Interface::PHYSICAL &&
            itf->name() == agent_->fabric_interface_name()) {
            set_ip_fabric_interface(itf);
            set_ip_fabric_interface_index(itf->id());
            set_ip_fabric_interface_mac(MacAddress());
        } else if (itf->type() == Interface::PACKET) {
            set_pkt_interface_index(itf->id());
        } else if (itf->type() == Interface::VM_INTERFACE) {
            VmInterface *vmi = static_cast<VmInterface *>(itf);
            if (vmi->vmi_type() == VmInterface::GATEWAY) {
                IGMP_PKT_TRACE(Trace, "Gateway interface added: " << itf->name());
                gw_vmi_list_.insert(vmi);
            }
        }
    }
}

void IgmpProto::VnNotify(DBEntryBase *entry) {
    VnEntry *vn = static_cast<VnEntry *>(entry);
    for (std::set<VmInterface *>::iterator it = gw_vmi_list_.begin();
        it != gw_vmi_list_.end(); ++it) {
        VmInterface *vmi = *it;
        if (vmi->vn() != vn)
            continue;
    }
}

