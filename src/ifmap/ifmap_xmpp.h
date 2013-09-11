/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_IFMAP_INC__
#define __XMPP_IFMAP_INC__

#include <map>
#include <string>

#include "base/queue_task.h"
#include <boost/function.hpp>
#include <boost/system/error_code.hpp>
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_server.h"
#include "xmpp/xmpp_channel.h"

class XmppChannel;
class XmppServer;
class IFMapChannelManager;
class IFMapXmppChannelMapEntry;

// Xmpp Channel Events
enum XCEvent {
    XCE_NOT_READY = 1,
    XCE_VR_SUBSCRIBE = 2,
    XCE_VM_SUBSCRIBE = 3,
    XCE_VM_UNSUBSCRIBE = 4,
};

struct ChannelEventInfo {
    XCEvent event;
    XmppChannel *channel;
    std::string name;
};

class IFMapXmppChannel { 
public:
    class IFMapSender;
    IFMapXmppChannel(XmppChannel *, IFMapServer *, IFMapChannelManager *);
    virtual ~IFMapXmppChannel();

    std::string ToString() const { return channel_->ToString(); } // hostname
    IFMapClient *Sender();

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *);
    XmppChannel *channel() { return channel_; }

    void ClearCounters();
    uint64_t msgs_sent() const;
    std::string VrSubscribeGetVrName(const std::string &iqnode, 
                                     bool *valid_message);
    std::string VmSubscribeGetVmUuid(const std::string &iqnode, 
                                     bool *valid_message);
    bool MustProcessChannelNotReady();

    void ProcessVrSubscribe(const std::string &identifier);
    void EnqueueVrSubscribe(const std::string &identifier);

    void ProcessVmSubscribe(const std::string &vm_uuid);
    void ProcessVmUnsubscribe(const std::string &vm_uuid);
    void EnqueueVmSubUnsub(bool subscribe, const std::string &vm_uuid);
    bool get_client_added() { return client_added_; }

private:
    friend class XmppIfmapTest;
    void WriteReadyCb(const boost::system::error_code &ec);

    xmps::PeerId peer_id_;
    XmppChannel *channel_;
    IFMapServer *ifmap_server_;
    IFMapChannelManager *ifmap_channel_manager_;
    IFMapSender *ifmap_client_;
    bool client_added_;  // true if ifmap_server has processed add-client
};

class IFMapChannelManager {
public:
    IFMapChannelManager(XmppServer *, IFMapServer *);
    virtual ~IFMapChannelManager();

    IFMapXmppChannel *FindChannel(XmppChannel *);
    IFMapXmppChannel *FindChannel(std::string);
    void IFMapXmppChannelEventCb(XmppChannel *, xmps::PeerState);
    virtual IFMapXmppChannel *CreateIFMapXmppChannel(XmppChannel *);
    void EnqueueChannelUnregister(XmppChannel *channel);
    void ProcessChannelReady(XmppChannel *channel);
    void ProcessChannelNotReady(XmppChannel *channel);

    void incr_unknown_subscribe_messages() { ++unknown_subscribe_messages; }
    void incr_unknown_unsubscribe_messages() { ++unknown_unsubscribe_messages; }
    void incr_duplicate_channel_ready_messages() {
        ++duplicate_channel_ready_messages;
    }
    void incr_invalid_channel_not_ready_messages() {
        ++invalid_channel_not_ready_messages;
    }
    void incr_invalid_channel_state_messages() {
        ++invalid_channel_state_messages;
    }
    void incr_invalid_vm_subscribe_messages() {
        ++invalid_vm_subscribe_messages;
    }
    void incr_vmsub_novrsub_messages() {
        ++vmsub_novrsub_messages;
    }
    void incr_vmunsub_novrsub_messages() {
        ++vmunsub_novrsub_messages;
    }
    void incr_vmunsub_novmsub_messages() {
        ++vmunsub_novmsub_messages;
    }
    void incr_dupicate_vrsub_messages() {
        ++dupicate_vrsub_messages;
    }
    void incr_dupicate_vmsub_messages() {
        ++dupicate_vmsub_messages;
    }

    uint64_t get_unknown_subscribe_messages() {
        return unknown_subscribe_messages; 
    }
    uint64_t get_unknown_unsubscribe_messages() {
        return unknown_unsubscribe_messages;
    }
    uint64_t get_duplicate_channel_ready_messages() {
        return duplicate_channel_ready_messages;
    }
    uint64_t get_invalid_channel_not_ready_messages() {
        return invalid_channel_not_ready_messages;
    }
    uint64_t get_invalid_channel_state_messages() {
        return invalid_channel_state_messages;
    }
    uint64_t get_invalid_vm_subscribe_messages() {
        return invalid_vm_subscribe_messages;
    }
    uint64_t get_vmsub_novrsub_messages() {
        return vmsub_novrsub_messages;
    }
    uint64_t get_vmunsub_novrsub_messages() {
        return vmunsub_novrsub_messages;
    }
    uint64_t get_vmunsub_novmsub_messages() {
        return vmunsub_novmsub_messages;
    }
    uint64_t get_dupicate_vrsub_messages() {
        return dupicate_vrsub_messages;
    }
    uint64_t get_dupicate_vmsub_messages() {
        return dupicate_vmsub_messages;
    }
    void FillChannelMap(std::vector<IFMapXmppChannelMapEntry> *out_map);

private:
    friend class IFMapChannelManagerTest;
    friend class XmppIfmapTest;
    typedef std::map<XmppChannel *, IFMapXmppChannel *> ChannelMap;
    struct ConfigTaskQueueEntry {
        XmppChannel *channel;
    };

    XmppServer *xmpp_server_;
    IFMapServer *ifmap_server_;
    ChannelMap channel_map_;
    tbb::mutex channel_map_mutex_; // serializes access to channel_map_
    WorkQueue<ConfigTaskQueueEntry> config_task_work_queue_;

    bool ProcessChannelUnregister(ConfigTaskQueueEntry entry);

    void DeleteIFMapXmppChannel(IFMapXmppChannel *ifmap_chnl);
    void EnqueueChannelEvent(XCEvent event, XmppChannel *channel);

    uint64_t unknown_subscribe_messages;
    uint64_t unknown_unsubscribe_messages;
    uint64_t duplicate_channel_ready_messages;
    uint64_t invalid_channel_not_ready_messages;
    uint64_t invalid_channel_state_messages;
    uint64_t invalid_vm_subscribe_messages;
    uint64_t vmsub_novrsub_messages;
    uint64_t vmunsub_novrsub_messages;
    uint64_t vmunsub_novmsub_messages;
    uint64_t dupicate_vrsub_messages;
    uint64_t dupicate_vmsub_messages;
};

#endif // __XMPP_IFMAP_INC__
