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
#include "tbb/atomic.h"
#include "xmpp/xmpp_channel.h"

namespace pugi {
class xml_node;
}

class BgpGlobalSystemConfig;
class BgpRouterState;
class BgpServer;
class BgpXmppRTargetManager;
struct DBRequest;
class IPeer;
class PeerCloseManager;
class XmppServer;
class BgpXmppChannelMock;
class BgpXmppChannelManager;
class BgpXmppChannelManagerMock;
class BgpXmppPeerClose;
class Timer;
class XmppConfigUpdater;
class XmppPeerInfoData;
class XmppSession;

class BgpXmppChannel {
public:
    static const int kEndOfRibSendRetryTimeMsecs = 2000; // 2 Seconds
    enum StatsIndex {
        RX,
        TX,
    };
    struct Stats {
        Stats() {
            rt_updates = 0;
            reach = 0;
            unreach = 0;
            end_of_rib = 0;
        }
        tbb::atomic<uint64_t> rt_updates;
        tbb::atomic<uint64_t> reach;
        tbb::atomic<uint64_t> unreach;
        tbb::atomic<uint64_t> end_of_rib;
    };
    struct ChannelStats {
        ChannelStats() {
            instance_subscribe = 0;
            instance_unsubscribe = 0;
            table_subscribe = 0;
            table_subscribe_complete = 0;
            table_unsubscribe = 0;
            table_unsubscribe_complete = 0;
        }
         tbb::atomic<uint64_t> instance_subscribe;
         tbb::atomic<uint64_t> instance_unsubscribe;
         tbb::atomic<uint64_t> table_subscribe;
         tbb::atomic<uint64_t> table_subscribe_complete;
         tbb::atomic<uint64_t> table_unsubscribe;
         tbb::atomic<uint64_t> table_unsubscribe_complete;
    };

    struct ErrorStats {
        ErrorStats() {
            inet6_rx_bad_xml_token_count = 0;
            inet6_rx_bad_prefix_count = 0;
            inet6_rx_bad_nexthop_count = 0;
            inet6_rx_bad_afi_safi_count = 0;
        }
        void incr_inet6_rx_bad_xml_token_count();
        void incr_inet6_rx_bad_prefix_count();
        void incr_inet6_rx_bad_nexthop_count();
        void incr_inet6_rx_bad_afi_safi_count();
        uint64_t get_inet6_rx_bad_xml_token_count() const;
        uint64_t get_inet6_rx_bad_prefix_count() const;
        uint64_t get_inet6_rx_bad_nexthop_count() const;
        uint64_t get_inet6_rx_bad_afi_safi_count() const;

         tbb::atomic<uint64_t> inet6_rx_bad_xml_token_count;
         tbb::atomic<uint64_t> inet6_rx_bad_prefix_count;
         tbb::atomic<uint64_t> inet6_rx_bad_nexthop_count;
         tbb::atomic<uint64_t> inet6_rx_bad_afi_safi_count;
    };

    explicit BgpXmppChannel(XmppChannel *channel, BgpServer *bgp_server = NULL,
                            BgpXmppChannelManager *manager = NULL);
    virtual ~BgpXmppChannel();

    void Close();
    IPeer *Peer();
    const IPeer *Peer() const;
    virtual TcpSession::Endpoint endpoint() const;

    const std::string &ToString() const;
    const std::string &ToUVEKey() const;
    std::string StateName() const;
    TcpSession::Endpoint remote_endpoint() const;
    TcpSession::Endpoint local_endpoint() const;
    std::string transport_address_string() const;

    void set_peer_closed(bool flag);
    bool peer_deleted() const;
    uint64_t peer_closed_at() const;
    bool table_membership_request_map_empty() const;
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
    XmppChannel *channel() { return channel_; }
    void StartEndOfRibReceiveTimer();
    void ResetEndOfRibSendState();
    bool EndOfRibSendTimerExpired();
    bool MembershipResponseHandler(std::string table_name);
    Timer *eor_send_timer() const { return eor_send_timer_; }
    bool eor_sent() const { return eor_sent_; }
    size_t table_membership_requests() const;
    void ClearEndOfRibState();
    PeerCloseManager *close_manager() { return close_manager_.get(); }

    uint64_t get_rx_route_reach() const { return stats_[RX].reach; }
    uint64_t get_rx_route_unreach() const { return stats_[RX].unreach; }
    uint64_t get_rx_update() const { return stats_[RX].rt_updates; }

    uint64_t get_tx_route_reach() const { return stats_[TX].reach; }
    uint64_t get_tx_route_unreach() const { return stats_[TX].unreach; }
    uint64_t get_tx_update() const { return stats_[TX].rt_updates; }
    bool SkipUpdateSend();
    bool delete_in_progress() const { return delete_in_progress_; }
    void set_delete_in_progress(bool flag) { delete_in_progress_ = flag; }

    BgpXmppRTargetManager *rtarget_manager() {
        return rtarget_manager_.get();
    }
    bool IsSubscriptionGrStale(RoutingInstance *instance) const;
    bool IsSubscriptionLlgrStale(RoutingInstance *instance) const;
    bool IsSubscriptionEmpty() const;
    const RoutingInstance::RouteTargetList &GetSubscribedRTargets(
            RoutingInstance *instance) const;
    void ClearSubscriptions() { routing_instances_.clear(); }
    BgpServer *bgp_server() { return bgp_server_; }
    const BgpXmppChannelManager *manager() const { return manager_; }
    BgpXmppChannelManager *manager() { return manager_; }
    XmppChannel *xmpp_channel() const { return channel_; }
    void ReceiveEndOfRIB(Address::Family family);
    void ProcessPendingSubscriptions();

protected:
    struct InstanceMembershipRequestState {
        InstanceMembershipRequestState(int instance_id = -1,
            bool no_ribout = false)
            : instance_id(instance_id), no_ribout(no_ribout) {
        };

        int instance_id;
        bool no_ribout;
    };

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
    struct TableMembershipRequestState {
        TableMembershipRequestState(RequestType current, int id,
            bool no_ribout = false)
            : current_req(current),
              instance_id(id),
              pending_req(current),
              no_ribout(no_ribout) {
        }
        RequestType current_req;
        int instance_id;
        RequestType pending_req;
        bool no_ribout;
    };

    // Map of routing instances to which this BgpXmppChannel is subscribed.
    struct SubscriptionState {
        enum State {
            NONE = 0,
            GR_STALE = 1 << 0,
            LLGR_STALE = 1 << 1
        };

        SubscriptionState() : index(-1), state(NONE) { }
        SubscriptionState(const RoutingInstance::RouteTargetList &targets,
                          int index)
                : targets(targets), index(index), state(NONE) { }

        bool IsGrStale() const { return((state & GR_STALE) != 0); }
        void SetGrStale() { state |= GR_STALE; }

        void SetLlgrStale() { state |= LLGR_STALE; }
        bool IsLlgrStale() const { return((state & LLGR_STALE) != 0); }

        void ClearStale() { state &= ~(GR_STALE | LLGR_STALE); }

        RoutingInstance::RouteTargetList targets;
        int index;
        uint32_t state;
    };

    typedef std::map<RoutingInstance *, SubscriptionState>
        SubscribedRoutingInstanceList;

    // Map of table name to subscription request state.
    typedef std::map<std::string,
        TableMembershipRequestState> TableMembershipRequestMap;

    // Map of routing-instance name to subscription request state. This map
    // maintains list of requests that are received for subscription before
    // routing instance is created or recreated (in case the instance is in
    // the process of being deleted).
    typedef std::map<std::string,
        InstanceMembershipRequestState> InstanceMembershipRequestMap;

    // The code assumes that multimap preserves insertion order for duplicate
    // values of same key.
    typedef std::pair<const std::string, const std::string> VrfTableName;
    typedef std::multimap<VrfTableName, DBRequest *> DeferQ;

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg);

    virtual bool GetMembershipInfo(BgpTable *table,
        int *instance_id, uint64_t *subscribed_at, RequestType *req_type);
    bool VerifyMembership(const std::string &vrf_name, Address::Family family,
        BgpTable **table, int *instance_id, uint64_t *subscribed_at,
        bool *subscribe_pending, bool add_change);

    void AddTableMembershipState(const std::string &table_name,
        TableMembershipRequestState tmr_state);
    bool DeleteTableMembershipState(const std::string &table_name);
    TableMembershipRequestState *GetTableMembershipState(
        const std::string &table_name);
    const TableMembershipRequestState *GetTableMembershipState(
        const std::string &table_name) const;

    void AddInstanceMembershipState(const std::string &instance,
        InstanceMembershipRequestState imr_state);
    bool DeleteInstanceMembershipState(const std::string &instance);
    virtual const InstanceMembershipRequestState *GetInstanceMembershipState(
        const std::string &instance) const;

    bool ProcessItem(std::string vrf_name, const pugi::xml_node &node,
                     bool add_change);
    bool ProcessInet6Item(std::string vrf_name, const pugi::xml_node &node,
                          bool add_change);
    bool ProcessMcastItem(std::string vrf_name,
                          const pugi::xml_node &item, bool add_change);
    bool ProcessEnetItem(std::string vrf_name,
                         const pugi::xml_node &item, bool add_change);
    void ProcessSubscriptionRequest(std::string rt_instance,
                                    const XmppStanza::XmppMessageIq *iq,
                                    bool add_change);
    void AddSubscriptionState(RoutingInstance *rt_instance, int index);
    void DeleteSubscriptionState(RoutingInstance *rt_instance);
    SubscriptionState *GetSubscriptionState(RoutingInstance *rt_instance);
    const SubscriptionState *GetSubscriptionState(
        RoutingInstance *rt_instance) const;

    void RegisterTable(int line, BgpTable *table,
        const TableMembershipRequestState *tmr_state);
    void UnregisterTable(int line, BgpTable *table);
    void MembershipRequestCallback(BgpTable *table);
    void DequeueRequest(const std::string &table_name, DBRequest *request);
    bool XmppDecodeAddress(int af, const std::string &address,
                           IpAddress *addrp, bool zero_ok = false);
    bool ResumeClose();
    void FlushDeferQ(std::string vrf_name);
    void FlushDeferQ(std::string vrf_name, std::string table_name);
    void ProcessDeferredSubscribeRequest(RoutingInstance *rt_instance,
        const InstanceMembershipRequestState &imr_state);
    void ClearStaledSubscription(RoutingInstance *rt_instance,
                                 SubscriptionState *sub_state);
    bool ProcessMembershipResponse(std::string table_name,
                                   TableMembershipRequestState *tmr_state);
    bool EndOfRibReceiveTimerExpired();
    void EndOfRibTimerErrorHandler(std::string error_name,
                                   std::string error_message);
    void SendEndOfRIB();

    xmps::PeerId peer_id_;
    boost::scoped_ptr<BgpXmppRTargetManager> rtarget_manager_;
    BgpServer *bgp_server_;
    boost::scoped_ptr<XmppPeer> peer_;
    boost::scoped_ptr<BgpXmppPeerClose> peer_close_;
    boost::scoped_ptr<PeerCloseManager> close_manager_;
    boost::scoped_ptr<PeerStats> peer_stats_;
    RibExportPolicy bgp_policy_;

    // DB Requests pending membership request response.
    DeferQ defer_q_;

    TableMembershipRequestMap table_membership_request_map_;
    InstanceMembershipRequestMap instance_membership_request_map_;
    BgpXmppChannelManager *manager_;
    bool delete_in_progress_;
    bool deleted_;
    bool defer_peer_close_;
    bool skip_update_send_;
    bool skip_update_send_cached_;
    bool eor_sent_;
    Timer *eor_receive_timer_;
    Timer *eor_send_timer_;
    uint64_t eor_receive_timer_start_time_;
    uint64_t eor_send_timer_start_time_;
    WorkQueue<std::string> membership_response_worker_;
    SubscribedRoutingInstanceList routing_instances_;

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
    void AdminDownCallback();
    void ASNUpdateCallback(as_t old_asn, as_t old_local_asn);
    void IdentifierUpdateCallback(Ip4Address old_identifier);
    void RoutingInstanceCallback(std::string vrf_name, int op);
    void DSCPUpdateCallback(uint8_t value);

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
    int dscp_listener_id_;
    tbb::atomic<int32_t> deleting_count_;
    // Generation number for subscription tracking
    tbb::atomic<uint64_t> subscription_gen_id_;

    DISALLOW_COPY_AND_ASSIGN(BgpXmppChannelManager);
};

#endif  // SRC_BGP_BGP_XMPP_CHANNEL_H_
