/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#include "mac_learning_init.h"
#include "mac_learning_proto.h"
#include "mac_learning_proto_handler.h"

MacLearningProtoHandler::MacLearningProtoHandler(Agent *agent,
                                                 boost::shared_ptr<PktInfo> info,
                                                 boost::asio::io_service &io):
    ProtoHandler(agent, info, io), intf_(NULL), vrf_(NULL), table_(NULL),
    entry_() {
}

void MacLearningProtoHandler::Log(std::string msg) {

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

void MacLearningProtoHandler::IngressPktHandler() {
    const VmInterface *vm_intf = dynamic_cast<const VmInterface *>(intf_);
    if (vm_intf == NULL) {
        Log("Ingress packet on non-VMI interface");
        return;
    }

    entry_.reset(new MacLearningEntryLocal(table_, pkt_info_->agent_hdr.vrf,
                                           pkt_info_->smac,
                                           pkt_info_->agent_hdr.cmd_param,
                                           intf_));
}

void MacLearningProtoHandler::EgressPktHandler() {
    const PhysicalInterface *p_intf =
        dynamic_cast<const PhysicalInterface *>(intf_);
    if (p_intf == NULL) {
        Log("Invalid packet on physical interface");
        return;
    }

   if (pkt_info_->pbb_header == NULL) {
        Log("Non PBB packet on physical interface");
        return;
    }

   std::string bmac_vrf = Agent::NullString();
   if (vrf_->vn() && vrf_->vn()->GetVrf()) {
       bmac_vrf = vrf_->vn()->GetVrf()->GetName();
   }

    entry_.reset(new MacLearningEntryPBB(table_, pkt_info_->agent_hdr.vrf,
                                         pkt_info_->smac,
                                         pkt_info_->agent_hdr.cmd_param,
                                         pkt_info_->b_smac));
}

bool MacLearningProtoHandler::Run() {
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

    uint32_t table_index = agent()->mac_learning_proto()->Hash(
                               pkt_info_->agent_hdr.vrf, pkt_info_->smac);
    table_ = agent()->mac_learning_proto()->Find(table_index);
    if (table_ == NULL) {
        Log("Mac learning table not found");
        return true;
    }

    if (intf_->type() == Interface::PHYSICAL) {
        EgressPktHandler();
    } else {
        IngressPktHandler();
    }

    if (entry_.get()) {
        table_->Add(entry_);
        Log("Mac entry added");
    }

    return true;
}
