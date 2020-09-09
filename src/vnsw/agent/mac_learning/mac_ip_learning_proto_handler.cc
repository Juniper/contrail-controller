/*
 * Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
 */
#include "mac_learning_init.h"
#include "mac_learning_proto.h"
#include "mac_ip_learning_proto_handler.h"

MacIpLearningProtoHandler::MacIpLearningProtoHandler(Agent *agent,
                                                 boost::shared_ptr<PktInfo> info,
                                                 boost::asio::io_service &io):
    ProtoHandler(agent, info, io), intf_(NULL), vrf_(NULL), table_(NULL),
    entry_() {
}

void MacIpLearningProtoHandler::Log(std::string msg) {

    std::string vrf = "";
    std::string intf = "";
    if (vrf_ != NULL) {
        vrf = vrf_->GetName();
    }

    if (intf_ != NULL) {
        intf = intf_->name();
    }

    MAC_LEARNING_TRACE(MacLearningTraceBuf, vrf, pkt_info_->smac.ToString(), intf, msg);
}

bool MacIpLearningProtoHandler::Run() {
    intf_ = agent()->interface_table()->
                FindInterface(pkt_info_->agent_hdr.ifindex);
    if (intf_ == NULL) {
        Log("Invalid interface");
        return true;
    }

    vrf_ = agent()->vrf_table()->FindVrfFromId(pkt_info_->agent_hdr.vrf);
    if (vrf_ == NULL) {
        Log("Invalid VRF");
        return true;
    }

    const VmInterface *vm_intf = dynamic_cast<const VmInterface *>(intf_);
    if (vm_intf == NULL) {
        Log("Ingress packet on non-VMI interface");
        return true;
    }
    table_= agent()->mac_learning_proto()->GetMacIpLearningTable();
    entry_.reset(new MacIpLearningEntry(table_, pkt_info_->agent_hdr.vrf,
                                           pkt_info_->ip_saddr,
                                           pkt_info_->smac,
                                           intf_));
    if (entry_.get()) {
        agent()->mac_learning_proto()->GetMacIpLearningTable()->Add(entry_);
        Log("Mac entry added");
    }

    return true;
}
