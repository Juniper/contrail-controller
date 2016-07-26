/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_XMPP_CHANNEL_H_
#define SRC_BGP_BGP_XMPP_CHANNEL_H_

#include <boost/function.hpp>
#include <boost/system/error_code.hpp>
#include <boost/scoped_ptr.hpp>
#include <tbb/mutex.h>

#include <map>
#include <set>
#include <string>
#include <utility>

#include "base/queue_task.h"
#include "bgp/bgp_rib_policy.h"
#include "bgp/routing-instance/routing_instance.h"
#include "io/tcp_session.h"
#include "net/rd.h"
#include "xmpp/xmpp_channel.h"

namespace pugi {
class xml_node;
}

class BgpGlobalSystemConfig;
class BgpRouterState;
class BgpServer;
struct DBRequest;
class IPeer;
class PeerCloseManager;
class XmppServer;
class BgpXmppChannelMock;
class BgpXmppChannelManager;
class BgpXmppChannelManagerMock;
class Timer;
class XmppConfigUpdater;
class XmppPeerInfoData;
class XmppSession;

class BgpXmppChannel {
public:
    static const int kEndOfRibTime = 30; // seconds
    enum StatsIndex {
        RX,
        TX,
    };
    struct Stats {
        Stats();
        uint64_t rt_updates;
        uint64_t reach;
        uint64_t unreach;
    };
    struct ChannelStats {
        ChannelStats();
        uint64_t instance_subscribe;
        uint64_t instance_unsubscribe;
        uint64_t table_subscribe;
        uint64_t table_subscribe_complete;
        uint64_t table_unsubscribe;
        uint64_t table_unsubscribe_complete;
    };

    struct ErrorStats {
        ErrorStats();
        void incr_inet6_rx_bad_xml_token_count();
        void incr_inet6_rx_bad_prefix_count();
        void incr_inet6_rx_bad_nexthop_count();
        void incr_inet6_rx_bad_afi_safi_count();
        uint64_t get_inet6_rx_bad_xml_token_count() const;
        uint64_t get_inet6_rx_bad_prefix_count() const;
        uint64_t get_inet6_rx_bad_nexthop_count() const;
        uint64_t get_inet6_rx_bad_afi_safi_count() const;

        uint64_t inet6_rx_bad_xml_token_count;
        uint64_t inet6_rx_bad_prefix_count;
        uint64_t inet6_rx_bad_nexthop_count;
        uint64_t inet6_rx_bad_afi_safi_count;
    };

    explicit BgpXmppChannel(XmppChannel *channel, BgpServer *bgp_server = NULL,
                            BgpXmppChannelManager *manager = NULL);
    virtual ~BgpXmppChannel();

    void Close();
    IPeer *Peer();
    const IPeer *Peer() const;
    virtual TcpSession::Endpoint endpoint() const;

    std::string ToString() const;
    std::string ToUVEKey() const;
    std::string StateName() const;
    TcpSession::Endpoint remote_endpoint() const;
    TcpSession::Endpoint local_endpoint() const;
    std::string transport_address_string() const;

    void set_peer_closed(bool flag);
    bool peer_deleted() const;
    bool membership_unavailable() const { return membership_unavailable_; }
    uint64_t peer_closed_at() const;
    bool routingtable_membership_request_map_empty() const;
    size_t GetMembershipRequestQueueSize() const;

    const XmppSession *GetSession() const;
    const Stats &rx_stats() const { return stats_[RX]; }
    const Stats &tx_stats() const { return stats_[TX]; }
    ErrorStats &error_stats() { return error_stats_; }
    const ErrorStats &error_stats() const { return error_stats_; }
    void set_deleted(bool deleted) { deleted_ = deleted; }
    bool deleted() { return deleted_; }
    void RoutingInstanceCallback(std::string vrf_name, int op);
    void ASNUpdateCallback(as_t old_asn, as_t old_local_asn);
    void IdentifierUpdateCallback(Ip4Address old_identifier);
    void FillInstanceMembershipInfo(BgpNeighborResp *resp) const;
    void FillTableMembershipInfo(BgpNeighborResp *resp) const;
    void FillCloseInfo(BgpNeighborResp *resp) const;
    void StaleCurrentSubscriptions();
    void LlgrStaleCurrentSubscriptions();
    void SweepCurrentSubscriptions();
    void XMPPPeerInfoSend(const XmppPeerInfoData &peer_info) const;
    const XmppChannel *channel() const { return channel_; }
    void StartEndOfRibTimer();

    uint64_t get_rx_route_reach() const { return stats_[RX].reach; }
    uint64_t get_rx_route_unreach() const { return stats_[RX].unreach; }
    uint64_t get_rx_update() const { return stats_[RX].rt_updates; }

    uint64_t get_tx_route_reach() const { return stats_[TX].reach; }
    uint64_t get_tx_route_unreach() const { return stats_[TX].unreach; }
    uint64_t get_tx_update() const { return stats_[TX].rt_updates; }
    bool SkipUpdateSend();

protected:
    XmppChannel *channel_;

private:
    friend class BgpXmppChannelMock;
    friend class BgpXmppChannelManager;
    friend class BgpXmppParseTest;
    friend class BgpXmppUnitTest;
    class XmppPeer;
    class PeerClose;
    class PeerStats;

    //
    // State the instance id received in Membership subscription request
    // Also remember we received unregister request
    //
    enum RequestType {
        NONE,
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
        enum State { NONE, STALE };
        SubscriptionState(const RoutingInstance::RouteTargetList &targets,
                          int index)
                : targets(targets), index(index), state(NONE) { }
        const bool IsStale() const { return(state == STALE); }
        void SetStale() { state = STALE; }
        void ClearStale() { state = NONE; }

        RoutingInstance::RouteTargetList targets;
        int index;
        State state;
    };
    typedef std::map<RoutingInstance *, SubscriptionState>
        SubscribedRoutingInstanceList;
    typedef std::set<RoutingInstance *> RoutingInstanceList;
    typedef std::map<RouteTarget, RoutingInstanceList> PublishedRTargetRoutes;

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

    virtual bool GetMembershipInfo(BgpTable *table,
        int *instance_id, uint64_t *subscribed_at, RequestType *req_type);
    virtual bool GetMembershipInfo(const std::string &vrf_name,
        int *instance_id);
    bool VerifyMembership(const std::string &vrf_name, Address::Family family,
        BgpTable **table, int *instance_id, uint64_t *subscribed_at,
        bool *subscribe_pending);

    bool ProcessItem(std::string vrf_name, const pugi::xml_node &node,
                     bool add_change);
    bool ProcessInet6Item(std::string vrf_name, const pugi::xml_node &node,
                          bool add_change);
    bool ProcessMcastItem(std::string vrf_name,
                          const pugi::xml_node &item, bool add_change);
    bool ProcessEnetItem(std::string vrf_name,
                         const pugi::xml_node &item, bool add_change);
    void PublishRTargetRoute(RoutingInstance *instance, bool add_change,
                             int index);
    void RTargetRouteOp(BgpTable *rtarget_table, as4_t asn,
                    const RouteTarget &rt, BgpAttrPtr attr, bool add_change);
    void AddNewRTargetRoute(BgpTable *rtarget_table,
        RoutingInstance *rtinstance, const RouteTarget &rtarget,
        BgpAttrPtr attr);
    void DeleteRTargetRoute(BgpTable *rtarget_table,
        RoutingInstance *rtinstance, const RouteTarget &rtarget);
    void ProcessASUpdate(as4_t old_as);
    void ProcessSubscriptionRequest(std::string rt_instance,
                                    const XmppStanza::XmppMessageIq *iq,
                                    bool add_change);

    void RegisterTable(int line, BgpTable *table, int instance_id);
    void UnregisterTable(int line, BgpTable *table);
    bool MembershipResponseHandler(std::string table_name);
    void MembershipRequestCallback(BgpTable *table);
    void ProcessPendingSubscriptions();
    void DequeueRequest(const std::string &table_name, DBRequest *request);
    bool XmppDecodeAddress(int af, const std::string &address,
                           IpAddress *addrp, bool zero_ok = false);
    bool ResumeClose();
    void FlushDeferQ(std::string vrf_name);
    void FlushDeferQ(std::string vrf_name, std::string table_name);
    void ProcessDeferredSubscribeRequest(RoutingInstance *rt_instance,
                                         int instance_id);
    void ClearStaledSubscription(BgpTable *rtarget_table,
            RoutingInstance *rt_instance, BgpAttrPtr attr,
            SubscriptionState *sub_state);
    void UpdateRouteTargetRouteFlag(const SubscriptionState *sub_state,
        bool llgr);
    const BgpXmppChannelManager *manager() const { return manager_; }
    bool ProcessMembershipResponse(std::string table_name,
             RoutingTableMembershipRequestMap::iterator loc);
    void ReceiveEndOfRIB(Address::Family family);
    void EndOfRibTimerErrorHandler(std::string error_name,
                                   std::string error_message);
    bool EndOfRibTimerExpired();

    xmps::PeerId peer_id_;
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
    bool delete_in_progress_;
    bool deleted_;
    bool defer_peer_close_;
    bool membership_unavailable_;
    bool skip_update_send_;
    bool skip_update_send_cached_;
    Timer *end_of_rib_timer_;
    WorkQueue<std::string> membership_response_worker_;
    SubscribedRoutingInstanceList routing_instances_;
    PublishedRTargetRoutes rtarget_routes_;

    // statistics
    Stats stats_[2];
    ChannelStats channel_stats_;
    ErrorStats error_stats_;

    // Label block manager for multicast labels.
    LabelBlockManagerPtr lb_mgr_;

    DISALLOW_COPY_AND_ASSIGN(BgpXmppChannel);
};

class BgpXmppChannelManager {
public:
    typedef std::map<const XmppChannel *, BgpXmppChannel *> XmppChannelMap;
    typedef std::map<std::string, BgpXmppChannel *> XmppChannelNameMap;
    typedef XmppChannelNameMap::const_iterator const_name_iterator;
    typedef boost::function<void(BgpXmppChannel *)> VisitorFn;

    BgpXmppChannelManager(XmppServer *, BgpServer *);
    virtual ~BgpXmppChannelManager();

    const_name_iterator name_cbegin() const {
        return channel_name_map_.begin();
    }
    const_name_iterator name_cend() const {
        return channel_name_map_.end();
    }
    const_name_iterator name_clower_bound(const std::string &name) const {
        return channel_name_map_.lower_bound(name);
    }

    void VisitChannels(BgpXmppChannelManager::VisitorFn);
    void VisitChannels(BgpXmppChannelManager::VisitorFn) const;
    BgpXmppChannel *FindChannel(const XmppChannel *channel);
    BgpXmppChannel *FindChannel(std::string client);
    void RemoveChannel(XmppChannel *channel);
    virtual void XmppHandleChannelEvent(XmppChannel *channel,
                                        xmps::PeerState state);

    const XmppChannelMap &channel_map() const { return channel_map_; }
    bool DeleteExecutor(BgpXmppChannel *bx_channel);
    void Enqueue(BgpXmppChannel *bx_channel) {
        queue_.Enqueue(bx_channel);
    }
    bool IsReadyForDeletion();
    void SetQueueDisable(bool disabled);
    size_t GetQueueSize() const;
    void RoutingInstanceCallback(std::string vrf_name, int op);
    void AdminDownCallback();
    void ASNUpdateCallback(as_t old_asn, as_t old_local_asn);
    void IdentifierUpdateCallback(Ip4Address old_identifier);

    uint32_t count() const {
        tbb::mutex::scoped_lock lock(mutex_);
        return channel_map_.size();
    }
    uint32_t NumUpPeer() const {
        tbb::mutex::scoped_lock lock(mutex_);
        return channel_map_.size();
    }

    int32_t deleting_count() const { return deleting_count_; }
    void increment_deleting_count() { deleting_count_++; }
    void decrement_deleting_count() {
        assert(deleting_count_);
        deleting_count_--;
    }

    BgpServer *bgp_server() { return bgp_server_; }
    XmppServer *xmpp_server() { return xmpp_server_; }
    const XmppServer *xmpp_server() const { return xmpp_server_; }
    uint64_t get_subscription_gen_id() {
        return subscription_gen_id_.fetch_and_increment();
    }
    bool CollectStats(BgpRouterState *state, bool first) const;

protected:
    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel);

private:
    friend class BgpXmppChannelManagerMock;
    friend class BgpXmppUnitTest;

    void FillPeerInfo(const BgpXmppChannel *channel) const;

    XmppServer *xmpp_server_;
    BgpServer *bgp_server_;
    WorkQueue<BgpXmppChannel *> queue_;
    mutable tbb::mutex mutex_;
    XmppChannelMap channel_map_;
    XmppChannelNameMap channel_name_map_;
    int id_;
    int admin_down_listener_id_;
    int asn_listener_id_;
    int identifier_listener_id_;
    tbb::atomic<int32_t> deleting_count_;
    // Generation number for subscription tracking
    tbb::atomic<uint64_t> subscription_gen_id_;

    DISALLOW_COPY_AND_ASSIGN(BgpXmppChannelManager);
};

#endif  // SRC_BGP_BGP_XMPP_CHANNEL_H_
