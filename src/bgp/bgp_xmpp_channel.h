/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_XMPP_CHANNEL_H__
#define __BGP_XMPP_CHANNEL_H__

#include <map>
#include <string>
#include <boost/function.hpp>
#include <boost/system/error_code.hpp>
#include <boost/scoped_ptr.hpp>

#include "net/rd.h"

#include "base/queue_task.h"
#include "bgp/bgp_ribout.h"
#include "bgp/routing-instance/routing_instance.h"
#include "xmpp/xmpp_channel.h"

namespace pugi{
class xml_node;
}

class BgpServer;
struct DBRequest;
class IPeer;
class PeerCloseManager;
class XmppServer;
class BgpXmppChannelMock;
class BgpXmppChannelManager;
class BgpXmppChannelManagerMock;
class XmppSession;

class BgpXmppChannel {
public:
    struct Stats {
        Stats();
        int rt_updates;
        int reach;
        int unreach;
    };

    BgpXmppChannel(XmppChannel *, BgpServer *, BgpXmppChannelManager *);
    virtual ~BgpXmppChannel();

    void Close();
    IPeer *Peer();

    std::string ToString() const;
    std::string StateName() const;
    boost::asio::ip::tcp::endpoint remote_endpoint();
    boost::asio::ip::tcp::endpoint local_endpoint();
    bool peer_deleted();

    const XmppSession *GetSession() const;
    const Stats &rx_stats() const { return stats_[0]; }
    const Stats &tx_stats() const { return stats_[1]; }
    void set_deleted(bool deleted) { deleted_ = deleted; }
    bool deleted() { return deleted_; }
    void RoutingInstanceCallback(std::string vrf_name, int op);
    void ASNUpdateCallback(as_t old_asn);
    void FillInstanceMembershipInfo(BgpNeighborResp *resp);
    void FillTableMembershipInfo(BgpNeighborResp *resp);

private:
    friend class BgpXmppChannelMock;
    friend class BgpXmppChannelManager;
    friend class BgpXmppUnitTest;
    class XmppPeer;
    class PeerClose;
    class PeerStats;

    //
    // State the instance id received in Membership subscription request
    // Also remember we received unregister request
    //
    enum RequestType {
        SUBSCRIBE,
        UNSUBSCRIBE,
    };
    struct MembershipRequestState {
        MembershipRequestState(RequestType current, int id) 
            : current_req(current), instance_id(id), pending_req(current) {
        }
        RequestType current_req;
        int instance_id;
        RequestType pending_req;

    };

    // Map of routing instances to which this BgpXmppChannel is subscribed.
    struct SubscriptionState {
        SubscriptionState(const RoutingInstance::RouteTargetList &targets,
            int index) : targets(targets), index(index) {
        }
        RoutingInstance::RouteTargetList targets;
        int index;
    };
    typedef std::map<RoutingInstance *, SubscriptionState>
        SubscribedRoutingInstanceList;

    // map of routing-instance table name to XMPP subscription request state
    typedef std::map<std::string, MembershipRequestState> 
                                            RoutingTableMembershipRequestMap;

    // map of routing-instance name to XMPP subscription request state
    // This map maintains list of requests that are rxed for subscription 
    // before routing instance is actually created
    typedef std::map<std::string, int> VrfMembershipRequestMap;

    // The code assumes that multimap preserves insertion order for duplicate
    // values of same key.
    typedef std::pair<const std::string, const std::string> VrfTableName;
    typedef std::multimap<VrfTableName, DBRequest *> DeferQ;

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg);

    void ProcessItem(std::string rt_instance, const pugi::xml_node &item,
                     bool add_change);
    void ProcessMcastItem(std::string rt_instance, 
                          const pugi::xml_node &item, bool add_change);
    void ProcessEnetItem(std::string rt_instance,
                         const pugi::xml_node &item, bool add_change);
    void PublishRTargetRoute(RoutingInstance *instance, bool add_change,
                             int index);
    void RTargetRouteOp(BgpTable *rtarget_table, as4_t asn, 
                    const RouteTarget &rt, BgpAttrPtr attr, bool add_change);
    void ProcessASUpdate(as4_t old_as);
    void ProcessSubscriptionRequest(std::string rt_instance,
                                    const XmppStanza::XmppMessageIq *iq,
                                    bool add_change);

    void RegisterTable(BgpTable *table, int instance_id);
    void UnregisterTable(BgpTable *table);
    bool MembershipResponseHandler(std::string table_name);
    void MembershipRequestCallback(IPeer *ipeer, BgpTable *table);
    void DequeueRequest(const std::string &table_name, DBRequest *request);
    bool XmppDecodeAddress(int af, const std::string &address,
                           IpAddress *addrp);
    bool ResumeClose();
    void FlushDeferQ(std::string vrf_name);
    void FlushDeferQ(std::string vrf_name, std::string table_name);
    void ProcessDeferredSubscribeRequest(RoutingInstance *rt_instance, 
                                         int instance_id);
    xmps::PeerId peer_id_;
    XmppChannel *channel_;
    BgpServer *bgp_server_;
    boost::scoped_ptr<XmppPeer> peer_;
    boost::scoped_ptr<PeerClose> peer_close_;
    boost::scoped_ptr<PeerStats> peer_stats_;
    RibExportPolicy bgp_policy_;

    // DB Requests pending membership request response.
    DeferQ defer_q_;

    RoutingTableMembershipRequestMap routingtable_membership_request_map_;
    VrfMembershipRequestMap vrf_membership_request_map_;
    BgpXmppChannelManager *manager_;
    bool close_in_progress_;
    bool deleted_;
    bool defer_peer_close_;
    WorkQueue<std::string> membership_response_worker_;
    SubscribedRoutingInstanceList routing_instances_;

    // statistics
    Stats stats_[2];

    // Label block manager for multicast labels.
    LabelBlockManagerPtr lb_mgr_;

    DISALLOW_COPY_AND_ASSIGN(BgpXmppChannel);
};

class BgpXmppChannelManager {
public:
    typedef std::map<const XmppChannel *, BgpXmppChannel *> XmppChannelMap;

    BgpXmppChannelManager(XmppServer *, BgpServer *);
    virtual ~BgpXmppChannelManager();

    typedef boost::function<void(BgpXmppChannel *)> VisitorFn;
    void VisitChannels(BgpXmppChannelManager::VisitorFn);
    BgpXmppChannel *FindChannel(const XmppChannel *);
    BgpXmppChannel *FindChannel(std::string client);
    void RemoveChannel(XmppChannel *);
    virtual void XmppHandleChannelEvent(XmppChannel *, xmps::PeerState);

    const XmppChannelMap &channel_map() const { return channel_map_; }
    bool DeleteExecutor(BgpXmppChannel *channel);
    void Enqueue(BgpXmppChannel *channel) {
        queue_.Enqueue(channel);
    }
    bool IsReadyForDeletion();
    void RoutingInstanceCallback(std::string vrf_name, int op);
    void ASNUpdateCallback(as_t old_asn);

    uint32_t count() const {
        return channel_map_.size();
    }
    uint32_t NumUpPeer() const {
        return channel_map_.size();
    }
    BgpServer *bgp_server() { return bgp_server_; }
    XmppServer *xmpp_server() { return xmpp_server_; }
protected:
    virtual BgpXmppChannel *CreateChannel(XmppChannel *);

private:
    friend class BgpXmppChannelManagerMock;
    friend class BgpXmppUnitTest;

    XmppServer *xmpp_server_;
    BgpServer  *bgp_server_;
    WorkQueue<BgpXmppChannel *> queue_;
    XmppChannelMap channel_map_;
    int id_;
    int asn_listener_id_;

    DISALLOW_COPY_AND_ASSIGN(BgpXmppChannelManager);
};

#endif // __BGP_XMPP_CHANNEL_H__
