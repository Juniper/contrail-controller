/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <pugixml/pugixml.hpp>
#include <xml/xml_base.h>
#include <xml/xml_pugi.h>

#include <base/util.h>
#include <base/logging.h>
#include <xmpp/xmpp_channel.h>
#include <ifmap/ifmap_agent_parser.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>

#include <cmn/agent_cmn.h>
#include <cfg/cfg_interface.h>

#include <controller/controller_init.h>
#include <controller/controller_ifmap.h>
#include <controller/controller_peer.h>
#include <controller/controller_types.h>


uint64_t AgentIfMapXmppChannel::seq_number_;

AgentIfMapXmppChannel::AgentIfMapXmppChannel(Agent *agent, XmppChannel *channel,
                                             uint8_t cnt) : channel_(channel), 
                                             xs_idx_(cnt), agent_(agent) {
    channel_->RegisterReceive(xmps::CONFIG, 
                              boost::bind(&AgentIfMapXmppChannel::ReceiveInternal,
                                          this, _1));
}

AgentIfMapXmppChannel::~AgentIfMapXmppChannel() {
    channel_->UnRegisterWriteReady(xmps::CONFIG);
    channel_->UnRegisterReceive(xmps::CONFIG);
}

uint64_t AgentIfMapXmppChannel::NewSeqNumber() {

    seq_number_++;

    if (seq_number_ == 0) {
        return ++seq_number_;
    }

    CONTROLLER_TRACE(IFMapSeqTrace, GetSeqNumber(), seq_number_, "New Config Seq Num");
    return  seq_number_;
}


bool AgentIfMapXmppChannel::SendUpdate(const std::string &msg) {
    if (!channel_) return false;
    return channel_->Send((const uint8_t *)msg.data(), msg.size(), xmps::CONFIG,
                           boost::bind(&AgentIfMapXmppChannel::WriteReadyCb, this, _1));
}

void AgentIfMapXmppChannel::ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
    if (msg && msg->type == XmppStanza::IQ_STANZA) {
        std::auto_ptr<XmlBase> impl(XmppXmlImplFactory::Instance()->GetXmlImpl());
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());
        XmlPugi *msg_pugi = reinterpret_cast<XmlPugi *>(msg->dom.get());
        pugi->LoadXmlDoc(msg_pugi->doc());
        boost::shared_ptr<ControllerXmppData> data(new ControllerXmppData(xmps::CONFIG,
                                                                          xmps::UNKNOWN,
                                                                          xs_idx_,
                                                                          impl,
                                                                          true));
        agent_->controller()->Enqueue(data);
    }
}

void AgentIfMapXmppChannel::ReceiveConfigMessage(std::auto_ptr<XmlBase> impl) {

    if (GetXmppServerIdx() != agent_->ifmap_active_xmpp_server_index()) {
        LOG(WARN, "IFMap config on non primary channel");
        return;
    }

    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());
    pugi::xml_node node = pugi->FindNode("config");
    IFMapAgentParser *parser = agent_->ifmap_parser();
    assert(parser);
    parser->ConfigParse(node, seq_number_);
}

void AgentIfMapXmppChannel::ReceiveInternal(const XmppStanza::XmppMessage *msg) {
    ReceiveUpdate(msg);
}

std::string AgentIfMapXmppChannel::ToString() const {
    return channel_->ToString();
}

void AgentIfMapXmppChannel::WriteReadyCb(const boost::system::error_code &ec) {
}

void AgentIfMapVmExport::Notify(DBTablePartBase *partition, DBEntryBase *e) {
    CfgIntEntry *entry = static_cast<CfgIntEntry *>(e);
    AgentXmppChannel *peer = NULL;
    AgentIfMapXmppChannel *ifmap = NULL;
    struct VmExportInfo *info = NULL;

    std::stringstream vmid, vmiid;
    vmid << entry->GetVmUuid();
    vmiid << entry->GetUuid();
    if (entry->port_type() == CfgIntEntry::CfgIntNameSpacePort) {
        CONTROLLER_TRACE(IFMapVmExportTrace, vmid.str(), "NameSpacePort",
                         "Ignore Sending Subscribe/Unsubscribe");
        return;
    }

    VmMap::iterator vm_it = vm_map_.find(entry->GetVmUuid());
    if (vm_map_.end() != vm_it) {
        info = vm_it->second;
    }

    if (entry->IsDeleted()) {

        //If delete already processed, neglect the delete
        if (!info)
            return;

        std::list<boost::uuids::uuid>::iterator vmi_it = std::find(info->vmi_list_.begin(), 
                    info->vmi_list_.end(), entry->GetUuid());
        if (vmi_it == info->vmi_list_.end())
            return;

        if (agent_->ifmap_active_xmpp_server_index() != -1) {
            peer = agent_->controller_xmpp_channel(agent_->ifmap_active_xmpp_server_index());
            ifmap = agent_->ifmap_xmpp_channel(agent_->
                                                     ifmap_active_xmpp_server_index());
            if (AgentXmppChannel::IsBgpPeerActive(agent_, peer) && ifmap) {
                if ((info->seq_number_ == ifmap->GetSeqNumber()) && 
                                        (info->vmi_list_.size() == 1)) {
                    CONTROLLER_TRACE(IFMapVmExportTrace, vmid.str(), "",
                            "Unsubscribe ");
                    AgentXmppChannel::ControllerSendVmCfgSubscribe(peer, 
                                                entry->GetVmUuid(), false);
                }
            }
        }

        CONTROLLER_TRACE(IFMapVmExportTrace, vmid.str(), vmiid.str(),
                         "Delete");
        info->vmi_list_.remove(entry->GetUuid());
        if (!info->vmi_list_.size()) {
            vm_map_.erase(entry->GetVmUuid());
            delete info;
        }
        return;
    } else {

        //If first VMI create the data
        if (!info) {
            info = new struct VmExportInfo;
            info->seq_number_ = 0;
            vm_map_[entry->GetVmUuid()] = info;
        }

        //If VMI is not found, insert in the list
        std::list<boost::uuids::uuid>::iterator vmi_it = std::find(info->vmi_list_.begin(), 
                    info->vmi_list_.end(), entry->GetUuid());

        if (vmi_it == info->vmi_list_.end()) {
            CONTROLLER_TRACE(IFMapVmExportTrace, vmid.str(), vmiid.str(),
                         "Add");
            info->vmi_list_.push_back(entry->GetUuid());
        }


        //Ensure there is connection to control node
        if (agent_->ifmap_active_xmpp_server_index() == -1) {
            return;
        }

        //Ensure that peer exists and is in active state
        peer = agent_->controller_xmpp_channel(agent_->ifmap_active_xmpp_server_index());
        if (!AgentXmppChannel::IsBgpPeerActive(agent_, peer)) {
            return;
        }

        ifmap = agent_->ifmap_xmpp_channel(agent_->ifmap_active_xmpp_server_index());
        if (!ifmap) {
            return;
        }

        //We have already sent the subscribe
        if (info->seq_number_ == ifmap->GetSeqNumber()) {
            return;
        }

        CONTROLLER_TRACE(IFMapVmExportTrace, vmid.str(), vmiid.str(),
                         "Subscribe");
        AgentXmppChannel::ControllerSendVmCfgSubscribe(peer, 
                                    entry->GetVmUuid(), true);

        //Update the sequence number
        info->seq_number_ = ifmap->GetSeqNumber();
    }

}

void AgentIfMapVmExport::NotifyAll(AgentXmppChannel *peer) {
    VmMap::iterator vm_it;
    AgentIfMapXmppChannel *ifmap = NULL;
    struct VmExportInfo *info = NULL;
    Agent *agent = peer->agent();

    if (!AgentXmppChannel::IsBgpPeerActive(agent, peer)) {
        return;
    }
    
    ifmap = agent->ifmap_xmpp_channel(agent->ifmap_active_xmpp_server_index());
    if (!ifmap) {
        return;
    }

    //We have all the required data. Send config subscribe for all VM's
    AgentIfMapVmExport *agent_ifmap_vm_export = peer->agent()->controller()->
        agent_ifmap_vm_export();
    vm_it = agent_ifmap_vm_export->vm_map_.begin();
    for(; vm_it != agent_ifmap_vm_export->vm_map_.end(); vm_it++) {
        info = vm_it->second;
        if (info->seq_number_ == ifmap->GetSeqNumber()) {
            continue;
        }

        std::stringstream vmid;
        vmid << vm_it->first;
        CONTROLLER_TRACE(IFMapVmExportTrace, vmid.str(), "",
                         "Subscribe");
        AgentXmppChannel::ControllerSendVmCfgSubscribe(peer, 
                                    vm_it->first, true);

        //Update the sequence number so that we dont send duplicate
        //subscribe
        info->seq_number_ = ifmap->GetSeqNumber();
    }

    return;
}

AgentIfMapVmExport::AgentIfMapVmExport(Agent *agent) : agent_(agent) {
    DBTableBase *table = agent_->interface_config_table();
    vmi_list_id_ = table->Register(boost::bind(&AgentIfMapVmExport::Notify, 
                                               this, _1, _2));
}

AgentIfMapVmExport::~AgentIfMapVmExport() {
    VmMap::iterator vm_it;
    struct VmExportInfo *info = NULL;

    //Destroy the vm_map and vmi list
    for(vm_it = vm_map_.begin(); vm_it != vm_map_.end(); vm_it++) {
        info = vm_it->second;
        delete info;
    }
    //Register a nova interface cfg listener
    DBTableBase *table = agent_->interface_config_table();
    if (table) {
        table->Unregister(vmi_list_id_);
    }

    vm_map_.clear();
}
