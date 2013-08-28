/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/util.h"
#include "base/logging.h"
#include "controller/controller_init.h"
#include "controller/controller_ifmap.h"
#include "controller/controller_peer.h"
#include "xmpp/xmpp_channel.h"
#include "ifmap/ifmap_agent_parser.h"
#include "cmn/agent_cmn.h"
#include "pugixml/pugixml.hpp"
#include "xml/xml_pugi.h"
#include <cfg/interface_cfg.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include "controller/controller_types.h"


uint64_t AgentIfMapXmppChannel::seq_number_;
AgentIfMapVmExport *AgentIfMapVmExport::singleton_;

AgentIfMapXmppChannel::AgentIfMapXmppChannel(XmppChannel *channel, uint8_t cnt) : channel_(channel), xs_idx_(cnt) {
    channel_->RegisterReceive(xmps::CONFIG, 
                              boost::bind(&AgentIfMapXmppChannel::ReceiveInternal,
                                              this, _1));
}

AgentIfMapXmppChannel::~AgentIfMapXmppChannel() {
    channel_->UnRegisterReceive(xmps::CONFIG);
}

uint64_t AgentIfMapXmppChannel::NewSeqNumber() {

    seq_number_++;

    if (seq_number_ == 0) {
        return ++seq_number_;
    }

    return  seq_number_;
}


bool AgentIfMapXmppChannel::SendUpdate(const std::string &msg) {
    if (!channel_) return false;
    return channel_->Send((const uint8_t *)msg.data(), msg.size(), xmps::CONFIG,
                           boost::bind(&AgentIfMapXmppChannel::WriteReadyCb, this, _1));
}

void AgentIfMapXmppChannel::ReceiveUpdate(const XmppStanza::XmppMessage *msg) {

    if (GetXmppServerIdx() != Agent::GetXmppCfgServerIdx()) {
        LOG(WARN, "IFMap config on non primary channel");
        return;
    }

    if (msg && msg->type == XmppStanza::IQ_STANZA) {
        
        XmlBase *impl = msg->dom.get();
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl);
        pugi::xml_node node = pugi->FindNode("config");
        IFMapAgentParser *parser = Agent::GetIfMapAgentParser();
        assert(parser);
        parser->ConfigParse(node, seq_number_);
    }
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
    VmMap::iterator vm_it = vm_map_.find(entry->GetVmUuid());
    if (vm_map_.end() != vm_it) {
        info = vm_it->second;
    }

    std::stringstream vmid, vmiid;
    vmid << entry->GetVmUuid();
    vmiid << entry->GetUuid();
    if (entry->IsDeleted()) {
        assert(info);

        std::list<uuid>::iterator vmi_it = std::find(info->vmi_list_.begin(), 
                    info->vmi_list_.end(), entry->GetUuid());
        assert(vmi_it != info->vmi_list_.end());

        if (Agent::GetXmppCfgServerIdx() != -1) {
            peer = Agent::GetAgentXmppChannel(Agent::GetXmppCfgServerIdx());
            ifmap = Agent::GetAgentIfMapXmppChannel(Agent::GetXmppCfgServerIdx());
            if (peer && ifmap) {
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
        std::list<uuid>::iterator vmi_it = std::find(info->vmi_list_.begin(), 
                    info->vmi_list_.end(), entry->GetUuid());

        if (vmi_it == info->vmi_list_.end()) {
            CONTROLLER_TRACE(IFMapVmExportTrace, vmid.str(), vmiid.str(),
                         "Add");
            info->vmi_list_.push_back(entry->GetUuid());
        }


        //Ensure there is connection to control node
        if (Agent::GetXmppCfgServerIdx() == -1) {
            return;
        }

        //Ensure that peer exists
        peer = Agent::GetAgentXmppChannel(Agent::GetXmppCfgServerIdx());
        if (!peer) {
            return;
        }

        ifmap = Agent::GetAgentIfMapXmppChannel(Agent::GetXmppCfgServerIdx());
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

    if (!peer) {
        return;
    }
    
    ifmap = Agent::GetAgentIfMapXmppChannel(Agent::GetXmppCfgServerIdx());
    if (!ifmap) {
        return;
    }

    //We have all the required data. Send config subscribe for all VM's
    vm_it = singleton_->vm_map_.begin();
    for(; vm_it != singleton_->vm_map_.end(); vm_it++) {
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

void AgentIfMapVmExport::Shutdown() {
    delete singleton_;
    singleton_ = NULL;
}

void AgentIfMapVmExport::Init() {
    singleton_ = new AgentIfMapVmExport();
    //Register a nova interface cfg listener
    DBTableBase *table = Agent::GetIntfCfgTable();
    singleton_->vmi_list_id_ = table->Register(boost::bind(&AgentIfMapVmExport::Notify, 
                                               singleton_, _1, _2));
}

AgentIfMapVmExport::AgentIfMapVmExport() {
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
    DBTableBase *table = Agent::GetIntfCfgTable();
    if (table) {
        table->Unregister(vmi_list_id_);
    }

    vm_map_.clear();
}
