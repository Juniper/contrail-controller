/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include "ifmap/ifmap_xmpp.h"
#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>

#include "base/util.h"
#include "base/logging.h"

#include "ifmap/ifmap_factory.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_sandesh_context.h"
#include "ifmap/ifmap_server_show_types.h"
#include "ifmap/ifmap_log_types.h"
#include "ifmap/ifmap_update_sender.h"

#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_server.h"

const std::string IFMapXmppChannel::NoFqnSet = "NoFqnSet";

// There are 3 task interactions:
// "xmpp::StateMachine" gives all the channel triggers.
// "db::IFMapTable" does all the work related to those triggers - except Ready
// "bgp::Config" does the final channel-unregister
//
// "xmpp::StateMachine" task gives us 5 triggers:
// 1. Ready/NotReady indicating the channel create/delete
// 2. VR-subscribe indicating the existence of the agent
// 3. VM-sub/unsub indicating the create/delete of a virtual-machine
// Process all the triggers in the context of the db::IFMapTable task - except
// the 'ready' trigger that is processed right away.
// #1 must be processed via the IFMapChannelManager.
// #2/#3 must be processed via the IFMapXmppChannel since they are
// channel-specific
//
// "bgp::Config":
// The NotReady trigger will cleanup the channel related resources and then
// Unregister via ProcessChannelUnregister() in the context of bgp::Config.
class ChannelEventProcTask : public Task {
public:
    // To be used for #1
    explicit ChannelEventProcTask(const ChannelEventInfo &ev,
                                  IFMapChannelManager *mgr)
        : Task(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable"), 0),
          event_info_(ev), ifmap_channel_manager_(mgr) {
    }

    // To be used for #2/#3
    explicit ChannelEventProcTask(const ChannelEventInfo &ev,
                                  IFMapXmppChannel *chnl)
        : Task(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable"), 0),
          event_info_(ev), ifmap_chnl_(chnl) {
    }

    virtual bool Run() {
        if (event_info_.event == XCE_NOT_READY) {
            ifmap_channel_manager_->ProcessChannelNotReady(event_info_.channel);
        } else if (event_info_.event == XCE_VR_SUBSCRIBE) {
            ifmap_chnl_->ProcessVrSubscribe(event_info_.name);
        } else if (event_info_.event == XCE_VM_SUBSCRIBE) {
            ifmap_chnl_->ProcessVmSubscribe(event_info_.name);
        } else if (event_info_.event == XCE_VM_UNSUBSCRIBE) {
            ifmap_chnl_->ProcessVmUnsubscribe(event_info_.name);
        }

        return true;
    }
    std::string Description() const { return "ChannelEventProcTask"; }

private:
    ChannelEventInfo event_info_;
    IFMapChannelManager *ifmap_channel_manager_;
    IFMapXmppChannel *ifmap_chnl_;
};

class IFMapXmppChannel::IFMapSender : public IFMapClient {
public:
    IFMapSender(IFMapXmppChannel *parent);

    virtual bool SendUpdate(const std::string &msg);

    virtual std::string ToString() const { return identifier_; }
    virtual const std::string &identifier() const { return identifier_; }
    const std::string &hostname() const { return hostname_; }
    IFMapXmppChannel *parent() { return parent_; }

    void SetIdentifier(const std::string &identifier) {
        identifier_ = identifier;
    }

private:
    IFMapXmppChannel *parent_;
    std::string hostname_;      // hostname
    std::string identifier_;    // FQN
};

IFMapXmppChannel::IFMapSender::IFMapSender(IFMapXmppChannel *parent)
    : parent_(parent), hostname_(parent_->channel_->ToString()) {
}

bool IFMapXmppChannel::IFMapSender::SendUpdate(const std::string &msg) {
    bool sent = parent_->channel_->Send(
        reinterpret_cast<const uint8_t *>(msg.data()), msg.size(), &msg,
        xmps::CONFIG,
        boost::bind(&IFMapXmppChannel::WriteReadyCb, parent_, _1));

    if (sent) {
        incr_msgs_sent();
        incr_bytes_sent(msg.size());
    } else {
        set_send_is_blocked(true);
        incr_msgs_blocked();
    }
    return sent;
}

void IFMapXmppChannel::WriteReadyCb(const boost::system::error_code &ec) {
    ifmap_client_->set_send_is_blocked(false);
    ifmap_server_->sender()->SendActive(ifmap_client_->index());
}

IFMapXmppChannel::IFMapXmppChannel(XmppChannel *channel, IFMapServer *server,
        IFMapChannelManager *ifmap_channel_manager) 
    : peer_id_(xmps::CONFIG), channel_(channel), ifmap_server_(server),
      ifmap_channel_manager_(ifmap_channel_manager),
      ifmap_client_(new IFMapSender(this)), client_added_(false),
      channel_name_(channel->connection()->ToUVEKey()) {

    ifmap_client_->SetName(channel->connection()->ToUVEKey());
    channel_->RegisterReceive(xmps::CONFIG, 
                              boost::bind(&IFMapXmppChannel::ReceiveUpdate, 
                                          this, _1));
}

IFMapXmppChannel::~IFMapXmppChannel() { 
    delete ifmap_client_;
    // Enqueue Unregister and process in the context of bgp::Config task
    ifmap_channel_manager_->EnqueueChannelUnregister(channel_);
}

// iqn should be [virtual-router:FQN-of-vr]
std::string IFMapXmppChannel::VrSubscribeGetVrName(const std::string &iqn,
                                                   bool *valid_message) {
    std::string vr_name;
    size_t tstart = iqn.find("virtual-router:");
    if (tstart == 0) {
        vr_name = std::string(iqn, (sizeof("virtual-router:") - 1));
        *valid_message = true;
    } else {
        *valid_message = false;
    }

    return vr_name;
}

// iqn should be [virtual-machine:UUID-of-vm]
std::string IFMapXmppChannel::VmSubscribeGetVmUuid(const std::string &iqn,
                                                   bool *valid_message) {
    std::string vm_uuid;
    size_t tstart = iqn.find("virtual-machine:");
    if (tstart == 0) {
        vm_uuid = std::string(iqn, (sizeof("virtual-machine:") - 1));
        *valid_message = true;
    } else {
        ifmap_channel_manager_->incr_invalid_vm_subscribe_messages();
        *valid_message = false;
    }

    return vm_uuid;
}

// If add-client was sent to ifmap_server, we must send delete-client too and
// vice-versa
bool IFMapXmppChannel::MustProcessChannelNotReady() {
    return client_added_;
}

const std::string& IFMapXmppChannel::FQName() const {
    if (ifmap_client_) {
        return ifmap_client_->identifier();
    } else {
        return IFMapXmppChannel::NoFqnSet;
    }
}

void IFMapXmppChannel::ProcessVrSubscribe(const std::string &identifier) {
    // If we have already received a vr-subscribe on this channel...
    if (client_added_) {
        ifmap_channel_manager_->incr_duplicate_vrsub_messages();
        IFMAP_XMPP_WARN(IFMapDuplicateVrSub, channel_name(), FQName());
        return;
    }

    ifmap_client_->SetIdentifier(identifier);
    bool add_client = true;
    ifmap_server_->ProcessClientWork(add_client, ifmap_client_);
    client_added_ = true;
    IFMAP_XMPP_DEBUG(IFMapXmppVrSubUnsub, "VrSubscribe", channel_name(),
                     FQName());
}

void IFMapXmppChannel::ProcessVmSubscribe(const std::string &vm_uuid) {
    if (!client_added_) {
        // If we didnt receive the vr-subscribe for this vm...
        ifmap_channel_manager_->incr_vmsub_novrsub_messages();
        IFMAP_XMPP_WARN(IFMapNoVrSub, "VmSubscribe", channel_name(), FQName(),
                        vm_uuid);
        return;
    }

    if (!ifmap_client_->HasAddedVm(vm_uuid)) {
        ifmap_client_->AddVm(vm_uuid);
        bool subscribe = true;
        ifmap_server_->ProcessVmSubscribe(ifmap_client_->identifier(), vm_uuid,
                                          subscribe, ifmap_client_->HasVms());
        IFMAP_XMPP_DEBUG(IFMapXmppVmSubUnsub, "VmSubscribe", channel_name(),
                         FQName(), vm_uuid);
    } else {
        // If we have already received a subscribe for this vm
        ifmap_channel_manager_->incr_duplicate_vmsub_messages();
        IFMAP_XMPP_WARN(IFMapDuplicateVmSub, channel_name(), FQName(), vm_uuid);
    }
}

void IFMapXmppChannel::ProcessVmUnsubscribe(const std::string &vm_uuid) {
    // If we didnt receive the vr-sub for this vm, ignore the request
    if (!client_added_) {
        ifmap_channel_manager_->incr_vmunsub_novrsub_messages();
        IFMAP_XMPP_WARN(IFMapNoVrSub, "VmUnsubscribe", channel_name(), FQName(),
                        vm_uuid);
        return;
    }

    if (ifmap_client_->HasAddedVm(vm_uuid)) {
        ifmap_client_->DeleteVm(vm_uuid);
        bool subscribe = false;
        ifmap_server_->ProcessVmSubscribe(ifmap_client_->identifier(), vm_uuid,
                                          subscribe, ifmap_client_->HasVms());
        IFMAP_XMPP_DEBUG(IFMapXmppVmSubUnsub, "VmUnsubscribe", channel_name(),
                         FQName(), vm_uuid);
    } else {
        // If we didnt receive the subscribe for this vm, ignore the unsubscribe
        ifmap_channel_manager_->incr_vmunsub_novmsub_messages();
        IFMAP_XMPP_WARN(IFMapNoVmSub, channel_name(), FQName(), vm_uuid);
    }
}

void IFMapXmppChannel::EnqueueVrSubscribe(const std::string &identifier) {
    ChannelEventInfo info;
    info.event = XCE_VR_SUBSCRIBE;
    info.name = identifier;

    ChannelEventProcTask *proc_task = new ChannelEventProcTask(info, this);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(proc_task);
}

void IFMapXmppChannel::EnqueueVmSubUnsub(bool subscribe,
                                         const std::string &vm_uuid) {
    ChannelEventInfo info;
    info.event = (subscribe == true) ? XCE_VM_SUBSCRIBE : XCE_VM_UNSUBSCRIBE;
    info.name = vm_uuid;

    ChannelEventProcTask *proc_task = new ChannelEventProcTask(info, this);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(proc_task);
}

// This runs in the context of the "xmpp::StateMachine" and queues all requests
// which are then processed in the context of "db::IFMapTable"
void IFMapXmppChannel::ReceiveUpdate(const XmppStanza::XmppMessage *msg) {

    if (msg->type == XmppStanza::IQ_STANZA) {
        const XmppStanza::XmppMessageIq *iq =
            static_cast<const XmppStanza::XmppMessageIq *>(msg);

        const char* const vr_string = "virtual-router:";
        const char* const vm_string = "virtual-machine:";
        if ((iq->iq_type.compare("set") == 0) && 
            (iq->action.compare("subscribe") == 0)) {
            if (iq->node.compare(0, strlen(vr_string), vr_string) == 0) {
                bool valid_message = false;
                std::string vr_name = VrSubscribeGetVrName(iq->node,
                                                           &valid_message);
                if (valid_message) {
                    EnqueueVrSubscribe(vr_name);
                }
            } else if (iq->node.compare(0, strlen(vm_string), vm_string) == 0) {
                bool valid_message = false;
                std::string vm_uuid = VmSubscribeGetVmUuid(iq->node,
                                                           &valid_message);
                if (valid_message) {
                    bool subscribe = true;
                    EnqueueVmSubUnsub(subscribe, vm_uuid);
                }
            } else {
                ifmap_channel_manager_->incr_unknown_subscribe_messages();
                IFMAP_XMPP_WARN(IFMapXmppUnknownMessage, iq->iq_type,
                                iq->action, iq->node, channel_name());
            }
        }
        if ((iq->iq_type.compare("set") == 0) && 
            (iq->action.compare("unsubscribe") == 0)) {
            if (iq->node.compare(0, strlen(vm_string), vm_string) == 0) {
                bool valid_message = false;
                std::string vm_uuid = VmSubscribeGetVmUuid(iq->node,
                                                           &valid_message);
                if (valid_message) {
                    bool subscribe = false;
                    EnqueueVmSubUnsub(subscribe, vm_uuid);
                }
            } else {
                ifmap_channel_manager_->incr_unknown_unsubscribe_messages();
                IFMAP_XMPP_WARN(IFMapXmppUnknownMessage, iq->iq_type,
                                iq->action, iq->node, channel_name());
            }
        }
    }
}

IFMapClient *IFMapXmppChannel::Sender() {
    return ifmap_client_;
}

uint64_t IFMapXmppChannel::msgs_sent() const {
    return ifmap_client_->msgs_sent();
}

// IFMapClient Manager routines
IFMapChannelManager::IFMapChannelManager(XmppServer *xmpp_server,
                                         IFMapServer *ifmap_server) 
    : xmpp_server_(xmpp_server), ifmap_server_(ifmap_server),
      config_task_work_queue_(
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"),
          0, boost::bind(&IFMapChannelManager::ProcessChannelUnregister,
                         this, _1)) {
    unknown_subscribe_messages = 0;
    unknown_unsubscribe_messages = 0;
    duplicate_channel_ready_messages = 0;
    invalid_channel_not_ready_messages = 0;
    invalid_channel_state_messages = 0;
    invalid_vm_subscribe_messages = 0;
    vmsub_novrsub_messages = 0;
    vmunsub_novrsub_messages = 0;
    vmunsub_novmsub_messages = 0;
    duplicate_vrsub_messages = 0;
    duplicate_vmsub_messages = 0;
    xmpp_server_->RegisterConnectionEvent(xmps::CONFIG,
        boost::bind(&IFMapChannelManager::IFMapXmppChannelEventCb, this, _1,
                    _2));
}

IFMapChannelManager::~IFMapChannelManager() {
    tbb::mutex::scoped_lock lock(channel_map_mutex_);
    STLDeleteElements(&channel_map_);
}

IFMapXmppChannel *IFMapChannelManager::FindChannel(XmppChannel *channel) {
    tbb::mutex::scoped_lock lock(channel_map_mutex_);
    ChannelMap::iterator loc =
        channel_map_.find(const_cast<XmppChannel *>(channel));
    if (loc != channel_map_.end()) {
        return loc->second;
    }
    return NULL;
}

IFMapXmppChannel *IFMapChannelManager::FindChannel(std::string tostring) {
    tbb::mutex::scoped_lock lock(channel_map_mutex_);
    BOOST_FOREACH(ChannelMap::value_type &i, channel_map_) {
        if (i.second->ToString() == tostring) 
            return i.second;
    }
    return NULL;
}

IFMapXmppChannel *IFMapChannelManager::CreateIFMapXmppChannel(
        XmppChannel *channel) {
    IFMapXmppChannel *ifmap_chnl = IFMapFactory::Create<IFMapXmppChannel>(
                                       channel, ifmap_server_, this);
    tbb::mutex::scoped_lock lock(channel_map_mutex_);
    channel_map_.insert(std::make_pair(channel, ifmap_chnl));
    return ifmap_chnl;
}

void IFMapChannelManager::DeleteIFMapXmppChannel(IFMapXmppChannel *ifmap_chnl) {
    tbb::mutex::scoped_lock lock(channel_map_mutex_);
    channel_map_.erase(ifmap_chnl->channel());
    delete ifmap_chnl;
}

void IFMapChannelManager::ProcessChannelReady(XmppChannel *channel) {
    IFMapXmppChannel *ifmap_chnl = FindChannel(channel);
    if (ifmap_chnl == NULL) {
        IFMAP_XMPP_DEBUG(IFMapXmppChannelEvent, "Create",
            channel->connection()->ToUVEKey(), IFMapXmppChannel::NoFqnSet);
        CreateIFMapXmppChannel(channel);
        ConfigClientManager *client_manager = ifmap_server_->get_config_manager();
        if (client_manager && !client_manager->GetEndOfRibComputed()) {
            IFMAP_XMPP_DEBUG(IFMapXmppChannelEvent, "Close",
                             channel->connection()->ToUVEKey(),
                             IFMapXmppChannel::NoFqnSet);
            channel->Close();
        }
    } else {
        incr_duplicate_channel_ready_messages();
    }
}

void IFMapChannelManager::ProcessChannelNotReady(XmppChannel *channel) {
    IFMapXmppChannel *ifmap_chnl = FindChannel(channel);
    if (ifmap_chnl) {
        // If we have received subscriptions and ifmap_server knows about the
        // client for this channel, ask ifmap_server to cleanup.
        std::string fq_name = ifmap_chnl->FQName();
        if (ifmap_chnl->MustProcessChannelNotReady()) {
            bool add_client = false;
            ifmap_server_->ProcessClientWork(add_client, ifmap_chnl->Sender());
        }
        IFMAP_XMPP_DEBUG(IFMapXmppChannelEvent, "Destroy",
                         channel->connection()->ToUVEKey(), fq_name);
        DeleteIFMapXmppChannel(ifmap_chnl);
    } else {
        incr_invalid_channel_not_ready_messages();
    }
}

void IFMapChannelManager::EnqueueChannelEvent(XCEvent event,
                                              XmppChannel *channel) {
    ChannelEventInfo info;
    info.event = event;
    info.channel = channel;

    ChannelEventProcTask *proc_task = new ChannelEventProcTask(info, this);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(proc_task);
}

// This runs in the context of the "xmpp::StateMachine" 
void IFMapChannelManager::IFMapXmppChannelEventCb(XmppChannel *channel,
                                                  xmps::PeerState state) {
    if (state == xmps::READY) {
        // Process the READY right away
        ProcessChannelReady(channel);
    } else if (state == xmps::NOT_READY) {
        // Queue the NOT_READY to be processed via the "db::IFMapTable" task
        EnqueueChannelEvent(XCE_NOT_READY, channel);
    } else {
        incr_invalid_channel_state_messages();
    }
}

// This runs in the context of bgp::Config
bool IFMapChannelManager::ProcessChannelUnregister(ConfigTaskQueueEntry entry) {
    IFMAP_XMPP_DEBUG(IFMapChannelUnregisterMessage,
                     "Unregistering config peer for channel",
                     entry.channel->connection()->ToUVEKey());
    entry.channel->UnRegisterReceive(xmps::CONFIG);
    return true;
}

void IFMapChannelManager::EnqueueChannelUnregister(XmppChannel *channel) {
    // Let ProcessChannelUnregister() unregister in the context of bgp::Config
    ConfigTaskQueueEntry entry;
    entry.channel = channel;
    config_task_work_queue_.Enqueue(entry);
}

void IFMapChannelManager::FillChannelMap(
        std::vector<IFMapXmppChannelMapEntry> *out_map) {
    tbb::mutex::scoped_lock lock(channel_map_mutex_);
    for (ChannelMap::iterator iter = channel_map_.begin();
         iter != channel_map_.end(); ++iter) {
        IFMapXmppChannel *ifmap_chnl = iter->second;
        IFMapXmppChannelMapEntry entry;
        entry.set_client_name(ifmap_chnl->FQName());
        entry.set_host_name(ifmap_chnl->channel()->ToString());
        entry.set_channel_name(ifmap_chnl->channel_name());
        entry.set_client_added(ifmap_chnl->get_client_added());
        out_map->push_back(entry);
    }
}

static bool IFMapXmppShowReqHandleRequest(const Sandesh *sr,
                                          const RequestPipeline::PipeSpec ps,
                                          int stage, int instNum,
                                          RequestPipeline::InstData *data) {
    const IFMapXmppShowReq *request =
        static_cast<const IFMapXmppShowReq *>(ps.snhRequest_.get());
    IFMapSandeshContext *sctx =
        static_cast<IFMapSandeshContext *>(request->module_context("IFMap"));
    IFMapChannelManager *ifmap_channel_manager =
        sctx->ifmap_server()->get_ifmap_channel_manager();

    IFMapXmppShowResp *response = new IFMapXmppShowResp();
    response->set_context(request->context());
    response->set_more(false);

    if (!ifmap_channel_manager) {
        response->Response();
        return true;
    }

    IFMapChannelManagerInfo channel_manager_info;
    IFMapChannelManagerStats channel_manager_stats;

    channel_manager_stats.set_unknown_subscribe_messages(
        ifmap_channel_manager->get_unknown_subscribe_messages());
    channel_manager_stats.set_unknown_unsubscribe_messages(
        ifmap_channel_manager->get_unknown_unsubscribe_messages());
    channel_manager_stats.set_duplicate_channel_ready_messages(
        ifmap_channel_manager->get_duplicate_channel_ready_messages());
    channel_manager_stats.set_invalid_channel_not_ready_messages(
        ifmap_channel_manager->get_invalid_channel_not_ready_messages());
    channel_manager_stats.set_invalid_channel_state_messages(
        ifmap_channel_manager->get_invalid_channel_state_messages());
    channel_manager_stats.set_invalid_vm_subscribe_messages(
        ifmap_channel_manager->get_invalid_vm_subscribe_messages());
    channel_manager_stats.set_vmsub_novrsub_messages(
        ifmap_channel_manager->get_vmsub_novrsub_messages());
    channel_manager_stats.set_vmunsub_novrsub_messages(
        ifmap_channel_manager->get_vmunsub_novrsub_messages());
    channel_manager_stats.set_vmunsub_novmsub_messages(
        ifmap_channel_manager->get_vmunsub_novmsub_messages());
    channel_manager_stats.set_duplicate_vrsub_messages(
        ifmap_channel_manager->get_duplicate_vrsub_messages());
    channel_manager_stats.set_duplicate_vmsub_messages(
        ifmap_channel_manager->get_duplicate_vmsub_messages());

    IFMapXmppChannelMapList channel_map_list;
    std::vector<IFMapXmppChannelMapEntry> channel_map;
    ifmap_channel_manager->FillChannelMap(&channel_map);
    channel_map_list.set_channel_list(channel_map);
    channel_map_list.set_channel_count(channel_map.size());

    channel_manager_info.set_channel_manager_stats(channel_manager_stats);
    channel_manager_info.set_channel_manager_map(channel_map_list);

    response->set_channel_manager_info(channel_manager_info);
    response->Response();

    // Return 'true' so that we are not called again
    return true;
}

void IFMapXmppShowReq::HandleRequest() const {

    RequestPipeline::StageSpec s0;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s0.taskId_ = scheduler->GetTaskId("db::IFMapTable");
    s0.cbFn_ = IFMapXmppShowReqHandleRequest;
    s0.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= boost::assign::list_of(s0);
    RequestPipeline rp(ps);
}

