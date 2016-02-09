/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_xmpp_channel.h"

#include <boost/foreach.hpp>

#include <limits>
#include <vector>
#include <sstream>

#include "base/task_annotations.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer_close.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/extended-community/load_balance.h"
#include "bgp/extended-community/mac_mobility.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/rtarget/rtarget_table.h"
#include "bgp/scheduling_group.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "net/community_type.h"
#include "schema/xmpp_multicast_types.h"
#include "schema/xmpp_enet_types.h"
#include "xml/xml_pugi.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/sandesh/xmpp_peer_info_types.h"

using autogen::EnetItemType;
using autogen::EnetNextHopListType;
using autogen::EnetSecurityGroupListType;
using autogen::EnetTunnelEncapsulationListType;

using autogen::McastItemType;
using autogen::McastNextHopsType;
using autogen::McastTunnelEncapsulationListType;

using autogen::ItemType;
using autogen::NextHopListType;
using autogen::SecurityGroupListType;
using autogen::CommunityTagListType;
using autogen::TunnelEncapsulationListType;

using boost::system::error_code;
using pugi::xml_node;
using std::auto_ptr;
using std::make_pair;
using std::numeric_limits;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::vector;

//
// Calculate med from local preference.
// Should move agent definitions to a common location and use those here
// instead of hard coded values.
//
static uint32_t GetMedFromLocalPref(uint32_t local_pref) {
    if (local_pref == 0)
        return 0;
    if (local_pref == 100)
        return 200;
    if (local_pref == 200)
        return 100;
    return numeric_limits<uint32_t>::max() - local_pref;
}

BgpXmppChannel::ErrorStats::ErrorStats()
    : inet6_rx_bad_xml_token_count(0), inet6_rx_bad_prefix_count(0),
      inet6_rx_bad_nexthop_count(0), inet6_rx_bad_afi_safi_count(0) {
}

void BgpXmppChannel::ErrorStats::incr_inet6_rx_bad_xml_token_count() {
    ++inet6_rx_bad_xml_token_count;
}

void BgpXmppChannel::ErrorStats::incr_inet6_rx_bad_prefix_count() {
    ++inet6_rx_bad_prefix_count;
}

void BgpXmppChannel::ErrorStats::incr_inet6_rx_bad_nexthop_count() {
    ++inet6_rx_bad_nexthop_count;
}

void BgpXmppChannel::ErrorStats::incr_inet6_rx_bad_afi_safi_count() {
    ++inet6_rx_bad_afi_safi_count;
}

uint64_t BgpXmppChannel::ErrorStats::get_inet6_rx_bad_xml_token_count() const {
    return inet6_rx_bad_xml_token_count;
}

uint64_t BgpXmppChannel::ErrorStats::get_inet6_rx_bad_prefix_count() const {
    return inet6_rx_bad_prefix_count;
}

uint64_t BgpXmppChannel::ErrorStats::get_inet6_rx_bad_nexthop_count() const {
    return inet6_rx_bad_nexthop_count;
}

uint64_t BgpXmppChannel::ErrorStats::get_inet6_rx_bad_afi_safi_count() const {
    return inet6_rx_bad_afi_safi_count;
}

BgpXmppChannel::Stats::Stats()
    : rt_updates(0), reach(0), unreach(0) {
}

BgpXmppChannel::ChannelStats::ChannelStats()
    : instance_subscribe(0),
      instance_unsubscribe(0),
      table_subscribe(0),
      table_subscribe_complete(0),
      table_unsubscribe(0),
      table_unsubscribe_complete(0) {
}

class BgpXmppChannel::PeerClose : public IPeerClose {
public:
    explicit PeerClose(BgpXmppChannel *channel)
       : parent_(channel),
         manager_(BgpObjectFactory::Create<PeerCloseManager>(channel->Peer())) {
    }
    virtual ~PeerClose() {
    }
    virtual string ToString() const {
        return parent_ ? parent_->ToString() : "";
    }

    virtual PeerCloseManager *close_manager() {
        return manager_.get();
    }

    virtual bool IsCloseGraceful() {
        if (!parent_ || !parent_->channel_) return false;

        XmppConnection *connection =
            const_cast<XmppConnection *>(parent_->channel_->connection());

        if (!connection || connection->IsActiveChannel()) return false;

        // Check from the server, if GR is enabled or not.
        return static_cast<XmppServer *>(
            connection->server())->IsPeerCloseGraceful();
    }

    virtual void CustomClose() {
        if (parent_->rtarget_routes_.empty()) return;
        BgpServer *server = parent_->bgp_server_;
        RoutingInstanceMgr *instance_mgr = server->routing_instance_mgr();
        RoutingInstance *master =
            instance_mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
        assert(master);
        BgpTable *rtarget_table = master->GetTable(Address::RTARGET);
        assert(rtarget_table);

        for (PublishedRTargetRoutes::iterator
             it = parent_->rtarget_routes_.begin();
             it != parent_->rtarget_routes_.end(); it++) {
            parent_->RTargetRouteOp(rtarget_table,
                                    server->local_autonomous_system(),
                                    it->first, NULL, false);
        }
        parent_->routing_instances_.clear();
        parent_->rtarget_routes_.clear();
    }

    virtual bool CloseComplete(bool from_timer, bool gr_cancelled) {
        if (!parent_) return true;

        if (!from_timer) {
            // If graceful restart is enabled, do not delete this peer yet
            // However, if a gr is already aborted, do not trigger another gr
            if (!gr_cancelled && IsCloseGraceful()) {
                return false;
            }
        } else {
            // Close is complete off graceful restart timer. Delete this peer
            // if the session has not come back up
            if (parent_->Peer()->IsReady()) return false;
        }

        XmppConnection *connection =
            const_cast<XmppConnection *>(parent_->channel_->connection());

        // TODO(ananth): This needs to be cleaned up properly by clearly
        // separating GR entry and exit steps. Avoid duplicate channel
        // deletions.
        if (connection && !connection->IsActiveChannel()) {
            parent_->manager_->Enqueue(parent_);
            parent_ = NULL;
        }
        return true;
    }

    void Close() {
        manager_->Close();
    }

private:
    BgpXmppChannel *parent_;
    auto_ptr<PeerCloseManager> manager_;
};

class BgpXmppChannel::PeerStats : public IPeerDebugStats {
public:
    explicit PeerStats(BgpXmppChannel *peer)
        : peer_(peer) {
    }

    // Used when peer flaps.
    // Don't need to do anything since the BgpXmppChannel itself gets deleted.
    virtual void Clear() {
    }

    // Printable name
    virtual string ToString() const {
        return peer_->ToString();
    }

    // Previous State of the peer
    virtual string last_state() const {
        return (peer_->channel_->LastStateName());
    }
    // Last state change occurred at
    virtual string last_state_change_at() const {
        return (peer_->channel_->LastStateChangeAt());
    }

    // Last error on this peer
    virtual string last_error() const {
        return "";
    }

    // Last Event on this peer
    virtual string last_event() const {
        return (peer_->channel_->LastEvent());
    }

    // When was the Last
    virtual string last_flap() const {
        return (peer_->channel_->LastFlap());
    }

    // Total number of flaps
    virtual uint64_t num_flaps() const {
        return (peer_->channel_->FlapCount());
    }

    virtual void GetRxProtoStats(ProtoStats *stats) const {
        stats->open = peer_->channel_->rx_open();
        stats->close = peer_->channel_->rx_close();
        stats->keepalive = peer_->channel_->rx_keepalive();
        stats->update = peer_->channel_->rx_update();
    }

    virtual void GetTxProtoStats(ProtoStats *stats) const {
        stats->open = peer_->channel_->tx_open();
        stats->close = peer_->channel_->tx_close();
        stats->keepalive = peer_->channel_->tx_keepalive();
        stats->update = peer_->channel_->tx_update();
    }

    virtual void GetRxRouteUpdateStats(UpdateStats *stats)  const {
        stats->total = peer_->stats_[RX].rt_updates;
        stats->reach = peer_->stats_[RX].reach;
        stats->unreach = peer_->stats_[RX].unreach;
    }

    virtual void GetTxRouteUpdateStats(UpdateStats *stats)  const {
        stats->total = peer_->stats_[TX].rt_updates;
        stats->reach = peer_->stats_[TX].reach;
        stats->unreach = peer_->stats_[TX].unreach;
    }

    virtual void GetRxSocketStats(IPeerDebugStats::SocketStats *stats) const {
        const XmppSession *session = peer_->GetSession();
        if (session) {
            io::SocketStats socket_stats(session->GetSocketStats());
            stats->calls = socket_stats.read_calls;
            stats->bytes = socket_stats.read_bytes;
        }
    }

    virtual void GetTxSocketStats(IPeerDebugStats::SocketStats *stats) const {
        const XmppSession *session = peer_->GetSession();
        if (session) {
            io::SocketStats socket_stats(session->GetSocketStats());
            stats->calls = socket_stats.write_calls;
            stats->bytes = socket_stats.write_bytes;
            stats->blocked_count = socket_stats.write_blocked;
            stats->blocked_duration_usecs =
                socket_stats.write_blocked_duration_usecs;
        }
    }

    virtual void GetRxErrorStats(RxErrorStats *stats) const {
        const BgpXmppChannel::ErrorStats &err_stats = peer_->error_stats();
        stats->inet6_bad_xml_token_count =
            err_stats.get_inet6_rx_bad_xml_token_count();
        stats->inet6_bad_prefix_count =
            err_stats.get_inet6_rx_bad_prefix_count();
        stats->inet6_bad_nexthop_count =
            err_stats.get_inet6_rx_bad_nexthop_count();
        stats->inet6_bad_afi_safi_count =
            err_stats.get_inet6_rx_bad_afi_safi_count();
    }

    virtual void UpdateTxUnreachRoute(uint64_t count) {
        peer_->stats_[TX].unreach += count;
    }

    virtual void UpdateTxReachRoute(uint64_t count) {
        peer_->stats_[TX].reach += count;
    }

private:
    BgpXmppChannel *peer_;
};


class BgpXmppChannel::XmppPeer : public IPeer {
public:
    XmppPeer(BgpServer *server, BgpXmppChannel *channel)
        : server_(server),
          parent_(channel),
          is_deleted_(false),
          send_ready_(true),
          deleted_at_(0) {
        refcount_ = 0;
        primary_path_count_ = 0;
    }

    virtual ~XmppPeer() {
        assert(GetRefCount() == 0);
    }

    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize);
    virtual string ToString() const {
        return parent_->ToString();
    }

    virtual string ToUVEKey() const {
        return parent_->ToUVEKey();
    }

    virtual BgpServer *server() {
        return server_;
    }
    virtual IPeerClose *peer_close() {
        return parent_->peer_close_.get();
    }

    virtual IPeerDebugStats *peer_stats() {
        return parent_->peer_stats_.get();
    }
    virtual const IPeerDebugStats *peer_stats() const {
        return parent_->peer_stats_.get();
    }

    virtual bool IsReady() const {
        return (parent_->channel_->GetPeerState() == xmps::READY);
    }
    virtual const string GetStateName() const {
        switch (parent_->channel_->GetPeerState()) {
            case xmps::UNKNOWN: return "UNKNOWN";
            case xmps::READY: return "READY";
            case xmps::NOT_READY: return "NOT_READY";
            case xmps::TIMEDOUT: return "TIMEDOUT";
        }
        return "UNKNOWN";
    }
    virtual bool IsXmppPeer() const {
        return true;
    }
    virtual void Close();

    const bool IsDeleted() const { return is_deleted_; }
    void SetDeleted(bool deleted) {
        is_deleted_ = deleted;
        if (is_deleted_)
            deleted_at_ = UTCTimestampUsec();
    }
    uint64_t deleted_at() const { return deleted_at_; }

    virtual BgpProto::BgpPeerType PeerType() const {
        return BgpProto::XMPP;
    }

    virtual uint32_t bgp_identifier() const {
        const TcpSession::Endpoint &remote = parent_->endpoint();
        if (remote.address().is_v4()) {
            return remote.address().to_v4().to_ulong();
        }
        return 0;
    }

    virtual void UpdateRefCount(int count) const { refcount_ += count; }
    virtual tbb::atomic<int> GetRefCount() const { return refcount_; }
    virtual void UpdatePrimaryPathCount(int count) const {
        primary_path_count_ += count;
    }
    virtual int GetPrimaryPathCount() const {
         return primary_path_count_;
    }

private:
    void WriteReadyCb(const boost::system::error_code &ec) {
        if (!server_) return;
        SchedulingGroupManager *sg_mgr = server_->scheduling_group_manager();
        BGP_LOG_PEER(Event, this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_NA, "Send ready");
        sg_mgr->SendReady(this);
        send_ready_ = true;
        XmppPeerInfoData peer_info;
        peer_info.set_name(ToUVEKey());
        peer_info.set_send_state("in sync");
        XMPPPeerInfo::Send(peer_info);
    }

    BgpServer *server_;
    BgpXmppChannel *parent_;
    mutable tbb::atomic<int> refcount_;
    mutable tbb::atomic<int> primary_path_count_;
    bool is_deleted_;
    bool send_ready_;
    uint64_t deleted_at_;
};

static bool SkipUpdateSend() {
    static bool init_;
    static bool skip_;

    if (init_) return skip_;

    skip_ = getenv("XMPP_SKIP_UPDATE_SEND") != NULL;
    init_ = true;

    return skip_;
}

bool BgpXmppChannel::XmppPeer::SendUpdate(const uint8_t *msg, size_t msgsize) {
    XmppChannel *channel = parent_->channel_;
    if (channel->GetPeerState() == xmps::READY) {
        parent_->stats_[TX].rt_updates++;
        if (SkipUpdateSend()) return true;
        send_ready_ = channel->Send(msg, msgsize, xmps::BGP,
                boost::bind(&BgpXmppChannel::XmppPeer::WriteReadyCb, this, _1));
        if (!send_ready_) {
            BGP_LOG_PEER(Event, this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                         BGP_PEER_DIR_NA, "Send blocked");
            XmppPeerInfoData peer_info;
            peer_info.set_name(ToUVEKey());
            peer_info.set_send_state("not in sync");
            XMPPPeerInfo::Send(peer_info);
        }
        return send_ready_;
    } else {
        return false;
    }
}

void BgpXmppChannel::XmppPeer::Close() {
    SetDeleted(true);
    if (server_ == NULL) {
        return;
    }
    parent_->peer_close_->Close();
}

BgpXmppChannel::BgpXmppChannel(XmppChannel *channel, BgpServer *bgp_server,
        BgpXmppChannelManager *manager)
    : channel_(channel),
      peer_id_(xmps::BGP),
      bgp_server_(bgp_server),
      peer_(new XmppPeer(bgp_server, this)),
      peer_close_(new PeerClose(this)),
      peer_stats_(new PeerStats(this)),
      bgp_policy_(peer_->PeerType(), RibExportPolicy::XMPP, 0, -1, 0),
      manager_(manager),
      close_in_progress_(false),
      deleted_(false),
      defer_peer_close_(false),
      membership_response_worker_(
            TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"),
            channel->connection()->GetIndex(),
            boost::bind(&BgpXmppChannel::MembershipResponseHandler, this, _1)),
      lb_mgr_(new LabelBlockManager()) {
    channel_->RegisterReceive(peer_id_,
         boost::bind(&BgpXmppChannel::ReceiveUpdate, this, _1));
    BGP_LOG_PEER(Event, peer_.get(), SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
        BGP_PEER_DIR_NA, "Created");
}

BgpXmppChannel::~BgpXmppChannel() {
    if (channel_->connection() && !channel_->connection()->IsActiveChannel()) {
        CHECK_CONCURRENCY("bgp::Config");
    }

    if (manager_)
        manager_->RemoveChannel(channel_);
    if (manager_ && close_in_progress_)
        manager_->decrement_closing_count();
    STLDeleteElements(&defer_q_);
    assert(peer_->IsDeleted());
    BGP_LOG_PEER(Event, peer_.get(), SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
        BGP_PEER_DIR_NA, "Deleted");
    channel_->UnRegisterReceive(peer_id_);
}

const XmppSession *BgpXmppChannel::GetSession() const {
    if (channel_ && channel_->connection()) {
        return channel_->connection()->session();
    }
    return NULL;
}

string BgpXmppChannel::ToString() const {
    return channel_->ToString();
}

string BgpXmppChannel::ToUVEKey() const {
    if (channel_->connection()) {
        return channel_->connection()->ToUVEKey();
    } else {
        return channel_->ToString();
    }
}

string BgpXmppChannel::StateName() const {
    return channel_->StateName();
}

void BgpXmppChannel::RTargetRouteOp(BgpTable *rtarget_table, as4_t asn,
                                    const RouteTarget &rtarget, BgpAttrPtr attr,
                                    bool add_change) {
    if (add_change && close_in_progress_)
        return;

    DBRequest req;
    RTargetPrefix rt_prefix(asn, rtarget);
    req.key.reset(new RTargetTable::RequestKey(rt_prefix, Peer()));
    if (add_change) {
        req.data.reset(new RTargetTable::RequestData(attr, 0, 0));
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    } else {
        req.oper = DBRequest::DB_ENTRY_DELETE;
    }
    rtarget_table->Enqueue(&req);
}

void BgpXmppChannel::ASNUpdateCallback(as_t old_asn, as_t old_local_asn) {
    if (bgp_server_->local_autonomous_system() == old_local_asn)
        return;
    if (routing_instances_.empty())
        return;

    RoutingInstanceMgr *instance_mgr = bgp_server_->routing_instance_mgr();
    assert(instance_mgr);
    RoutingInstance *master =
        instance_mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
    assert(master);
    BgpTable *rtarget_table = master->GetTable(Address::RTARGET);
    assert(rtarget_table);

    BgpAttrSpec attrs;
    BgpAttrNextHop nexthop(bgp_server_->bgp_identifier());
    attrs.push_back(&nexthop);
    BgpAttrOrigin origin(BgpAttrOrigin::IGP);
    attrs.push_back(&origin);
    BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attrs);

    // Delete the route and add with new local ASN
    for (PublishedRTargetRoutes::iterator it = rtarget_routes_.begin();
         it != rtarget_routes_.end(); it++) {
        RTargetRouteOp(rtarget_table, old_local_asn, it->first, NULL, false);
        RTargetRouteOp(rtarget_table, bgp_server_->local_autonomous_system(),
                       it->first, attr, true);
    }
}

void BgpXmppChannel::IdentifierUpdateCallback(Ip4Address old_identifier) {
    if (routing_instances_.empty())
        return;

    RoutingInstanceMgr *instance_mgr = bgp_server_->routing_instance_mgr();
    assert(instance_mgr);
    RoutingInstance *master =
        instance_mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
    assert(master);
    BgpTable *rtarget_table = master->GetTable(Address::RTARGET);
    assert(rtarget_table);

    BgpAttrSpec attrs;
    BgpAttrNextHop nexthop(bgp_server_->bgp_identifier());
    attrs.push_back(&nexthop);
    BgpAttrOrigin origin(BgpAttrOrigin::IGP);
    attrs.push_back(&origin);
    BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attrs);

    // Update the route with new nexthop
    for (PublishedRTargetRoutes::iterator it = rtarget_routes_.begin();
         it != rtarget_routes_.end(); it++) {
        RTargetRouteOp(rtarget_table, bgp_server_->local_autonomous_system(),
                       it->first, attr, true);
    }
}

void
BgpXmppChannel::AddNewRTargetRoute(BgpTable *rtarget_table,
                                   RoutingInstance *rtinstance,
                                   const RouteTarget &rtarget,
                                   BgpAttrPtr attr) {
    PublishedRTargetRoutes::iterator rt_loc = rtarget_routes_.find(rtarget);
    if (rt_loc == rtarget_routes_.end()) {
        pair<PublishedRTargetRoutes::iterator, bool> ret =
         rtarget_routes_.insert(make_pair(rtarget, RoutingInstanceList()));

        rt_loc = ret.first;
        // Send rtarget route ADD
        RTargetRouteOp(rtarget_table,
                       bgp_server_->local_autonomous_system(),
                       rtarget, attr, true);
    }
    rt_loc->second.insert(rtinstance);
}

void
BgpXmppChannel::DeleteRTargetRoute(BgpTable *rtarget_table,
                                   RoutingInstance *rtinstance,
                                   const RouteTarget &rtarget) {
    PublishedRTargetRoutes::iterator rt_loc = rtarget_routes_.find(rtarget);
    assert(rt_loc != rtarget_routes_.end());
    assert(rt_loc->second.erase(rtinstance));
    if (rt_loc->second.empty()) {
        rtarget_routes_.erase(rtarget);
        // Send rtarget route DELETE
        RTargetRouteOp(rtarget_table,
                       bgp_server_->local_autonomous_system(),
                       rtarget, NULL, false);
    }
}

void BgpXmppChannel::RoutingInstanceCallback(string vrf_name, int op) {
    if (close_in_progress_)
        return;
    if (vrf_name == BgpConfigManager::kMasterInstance)
        return;
    if (op == RoutingInstanceMgr::INSTANCE_DELETE)
        return;

    RoutingInstanceMgr *instance_mgr = bgp_server_->routing_instance_mgr();
    assert(instance_mgr);
    RoutingInstance *rt_instance = instance_mgr->GetRoutingInstance(vrf_name);
    assert(rt_instance);

    if (op == RoutingInstanceMgr::INSTANCE_ADD) {
        VrfMembershipRequestMap::iterator it =
            vrf_membership_request_map_.find(vrf_name);
        if (it == vrf_membership_request_map_.end())
            return;
        ProcessDeferredSubscribeRequest(rt_instance, it->second);
        vrf_membership_request_map_.erase(it);
    } else {
        SubscribedRoutingInstanceList::iterator it =
            routing_instances_.find(rt_instance);
        if (it == routing_instances_.end()) return;

        // Prepare for route add to rtarget table
        // get rtarget_table and locate the attr
        RoutingInstance *master =
            instance_mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
        assert(master);
        BgpTable *rtarget_table = master->GetTable(Address::RTARGET);
        assert(rtarget_table);

        BgpAttrSpec attrs;
        BgpAttrNextHop nexthop(bgp_server_->bgp_identifier());
        attrs.push_back(&nexthop);
        BgpAttrOrigin origin(BgpAttrOrigin::IGP);
        attrs.push_back(&origin);
        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attrs);

        // Import list in the routing instance
        const RoutingInstance::RouteTargetList &new_list =
            rt_instance->GetImportList();

        // Previous route target list for which the rtarget route was added
        RoutingInstance::RouteTargetList &current = it->second.targets;
        RoutingInstance::RouteTargetList::iterator cur_next_it, cur_it;
        cur_it = cur_next_it = current.begin();
        RoutingInstance::RouteTargetList::const_iterator new_it =
            new_list.begin();

        pair<RoutingInstance::RouteTargetList::iterator, bool> r;
        while (cur_it != current.end() && new_it != new_list.end()) {
            if (*new_it < *cur_it) {
                r = current.insert(*new_it);
                assert(r.second);
                AddNewRTargetRoute(rtarget_table, it->first, *new_it, attr);
                new_it++;
            } else if (*new_it > *cur_it) {
                cur_next_it++;
                DeleteRTargetRoute(rtarget_table, it->first, *cur_it);
                current.erase(cur_it);
                cur_it = cur_next_it;
            } else {
                // Update
                cur_it++;
                new_it++;
            }
            cur_next_it = cur_it;
        }
        for (; new_it != new_list.end(); ++new_it) {
            r = current.insert(*new_it);
            assert(r.second);
            AddNewRTargetRoute(rtarget_table, it->first, *new_it, attr);
        }
        for (cur_next_it = cur_it;
             cur_it != current.end();
             cur_it = cur_next_it) {
            cur_next_it++;
            DeleteRTargetRoute(rtarget_table, it->first, *cur_it);
            current.erase(cur_it);
        }
    }
}

IPeer *BgpXmppChannel::Peer() {
    return peer_.get();
}

const IPeer *BgpXmppChannel::Peer() const {
    return peer_.get();
}

TcpSession::Endpoint BgpXmppChannel::endpoint() const {
    return channel_->connection()->endpoint();
}

bool BgpXmppChannel::XmppDecodeAddress(int af, const string &address,
                                       IpAddress *addrp, bool zero_ok) {
    switch (af) {
    case BgpAf::IPv4:
        break;
    default:
        return false;
    }

    error_code error;
    *addrp = IpAddress::from_string(address, error);
    if (error) {
        return false;
    }
    if (zero_ok) {
        return true;
    } else {
        return (addrp->to_v4().to_ulong() != 0);
    }
}

//
// Return true if there's a pending request, false otherwise.
//
bool BgpXmppChannel::GetMembershipInfo(BgpTable *table,
    int *instance_id, RequestType *req_type) {
    *instance_id = -1;
    RoutingTableMembershipRequestMap::iterator loc =
        routingtable_membership_request_map_.find(table->name());
    if (loc != routingtable_membership_request_map_.end()) {
        *req_type = loc->second.pending_req;
        *instance_id = loc->second.instance_id;
        return true;
    } else {
        *req_type = NONE;
        PeerRibMembershipManager *mgr = bgp_server_->membership_mgr();
        *instance_id = mgr->GetRegistrationId(peer_.get(), table);
        return false;
    }
}

//
// Return true if there's a pending request, false otherwise.
//
bool BgpXmppChannel::GetMembershipInfo(const string &vrf_name,
    int *instance_id) {
    *instance_id = -1;
    VrfMembershipRequestMap::iterator loc =
        vrf_membership_request_map_.find(vrf_name);
    if (loc != vrf_membership_request_map_.end()) {
        *instance_id = loc->second;
        return true;
    } else {
        return false;
    }
}

//
// Verify that there's a subscribe or pending subscribe for the table
// corresponding to the vrf and family.
// If there's a subscribe, populate the table and instance_id.
// If there's a pending subscribe, populate the instance_id.
// The subscribe_pending parameter is set appropriately.
//
// Return true if there's a subscribe or pending subscribe, false otherwise.
//
bool BgpXmppChannel::VerifyMembership(const string &vrf_name,
    Address::Family family, BgpTable **table,
    int *instance_id, bool *subscribe_pending) {
    *table = NULL;
    *subscribe_pending = false;

    RoutingInstanceMgr *instance_mgr = bgp_server_->routing_instance_mgr();
    RoutingInstance *rt_instance = instance_mgr->GetRoutingInstance(vrf_name);
    if (rt_instance != NULL && !rt_instance->deleted()) {
        *table = rt_instance->GetTable(family);
        RequestType req_type;
        if (GetMembershipInfo(*table, instance_id, &req_type)) {
            // Bail if there's a pending unsubscribe.
            if (req_type != SUBSCRIBE) {
                BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                    SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                    "Received route after unsubscribe");
                return false;
            }
            *subscribe_pending = true;
        } else {
            // Bail if we are not subscribed to the table.
            if (*instance_id < 0) {
                BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                    SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                    "Received route without subscribe");
                return false;
            }
        }
    } else {
        // Bail if there's no pending subscribe.
        if (GetMembershipInfo(vrf_name, instance_id)) {
            *subscribe_pending = true;
        } else {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
               SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
               "Received route without pending subscribe");
            return false;
        }
    }

    return true;
}

bool BgpXmppChannel::ProcessMcastItem(string vrf_name,
    const pugi::xml_node &node, bool add_change) {
    McastItemType item;
    item.Clear();

    if (!item.XmlParse(node)) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Invalid multicast route message received");
        return false;
    }

    if (item.entry.nlri.af != BgpAf::IPv4) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Unsupported address family " << item.entry.nlri.af <<
            " for multicast route");
        return false;
    }

    if (item.entry.nlri.safi != BgpAf::Mcast) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
            BGP_LOG_FLAG_ALL, "Unsupported subsequent address family " <<
            item.entry.nlri.safi << " for multicast route");
        return false;
    }

    error_code error;
    IpAddress grp_address = IpAddress::from_string("0.0.0.0", error);
    if (!item.entry.nlri.group.empty()) {
        if (!XmppDecodeAddress(item.entry.nlri.af,
            item.entry.nlri.group, &grp_address, false)) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Bad group address " << item.entry.nlri.group);
            return false;
        }
    }

    IpAddress src_address = IpAddress::from_string("0.0.0.0", error);
    if (!item.entry.nlri.source.empty()) {
        if (!XmppDecodeAddress(item.entry.nlri.af,
            item.entry.nlri.source, &src_address, true)) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Bad source address " << item.entry.nlri.source);
            return false;
        }
    }

    bool subscribe_pending;
    int instance_id;
    BgpTable *table;
    if (!VerifyMembership(vrf_name, Address::ERMVPN, &table, &instance_id,
        &subscribe_pending)) {
        channel_->Close();
        return false;
    }

    // Build the key to the Multicast DBTable
    RouteDistinguisher mc_rd(peer_->bgp_identifier(), instance_id);
    ErmVpnPrefix mc_prefix(ErmVpnPrefix::NativeRoute, mc_rd,
        grp_address.to_v4(), src_address.to_v4());

    // Build and enqueue a DB request for route-addition
    DBRequest req;
    req.key.reset(new ErmVpnTable::RequestKey(mc_prefix, peer_.get()));

    uint32_t flags = 0;
    ExtCommunitySpec ext;
    string label_range("none");

    if (add_change) {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        vector<uint32_t> labels;
        const McastNextHopsType &inh_list = item.entry.next_hops;

        if (inh_list.next_hop.empty()) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Missing next-hop for multicast route " <<
                mc_prefix.ToString());
            return false;
        }

        // Agents should send only one next-hop in the item
        if (inh_list.next_hop.size() != 1) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "More than one nexthop received for multicast route " <<
                mc_prefix.ToString());
            return false;
        }

        McastNextHopsType::const_iterator nit = inh_list.begin();

        // Label Allocation item.entry.label by parsing the range
        label_range = nit->label;
        if (!stringToIntegerList(label_range, "-", labels) ||
            labels.size() != 2) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Bad label range " << label_range <<
                " for multicast route " << mc_prefix.ToString());
            return false;
        }

        BgpAttrSpec attrs;
        LabelBlockPtr lbptr = lb_mgr_->LocateBlock(labels[0], labels[1]);

        BgpAttrLabelBlock attr_label(lbptr);
        attrs.push_back(&attr_label);

        // Next-hop ip address
        IpAddress nh_address;
        if (!XmppDecodeAddress(nit->af, nit->address, &nh_address)) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Bad nexthop address " << nit->address <<
                " for multicast route " << mc_prefix.ToString());
            return false;
        }
        BgpAttrNextHop nexthop(nh_address.to_v4().to_ulong());
        attrs.push_back(&nexthop);

        // Process tunnel encapsulation list.
        bool no_tunnel_encap = true;
        bool no_valid_tunnel_encap = true;
        for (McastTunnelEncapsulationListType::const_iterator eit =
             nit->tunnel_encapsulation_list.begin();
             eit != nit->tunnel_encapsulation_list.end(); ++eit) {
            no_tunnel_encap = false;
            TunnelEncap tun_encap(*eit);
            if (tun_encap.tunnel_encap() == TunnelEncapType::UNSPEC)
                continue;
            no_valid_tunnel_encap = false;
            ext.communities.push_back(tun_encap.GetExtCommunityValue());
        }

        // Mark the path as infeasible if all tunnel encaps published
        // by agent are invalid.
        if (!no_tunnel_encap && no_valid_tunnel_encap) {
            flags = BgpPath::NoTunnelEncap;
        }

        if (!ext.communities.empty())
            attrs.push_back(&ext);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attrs);
        req.data.reset(new ErmVpnTable::RequestData(attr, flags, 0));
        stats_[RX].reach++;
    } else {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        stats_[RX].unreach++;
    }

    // Defer all requests till subscribe is processed.
    if (subscribe_pending) {
        DBRequest *request_entry = new DBRequest();
        request_entry->Swap(&req);
        string table_name =
            RoutingInstance::GetTableName(vrf_name, Address::ERMVPN);
        defer_q_.insert(make_pair(
            make_pair(vrf_name, table_name), request_entry));
        return true;
    }

    assert(table);
    BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
        "Multicast group " << item.entry.nlri.group <<
        " source " << item.entry.nlri.source <<
        " and label range " << label_range <<
        " enqueued for " << (add_change ? "add/change" : "delete"));
    table->Enqueue(&req);
    return true;
}

bool BgpXmppChannel::ProcessItem(string vrf_name,
    const pugi::xml_node &node, bool add_change) {
    ItemType item;
    item.Clear();

    if (!item.XmlParse(node)) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Invalid inet route message received");
        return false;
    }

    if (item.entry.nlri.af != BgpAf::IPv4) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Unsupported address family " << item.entry.nlri.af <<
            " for inet route " << item.entry.nlri.address);
        return false;
    }

    error_code error;
    Ip4Prefix rt_prefix = Ip4Prefix::FromString(item.entry.nlri.address,
                                                &error);
    if (error) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Bad inet route " << item.entry.nlri.address);
        return false;
    }

    bool subscribe_pending;
    int instance_id;
    BgpTable *table;
    if (!VerifyMembership(vrf_name, Address::INET, &table, &instance_id,
        &subscribe_pending)) {
        channel_->Close();
        return false;
    }

    InetTable::RequestData::NextHops nexthops;
    DBRequest req;
    req.key.reset(new InetTable::RequestKey(rt_prefix, peer_.get()));

    IpAddress nh_address(Ip4Address(0));
    uint32_t label = 0;
    uint32_t flags = 0;
    ExtCommunitySpec ext;
    CommunitySpec comm;

    if (add_change) {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        BgpAttrSpec attrs;
        const NextHopListType &inh_list = item.entry.next_hops;

        if (inh_list.next_hop.empty()) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Missing next-hops for inet route " << rt_prefix.ToString());
            return false;
        }

        bool first_nh = true;
        for (NextHopListType::const_iterator nit = inh_list.begin();
             nit != inh_list.end(); ++nit, first_nh = false) {
            InetTable::RequestData::NextHop nexthop;

            IpAddress nhop_address(Ip4Address(0));
            if (!XmppDecodeAddress(nit->af, nit->address, &nhop_address)) {
                BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                    SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                    "Bad nexthop address " << nit->address <<
                    " for inet route " << rt_prefix.ToString());
                return false;
            }

            if (first_nh) {
                nh_address = nhop_address;
                label = nit->label;
            }

            // Process tunnel encapsulation list.
            bool no_tunnel_encap = true;
            bool no_valid_tunnel_encap = true;
            for (TunnelEncapsulationListType::const_iterator eit =
                 nit->tunnel_encapsulation_list.begin();
                 eit != nit->tunnel_encapsulation_list.end(); ++eit) {
                no_tunnel_encap = false;
                TunnelEncap tun_encap(*eit);
                if (tun_encap.tunnel_encap() == TunnelEncapType::UNSPEC)
                    continue;
                no_valid_tunnel_encap = false;
                if (first_nh) {
                    ext.communities.push_back(
                        tun_encap.GetExtCommunityValue());
                }
                nexthop.tunnel_encapsulations_.push_back(
                    tun_encap.GetExtCommunity());
            }

            // Mark the path as infeasible if all tunnel encaps published
            // by agent are invalid.
            if (!no_tunnel_encap && no_valid_tunnel_encap) {
                flags = BgpPath::NoTunnelEncap;
            }
            nexthop.flags_ = flags;
            nexthop.address_ = nhop_address;
            nexthop.label_ = nit->label;
            nexthop.source_rd_ = RouteDistinguisher(
                nhop_address.to_v4().to_ulong(), instance_id);
            nexthops.push_back(nexthop);
        }

        BgpAttrLocalPref local_pref(item.entry.local_preference);
        if (local_pref.local_pref != 0)
            attrs.push_back(&local_pref);

        // If there's no explicit med, calculate it automatically from the
        // local pref.
        uint32_t med_value = item.entry.med;
        if (!med_value)
            med_value = GetMedFromLocalPref(local_pref.local_pref);
        BgpAttrMultiExitDisc med(med_value);
        if (med.med != 0)
            attrs.push_back(&med);

        // Process community tags
        const CommunityTagListType &ict_list = item.entry.community_tag_list;
        for (CommunityTagListType::const_iterator cit = ict_list.begin();
             cit != ict_list.end(); ++cit) {
            error_code error;
            uint32_t rt_community =
                CommunityType::CommunityFromString(*cit, &error);
            if (error)
                continue;
            comm.communities.push_back(rt_community);
        }

        BgpAttrNextHop nexthop(nh_address.to_v4().to_ulong());
        attrs.push_back(&nexthop);

        BgpAttrSourceRd source_rd(
            RouteDistinguisher(nh_address.to_v4().to_ulong(), instance_id));
        attrs.push_back(&source_rd);

        // Process security group list.
        const SecurityGroupListType &isg_list = item.entry.security_group_list;
        for (SecurityGroupListType::const_iterator sit = isg_list.begin();
             sit != isg_list.end(); ++sit) {
            SecurityGroup sg(bgp_server_->autonomous_system(), *sit);
            ext.communities.push_back(sg.GetExtCommunityValue());
        }

        if (item.entry.sequence_number) {
            MacMobility mm(item.entry.sequence_number);
            ext.communities.push_back(mm.GetExtCommunityValue());
        }

        // Process load-balance extended community
        LoadBalance load_balance(item.entry.load_balance);
        if (!load_balance.IsDefault())
            ext.communities.push_back(load_balance.GetExtCommunityValue());

        if (!comm.communities.empty())
            attrs.push_back(&comm);

        if (!ext.communities.empty())
            attrs.push_back(&ext);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attrs);

        req.data.reset(new InetTable::RequestData(attr, nexthops));
        stats_[RX].reach++;
    } else {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        stats_[RX].unreach++;
    }

    // Defer all requests till subscribe is processed.
    if (subscribe_pending) {
        DBRequest *request_entry = new DBRequest();
        request_entry->Swap(&req);
        string table_name =
            RoutingInstance::GetTableName(vrf_name, Address::INET);
        defer_q_.insert(make_pair(
            make_pair(vrf_name, table_name), request_entry));
        return true;
    }

    assert(table);
    BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
        "Inet route " << item.entry.nlri.address <<
        " with next-hop " << nh_address << " and label " << label <<
        " enqueued for " << (add_change ? "add/change" : "delete"));
    table->Enqueue(&req);
    return true;
}

bool BgpXmppChannel::ProcessInet6Item(string vrf_name,
    const pugi::xml_node &node, bool add_change) {
    ItemType item;
    item.Clear();

    if (!item.XmlParse(node)) {
        error_stats().incr_inet6_rx_bad_xml_token_count();
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Invalid inet6 route message received");
        return false;
    }

    if (item.entry.nlri.af != BgpAf::IPv6) {
        error_stats().incr_inet6_rx_bad_afi_safi_count();
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Unsupported address family " << item.entry.nlri.af <<
            " for inet6 route " << item.entry.nlri.address);
        return false;
    }

    if (item.entry.nlri.safi != BgpAf::Unicast) {
        error_stats().incr_inet6_rx_bad_afi_safi_count();
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Unsupported subsequent address family " << item.entry.nlri.safi <<
            " for inet6 route " << item.entry.nlri.address);
        return false;
    }

    error_code error;
    Inet6Prefix rt_prefix =
        Inet6Prefix::FromString(item.entry.nlri.address, &error);
    if (error) {
        error_stats().incr_inet6_rx_bad_prefix_count();
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Bad inet6 route " << item.entry.nlri.address);
        return false;
    }

    bool subscribe_pending;
    int instance_id;
    BgpTable *table;
    if (!VerifyMembership(vrf_name, Address::INET6, &table, &instance_id,
        &subscribe_pending)) {
        channel_->Close();
        return false;
    }

    Inet6Table::RequestData::NextHops nexthops;
    DBRequest req;
    req.key.reset(new Inet6Table::RequestKey(rt_prefix, peer_.get()));

    IpAddress nh_address(Ip4Address(0));
    uint32_t label = 0;
    uint32_t flags = 0;
    ExtCommunitySpec ext;
    CommunitySpec comm;

    if (add_change) {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        BgpAttrSpec attrs;
        const NextHopListType &inh_list = item.entry.next_hops;

        if (inh_list.next_hop.empty()) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Missing next-hops for inet6 route " << rt_prefix.ToString());
            return false;
        }

        bool first_nh = true;
        for (NextHopListType::const_iterator nit = inh_list.begin();
             nit != inh_list.end(); ++nit, first_nh = false) {
            Inet6Table::RequestData::NextHop nexthop;

            IpAddress nhop_address(Ip4Address(0));
            if (!XmppDecodeAddress(nit->af, nit->address, &nhop_address)) {
                error_stats().incr_inet6_rx_bad_nexthop_count();
                BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                    SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                    "Bad nexthop address " << nit->address <<
                    " for inet6 route " << rt_prefix.ToString());
                return false;
            }

            if (first_nh) {
                nh_address = nhop_address;
                label = nit->label;
            }

            // Process tunnel encapsulation list.
            bool no_tunnel_encap = true;
            bool no_valid_tunnel_encap = true;
            for (TunnelEncapsulationListType::const_iterator eit =
                 nit->tunnel_encapsulation_list.begin();
                 eit != nit->tunnel_encapsulation_list.end(); ++eit) {
                no_tunnel_encap = false;
                TunnelEncap tun_encap(*eit);
                if (tun_encap.tunnel_encap() == TunnelEncapType::UNSPEC)
                    continue;
                no_valid_tunnel_encap = false;
                if (first_nh) {
                    ext.communities.push_back(
                        tun_encap.GetExtCommunityValue());
                }
                nexthop.tunnel_encapsulations_.push_back(
                    tun_encap.GetExtCommunity());
            }

            // Mark the path as infeasible if all tunnel encaps published
            // by agent are invalid.
            if (!no_tunnel_encap && no_valid_tunnel_encap) {
                flags = BgpPath::NoTunnelEncap;
            }

            nexthop.flags_ = flags;
            nexthop.address_ = nhop_address;
            nexthop.label_ = nit->label;
            nexthop.source_rd_ = RouteDistinguisher(
                nhop_address.to_v4().to_ulong(), instance_id);
            nexthops.push_back(nexthop);
        }

        BgpAttrLocalPref local_pref(item.entry.local_preference);
        if (local_pref.local_pref != 0) {
            attrs.push_back(&local_pref);
        }

        // If there's no explicit med, calculate it automatically from the
        // local pref.
        uint32_t med_value = item.entry.med;
        if (!med_value)
            med_value = GetMedFromLocalPref(local_pref.local_pref);
        BgpAttrMultiExitDisc med(med_value);
        if (med.med != 0)
            attrs.push_back(&med);

        // Process community tags
        const CommunityTagListType &ict_list = item.entry.community_tag_list;
        for (CommunityTagListType::const_iterator cit = ict_list.begin();
             cit != ict_list.end(); ++cit) {
            error_code error;
            uint32_t rt_community =
                CommunityType::CommunityFromString(*cit, &error);
            if (error)
                continue;
            comm.communities.push_back(rt_community);
        }

        BgpAttrNextHop nexthop(nh_address.to_v4().to_ulong());
        attrs.push_back(&nexthop);

        BgpAttrSourceRd source_rd(
            RouteDistinguisher(nh_address.to_v4().to_ulong(), instance_id));
        attrs.push_back(&source_rd);

        // Process security group list.
        const SecurityGroupListType &isg_list = item.entry.security_group_list;
        for (SecurityGroupListType::const_iterator sit = isg_list.begin();
             sit != isg_list.end(); ++sit) {
            SecurityGroup sg(bgp_server_->autonomous_system(), *sit);
            ext.communities.push_back(sg.GetExtCommunityValue());
        }

        if (item.entry.sequence_number) {
            MacMobility mm(item.entry.sequence_number);
            ext.communities.push_back(mm.GetExtCommunityValue());
        }

        // Process load-balance extended community
        LoadBalance load_balance(item.entry.load_balance);
        if (!load_balance.IsDefault())
            ext.communities.push_back(load_balance.GetExtCommunityValue());

        if (!comm.communities.empty())
            attrs.push_back(&comm);

        if (!ext.communities.empty()) {
            attrs.push_back(&ext);
        }

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attrs);

        req.data.reset(new Inet6Table::RequestData(attr, nexthops));
        stats_[RX].reach++;
    } else {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        stats_[RX].unreach++;
    }

    // Defer all requests till subscribe is processed.
    if (subscribe_pending) {
        DBRequest *request_entry = new DBRequest();
        request_entry->Swap(&req);
        string table_name =
            RoutingInstance::GetTableName(vrf_name, Address::INET6);
        defer_q_.insert(make_pair(
            make_pair(vrf_name, table_name), request_entry));
        return true;
    }

    assert(table);
    BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
        "Inet6 route " << item.entry.nlri.address <<
        " with next-hop " << nh_address << " and label " << label <<
        " enqueued for " << (add_change ? "add/change" : "delete"));
    table->Enqueue(&req);
    return true;
}

bool BgpXmppChannel::ProcessEnetItem(string vrf_name,
    const pugi::xml_node &node, bool add_change) {
    EnetItemType item;
    item.Clear();

    if (!item.XmlParse(node)) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Invalid enet route message received");
        return false;
    }

    if (item.entry.nlri.af != BgpAf::L2Vpn) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Unsupported address family " << item.entry.nlri.af <<
            " for enet route " << item.entry.nlri.address);
        return false;
    }

    if (item.entry.nlri.safi != BgpAf::Enet) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Unsupported subsequent address family " << item.entry.nlri.safi <<
            " for enet route " << item.entry.nlri.mac);
        return false;
    }

    error_code error;
    MacAddress mac_addr = MacAddress::FromString(item.entry.nlri.mac, &error);

    if (error) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
            "Bad mac address " << item.entry.nlri.mac);
        return false;
    }

    Ip4Prefix inet_prefix;
    Inet6Prefix inet6_prefix;
    IpAddress ip_addr;
    if (!item.entry.nlri.address.empty()) {
        size_t pos = item.entry.nlri.address.find('/');
        if (pos == string::npos) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Missing / in address " << item.entry.nlri.address);
            return false;
        }

        string plen_str = item.entry.nlri.address.substr(pos + 1);
        if (plen_str == "32") {
            inet_prefix =
                Ip4Prefix::FromString(item.entry.nlri.address, &error);
            if (error || inet_prefix.prefixlen() != 32) {
                BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                    SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                    "Bad inet address " << item.entry.nlri.address);
                return false;
            }
            ip_addr = inet_prefix.ip4_addr();
        } else if (plen_str == "128") {
            inet6_prefix =
                Inet6Prefix::FromString(item.entry.nlri.address, &error);
            if (error || inet6_prefix.prefixlen() != 128) {
                BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                    SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                    "Bad inet6 address " << item.entry.nlri.address);
                return false;
            }
            ip_addr = inet6_prefix.ip6_addr();
        } else if (item.entry.nlri.address != "0.0.0.0/0") {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Bad prefix length in address " <<
                item.entry.nlri.address);
            return false;
        }
    }

    bool subscribe_pending;
    int instance_id;
    BgpTable *table;
    if (!VerifyMembership(vrf_name, Address::EVPN, &table, &instance_id,
        &subscribe_pending)) {
        channel_->Close();
        return false;
    }

    RouteDistinguisher rd;
    if (mac_addr.IsBroadcast()) {
        rd = RouteDistinguisher(peer_->bgp_identifier(), instance_id);
    } else {
        rd = RouteDistinguisher::kZeroRd;
    }

    uint32_t ethernet_tag = item.entry.nlri.ethernet_tag;
    EvpnPrefix evpn_prefix(rd, ethernet_tag, mac_addr, ip_addr);

    EvpnTable::RequestData::NextHops nexthops;
    DBRequest req;
    ExtCommunitySpec ext;
    req.key.reset(new EvpnTable::RequestKey(evpn_prefix, peer_.get()));

    IpAddress nh_address(Ip4Address(0));
    uint32_t label = 0;
    uint32_t flags = 0;
    bool label_is_vni = false;

    if (add_change) {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        BgpAttrSpec attrs;
        const EnetNextHopListType &inh_list = item.entry.next_hops;

        if (inh_list.next_hop.empty()) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Missing next-hops for enet route " <<
                evpn_prefix.ToXmppIdString());
            return false;
        }

        bool first_nh = true;
        for (EnetNextHopListType::const_iterator nit = inh_list.begin();
             nit != inh_list.end(); ++nit, first_nh = false) {
            EvpnTable::RequestData::NextHop nexthop;
            IpAddress nhop_address(Ip4Address(0));

            if (!XmppDecodeAddress(nit->af, nit->address, &nhop_address)) {
                BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                    SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                    "Bad nexthop address " << nit->address <<
                    " for enet route " << evpn_prefix.ToXmppIdString());
                return false;
            }
            if (first_nh) {
                nh_address = nhop_address;
                label = nit->label;
            }

            // Process tunnel encapsulation list.
            bool no_tunnel_encap = true;
            bool no_valid_tunnel_encap = true;
            for (EnetTunnelEncapsulationListType::const_iterator eit =
                 nit->tunnel_encapsulation_list.begin();
                 eit != nit->tunnel_encapsulation_list.end(); ++eit) {
                no_tunnel_encap = false;
                TunnelEncap tun_encap(*eit);
                if (tun_encap.tunnel_encap() == TunnelEncapType::UNSPEC)
                    continue;
                no_valid_tunnel_encap = false;
                if (tun_encap.tunnel_encap() == TunnelEncapType::VXLAN)
                    label_is_vni = true;
                if (first_nh) {
                    ext.communities.push_back(
                        tun_encap.GetExtCommunityValue());
                }
                nexthop.tunnel_encapsulations_.push_back(
                    tun_encap.GetExtCommunity());
            }

            // Mark the path as infeasible if all tunnel encaps published
            // by agent are invalid.
            if (!no_tunnel_encap && no_valid_tunnel_encap) {
                flags = BgpPath::NoTunnelEncap;
            }

            nexthop.flags_ = flags;
            nexthop.address_ = nhop_address;
            nexthop.label_ = nit->label;
            nexthop.source_rd_ = RouteDistinguisher(
                nhop_address.to_v4().to_ulong(), instance_id);
            nexthops.push_back(nexthop);
        }

        BgpAttrLocalPref local_pref(item.entry.local_preference);
        if (local_pref.local_pref != 0) {
            attrs.push_back(&local_pref);
        }

        // If there's no explicit med, calculate it automatically from the
        // local pref.
        uint32_t med_value = item.entry.med;
        if (!med_value)
            med_value = GetMedFromLocalPref(local_pref.local_pref);
        BgpAttrMultiExitDisc med(med_value);
        if (med.med != 0)
            attrs.push_back(&med);

        BgpAttrNextHop nexthop(nh_address.to_v4().to_ulong());
        attrs.push_back(&nexthop);

        BgpAttrSourceRd source_rd(
            RouteDistinguisher(nh_address.to_v4().to_ulong(), instance_id));
        attrs.push_back(&source_rd);

        // Process security group list.
        const EnetSecurityGroupListType &isg_list =
            item.entry.security_group_list;
        for (EnetSecurityGroupListType::const_iterator sit = isg_list.begin();
             sit != isg_list.end(); ++sit) {
            SecurityGroup sg(bgp_server_->autonomous_system(), *sit);
            ext.communities.push_back(sg.GetExtCommunityValue());
        }

        if (item.entry.sequence_number) {
            MacMobility mm(item.entry.sequence_number);
            ext.communities.push_back(mm.GetExtCommunityValue());
        }

        if (!ext.communities.empty())
            attrs.push_back(&ext);

        PmsiTunnelSpec pmsi_spec;
        if (mac_addr.IsBroadcast()) {
            if (!item.entry.replicator_address.empty()) {
                IpAddress replicator_address;
                if (!XmppDecodeAddress(BgpAf::IPv4,
                    item.entry.replicator_address, &replicator_address)) {
                    BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                        SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                        "Bad replicator address " <<
                        item.entry.replicator_address <<
                        " for enet route " << evpn_prefix.ToXmppIdString());
                    return false;
                }
                pmsi_spec.tunnel_type =
                    PmsiTunnelSpec::AssistedReplicationContrail;
                pmsi_spec.tunnel_flags = PmsiTunnelSpec::ARLeaf;
                pmsi_spec.SetIdentifier(replicator_address.to_v4());
            } else {
                pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
                if (item.entry.assisted_replication_supported) {
                    pmsi_spec.tunnel_flags |= PmsiTunnelSpec::ARReplicator;
                    pmsi_spec.tunnel_flags |= PmsiTunnelSpec::LeafInfoRequired;
                }
                if (!item.entry.edge_replication_not_supported) {
                    pmsi_spec.tunnel_flags |=
                        PmsiTunnelSpec::EdgeReplicationSupported;
                }
                pmsi_spec.SetIdentifier(nh_address.to_v4());
            }
            pmsi_spec.SetLabel(label, label_is_vni);
            attrs.push_back(&pmsi_spec);
        }

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attrs);

        req.data.reset(new EvpnTable::RequestData(attr, nexthops));
        stats_[0].reach++;
    } else {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        stats_[RX].unreach++;
    }

    // Defer all requests till subscribe is processed.
    if (subscribe_pending) {
        DBRequest *request_entry = new DBRequest();
        request_entry->Swap(&req);
        string table_name =
            RoutingInstance::GetTableName(vrf_name, Address::EVPN);
        defer_q_.insert(make_pair(
            make_pair(vrf_name, table_name), request_entry));
        return true;
    }

    assert(table);
    BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
        "Enet route " << evpn_prefix.ToXmppIdString() <<
        " with next-hop " << nh_address << " and label " << label <<
        " enqueued for " << (add_change ? "add/change" : "delete"));
    table->Enqueue(&req);
    return true;
}

void BgpXmppChannel::DequeueRequest(const string &table_name,
                                    DBRequest *request) {
    auto_ptr<DBRequest> ptr(request);

    BgpTable *table = static_cast<BgpTable *>
        (bgp_server_->database()->FindTable(table_name));
    if (table == NULL || table->IsDeleted()) {
        return;
    }

    PeerRibMembershipManager *mgr = bgp_server_->membership_mgr();
    if (mgr && !mgr->PeerRegistered(peer_.get(), table)) {
        BGP_LOG_PEER(Event, Peer(),
            SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
            "Not subscribed to table " << table->name());
        return;
    }

    table->Enqueue(ptr.get());
}

bool BgpXmppChannel::ResumeClose() {
    peer_->Close();
    return true;
}

void BgpXmppChannel::RegisterTable(BgpTable *table, int instance_id) {
    PeerRibMembershipManager *mgr = bgp_server_->membership_mgr();
    BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG,
                 BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                 "Subscribe to table " << table->name() <<
                 " with id " << instance_id);
    mgr->Register(peer_.get(), table, bgp_policy_, instance_id,
        boost::bind(&BgpXmppChannel::MembershipRequestCallback, this, _1, _2));
    channel_stats_.table_subscribe++;
}

void BgpXmppChannel::UnregisterTable(BgpTable *table) {
    PeerRibMembershipManager *mgr = bgp_server_->membership_mgr();
    BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG,
                 BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                 "Unsubscribe to table " << table->name());
    mgr->Unregister(peer_.get(), table,
        boost::bind(&BgpXmppChannel::MembershipRequestCallback, this, _1, _2));
    channel_stats_.table_unsubscribe++;
}

bool BgpXmppChannel::MembershipResponseHandler(string table_name) {
    RoutingTableMembershipRequestMap::iterator loc =
        routingtable_membership_request_map_.find(table_name);
    if (loc == routingtable_membership_request_map_.end()) {
        BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                     BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                     "Table " << table_name <<
                     " not in subscribe/unsubscribe request queue");
        assert(false);
    }

    MembershipRequestState state = loc->second;
    if (state.current_req == SUBSCRIBE) {
        BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG,
                     BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                     "Subscribe to table " << table_name << " completed");
        channel_stats_.table_subscribe_complete++;
    } else {
        BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG,
                     BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                     "Unsubscribe to table " << table_name << " completed");
        channel_stats_.table_unsubscribe_complete++;
    }

    if (defer_peer_close_) {
        routingtable_membership_request_map_.erase(loc);
        if (routingtable_membership_request_map_.size()) {
            return true;
        }
        defer_peer_close_ = false;
        ResumeClose();
        return true;
    }

    BgpTable *table = static_cast<BgpTable *>
        (bgp_server_->database()->FindTable(table_name));
    if (!table) {
        routingtable_membership_request_map_.erase(loc);
        return true;
    }

    PeerRibMembershipManager *mgr = bgp_server_->membership_mgr();
    if ((state.current_req == UNSUBSCRIBE) &&
        (state.pending_req == SUBSCRIBE)) {
        // Process pending subscribe now that unsubscribe has completed.
        RegisterTable(table, state.instance_id);
        loc->second.current_req = SUBSCRIBE;
        return true;
    } else if ((state.current_req == SUBSCRIBE) &&
               (state.pending_req == UNSUBSCRIBE)) {
        // Process pending unsubscribe now that subscribe has completed.
        UnregisterTable(table);
        loc->second.current_req = UNSUBSCRIBE;
        return true;
    }

    routingtable_membership_request_map_.erase(loc);

    string vrf_name = table->routing_instance()->name();
    VrfTableName vrf_n_table = make_pair(vrf_name, table_name);

    if (state.pending_req == UNSUBSCRIBE) {
        if (vrf_membership_request_map_.find(vrf_name) ==
            vrf_membership_request_map_.end()) {
            assert(defer_q_.count(vrf_n_table) == 0);
        }
        return true;
    } else if (state.pending_req == SUBSCRIBE) {
        IPeerRib *rib = mgr->IPeerRibFind(peer_.get(), table);
        if (rib)
            rib->set_instance_id(state.instance_id);
    }

    for (DeferQ::iterator it = defer_q_.find(vrf_n_table);
         it != defer_q_.end() && it->first.second == table_name; ++it) {
        DequeueRequest(table_name, it->second);
    }

    // Erase all elements for the table
    defer_q_.erase(vrf_n_table);
    return true;
}

void BgpXmppChannel::MembershipRequestCallback(IPeer *ipeer, BgpTable *table) {
    membership_response_worker_.Enqueue(table->name());
}

void BgpXmppChannel::FillInstanceMembershipInfo(BgpNeighborResp *resp) const {
    vector<BgpNeighborRoutingInstance> instance_list;
    BOOST_FOREACH(const SubscribedRoutingInstanceList::value_type &entry,
        routing_instances_) {
        BgpNeighborRoutingInstance instance;
        instance.set_name(entry.first->name());
        instance.set_state("subscribed");
        instance.set_index(entry.second.index);
        vector<string> import_targets;
        BOOST_FOREACH(RouteTarget rt, entry.second.targets) {
            import_targets.push_back(rt.ToString());
        }
        instance.set_import_targets(import_targets);
        instance_list.push_back(instance);
    }
    BOOST_FOREACH(const VrfMembershipRequestMap::value_type &entry,
        vrf_membership_request_map_) {
        BgpNeighborRoutingInstance instance;
        instance.set_name(entry.first);
        instance.set_state("pending");
        instance.set_index(entry.second);
        instance_list.push_back(instance);
    }
    resp->set_routing_instances(instance_list);
}

void BgpXmppChannel::FillTableMembershipInfo(BgpNeighborResp *resp) const {
    vector<BgpNeighborRoutingTable> old_table_list = resp->get_routing_tables();
    set<string> old_table_set;
    vector<BgpNeighborRoutingTable> new_table_list;

    BOOST_FOREACH(const BgpNeighborRoutingTable &table, old_table_list) {
        old_table_set.insert(table.get_name());
        if (routingtable_membership_request_map_.find(table.get_name()) ==
            routingtable_membership_request_map_.end()) {
            new_table_list.push_back(table);
        }
    }

    BOOST_FOREACH(const RoutingTableMembershipRequestMap::value_type &entry,
        routingtable_membership_request_map_) {
        BgpNeighborRoutingTable table;
        table.set_name(entry.first);
        if (old_table_set.find(entry.first) != old_table_set.end())
            table.set_current_state("subscribed");
        const MembershipRequestState &state = entry.second;
        if (state.current_req == SUBSCRIBE) {
            table.set_current_request("subscribe");
        } else {
            table.set_current_request("unsubscribe");
        }
        if (state.pending_req == SUBSCRIBE) {
            table.set_pending_request("subscribe");
        } else {
            table.set_pending_request("unsubscribe");
        }
        new_table_list.push_back(table);
    }
    resp->set_routing_tables(new_table_list);
}

//
// Erase all defer_q_ elements with the given (vrf, table).
//
void BgpXmppChannel::FlushDeferQ(string vrf_name, string table_name) {
    for (DeferQ::iterator it =
        defer_q_.find(make_pair(vrf_name, table_name)), itnext;
        (it != defer_q_.end() && it->first.second == table_name);
        it = itnext) {
        itnext = it;
        itnext++;
        delete it->second;
        defer_q_.erase(it);
    }
}

//
// Erase all defer_q_ elements for all tables for the given vrf.
//
void BgpXmppChannel::FlushDeferQ(string vrf_name) {
    for (DeferQ::iterator it =
        defer_q_.lower_bound(make_pair(vrf_name, string())), itnext;
        (it != defer_q_.end() && it->first.first == vrf_name);
        it = itnext) {
        itnext = it;
        itnext++;
        delete it->second;
        defer_q_.erase(it);
    }
}

void BgpXmppChannel::PublishRTargetRoute(RoutingInstance *rt_instance,
    bool add_change, int index) {
    // Add rtarget route for import route target of the routing instance.
    RoutingInstanceMgr *instance_mgr = bgp_server_->routing_instance_mgr();
    RoutingInstance *master =
        instance_mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
    assert(master);
    BgpTable *rtarget_table = master->GetTable(Address::RTARGET);
    assert(rtarget_table);

    SubscribedRoutingInstanceList::iterator it;
    BgpAttrPtr attr = NULL;

    if (add_change) {
        BgpAttrSpec attrs;
        BgpAttrNextHop nexthop(bgp_server_->bgp_identifier());
        attrs.push_back(&nexthop);
        BgpAttrOrigin origin(BgpAttrOrigin::IGP);
        attrs.push_back(&origin);
        attr = bgp_server_->attr_db()->Locate(attrs);
        SubscriptionState state(rt_instance->GetImportList(), index);
        pair<SubscribedRoutingInstanceList::iterator, bool> ret =
            routing_instances_.insert(
                  pair<RoutingInstance *, SubscriptionState>
                  (rt_instance, state));
        if (!ret.second) return;
        it = ret.first;
    } else {
        it = routing_instances_.find(rt_instance);
        if (it == routing_instances_.end()) return;
    }

    BOOST_FOREACH(RouteTarget rtarget, it->second.targets) {
        if (add_change) {
            AddNewRTargetRoute(rtarget_table, rt_instance, rtarget, attr);
        } else {
            DeleteRTargetRoute(rtarget_table, rt_instance, rtarget);
        }
    }

    if (!add_change)  {
        routing_instances_.erase(it);
    }
}

void BgpXmppChannel::ProcessDeferredSubscribeRequest(RoutingInstance *instance,
                                                     int instance_id) {
    PublishRTargetRoute(instance, true, instance_id);
    RoutingInstance::RouteTableList const rt_list = instance->GetTables();
    for (RoutingInstance::RouteTableList::const_iterator it = rt_list.begin();
         it != rt_list.end(); ++it) {
        BgpTable *table = it->second;
        if (table->IsVpnTable() || table->family() == Address::RTARGET)
            continue;

        RegisterTable(table, instance_id);
        MembershipRequestState state(SUBSCRIBE, instance_id);
        routingtable_membership_request_map_.insert(
            make_pair(table->name(), state));
    }
}

void BgpXmppChannel::ProcessSubscriptionRequest(
        string vrf_name, const XmppStanza::XmppMessageIq *iq,
        bool add_change) {
    int instance_id = -1;

    if (add_change) {
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(iq->dom.get());
        xml_node options = pugi->FindNode("options");
        for (xml_node node = options.first_child(); node;
             node = node.next_sibling()) {
            if (strcmp(node.name(), "instance-id") == 0) {
                instance_id = node.text().as_int();
            }
        }
    }

    RoutingInstanceMgr *instance_mgr = bgp_server_->routing_instance_mgr();
    assert(instance_mgr);
    RoutingInstance *rt_instance = instance_mgr->GetRoutingInstance(vrf_name);
    if (rt_instance == NULL) {
        BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_INFO,
                     BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                     "Routing instance " << vrf_name <<
                     " not found when processing " <<
                     (add_change ? "subscribe" : "unsubscribe"));
        if (add_change) {
            if (vrf_membership_request_map_.find(vrf_name) !=
                vrf_membership_request_map_.end()) {
                BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Duplicate subscribe for routing instance " <<
                             vrf_name << ", triggering close");
                channel_->Close();
            } else {
                vrf_membership_request_map_[vrf_name] = instance_id;
                channel_stats_.instance_subscribe++;
            }
        } else {
            if (vrf_membership_request_map_.erase(vrf_name)) {
                FlushDeferQ(vrf_name);
                channel_stats_.instance_unsubscribe++;
            } else {
                BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Spurious unsubscribe for routing instance " <<
                             vrf_name << ", triggering close");
                channel_->Close();
            }
        }
        return;
    } else if (rt_instance->deleted()) {
        BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG,
                     BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                     "Routing instance " << vrf_name <<
                     " is being deleted when processing " <<
                     (add_change ? "subscribe" : "unsubscribe"));
        if (add_change) {
            if (vrf_membership_request_map_.find(vrf_name) !=
                vrf_membership_request_map_.end()) {
                BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Duplicate subscribe for routing instance " <<
                             vrf_name << ", triggering close");
                channel_->Close();
            } else if (routing_instances_.find(rt_instance) !=
                routing_instances_.end()) {
                BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Duplicate subscribe for routing instance " <<
                             vrf_name << ", triggering close");
                channel_->Close();
            } else {
                vrf_membership_request_map_[vrf_name] = instance_id;
                channel_stats_.instance_subscribe++;
            }
            return;
        } else {
            // If instance is being deleted and agent is trying to unsubscribe
            // we need to process the unsubscribe if vrf is not in the request
            // map.  This would be the normal case where we wait for agent to
            // unsubscribe in order to remove routes added by it.
            if (vrf_membership_request_map_.erase(vrf_name)) {
                FlushDeferQ(vrf_name);
                channel_stats_.instance_unsubscribe++;
                return;
            } else if (routing_instances_.find(rt_instance) ==
                routing_instances_.end()) {
                BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Spurious unsubscribe for routing instance " <<
                             vrf_name << ", triggering close");
                channel_->Close();
                return;
            }
            channel_stats_.instance_unsubscribe++;
        }
    } else {
        if (add_change) {
            if (routing_instances_.find(rt_instance) !=
                routing_instances_.end()) {
                BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Duplicate subscribe for routing instance " <<
                             vrf_name << ", triggering close");
                channel_->Close();
                return;
            }
            channel_stats_.instance_subscribe++;
        } else {
            if (routing_instances_.find(rt_instance) ==
                routing_instances_.end()) {
                BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Spurious unsubscribe for routing instance " <<
                             vrf_name << ", triggering close");
                channel_->Close();
                return;
            }
            channel_stats_.instance_unsubscribe++;
        }
    }

    PublishRTargetRoute(rt_instance, add_change, instance_id);

    RoutingInstance::RouteTableList const rt_list = rt_instance->GetTables();
    for (RoutingInstance::RouteTableList::const_iterator it = rt_list.begin();
         it != rt_list.end(); ++it) {
        BgpTable *table = it->second;
        if (table->IsVpnTable() || table->family() == Address::RTARGET)
            continue;

        if (add_change) {
            RoutingTableMembershipRequestMap::iterator loc =
                routingtable_membership_request_map_.find(table->name());
            if (loc == routingtable_membership_request_map_.end()) {
                MembershipRequestState state(SUBSCRIBE, instance_id);
                routingtable_membership_request_map_.insert(
                    make_pair(table->name(), state));
            } else {
                loc->second.instance_id = instance_id;
                loc->second.pending_req = SUBSCRIBE;
                continue;
            }
            RegisterTable(table, instance_id);
        } else {
            if (defer_q_.count(make_pair(vrf_name, table->name()))) {
                BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG,
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Flush deferred route requests for table " <<
                             table->name() << " on unsubscribe");
            }

            // Erase all elements for the table
            FlushDeferQ(vrf_name, table->name());

            RoutingTableMembershipRequestMap::iterator loc =
                routingtable_membership_request_map_.find(table->name());
            if (loc == routingtable_membership_request_map_.end()) {
                MembershipRequestState state(UNSUBSCRIBE, instance_id);
                routingtable_membership_request_map_.insert(
                    make_pair(table->name(), state));
            } else {
                loc->second.instance_id = -1;
                loc->second.pending_req = UNSUBSCRIBE;
                continue;
            }
            UnregisterTable(table);
        }
    }
}

void BgpXmppChannel::ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
    CHECK_CONCURRENCY("xmpp::StateMachine");

    // Bail if the connection is being deleted. It's not safe to assert
    // because the Delete method can be called from the main thread.
    if (channel_->connection() && channel_->connection()->IsDeleted())
        return;

    // Make sure that peer is not set for closure already.
    assert(!defer_peer_close_);
    assert(!peer_->IsDeleted());

    if (msg->type == XmppStanza::IQ_STANZA) {
        const XmppStanza::XmppMessageIq *iq =
                   static_cast<const XmppStanza::XmppMessageIq *>(msg);
        if (iq->iq_type.compare("set") == 0) {
            if (iq->action.compare("subscribe") == 0) {
                ProcessSubscriptionRequest(iq->node, iq, true);
            } else if (iq->action.compare("unsubscribe") == 0) {
                ProcessSubscriptionRequest(iq->node, iq, false);
            } else if (iq->action.compare("publish") == 0) {
                XmlBase *impl = msg->dom.get();
                stats_[RX].rt_updates++;
                XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl);
                for (xml_node item = pugi->FindNode("item"); item;
                    item = item.next_sibling()) {
                    if (strcmp(item.name(), "item") != 0) continue;

                        string id(iq->as_node.c_str());
                        char *str = const_cast<char *>(id.c_str());
                        char *saveptr;
                        char *af = strtok_r(str, "/", &saveptr);
                        char *safi = strtok_r(NULL, "/", &saveptr);

                        if (atoi(af) == BgpAf::IPv4 &&
                            atoi(safi) == BgpAf::Unicast) {
                            ProcessItem(iq->node, item, iq->is_as_node);
                        } else if (atoi(af) == BgpAf::IPv6 &&
                                   atoi(safi) == BgpAf::Unicast) {
                            ProcessInet6Item(iq->node, item, iq->is_as_node);
                        } else if (atoi(af) == BgpAf::IPv4 &&
                            atoi(safi) == BgpAf::Mcast) {
                            ProcessMcastItem(iq->node, item, iq->is_as_node);
                        } else if (atoi(af) == BgpAf::L2Vpn &&
                                   atoi(safi) == BgpAf::Enet) {
                            ProcessEnetItem(iq->node, item, iq->is_as_node);
                        }
                }
            }
        }
    }
}

bool BgpXmppChannelManager::DeleteExecutor(BgpXmppChannel *channel) {
    if (channel->deleted()) return true;
    channel->set_deleted(true);

    // TODO(ananth): Enqueue an event to the deleter() and deleted this peer
    // and the channel from a different thread to solve concurrency issues
    delete channel;
    return true;
}


// BgpXmppChannelManager routines.

BgpXmppChannelManager::BgpXmppChannelManager(XmppServer *xmpp_server,
                                             BgpServer *server)
    : xmpp_server_(xmpp_server),
      bgp_server_(server),
      queue_(TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0,
          boost::bind(&BgpXmppChannelManager::DeleteExecutor, this, _1)),
      id_(-1),
      asn_listener_id_(-1),
      identifier_listener_id_(-1),
      closing_count_(0) {
    queue_.SetEntryCallback(
            boost::bind(&BgpXmppChannelManager::IsReadyForDeletion, this));
    if (xmpp_server) {
        xmpp_server->RegisterConnectionEvent(xmps::BGP,
               boost::bind(&BgpXmppChannelManager::XmppHandleChannelEvent,
                           this, _1, _2));
    }
    admin_down_listener_id_ =
        server->RegisterAdminDownCallback(boost::bind(
            &BgpXmppChannelManager::AdminDownCallback, this));
    asn_listener_id_ =
        server->RegisterASNUpdateCallback(boost::bind(
            &BgpXmppChannelManager::ASNUpdateCallback, this, _1, _2));
    identifier_listener_id_ =
        server->RegisterIdentifierUpdateCallback(boost::bind(
            &BgpXmppChannelManager::IdentifierUpdateCallback, this, _1));

    id_ = server->routing_instance_mgr()->RegisterInstanceOpCallback(
        boost::bind(&BgpXmppChannelManager::RoutingInstanceCallback,
                    this, _1, _2));
}

BgpXmppChannelManager::~BgpXmppChannelManager() {
    assert(channel_map_.empty());
    assert(channel_name_map_.empty());
    assert(closing_count_ == 0);
    if (xmpp_server_) {
        xmpp_server_->UnRegisterConnectionEvent(xmps::BGP);
    }

    queue_.Shutdown();
    bgp_server_->UnregisterAdminDownCallback(admin_down_listener_id_);
    bgp_server_->UnregisterASNUpdateCallback(asn_listener_id_);
    bgp_server_->routing_instance_mgr()->UnregisterInstanceOpCallback(id_);
}

bool BgpXmppChannelManager::IsReadyForDeletion() {
    return bgp_server_->IsReadyForDeletion();
}

void BgpXmppChannelManager::SetQueueDisable(bool disabled) {
    queue_.set_disable(disabled);
}

size_t BgpXmppChannelManager::GetQueueSize() const {
    return queue_.Length();
}

void BgpXmppChannelManager::AdminDownCallback() {
    xmpp_server_->ClearAllConnections();
}

void BgpXmppChannelManager::ASNUpdateCallback(as_t old_asn,
    as_t old_local_asn) {
    BOOST_FOREACH(XmppChannelMap::value_type &i, channel_map_) {
        i.second->ASNUpdateCallback(old_asn, old_local_asn);
    }
    if (bgp_server_->autonomous_system() != old_asn) {
        xmpp_server_->ClearAllConnections();
    }
}

void BgpXmppChannelManager::IdentifierUpdateCallback(
    Ip4Address old_identifier) {
    BOOST_FOREACH(XmppChannelMap::value_type &i, channel_map_) {
        i.second->IdentifierUpdateCallback(old_identifier);
    }
}

void BgpXmppChannelManager::RoutingInstanceCallback(string vrf_name, int op) {
    BOOST_FOREACH(XmppChannelMap::value_type &i, channel_map_) {
        i.second->RoutingInstanceCallback(vrf_name, op);
    }
}

void BgpXmppChannelManager::VisitChannels(BgpXmppChannelManager::VisitorFn fn) {
    BOOST_FOREACH(XmppChannelMap::value_type &i, channel_map_) {
        fn(i.second);
    }
}

BgpXmppChannel *BgpXmppChannelManager::FindChannel(string client) {
    BOOST_FOREACH(XmppChannelMap::value_type &i, channel_map_) {
        if (i.second->ToString() == client) {
            return i.second;
        }
    }
    return NULL;
}

BgpXmppChannel *BgpXmppChannelManager::FindChannel(
        const XmppChannel *ch) {
    XmppChannelMap::iterator it = channel_map_.find(ch);
    if (it == channel_map_.end())
        return NULL;
    return it->second;
}

void BgpXmppChannelManager::RemoveChannel(XmppChannel *channel) {
    channel_map_.erase(channel);
    channel_name_map_.erase(channel->ToString());
}

BgpXmppChannel *BgpXmppChannelManager::CreateChannel(XmppChannel *channel) {
    BgpXmppChannel *ch = new BgpXmppChannel(channel, bgp_server_, this);

    return ch;
}

void BgpXmppChannelManager::XmppHandleChannelEvent(XmppChannel *channel,
                                                   xmps::PeerState state) {
    XmppChannelMap::iterator it = channel_map_.find(channel);

    BgpXmppChannel *bgp_xmpp_channel = NULL;
    if (state == xmps::READY) {
        if (it == channel_map_.end()) {
            bgp_xmpp_channel = CreateChannel(channel);
            channel_map_.insert(make_pair(channel, bgp_xmpp_channel));
            channel_name_map_.insert(
                make_pair(channel->ToString(), bgp_xmpp_channel));
            BGP_LOG_PEER(Message, bgp_xmpp_channel->Peer(),
                         Sandesh::LoggingUtLevel(), BGP_LOG_FLAG_SYSLOG,
                         BGP_PEER_DIR_IN,
                         "Received XmppChannel up event");
            if (!bgp_server_->HasSelfConfiguration()) {
                BGP_LOG_PEER(Message, bgp_xmpp_channel->Peer(),
                             SandeshLevel::SYS_INFO, BGP_LOG_FLAG_SYSLOG,
                             BGP_PEER_DIR_IN,
                             "No BGP configuration for self - closing channel");
                channel->Close();
            }
            if (bgp_server_->admin_down()) {
                BGP_LOG_PEER(Message, bgp_xmpp_channel->Peer(),
                             SandeshLevel::SYS_INFO, BGP_LOG_FLAG_SYSLOG,
                             BGP_PEER_DIR_IN,
                             "BGP is administratively down - closing channel");
                channel->Close();
            }
        } else {
            bgp_xmpp_channel = (*it).second;
            bgp_xmpp_channel->peer_->SetDeleted(false);
        }
    } else if (state == xmps::NOT_READY) {
        if (it != channel_map_.end()) {
            bgp_xmpp_channel = (*it).second;
            BGP_LOG_PEER(Message, bgp_xmpp_channel->Peer(),
                         Sandesh::LoggingUtLevel(), BGP_LOG_FLAG_SYSLOG,
                         BGP_PEER_DIR_IN,
                         "Received XmppChannel down event");

            // Trigger closure of this channel
            bgp_xmpp_channel->Close();
        } else {
            BGP_LOG(BgpMessage, SandeshLevel::SYS_NOTICE, BGP_LOG_FLAG_ALL,
                    "Peer not found on channel not ready event");
        }
    }
    if (bgp_xmpp_channel) {
        XmppPeerInfoData peer_info;
        peer_info.set_name(bgp_xmpp_channel->Peer()->ToUVEKey());
        peer_info.set_send_state("not advertising");
        XMPPPeerInfo::Send(peer_info);
    }
}

void BgpXmppChannel::Close() {
    if (manager_)
        manager_->increment_closing_count();
    close_in_progress_ = true;
    vrf_membership_request_map_.clear();
    STLDeleteElements(&defer_q_);

    if (routingtable_membership_request_map_.size()) {
        BGP_LOG_PEER(Event, peer_.get(), SandeshLevel::SYS_INFO,
            BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA, "Close procedure deferred");
        defer_peer_close_ = true;
        return;
    }
    peer_->Close();
}

//
// Return connection's remote tcp endpoint if available
//
TcpSession::Endpoint BgpXmppChannel::remote_endpoint() const {
    const XmppSession *session = GetSession();
    if (session) {
        return session->remote_endpoint();
    }
    return TcpSession::Endpoint();
}

//
// Return connection's local tcp endpoint if available
//
TcpSession::Endpoint BgpXmppChannel::local_endpoint() const {
    const XmppSession *session = GetSession();
    if (session) {
        return session->local_endpoint();
    }
    return TcpSession::Endpoint();
}

//
// Return connection's remote tcp endpoint string.
//
string BgpXmppChannel::transport_address_string() const {
    TcpSession::Endpoint endpoint = remote_endpoint();
    ostringstream oss;
    oss << endpoint;
    return oss.str();
}

//
// Mark the XmppPeer as deleted.
// For unit testing only.
//
void BgpXmppChannel::set_peer_deleted() {
    peer_->SetDeleted(true);
}

//
// Return true if the XmppPeer is deleted.
//
bool BgpXmppChannel::peer_deleted() const {
    return peer_->IsDeleted();
}

//
// Return time stamp of when the XmppPeer delete was initiated.
//
uint64_t BgpXmppChannel::peer_deleted_at() const {
    return peer_->deleted_at();
}
