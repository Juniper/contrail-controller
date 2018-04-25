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

#include <cmn/agent_cmn.h>
#include <cmn/agent_stats.h>

#include <controller/controller_init.h>
#include <controller/controller_ifmap.h>
#include <controller/controller_peer.h>
#include <controller/controller_types.h>


uint64_t AgentIfMapXmppChannel::seq_number_;

AgentIfMapXmppChannel::AgentIfMapXmppChannel(Agent *agent, XmppChannel *channel,
                                             uint8_t cnt) : channel_(channel),
                                             xs_idx_(cnt),
                                             agent_(agent) {
    channel_->RegisterReceive(xmps::CONFIG,
                              boost::bind(&AgentIfMapXmppChannel::ReceiveInternal,
                                          this, _1));
    config_cleanup_timer_.reset(new ConfigCleanupTimer(agent));
    end_of_config_timer_.reset(new EndOfConfigTimer(agent, this));
}

AgentIfMapXmppChannel::~AgentIfMapXmppChannel() {
    channel_->UnRegisterWriteReady(xmps::CONFIG);
    channel_->UnRegisterReceive(xmps::CONFIG);
    config_cleanup_timer_.reset();
    end_of_config_timer_.reset();
}

uint64_t AgentIfMapXmppChannel::NewSeqNumber() {

    seq_number_++;

    if (seq_number_ == 0) {
        return ++seq_number_;
    }

    CONTROLLER_TRACE(IFMapSeqTrace, GetSeqNumber(), seq_number_, "New Config Seq Num");
    return  seq_number_;
}

ConfigCleanupTimer *AgentIfMapXmppChannel::config_cleanup_timer() {
    return config_cleanup_timer_.get();
}

EndOfConfigTimer *AgentIfMapXmppChannel::end_of_config_timer() {
    return end_of_config_timer_.get();
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
        end_of_config_timer()->last_config_receive_time_ = UTCTimestampUsec();
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
    if (agent_->stats())
        agent_->stats()->incr_xmpp_config_in_msgs(GetXmppServerIdx());
    ReceiveUpdate(msg);
}

std::string AgentIfMapXmppChannel::ToString() const {
    return channel_->ToString();
}

void AgentIfMapXmppChannel::WriteReadyCb(const boost::system::error_code &ec) {
}

void AgentIfMapXmppChannel::StartEndOfConfigTimer() {
    //First start for end of config identification.
    end_of_config_timer()->Start(agent_->controller_xmpp_channel(xs_idx_));
}

void AgentIfMapXmppChannel::StopEndOfConfigTimer() {
    //First start for end of config identification.
    end_of_config_timer()->Cancel();
}

void AgentIfMapXmppChannel::StartConfigCleanupTimer() {
    config_cleanup_timer()->Start(agent_->controller_xmpp_channel(xs_idx_));
}

void AgentIfMapXmppChannel::StopConfigCleanupTimer() {
    config_cleanup_timer()->Cancel();
}

void AgentIfMapXmppChannel::EnqueueEndOfConfig() {
    EndOfConfigDataPtr data(new EndOfConfigData(this));
    VNController::ControllerWorkQueueDataType base_data =
        boost::static_pointer_cast<ControllerWorkQueueData>(data);
    agent_->controller()->Enqueue(base_data);
}

void AgentIfMapXmppChannel::ProcessEndOfConfig() {
    config_cleanup_timer()->Start(agent_->controller_xmpp_channel(xs_idx_));
    agent_->controller()->StartEndOfRibTxTimer();
    end_of_config_timer()->end_of_config_processed_time_ = UTCTimestampUsec();
}

EndOfConfigData::EndOfConfigData(AgentIfMapXmppChannel *ch) :
    ControllerWorkQueueData(), channel_(ch) {
}

// Get active xmpp-peer
static AgentXmppChannel *GetActivePeer(Agent *agent) {
    int active_index = agent->ifmap_active_xmpp_server_index();
    if (active_index == -1) {
        return NULL;
    }

    AgentXmppChannel *peer = agent->controller_xmpp_channel(active_index);
    if (peer == NULL)
        return NULL;

    if (AgentXmppChannel::IsBgpPeerActive(agent, peer) == false) {
        return NULL;
    }

    return peer;
}


static AgentIfMapXmppChannel *GetActiveChannel
(Agent *agent, struct AgentIfMapVmExport::VmExportInfo *info) {
    int active_index = agent->ifmap_active_xmpp_server_index();
    AgentIfMapXmppChannel *ifmap = agent->ifmap_xmpp_channel(active_index);
    // Peer is valid, but channel may be down
    if (ifmap == NULL)
        return NULL;

    if (info && (info->seq_number_ != ifmap->GetSeqNumber())) {
        return NULL;
    }

    return ifmap;
}

void AgentIfMapVmExport::VmiDelete(const ControllerVmiSubscribeData *entry) {
    VmMap::iterator vm_it = vm_map_.find(entry->vm_uuid_);
    if (vm_map_.end() == vm_it) {
        return;
    }

    struct VmExportInfo *info = vm_it->second;
    //If delete already processed, neglect the delete
    if (!info)
        return;

    // Find VMI-UUID in the list of VMI
    UuidList::const_iterator vmi_it = std::find(info->vmi_list_.begin(),
                                                info->vmi_list_.end(),
                                                entry->vmi_uuid_);
    if (vmi_it == info->vmi_list_.end())
        return;

    CONTROLLER_TRACE(IFMapVmExportTrace, UuidToString(entry->vm_uuid_),
                     UuidToString(entry->vmi_uuid_), "Delete");
    info->vmi_list_.remove(entry->vmi_uuid_);
    // Stop here if VM has more interfaces
    if (info->vmi_list_.size() != 0) {
        return;
    }

    // All interfaces deleted. Remove the VM entry
    vm_map_.erase(entry->vm_uuid_);

    // Unsubscribe from config if we have active channel
    AgentXmppChannel *peer = GetActivePeer(agent_);
    if (peer == NULL) {
        delete info;
        CONTROLLER_TRACE(IFMapVmExportTrace, UuidToString(entry->vm_uuid_), "",
                         "Peer NULL skipped Unsubscribe ");
        return;
    }

    AgentIfMapXmppChannel *ifmap = GetActiveChannel(agent_, info);
    delete info;
    if (ifmap == NULL) {
        CONTROLLER_TRACE(IFMapVmExportTrace, UuidToString(entry->vm_uuid_), "",
                         "Channel NULL skipped Unsubscribe ");
        return;
    }

    CONTROLLER_TRACE(IFMapVmExportTrace, UuidToString(entry->vm_uuid_), "",
                     "Unsubscribe ");
    AgentXmppChannel::ControllerSendVmCfgSubscribe(peer, entry->vm_uuid_,
                                                   false);
    return;
}

void AgentIfMapVmExport::VmiAdd(const ControllerVmiSubscribeData *entry) {
    struct VmExportInfo *info = NULL;
    VmMap::iterator vm_it = vm_map_.find(entry->vm_uuid_);
    if (vm_map_.end() != vm_it) {
        info = vm_it->second;
    } else {
        //If first VMI create the data
        info = new VmExportInfo(0);
        vm_map_[entry->vm_uuid_] = info;
    }

    //If VMI is not found, insert in the list
    UuidList::const_iterator vmi_it = std::find(info->vmi_list_.begin(),
                                                info->vmi_list_.end(),
                                                entry->vmi_uuid_);
    if (vmi_it == info->vmi_list_.end()) {
        CONTROLLER_TRACE(IFMapVmExportTrace, UuidToString(entry->vm_uuid_),
                         UuidToString(entry->vmi_uuid_), "Add");
        info->vmi_list_.push_back(entry->vmi_uuid_);
    }

    // Ensure that peer exists and is in active state
    AgentXmppChannel *peer = GetActivePeer(agent_);
    if (peer == NULL) {
        return;
    }

    // Ensure that channel is valid
    AgentIfMapXmppChannel *ifmap = GetActiveChannel(agent_, NULL);
    if (ifmap == NULL) {
        return;
    }

    //We have already sent the subscribe
    if (info->seq_number_ == ifmap->GetSeqNumber()) {
        return;
    }

    CONTROLLER_TRACE(IFMapVmExportTrace, UuidToString(entry->vm_uuid_),
                     UuidToString(entry->vmi_uuid_), "Subscribe");
    AgentXmppChannel::ControllerSendVmCfgSubscribe(peer, entry->vm_uuid_, true);

    //Update the sequence number
    info->seq_number_ = ifmap->GetSeqNumber();
}

void AgentIfMapVmExport::VmiEvent(const ControllerVmiSubscribeData *entry) {
    if (entry->del_) {
        VmiDelete(entry);
        return;
    }

    VmiAdd(entry);
    return;
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

AgentIfMapVmExport::AgentIfMapVmExport(Agent *agent) :
    agent_(agent) {
}

AgentIfMapVmExport::~AgentIfMapVmExport() {
    VmMap::iterator vm_it;
    struct VmExportInfo *info = NULL;

    //Destroy the vm_map and vmi list
    for(vm_it = vm_map_.begin(); vm_it != vm_map_.end(); vm_it++) {
        info = vm_it->second;
        delete info;
    }

    vm_map_.clear();
}
