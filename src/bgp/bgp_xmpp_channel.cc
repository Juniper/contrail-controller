/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_xmpp_channel.h"

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>
#include <boost/regex.hpp>

#include <limits>
#include <sstream>
#include <vector>

#include "base/task_annotations.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update_sender.h"
#include "bgp/bgp_xmpp_peer_close.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/extended-community/etree.h"
#include "bgp/extended-community/load_balance.h"
#include "bgp/extended-community/mac_mobility.h"
#include "bgp/extended-community/router_mac.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/peer_close_manager.h"
#include "bgp/peer_stats.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/bgp_xmpp_rtarget_manager.h"
#include "control-node/sandesh/control_node_types.h"
#include "net/community_type.h"
#include "schema/xmpp_multicast_types.h"
#include "schema/xmpp_enet_types.h"
#include "xml/xml_pugi.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_init.h"
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

using boost::assign::list_of;
using boost::regex;
using boost::regex_search;
using boost::smatch;
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

class BgpXmppChannel::PeerStats : public IPeerDebugStats {
public:
    explicit PeerStats(BgpXmppChannel *peer)
        : parent_(peer) {
    }

    // Used when peer flaps.
    // Don't need to do anything since the BgpXmppChannel itself gets deleted.
    virtual void Clear() {
    }

    // Printable name
    virtual string ToString() const {
        return parent_->ToString();
    }

    // Previous State of the peer
    virtual string last_state() const {
        return (parent_->channel_->LastStateName());
    }
    // Last state change occurred at
    virtual string last_state_change_at() const {
        return (parent_->channel_->LastStateChangeAt());
    }

    // Last error on this peer
    virtual string last_error() const {
        return "";
    }

    // Last Event on this peer
    virtual string last_event() const {
        return (parent_->channel_->LastEvent());
    }

    // When was the Last
    virtual string last_flap() const {
        return (parent_->channel_->LastFlap());
    }

    // Total number of flaps
    virtual uint64_t num_flaps() const {
        return (parent_->channel_->FlapCount());
    }

    virtual void GetRxProtoStats(ProtoStats *stats) const {
        stats->open = parent_->channel_->rx_open();
        stats->close = parent_->channel_->rx_close();
        stats->keepalive = parent_->channel_->rx_keepalive();
        stats->update = parent_->channel_->rx_update();
    }

    virtual void GetTxProtoStats(ProtoStats *stats) const {
        stats->open = parent_->channel_->tx_open();
        stats->close = parent_->channel_->tx_close();
        stats->keepalive = parent_->channel_->tx_keepalive();
        stats->update = parent_->channel_->tx_update();
    }

    virtual void GetRxRouteUpdateStats(UpdateStats *stats)  const {
        stats->reach = parent_->stats_[RX].reach;
        stats->unreach = parent_->stats_[RX].unreach;
        stats->end_of_rib = parent_->stats_[RX].end_of_rib;
        stats->total = stats->reach + stats->unreach + stats->end_of_rib;
    }

    virtual void GetTxRouteUpdateStats(UpdateStats *stats)  const {
        stats->reach = parent_->stats_[TX].reach;
        stats->unreach = parent_->stats_[TX].unreach;
        stats->end_of_rib = parent_->stats_[TX].end_of_rib;
        stats->total = stats->reach + stats->unreach + stats->end_of_rib;
    }

    virtual void GetRxSocketStats(IPeerDebugStats::SocketStats *stats) const {
        const XmppSession *session = parent_->GetSession();
        if (session) {
            io::SocketStats socket_stats(session->GetSocketStats());
            stats->calls = socket_stats.read_calls;
            stats->bytes = socket_stats.read_bytes;
        }
    }

    virtual void GetTxSocketStats(IPeerDebugStats::SocketStats *stats) const {
        const XmppSession *session = parent_->GetSession();
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
        const BgpXmppChannel::ErrorStats &err_stats = parent_->error_stats();
        stats->inet6_bad_xml_token_count =
            err_stats.get_inet6_rx_bad_xml_token_count();
        stats->inet6_bad_prefix_count =
            err_stats.get_inet6_rx_bad_prefix_count();
        stats->inet6_bad_nexthop_count =
            err_stats.get_inet6_rx_bad_nexthop_count();
        stats->inet6_bad_afi_safi_count =
            err_stats.get_inet6_rx_bad_afi_safi_count();
    }

    virtual void GetRxRouteStats(RxRouteStats *stats) const {
        stats->total_path_count = parent_->Peer()->GetTotalPathCount();
        stats->primary_path_count = parent_->Peer()->GetPrimaryPathCount();
    }

    virtual void UpdateTxUnreachRoute(uint64_t count) {
        parent_->stats_[TX].unreach += count;
    }

    virtual void UpdateTxReachRoute(uint64_t count) {
        parent_->stats_[TX].reach += count;
    }

private:
    BgpXmppChannel *parent_;
};

class BgpXmppChannel::XmppPeer : public IPeer {
public:
    XmppPeer(BgpServer *server, BgpXmppChannel *channel)
        : server_(server),
          parent_(channel),
          is_closed_(false),
          send_ready_(true),
          closed_at_(0) {
        total_path_count_ = 0;
        primary_path_count_ = 0;
    }

    virtual ~XmppPeer() {
        assert(GetTotalPathCount() == 0);

        XmppPeerInfoData peer_info;
        peer_info.set_name(ToUVEKey());
        peer_info.set_deleted(true);
        parent_->XMPPPeerInfoSend(peer_info);

        PeerStatsData peer_stats_data;
        peer_stats_data.set_name(ToUVEKey());
        peer_stats_data.set_deleted(true);
        PeerStatsUve::Send(peer_stats_data, "ObjectXmppPeerInfo");
    }

    virtual bool MembershipPathCallback(DBTablePartBase *tpart, BgpRoute *rt,
                                        BgpPath *path) {
        if (parent_->close_manager_->IsMembershipInUse())
            return parent_->close_manager_->MembershipPathCallback(tpart, rt,
                                                                   path);

        BgpTable *table = static_cast<BgpTable *>(tpart->parent());
        return table->DeletePath(tpart, rt, path);
    }

    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize,
                            const std::string *msg_str);
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        return SendUpdate(msg, msgsize, NULL);
    }
    virtual const string &ToString() const {
        return parent_->ToString();
    }

    virtual bool CanUseMembershipManager() const {
        return parent_->GetMembershipRequestQueueSize() == 0;
    }

    virtual bool IsRegistrationRequired() const { return true; }

    virtual const string &ToUVEKey() const {
        return parent_->ToUVEKey();
    }

    virtual BgpServer *server() { return server_; }
    virtual BgpServer *server() const { return server_; }
    virtual IPeerClose *peer_close() {
        return parent_->peer_close_.get();
    }
    virtual IPeerClose *peer_close() const {
        return parent_->peer_close_.get();
    }

    void UpdateCloseRouteStats(Address::Family family, const BgpPath *old_path,
                               uint32_t path_flags) const {
        peer_close()->UpdateRouteStats(family, old_path, path_flags);
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
    virtual void Close(bool graceful);

    const bool IsDeleted() const { return is_closed_; }
    void SetPeerClosed(bool closed) {
        is_closed_ = closed;
        if (is_closed_)
            closed_at_ = UTCTimestampUsec();
    }
    uint64_t closed_at() const { return closed_at_; }

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

    virtual void UpdateTotalPathCount(int count) const {
        total_path_count_ += count;
    }
    virtual int GetTotalPathCount() const {
        return total_path_count_;
    }
    virtual void UpdatePrimaryPathCount(int count) const {
        primary_path_count_ += count;
    }
    virtual int GetPrimaryPathCount() const {
         return primary_path_count_;
    }
    virtual bool IsInGRTimerWaitState() const {
        return parent_->close_manager_->IsInGRTimerWaitState();
    }

    void MembershipRequestCallback(BgpTable *table) {
        parent_->MembershipRequestCallback(table);
    }

    virtual bool send_ready() const { return send_ready_; }

private:
    void WriteReadyCb(const boost::system::error_code &ec) {
        if (!server_) return;
        BgpUpdateSender *sender = server_->update_sender();
        BGP_LOG_PEER(Event, this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_NA, "Send ready");
        sender->PeerSendReady(this);
        send_ready_ = true;

        // Restart EndOfRib Send timer if necessary.
        parent_->ResetEndOfRibSendState();
    }

    BgpServer *server_;
    BgpXmppChannel *parent_;
    mutable tbb::atomic<int> total_path_count_;
    mutable tbb::atomic<int> primary_path_count_;
    bool is_closed_;
    bool send_ready_;
    uint64_t closed_at_;
};

// Skip sending updates if the destinatin matches against the pattern.
// XXX Used in test environments only
bool BgpXmppChannel::SkipUpdateSend() {
    static char *skip_env_ = getenv("XMPP_SKIP_UPDATE_SEND");
    if (!skip_env_)
        return false;

    // Use XMPP_SKIP_UPDATE_SEND as a regex pattern to match against destination
    // Cache the result to avoid redundant regex evaluation
    if (!skip_update_send_cached_) {
        smatch matches;
        skip_update_send_ = regex_search(ToString(), matches, regex(skip_env_));
        skip_update_send_cached_ = true;
    }
    return skip_update_send_;
}

bool BgpXmppChannel::XmppPeer::SendUpdate(const uint8_t *msg, size_t msgsize,
    const string *msg_str) {
    XmppChannel *channel = parent_->channel_;
    if (channel->GetPeerState() == xmps::READY) {
        parent_->stats_[TX].rt_updates++;
        if (parent_->SkipUpdateSend())
            return true;
        send_ready_ = channel->Send(msg, msgsize, msg_str, xmps::BGP,
                boost::bind(&BgpXmppChannel::XmppPeer::WriteReadyCb, this, _1));
        if (!send_ready_) {
            BGP_LOG_PEER(Event, this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                         BGP_PEER_DIR_NA, "Send blocked");

            // If EndOfRib Send timer is running, cancel it and reschedule it
            // after socket gets unblocked.
            if (parent_->eor_send_timer_ && parent_->eor_send_timer_->running())
                parent_->eor_send_timer_->Cancel();
        }
        return send_ready_;
    } else {
        return false;
    }
}

void BgpXmppChannel::XmppPeer::Close(bool graceful) {
    send_ready_ = true;
    parent_->set_peer_closed(true);
    if (server_ == NULL)
        return;

    XmppConnection *connection =
        const_cast<XmppConnection *>(parent_->channel_->connection());

    if (connection && !connection->IsActiveChannel()) {

        // Clear EOR state.
        parent_->ClearEndOfRibState();

        parent_->peer_close_->Close(graceful);
    }
}

BgpXmppChannel::BgpXmppChannel(XmppChannel *channel,
                               BgpServer *bgp_server,
                               BgpXmppChannelManager *manager)
    : channel_(channel),
      peer_id_(xmps::BGP),
      rtarget_manager_(new BgpXmppRTargetManager(this)),
      bgp_server_(bgp_server),
      peer_(new XmppPeer(bgp_server, this)),
      peer_close_(new BgpXmppPeerClose(this)),
      peer_stats_(new PeerStats(this)),
      bgp_policy_(BgpProto::XMPP, RibExportPolicy::XMPP, -1, 0),
      manager_(manager),
      delete_in_progress_(false),
      deleted_(false),
      defer_peer_close_(false),
      skip_update_send_(false),
      skip_update_send_cached_(false),
      eor_sent_(false),
      eor_receive_timer_(NULL),
      eor_send_timer_(NULL),
      eor_receive_timer_start_time_(0),
      eor_send_timer_start_time_(0),
      membership_response_worker_(
            TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"),
            channel->GetTaskInstance(),
            boost::bind(&BgpXmppChannel::MembershipResponseHandler, this, _1)),
      lb_mgr_(new LabelBlockManager()) {
    close_manager_.reset(
        BgpObjectFactory::Create<PeerCloseManager>(peer_close_.get()));
    if (bgp_server) {
        eor_receive_timer_ =
            TimerManager::CreateTimer(*bgp_server->ioservice(),
                "EndOfRib receive timer",
                TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"),
                channel->GetTaskInstance());
        eor_send_timer_ =
            TimerManager::CreateTimer(*bgp_server->ioservice(),
                "EndOfRib send timer",
                TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"),
                channel->GetTaskInstance());
    }
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
    if (manager_ && delete_in_progress_)
        manager_->decrement_deleting_count();
    STLDeleteElements(&defer_q_);
    assert(peer_deleted());
    assert(!close_manager_->IsMembershipInUse());
    assert(table_membership_request_map_.empty());
    TimerManager::DeleteTimer(eor_receive_timer_);
    TimerManager::DeleteTimer(eor_send_timer_);
    BGP_LOG_PEER(Event, peer_.get(), SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
        BGP_PEER_DIR_NA, "Deleted");
    channel_->UnRegisterReceive(peer_id_);
}

void BgpXmppChannel::XMPPPeerInfoSend(const XmppPeerInfoData &peer_info) const {
    XMPPPeerInfo::Send(peer_info);
}

const XmppSession *BgpXmppChannel::GetSession() const {
    if (channel_ && channel_->connection()) {
        return channel_->connection()->session();
    }
    return NULL;
}

const string &BgpXmppChannel::ToString() const {
    return channel_->ToString();
}

const string &BgpXmppChannel::ToUVEKey() const {
    if (channel_->connection()) {
        return channel_->connection()->ToUVEKey();
    } else {
        return channel_->ToString();
    }
}

string BgpXmppChannel::StateName() const {
    return channel_->StateName();
}


size_t BgpXmppChannel::GetMembershipRequestQueueSize() const {
    return table_membership_request_map_.size();
}

void BgpXmppChannel::RoutingInstanceCallback(string vrf_name, int op) {
    if (delete_in_progress_)
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
        const InstanceMembershipRequestState *imr_state =
            GetInstanceMembershipState(vrf_name);
        if (!imr_state)
            return;
        ProcessDeferredSubscribeRequest(rt_instance, *imr_state);
        DeleteInstanceMembershipState(vrf_name);
    } else {
        SubscriptionState *sub_state = GetSubscriptionState(rt_instance);
        if (!sub_state)
            return;
        rtarget_manager_->RoutingInstanceCallback(
            rt_instance, &sub_state->targets);
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
    if (af != BgpAf::IPv4 && af != BgpAf::IPv6)
        return false;

    error_code error;
    *addrp = IpAddress::from_string(address, error);
    if (error)
        return false;

    return (zero_ok ? true : !addrp->is_unspecified());
}

//
// Return true if there's a pending request, false otherwise.
//
bool BgpXmppChannel::GetMembershipInfo(BgpTable *table,
    int *instance_id, uint64_t *subscription_gen_id, RequestType *req_type) {
    *instance_id = -1;
    *subscription_gen_id = 0;
    TableMembershipRequestState *tmr_state =
        GetTableMembershipState(table->name());
    if (tmr_state) {
        *req_type = tmr_state->pending_req;
        *instance_id = tmr_state->instance_id;
        return true;
    } else {
        *req_type = NONE;
        BgpMembershipManager *mgr = bgp_server_->membership_mgr();
        mgr->GetRegistrationInfo(peer_.get(), table,
                                 instance_id, subscription_gen_id);
        return false;
    }
}

//
// Add entry to the pending table request map.
//
void BgpXmppChannel::AddTableMembershipState(const string &table_name,
    TableMembershipRequestState tmr_state) {
    table_membership_request_map_.insert(make_pair(table_name, tmr_state));
}

//
// Delete entry from the pending table request map.
// Return true if the entry was found and deleted.
//
bool BgpXmppChannel::DeleteTableMembershipState(const string &table_name) {
    return (table_membership_request_map_.erase(table_name) > 0);
}

//
// Find entry in the pending table request map.
//
BgpXmppChannel::TableMembershipRequestState *
BgpXmppChannel::GetTableMembershipState(
    const string &table_name) {
    TableMembershipRequestMap::iterator loc =
        table_membership_request_map_.find(table_name);
    return (loc == table_membership_request_map_.end() ? NULL : &loc->second);
}

//
// Find entry in the pending table request map.
// Const version.
//
const BgpXmppChannel::TableMembershipRequestState *
BgpXmppChannel::GetTableMembershipState(
    const string &table_name) const {
    TableMembershipRequestMap::const_iterator loc =
        table_membership_request_map_.find(table_name);
    return (loc == table_membership_request_map_.end() ? NULL : &loc->second);
}

//
// Add entry to the pending instance request map.
//
void BgpXmppChannel::AddInstanceMembershipState(const string &instance,
    InstanceMembershipRequestState imr_state) {
    instance_membership_request_map_.insert(make_pair(instance, imr_state));
}

//
// Delete entry from the pending instance request map.
// Return true if the entry was found and deleted.
//
bool BgpXmppChannel::DeleteInstanceMembershipState(const string &instance) {
    return (instance_membership_request_map_.erase(instance) > 0);
}

//
// Find the entry in the pending instance request map.
//
const BgpXmppChannel::InstanceMembershipRequestState *
BgpXmppChannel::GetInstanceMembershipState(const string &instance) const {
    InstanceMembershipRequestMap::const_iterator loc =
        instance_membership_request_map_.find(instance);
    return loc != instance_membership_request_map_.end() ? &loc->second : NULL;
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
    int *instance_id, uint64_t *subscription_gen_id, bool *subscribe_pending,
    bool add_change) {
    *table = NULL;
    *subscribe_pending = false;

    RoutingInstanceMgr *instance_mgr = bgp_server_->routing_instance_mgr();
    RoutingInstance *rt_instance = instance_mgr->GetRoutingInstance(vrf_name);
    if (rt_instance)
        *table = rt_instance->GetTable(family);
    if (rt_instance != NULL && !rt_instance->deleted()) {
        RequestType req_type;
        if (GetMembershipInfo(*table, instance_id,
                              subscription_gen_id, &req_type)) {
            // Bail if there's a pending unsubscribe.
            if (req_type != SUBSCRIBE) {
                BGP_LOG_PEER_INSTANCE_CRITICAL(Peer(), vrf_name,
                    BGP_PEER_DIR_IN, BGP_LOG_FLAG_ALL,
                    "Received route after unsubscribe");
                return false;
            }
            *subscribe_pending = true;
        } else {
            // Bail if we are not subscribed to the table.
            if (*instance_id < 0) {
                BGP_LOG_PEER_INSTANCE_CRITICAL(Peer(), vrf_name,
                    BGP_PEER_DIR_IN, BGP_LOG_FLAG_ALL,
                    "Received route without subscribe");
                return false;
            }
        }
    } else {
        // Bail if there's no pending subscribe for the instance.
        // Note that route retract can be received while the instance is
        // marked for deletion.
        const InstanceMembershipRequestState *imr_state =
            GetInstanceMembershipState(vrf_name);
        if (imr_state) {
            *instance_id = imr_state->instance_id;
            *subscribe_pending = true;
        } else if (add_change || !rt_instance) {
            BGP_LOG_PEER_INSTANCE_CRITICAL(Peer(), vrf_name, BGP_PEER_DIR_IN,
               BGP_LOG_FLAG_ALL, "Received route without pending subscribe");
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
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
            BGP_LOG_FLAG_ALL, "Invalid multicast route message received");
        return false;
    }

    if (item.entry.nlri.af != BgpAf::IPv4) {
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Unsupported address family " << item.entry.nlri.af <<
            " for multicast route");
        return false;
    }

    if (item.entry.nlri.safi != BgpAf::Mcast) {
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
            BGP_LOG_FLAG_ALL, "Unsupported subsequent address family " <<
            item.entry.nlri.safi << " for multicast route");
        return false;
    }

    error_code error;
    IpAddress grp_address = IpAddress::from_string("0.0.0.0", error);
    if (!item.entry.nlri.group.empty()) {
        if (!XmppDecodeAddress(item.entry.nlri.af,
            item.entry.nlri.group, &grp_address, false)) {
            BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
                "Bad group address " << item.entry.nlri.group);
            return false;
        }
    }

    IpAddress src_address = IpAddress::from_string("0.0.0.0", error);
    if (!item.entry.nlri.source.empty()) {
        if (!XmppDecodeAddress(item.entry.nlri.af,
            item.entry.nlri.source, &src_address, true)) {
            BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
                "Bad source address " << item.entry.nlri.source);
            return false;
        }
    }

    bool subscribe_pending;
    int instance_id;
    uint64_t subscription_gen_id;
    BgpTable *table;
    if (!VerifyMembership(vrf_name, Address::ERMVPN, &table, &instance_id,
        &subscription_gen_id, &subscribe_pending, add_change)) {
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
            BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                BGP_LOG_FLAG_ALL, "Missing next-hop for multicast route " <<
                mc_prefix.ToString());
            return false;
        }

        // Agents should send only one next-hop in the item
        if (inh_list.next_hop.size() != 1) {
            BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
                "More than one nexthop received for multicast route " <<
                mc_prefix.ToString());
            return false;
        }

        McastNextHopsType::const_iterator nit = inh_list.begin();

        // Label Allocation item.entry.label by parsing the range
        label_range = nit->label;
        if (!stringToIntegerList(label_range, "-", labels) ||
            labels.size() != 2) {
            BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
                "Bad label range " << label_range <<
                " for multicast route " << mc_prefix.ToString());
            return false;
        }

        if (!labels[0] || !labels[1] || labels[1] < labels[0]) {
            BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                BGP_LOG_FLAG_ALL, "Bad label range " << label_range <<
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
            BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                BGP_LOG_FLAG_ALL, "Bad nexthop address " << nit->address <<
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
        req.data.reset(new ErmVpnTable::RequestData(
            attr, flags, 0, 0, subscription_gen_id));
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
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Invalid inet route message received");
        return false;
    }

    if (item.entry.nlri.af != BgpAf::IPv4) {
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Unsupported address family " << item.entry.nlri.af <<
            " for inet route " << item.entry.nlri.address);
        return false;
    }

    error_code error;
    Ip4Prefix inet_prefix =
        Ip4Prefix::FromString(item.entry.nlri.address, &error);
    if (error) {
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Bad inet route " << item.entry.nlri.address);
        return false;
    }

    if (add_change && item.entry.next_hops.next_hop.empty()) {
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Missing next-hops for inet route " << inet_prefix.ToString());
        return false;
    }

    // Rules for routes in master instance:
    // - Label must be 0
    // - Tunnel encapsulation is not required
    // - Do not add SourceRd and ExtCommunitySpec
    bool master = (vrf_name == BgpConfigManager::kMasterInstance);

    // vector<Address::Family> family_list = list_of(Address::INET)(Address::EVPN);
    vector<Address::Family> family_list = list_of(Address::INET);
    BOOST_FOREACH(Address::Family family, family_list) {
        bool subscribe_pending;
        int instance_id;
        uint64_t subscription_gen_id;
        BgpTable *table;
        if (!VerifyMembership(vrf_name, family, &table, &instance_id,
            &subscription_gen_id, &subscribe_pending, add_change)) {
            channel_->Close();
            return false;
        }

        DBRequest req;
        if (family == Address::INET) {
            req.key.reset(new InetTable::RequestKey(inet_prefix, peer_.get()));
        } else {
            EvpnPrefix evpn_prefix(RouteDistinguisher::kZeroRd, 0,
                inet_prefix.addr(), inet_prefix.prefixlen());
            req.key.reset(new EvpnTable::RequestKey(evpn_prefix, peer_.get()));
        }

        IpAddress nh_address(Ip4Address(0));
        uint32_t label = 0;
        uint32_t flags = 0;
        ExtCommunitySpec ext;
        CommunitySpec comm;

        if (add_change) {
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            BgpAttrSpec attrs;

            const NextHopListType &inh_list = item.entry.next_hops;

            // Agents should send only one next-hop in the item.
            if (inh_list.next_hop.size() != 1) {
                BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                    BGP_LOG_FLAG_ALL,
                    "More than one nexthop received for inet route " <<
                    inet_prefix.ToString());
                return false;
            }

            NextHopListType::const_iterator nit = inh_list.begin();

            IpAddress nhop_address(Ip4Address(0));
            if (!XmppDecodeAddress(nit->af, nit->address, &nhop_address)) {
                BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                    BGP_LOG_FLAG_ALL,
                    "Bad nexthop address " << nit->address <<
                    " for inet route " << inet_prefix.ToString());
                return false;
            }

            if (family == Address::EVPN) {
                if (nit->vni > 0xFFFFFF) {
                    BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                        BGP_LOG_FLAG_ALL,
                        "Bad label " << nit->vni <<
                        " for inet route " << inet_prefix.ToString());
                    return false;
                }
                if (!nit->vni)
                    continue;
                if (nit->mac.empty())
                    continue;

                MacAddress mac_addr =
                    MacAddress::FromString(nit->mac, &error);
                if (error) {
                    BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                        BGP_LOG_FLAG_ALL,
                        "Bad next-hop mac address " << nit->mac);
                    return false;
                }
                RouterMac router_mac(mac_addr);
                ext.communities.push_back(router_mac.GetExtCommunityValue());
            } else {
                if (nit->label > 0xFFFFF || (master && nit->label)) {
                    BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                        BGP_LOG_FLAG_ALL,
                        "Bad label " << nit->label <<
                        " for inet route " << inet_prefix.ToString());
                    return false;
                }
                if (!master && !nit->label)
                    continue;
            }

            nh_address = nhop_address;
            if (family == Address::INET) {
                label = nit->label;
            } else {
                label = nit->vni;
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
                if (family == Address::INET &&
                    tun_encap.tunnel_encap() == TunnelEncapType::VXLAN) {
                    continue;
                }
                if (family == Address::EVPN &&
                    tun_encap.tunnel_encap() != TunnelEncapType::VXLAN) {
                    continue;
                }
                no_valid_tunnel_encap = false;
                ext.communities.push_back(tun_encap.GetExtCommunityValue());
            }

            // Mark the path as infeasible if all tunnel encaps published
            // by agent are invalid.
            if (!no_tunnel_encap && no_valid_tunnel_encap && !master) {
                flags = BgpPath::NoTunnelEncap;
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

            // Process community tags.
            const CommunityTagListType &ict_list =
                item.entry.community_tag_list;
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
            if (!master)
                attrs.push_back(&source_rd);

            // Process security group list.
            const SecurityGroupListType &isg_list =
                item.entry.security_group_list;
            for (SecurityGroupListType::const_iterator sit = isg_list.begin();
                sit != isg_list.end(); ++sit) {
                SecurityGroup sg(bgp_server_->autonomous_system(), *sit);
                ext.communities.push_back(sg.GetExtCommunityValue());
            }

            if (item.entry.mobility.seqno) {
                MacMobility mm(item.entry.mobility.seqno,
                               item.entry.mobility.sticky);
                ext.communities.push_back(mm.GetExtCommunityValue());
            } else if (item.entry.sequence_number) {
                MacMobility mm(item.entry.sequence_number);
                ext.communities.push_back(mm.GetExtCommunityValue());
            }

            // Process load-balance extended community.
            LoadBalance load_balance(item.entry.load_balance);
            if (!load_balance.IsDefault())
                ext.communities.push_back(load_balance.GetExtCommunityValue());

            if (!comm.communities.empty())
                attrs.push_back(&comm);
            if (!master && !ext.communities.empty())
                attrs.push_back(&ext);

            BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attrs);
            req.data.reset(new BgpTable::RequestData(
                attr, flags, label, 0, subscription_gen_id));
        } else {
            req.oper = DBRequest::DB_ENTRY_DELETE;
        }

        // Defer all requests till subscribe is processed.
        if (subscribe_pending) {
            DBRequest *request_entry = new DBRequest();
            request_entry->Swap(&req);
            string table_name =
                RoutingInstance::GetTableName(vrf_name, family);
            defer_q_.insert(make_pair(
                make_pair(vrf_name, table_name), request_entry));
            continue;
        }

        assert(table);
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            "Inet route " << item.entry.nlri.address <<
            " with next-hop " << nh_address << " and label " << label <<
            " enqueued for " << (add_change ? "add/change" : "delete") <<
            " to table " << table->name());
        table->Enqueue(&req);
    }

    if (add_change) {
        stats_[RX].reach++;
    } else {
        stats_[RX].unreach++;
    }

    return true;
}

bool BgpXmppChannel::ProcessInet6Item(string vrf_name,
    const pugi::xml_node &node, bool add_change) {
    ItemType item;
    item.Clear();

    if (!item.XmlParse(node)) {
        error_stats().incr_inet6_rx_bad_xml_token_count();
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Invalid inet6 route message received");
        return false;
    }

    if (item.entry.nlri.af != BgpAf::IPv6) {
        error_stats().incr_inet6_rx_bad_afi_safi_count();
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Unsupported address family " << item.entry.nlri.af <<
            " for inet6 route " << item.entry.nlri.address);
        return false;
    }

    if (item.entry.nlri.safi != BgpAf::Unicast) {
        error_stats().incr_inet6_rx_bad_afi_safi_count();
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Unsupported subsequent address family " << item.entry.nlri.safi <<
            " for inet6 route " << item.entry.nlri.address);
        return false;
    }

    error_code error;
    Inet6Prefix inet6_prefix =
        Inet6Prefix::FromString(item.entry.nlri.address, &error);
    if (error) {
        error_stats().incr_inet6_rx_bad_prefix_count();
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Bad inet6 route " << item.entry.nlri.address);
        return false;
    }

    if (add_change && item.entry.next_hops.next_hop.empty()) {
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Missing next-hops for inet6 route " << inet6_prefix.ToString());
        return false;
    }

    // Rules for routes in master instance:
    // - Label must be 0
    // - Tunnel encapsulation is not required
    // - Do not add SourceRd and ExtCommunitySpec
    bool master = (vrf_name == BgpConfigManager::kMasterInstance);

    // vector<Address::Family> family_list = list_of(Address::INET6)(Address::EVPN);
    vector<Address::Family> family_list = list_of(Address::INET6);
    BOOST_FOREACH(Address::Family family, family_list) {
        bool subscribe_pending;
        int instance_id;
        uint64_t subscription_gen_id;
        BgpTable *table;
        if (!VerifyMembership(vrf_name, family, &table, &instance_id,
            &subscription_gen_id, &subscribe_pending, add_change)) {
            channel_->Close();
            return false;
        }

        DBRequest req;
        if (family == Address::INET6) {
            req.key.reset(new Inet6Table::RequestKey(inet6_prefix, peer_.get()));
        } else {
            EvpnPrefix evpn_prefix(RouteDistinguisher::kZeroRd, 0,
                inet6_prefix.addr(), inet6_prefix.prefixlen());
            req.key.reset(new EvpnTable::RequestKey(evpn_prefix, peer_.get()));
        }

        IpAddress nh_address(Ip4Address(0));
        uint32_t label = 0;
        uint32_t flags = 0;
        ExtCommunitySpec ext;
        CommunitySpec comm;

        if (add_change) {
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            BgpAttrSpec attrs;

            const NextHopListType &inh_list = item.entry.next_hops;

            // Agents should send only one next-hop in the item.
            if (inh_list.next_hop.size() != 1) {
                BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                    BGP_LOG_FLAG_ALL,
                    "More than one nexthop received for inet6 route " <<
                    inet6_prefix.ToString());
                return false;
            }

            NextHopListType::const_iterator nit = inh_list.begin();

            IpAddress nhop_address(Ip4Address(0));
            if (!XmppDecodeAddress(nit->af, nit->address, &nhop_address)) {
                error_stats().incr_inet6_rx_bad_nexthop_count();
                BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                    BGP_LOG_FLAG_ALL,
                    "Bad nexthop address " << nit->address <<
                    " for inet6 route " << inet6_prefix.ToString());
                return false;
            }

            if (family == Address::EVPN) {
                if (nit->vni > 0xFFFFFF) {
                    BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                        BGP_LOG_FLAG_ALL,
                        "Bad label " << nit->vni <<
                        " for inet6 route " << inet6_prefix.ToString());
                    return false;
                }
                if (!nit->vni)
                    continue;
                if (nit->mac.empty())
                    continue;

                MacAddress mac_addr =
                    MacAddress::FromString(nit->mac, &error);
                if (error) {
                    BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                        BGP_LOG_FLAG_ALL,
                        "Bad next-hop mac address " << nit->mac);
                    return false;
                }
                RouterMac router_mac(mac_addr);
                ext.communities.push_back(router_mac.GetExtCommunityValue());
            } else {
                if (nit->label > 0xFFFFF || (master && nit->label)) {
                    BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                        BGP_LOG_FLAG_ALL,
                        "Bad label " << nit->label <<
                        " for inet6 route " << inet6_prefix.ToString());
                    return false;
                }
                if (!master && !nit->label)
                    continue;
            }

            nh_address = nhop_address;
            if (family == Address::INET6) {
                label = nit->label;
            } else {
                label = nit->vni;
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
                if (family == Address::INET6 &&
                    tun_encap.tunnel_encap() == TunnelEncapType::VXLAN) {
                    continue;
                }
                if (family == Address::EVPN &&
                    tun_encap.tunnel_encap() != TunnelEncapType::VXLAN) {
                    continue;
                }
                no_valid_tunnel_encap = false;
                ext.communities.push_back(tun_encap.GetExtCommunityValue());
            }

            // Mark the path as infeasible if all tunnel encaps published
            // by agent are invalid.
            if (!no_tunnel_encap && no_valid_tunnel_encap && !master) {
                flags = BgpPath::NoTunnelEncap;
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

            // Process community tags.
            const CommunityTagListType &ict_list =
                item.entry.community_tag_list;
            for (CommunityTagListType::const_iterator cit = ict_list.begin();
                cit != ict_list.end(); ++cit) {
                error_code error;
                uint32_t rt_community =
                    CommunityType::CommunityFromString(*cit, &error);
                if (error)
                    continue;
                comm.communities.push_back(rt_community);
            }

            BgpAttrNextHop nexthop(nh_address);
            attrs.push_back(&nexthop);

            BgpAttrSourceRd source_rd;
            if (!master) {
                uint32_t addr = nh_address.to_v4().to_ulong();
                source_rd =
                    BgpAttrSourceRd(RouteDistinguisher(addr, instance_id));
                attrs.push_back(&source_rd);
            }

            // Process security group list.
            const SecurityGroupListType &isg_list =
                item.entry.security_group_list;
            for (SecurityGroupListType::const_iterator sit = isg_list.begin();
                sit != isg_list.end(); ++sit) {
                SecurityGroup sg(bgp_server_->autonomous_system(), *sit);
                ext.communities.push_back(sg.GetExtCommunityValue());
            }

            if (item.entry.mobility.seqno) {
                MacMobility mm(item.entry.mobility.seqno,
                    item.entry.mobility.sticky);
                ext.communities.push_back(mm.GetExtCommunityValue());
            } else if (item.entry.sequence_number) {
                MacMobility mm(item.entry.sequence_number);
                ext.communities.push_back(mm.GetExtCommunityValue());
            }

            // Process load-balance extended community.
            LoadBalance load_balance(item.entry.load_balance);
            if (!load_balance.IsDefault())
                ext.communities.push_back(load_balance.GetExtCommunityValue());

            if (!comm.communities.empty())
                attrs.push_back(&comm);
            if (!master && !ext.communities.empty())
                attrs.push_back(&ext);

            BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attrs);
            req.data.reset(new BgpTable::RequestData(
                attr, flags, label, 0, subscription_gen_id));
        } else {
            req.oper = DBRequest::DB_ENTRY_DELETE;
        }

        // Defer all requests till subscribe is processed.
        if (subscribe_pending) {
            DBRequest *request_entry = new DBRequest();
            request_entry->Swap(&req);
            string table_name =
                RoutingInstance::GetTableName(vrf_name, family);
            defer_q_.insert(make_pair(
                make_pair(vrf_name, table_name), request_entry));
            continue;
        }

        assert(table);
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            "Inet6 route " << item.entry.nlri.address <<
            " with next-hop " << nh_address << " and label " << label <<
            " enqueued for " << (add_change ? "add/change" : "delete") <<
            " to table " << table->name());
        table->Enqueue(&req);
    }

    if (add_change) {
        stats_[RX].reach++;
    } else {
        stats_[RX].unreach++;
    }

    return true;
}

bool BgpXmppChannel::ProcessEnetItem(string vrf_name,
    const pugi::xml_node &node, bool add_change) {
    EnetItemType item;
    item.Clear();

    if (!item.XmlParse(node)) {
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Invalid enet route message received");
        return false;
    }

    if (item.entry.nlri.af != BgpAf::L2Vpn) {
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Unsupported address family " << item.entry.nlri.af <<
            " for enet route " << item.entry.nlri.address);
        return false;
    }

    if (item.entry.nlri.safi != BgpAf::Enet) {
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Unsupported subsequent address family " << item.entry.nlri.safi <<
            " for enet route " << item.entry.nlri.mac);
        return false;
    }

    error_code error;
    MacAddress mac_addr = MacAddress::FromString(item.entry.nlri.mac, &error);

    if (error) {
        BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
            "Bad mac address " << item.entry.nlri.mac);
        return false;
    }

    Ip4Prefix inet_prefix;
    Inet6Prefix inet6_prefix;
    IpAddress ip_addr;
    if (!item.entry.nlri.address.empty()) {
        size_t pos = item.entry.nlri.address.find('/');
        if (pos == string::npos) {
            BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
                "Missing / in address " << item.entry.nlri.address);
            return false;
        }

        string plen_str = item.entry.nlri.address.substr(pos + 1);
        if (plen_str == "32") {
            inet_prefix =
                Ip4Prefix::FromString(item.entry.nlri.address, &error);
            if (error || inet_prefix.prefixlen() != 32) {
                BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                    BGP_LOG_FLAG_ALL,
                    "Bad inet address " << item.entry.nlri.address);
                return false;
            }
            ip_addr = inet_prefix.ip4_addr();
        } else if (plen_str == "128") {
            inet6_prefix =
                Inet6Prefix::FromString(item.entry.nlri.address, &error);
            if (error || inet6_prefix.prefixlen() != 128) {
                BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                    BGP_LOG_FLAG_ALL,
                    "Bad inet6 address " << item.entry.nlri.address);
                return false;
            }
            ip_addr = inet6_prefix.ip6_addr();
        } else if (item.entry.nlri.address != "0.0.0.0/0") {
            BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name, BGP_LOG_FLAG_ALL,
                "Bad prefix length in address " <<
                item.entry.nlri.address);
            return false;
        }
    }

    bool subscribe_pending;
    int instance_id;
    uint64_t subscription_gen_id;
    BgpTable *table;
    if (!VerifyMembership(vrf_name, Address::EVPN, &table, &instance_id,
        &subscription_gen_id, &subscribe_pending, add_change)) {
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

    DBRequest req;
    ExtCommunitySpec ext;
    req.key.reset(new EvpnTable::RequestKey(evpn_prefix, peer_.get()));

    IpAddress nh_address(Ip4Address(0));
    uint32_t label = 0;
    uint32_t l3_label = 0;
    uint32_t flags = 0;
    bool label_is_vni = false;

    if (add_change) {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        BgpAttrSpec attrs;
        const EnetNextHopListType &inh_list = item.entry.next_hops;

        if (inh_list.next_hop.empty()) {
            BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                BGP_LOG_FLAG_ALL, "Missing next-hops for enet route " <<
                                  evpn_prefix.ToXmppIdString());
            return false;
        }

        // Agents should send only one next-hop in the item.
        if (inh_list.next_hop.size() != 1) {
            BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                BGP_LOG_FLAG_ALL,
                "More than one nexthop received for enet route " <<
                evpn_prefix.ToXmppIdString());
            return false;
        }

        EnetNextHopListType::const_iterator nit = inh_list.begin();

        IpAddress nhop_address(Ip4Address(0));

        if (!XmppDecodeAddress(nit->af, nit->address, &nhop_address)) {
            BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                BGP_LOG_FLAG_ALL, "Bad nexthop address " << nit->address <<
                " for enet route " << evpn_prefix.ToXmppIdString());
            return false;
        }

        nh_address = nhop_address;
        label = nit->label;
        l3_label = nit->l3_label;
        if (!nit->mac.empty()) {
            MacAddress rmac_addr =
                MacAddress::FromString(nit->mac, &error);
            if (error) {
                BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                    BGP_LOG_FLAG_ALL,
                    "Bad next-hop mac address " << nit->mac <<
                    " for enet route " << evpn_prefix.ToXmppIdString());
                return false;
            }
            RouterMac router_mac(rmac_addr);
            ext.communities.push_back(router_mac.GetExtCommunityValue());
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
            ext.communities.push_back(tun_encap.GetExtCommunityValue());
            if (tun_encap.tunnel_encap() == TunnelEncapType::GRE) {
                TunnelEncap alt_tun_encap(TunnelEncapType::MPLS_O_GRE);
                ext.communities.push_back(alt_tun_encap.GetExtCommunityValue());
            }
        }

        // Mark the path as infeasible if all tunnel encaps published
        // by agent are invalid.
        if (!no_tunnel_encap && no_valid_tunnel_encap) {
            flags = BgpPath::NoTunnelEncap;
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

        if (item.entry.mobility.seqno) {
            MacMobility mm(item.entry.mobility.seqno,
                item.entry.mobility.sticky);
            ext.communities.push_back(mm.GetExtCommunityValue());
        } else if (item.entry.sequence_number) {
            MacMobility mm(item.entry.sequence_number);
            ext.communities.push_back(mm.GetExtCommunityValue());
        }

        ETree etree(item.entry.etree_leaf);
        ext.communities.push_back(etree.GetExtCommunityValue());

        if (!ext.communities.empty())
            attrs.push_back(&ext);

        PmsiTunnelSpec pmsi_spec;
        if (mac_addr.IsBroadcast()) {
            if (!item.entry.replicator_address.empty()) {
                IpAddress replicator_address;
                if (!XmppDecodeAddress(BgpAf::IPv4,
                    item.entry.replicator_address, &replicator_address)) {
                    BGP_LOG_PEER_INSTANCE_WARNING(Peer(), vrf_name,
                        BGP_LOG_FLAG_ALL,
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

        req.data.reset(new EvpnTable::RequestData(
            attr, flags, label, l3_label, subscription_gen_id));
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
            RoutingInstance::GetTableName(vrf_name, Address::EVPN);
        defer_q_.insert(make_pair(
            make_pair(vrf_name, table_name), request_entry));
        return true;
    }

    assert(table);
    BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
        "Enet route " << evpn_prefix.ToXmppIdString() <<
        " with next-hop " << nh_address <<
        " label " << label << " l3-label " << l3_label <<
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

    BgpMembershipManager *mgr = bgp_server_->membership_mgr();
    if (mgr) {
        int instance_id = -1;
        uint64_t subscription_gen_id = 0;
        bool is_registered = mgr->GetRegistrationInfo(peer_.get(), table,
                                            &instance_id, &subscription_gen_id);
        if (!is_registered) {
            BGP_LOG_PEER_WARNING(Membership, Peer(),
                BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                "Not subscribed to table " << table->name());
            return;
        }
        if (ptr->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
            ((BgpTable::RequestData *)ptr->data.get())
                ->set_subscription_gen_id(subscription_gen_id);
        }
    }

    table->Enqueue(ptr.get());
}

bool BgpXmppChannel::ResumeClose() {
    peer_->Close(true);
    return true;
}

void BgpXmppChannel::RegisterTable(int line, BgpTable *table,
    const TableMembershipRequestState *tmr_state) {
    // Defer if Membership manager is in use (by close manager).
    if (close_manager_->IsMembershipInUse()) {
        BGP_LOG_PEER_TABLE(Peer(), SandeshLevel::SYS_DEBUG,
                           BGP_LOG_FLAG_ALL, table, "RegisterTable deferred "
                           "from :" << line);
        return;
    }

    BgpMembershipManager *mgr = bgp_server_->membership_mgr();
    BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG,
                 BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                 "Subscribe to table " << table->name() <<
                 (tmr_state->no_ribout ? " (no ribout)" : "") <<
                 " with id " << tmr_state->instance_id);
    if (tmr_state->no_ribout) {
        mgr->RegisterRibIn(peer_.get(), table);
        mgr->SetRegistrationInfo(peer_.get(), table, tmr_state->instance_id,
            manager_->get_subscription_gen_id());
        channel_stats_.table_subscribe++;
        MembershipRequestCallback(table);
    } else {
        mgr->Register(peer_.get(), table, bgp_policy_, tmr_state->instance_id);
        channel_stats_.table_subscribe++;
    }

    // If EndOfRib Send timer is running, cancel it and reschedule it after all
    // outstanding membership registrations are complete.
    if (eor_send_timer_->running())
        eor_send_timer_->Cancel();
}

void BgpXmppChannel::UnregisterTable(int line, BgpTable *table) {
    // Defer if Membership manager is in use (by close manager).
    if (close_manager_->IsMembershipInUse()) {
        BGP_LOG_PEER_TABLE(Peer(), SandeshLevel::SYS_DEBUG,
                           BGP_LOG_FLAG_ALL, table, "UnregisterTable deferred "
                           "from :" << line);
        return;
    }

    BgpMembershipManager *mgr = bgp_server_->membership_mgr();
    BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG,
                 BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                 "Unsubscribe to table " << table->name());
    mgr->Unregister(peer_.get(), table);
    channel_stats_.table_unsubscribe++;
}

#define RegisterTable(table, tmr_state) \
    RegisterTable(__LINE__, table, tmr_state)
#define UnregisterTable(table) UnregisterTable(__LINE__, table)

// Process all pending membership requests of various tables.
void BgpXmppChannel::ProcessPendingSubscriptions() {
    assert(!close_manager_->IsMembershipInUse());
    BOOST_FOREACH(TableMembershipRequestMap::value_type &entry,
                  table_membership_request_map_) {
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(entry.first));
        const TableMembershipRequestState &tmr_state = entry.second;
        if (tmr_state.current_req == SUBSCRIBE) {
            RegisterTable(table, &tmr_state);
        } else {
            assert(tmr_state.current_req == UNSUBSCRIBE);
            UnregisterTable(table);
        }
    }
}

size_t BgpXmppChannel::table_membership_requests() const {
    return table_membership_request_map_.size();
}

bool BgpXmppChannel::MembershipResponseHandler(string table_name) {
    if (close_manager_->IsMembershipInUse()) {
        close_manager_->MembershipRequestCallback();
        return true;
    }

    TableMembershipRequestState *tmr_state =
        GetTableMembershipState(table_name);
    if (!tmr_state) {
        BGP_LOG_PEER_INSTANCE_CRITICAL(Peer(), table_name,
                     BGP_PEER_DIR_IN, BGP_LOG_FLAG_ALL,
                     "Table not in subscribe/unsubscribe request queue");
        assert(false);
    }

    if (tmr_state->current_req == SUBSCRIBE) {
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
        DeleteTableMembershipState(table_name);
        if (table_membership_requests())
            return true;
        defer_peer_close_ = false;
        ResumeClose();
    } else {
        ProcessMembershipResponse(table_name, tmr_state);
    }

    assert(channel_stats_.table_subscribe_complete <=
               channel_stats_.table_subscribe);
    assert(channel_stats_.table_unsubscribe_complete <=
               channel_stats_.table_unsubscribe);

    // Restart EndOfRib send if necessary.
    ResetEndOfRibSendState();

    // If Close manager is waiting to use membership, try now.
    if (close_manager_->IsMembershipInWait())
        close_manager_->MembershipRequest();

    return true;
}

bool BgpXmppChannel::ProcessMembershipResponse(string table_name,
    TableMembershipRequestState *tmr_state) {
    BgpTable *table = static_cast<BgpTable *>
        (bgp_server_->database()->FindTable(table_name));
    if (!table) {
        DeleteTableMembershipState(table_name);
        return true;
    }
    BgpMembershipManager *mgr = bgp_server_->membership_mgr();

    if ((tmr_state->current_req == UNSUBSCRIBE) &&
        (tmr_state->pending_req == SUBSCRIBE)) {
        // Process pending subscribe now that unsubscribe has completed.
        tmr_state->current_req = SUBSCRIBE;
        RegisterTable(table, tmr_state);
        return true;
    } else if ((tmr_state->current_req == SUBSCRIBE) &&
               (tmr_state->pending_req == UNSUBSCRIBE)) {
        // Process pending unsubscribe now that subscribe has completed.
        tmr_state->current_req = UNSUBSCRIBE;
        UnregisterTable(table);
        return true;
    } else if ((tmr_state->current_req == SUBSCRIBE) &&
        (tmr_state->pending_req == SUBSCRIBE) &&
        (mgr->IsRibOutRegistered(peer_.get(), table) == tmr_state->no_ribout)) {
        // Trigger an unsubscribe so that we can subsequently subscribe with
        // the updated value of no_ribout.
        tmr_state->current_req = UNSUBSCRIBE;
        UnregisterTable(table);
        return true;
    }

    string vrf_name = table->routing_instance()->name();
    VrfTableName vrf_n_table = make_pair(vrf_name, table->name());

    if (tmr_state->pending_req == UNSUBSCRIBE) {
        if (!GetInstanceMembershipState(vrf_name))
            assert(defer_q_.count(vrf_n_table) == 0);
        DeleteTableMembershipState(table_name);
        return true;
    } else if (tmr_state->pending_req == SUBSCRIBE) {
        mgr->SetRegistrationInfo(peer_.get(), table, tmr_state->instance_id,
            manager_->get_subscription_gen_id());
        DeleteTableMembershipState(table_name);
    }

    for (DeferQ::iterator it = defer_q_.find(vrf_n_table);
         it != defer_q_.end() && it->first.second == table->name(); ++it) {
        DequeueRequest(table->name(), it->second);
    }

    // Erase all elements for the table
    defer_q_.erase(vrf_n_table);

    return true;
}

void BgpXmppChannel::MembershipRequestCallback(BgpTable *table) {
    membership_response_worker_.Enqueue(table->name());
}

void BgpXmppChannel::FillCloseInfo(BgpNeighborResp *resp) const {
    close_manager_->FillCloseInfo(resp);
}

void BgpXmppChannel::FillInstanceMembershipInfo(BgpNeighborResp *resp) const {
    vector<BgpNeighborRoutingInstance> instance_list;
    BOOST_FOREACH(const SubscribedRoutingInstanceList::value_type &entry,
        routing_instances_) {
        BgpNeighborRoutingInstance instance;
        instance.set_name(entry.first->name());
        if (entry.second.IsLlgrStale()) {
            instance.set_state("subscribed-llgr-stale");
        } else if (entry.second.IsGrStale()) {
            instance.set_state("subscribed-gr-stale");
        } else {
            instance.set_state("subscribed");
        }
        instance.set_index(entry.second.index);
        rtarget_manager_->FillInfo(&instance, entry.second.targets);
        instance_list.push_back(instance);
    }
    BOOST_FOREACH(const InstanceMembershipRequestMap::value_type &entry,
        instance_membership_request_map_) {
        const InstanceMembershipRequestState &imr_state = entry.second;
        BgpNeighborRoutingInstance instance;
        instance.set_name(entry.first);
        instance.set_state("pending");
        instance.set_index(imr_state.instance_id);
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
        if (!GetTableMembershipState(table.get_name()))
            new_table_list.push_back(table);
    }

    BOOST_FOREACH(const TableMembershipRequestMap::value_type &entry,
        table_membership_request_map_) {
        BgpNeighborRoutingTable table;
        table.set_name(entry.first);
        if (old_table_set.find(entry.first) != old_table_set.end())
            table.set_current_state("subscribed");
        const TableMembershipRequestState &tmr_state = entry.second;
        if (tmr_state.current_req == SUBSCRIBE) {
            table.set_current_request("subscribe");
        } else {
            table.set_current_request("unsubscribe");
        }
        if (tmr_state.pending_req == SUBSCRIBE) {
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

// Mark all current subscriptions as 'stale'. This is called when peer close
// process is initiated by BgpXmppChannel via PeerCloseManager.
void BgpXmppChannel::StaleCurrentSubscriptions() {
    CHECK_CONCURRENCY(peer_close_->GetTaskName());
    BOOST_FOREACH(SubscribedRoutingInstanceList::value_type &entry,
                  routing_instances_) {
        entry.second.SetGrStale();
        rtarget_manager_->UpdateRouteTargetRouteFlag(entry.first,
                entry.second.targets, BgpPath::Stale);
    }
}

// Mark all current subscriptions as 'llgr_stale'.
void BgpXmppChannel::LlgrStaleCurrentSubscriptions() {
    CHECK_CONCURRENCY(peer_close_->GetTaskName());
    BOOST_FOREACH(SubscribedRoutingInstanceList::value_type &entry,
                  routing_instances_) {
        assert(entry.second.IsGrStale());
        entry.second.SetLlgrStale();
        rtarget_manager_->UpdateRouteTargetRouteFlag(entry.first,
                entry.second.targets, BgpPath::Stale | BgpPath::LlgrStale);
    }
}

// Sweep all current subscriptions which are still marked as 'stale'.
void BgpXmppChannel::SweepCurrentSubscriptions() {
    CHECK_CONCURRENCY(peer_close_->GetTaskName());
    for (SubscribedRoutingInstanceList::iterator i = routing_instances_.begin();
            i != routing_instances_.end();) {
        if (i->second.IsGrStale()) {
            string name = i->first->name();

            // Increment the iterator first as we expect the entry to be
            // soon removed.
            i++;
            BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG,
                         BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                         "Instance subscription " << name <<
                         " is still stale and hence unsubscribed");
            ProcessSubscriptionRequest(name, NULL, false);
        } else {
            i++;
        }
    }
}

// Clear staled subscription state as new subscription has been received.
void BgpXmppChannel::ClearStaledSubscription(RoutingInstance *rt_instance,
        SubscriptionState *sub_state) {
    if (!sub_state->IsGrStale())
        return;

    BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG,
                 BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                 "Instance subscription " << rt_instance->name() <<
                 " stale flag is cleared");
    sub_state->ClearStale();
    rtarget_manager_->Stale(sub_state->targets);
}

void BgpXmppChannel::AddSubscriptionState(RoutingInstance *rt_instance,
        int index) {
    SubscriptionState state(rt_instance->GetImportList(), index);
    pair<SubscribedRoutingInstanceList::iterator, bool> ret =
        routing_instances_.insert(pair<RoutingInstance *, SubscriptionState> (
                                      rt_instance, state));

    // During GR, we expect duplicate subscription requests. Clear stale
    // state, as agent did re-subscribe after restart.
    if (!ret.second) {
        ClearStaledSubscription(rt_instance, &ret.first->second);
    } else {
        rtarget_manager_->PublishRTargetRoute(rt_instance, true);
    }
}

void BgpXmppChannel::DeleteSubscriptionState(RoutingInstance *rt_instance) {
    routing_instances_.erase(rt_instance);
}

BgpXmppChannel::SubscriptionState *BgpXmppChannel::GetSubscriptionState(
    RoutingInstance *rt_instance) {
    SubscribedRoutingInstanceList::iterator loc =
        routing_instances_.find(rt_instance);
    return (loc != routing_instances_.end() ? &loc->second : NULL);
}

const BgpXmppChannel::SubscriptionState *BgpXmppChannel::GetSubscriptionState(
    RoutingInstance *rt_instance) const {
    SubscribedRoutingInstanceList::const_iterator loc =
        routing_instances_.find(rt_instance);
    return (loc != routing_instances_.end() ? &loc->second : NULL);
}

void BgpXmppChannel::ProcessDeferredSubscribeRequest(RoutingInstance *instance,
    const InstanceMembershipRequestState &imr_state) {
    int instance_id = imr_state.instance_id;
    bool no_ribout = imr_state.no_ribout;
    AddSubscriptionState(instance, instance_id);
    RoutingInstance::RouteTableList const rt_list = instance->GetTables();
    for (RoutingInstance::RouteTableList::const_iterator it = rt_list.begin();
         it != rt_list.end(); ++it) {
        BgpTable *table = it->second;
        if (table->IsVpnTable() || table->family() == Address::RTARGET)
            continue;

        TableMembershipRequestState tmr_state(
            SUBSCRIBE, instance_id, no_ribout);
        AddTableMembershipState(table->name(), tmr_state);
        RegisterTable(table, &tmr_state);
    }
}

void BgpXmppChannel::ProcessSubscriptionRequest(
        string vrf_name, const XmppStanza::XmppMessageIq *iq,
        bool add_change) {
    int instance_id = -1;
    bool no_ribout = false;

    if (add_change) {
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(iq->dom.get());
        xml_node options = pugi->FindNode("options");
        for (xml_node node = options.first_child(); node;
             node = node.next_sibling()) {
            if (strcmp(node.name(), "instance-id") == 0) {
                instance_id = node.text().as_int();
            }
            if (strcmp(node.name(), "no-ribout") == 0) {
                no_ribout = node.text().as_bool();
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
            if (GetInstanceMembershipState(vrf_name)) {
                BGP_LOG_PEER_WARNING(Membership, Peer(),
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Duplicate subscribe for routing instance " <<
                             vrf_name << ", triggering close");
                channel_->Close();
            } else {
                AddInstanceMembershipState(vrf_name,
                    InstanceMembershipRequestState(instance_id, no_ribout));
                channel_stats_.instance_subscribe++;
            }
        } else {
            if (DeleteInstanceMembershipState(vrf_name)) {
                FlushDeferQ(vrf_name);
                channel_stats_.instance_unsubscribe++;
            } else {
                BGP_LOG_PEER_WARNING(Membership, Peer(),
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
            if (GetInstanceMembershipState(vrf_name)) {
                BGP_LOG_PEER_WARNING(Membership, Peer(),
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Duplicate subscribe for routing instance " <<
                             vrf_name << ", triggering close");
                channel_->Close();
            } else if (GetSubscriptionState(rt_instance)) {
                BGP_LOG_PEER_WARNING(Membership, Peer(),
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Duplicate subscribe for routing instance " <<
                             vrf_name << ", triggering close");
                channel_->Close();
            } else {
                AddInstanceMembershipState(vrf_name,
                    InstanceMembershipRequestState(instance_id, no_ribout));
                channel_stats_.instance_subscribe++;
            }
            return;
        } else {
            // If instance is being deleted and agent is trying to unsubscribe
            // we need to process the unsubscribe if vrf is not in the request
            // map.  This would be the normal case where we wait for agent to
            // unsubscribe in order to remove routes added by it.
            if (DeleteInstanceMembershipState(vrf_name)) {
                FlushDeferQ(vrf_name);
                channel_stats_.instance_unsubscribe++;
                return;
            } else if (!GetSubscriptionState(rt_instance)) {
                BGP_LOG_PEER_WARNING(Membership, Peer(),
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
            if (GetSubscriptionState(rt_instance)) {
                if (!close_manager_->IsCloseInProgress()) {
                    BGP_LOG_PEER_WARNING(Membership, Peer(),
                                 BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                                 "Duplicate subscribe for routing instance " <<
                                 vrf_name << ", triggering close");
                    channel_->Close();
                    return;
                }
            }
            channel_stats_.instance_subscribe++;
        } else {
            if (!GetSubscriptionState(rt_instance)) {
                BGP_LOG_PEER_WARNING(Membership, Peer(),
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Spurious unsubscribe for routing instance " <<
                             vrf_name << ", triggering close");
                channel_->Close();
                return;
            }
            channel_stats_.instance_unsubscribe++;
        }
    }

    if (add_change) {
        AddSubscriptionState(rt_instance, instance_id);
    } else  {
        rtarget_manager_->PublishRTargetRoute(rt_instance, false);
        DeleteSubscriptionState(rt_instance);
    }

    RoutingInstance::RouteTableList const rt_list = rt_instance->GetTables();
    for (RoutingInstance::RouteTableList::const_iterator it = rt_list.begin();
         it != rt_list.end(); ++it) {
        BgpTable *table = it->second;
        if (table->IsVpnTable() || table->family() == Address::RTARGET)
            continue;

        if (add_change) {
            TableMembershipRequestState *tmr_state =
                GetTableMembershipState(table->name());
            if (!tmr_state) {
                TableMembershipRequestState tmp_tmr_state(
                    SUBSCRIBE, instance_id, no_ribout);
                AddTableMembershipState(table->name(), tmp_tmr_state);
                RegisterTable(table, &tmp_tmr_state);
            } else {
                tmr_state->instance_id = instance_id;
                tmr_state->pending_req = SUBSCRIBE;
                tmr_state->no_ribout = no_ribout;
            }
        } else {
            if (defer_q_.count(make_pair(vrf_name, table->name()))) {
                BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG,
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Flush deferred route requests for table " <<
                             table->name() << " on unsubscribe");
                FlushDeferQ(vrf_name, table->name());
            }

            // Erase all elements for the table.

            TableMembershipRequestState *tmr_state =
                GetTableMembershipState(table->name());
            if (!tmr_state) {
                AddTableMembershipState(table->name(),
                    TableMembershipRequestState(
                        UNSUBSCRIBE, instance_id, no_ribout));
                UnregisterTable(table);
            } else {
                tmr_state->instance_id = -1;
                tmr_state->pending_req = UNSUBSCRIBE;
                tmr_state->no_ribout = false;
            }
        }
    }
}

void BgpXmppChannel::ClearEndOfRibState() {
    eor_receive_timer_->Cancel();
    eor_send_timer_->Cancel();
    eor_sent_ = false;
}

void BgpXmppChannel::ReceiveEndOfRIB(Address::Family family) {
    eor_receive_timer_->Cancel();
    close_manager_->ProcessEORMarkerReceived(family);
}

void BgpXmppChannel::EndOfRibTimerErrorHandler(string error_name,
                                               string error_message) {
    BGP_LOG_PEER_CRITICAL(Timer, Peer(), BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                 "Timer error: " << error_name << " " << error_message);
}

bool BgpXmppChannel::EndOfRibReceiveTimerExpired() {
    if (!peer_->IsReady())
        return false;

    uint32_t timeout = manager() && manager()->xmpp_server() ?
        manager()->xmpp_server()->GetEndOfRibReceiveTime() :
        BgpGlobalSystemConfig::kEndOfRibTime;

    // If max timeout has not reached yet, check if we can exit GR sooner by
    // looking at the activity in the channel.
    if (UTCTimestampUsec() - eor_receive_timer_start_time_
            < timeout * 1000000 * 10) {

        // If there is some send or receive activity in the channel in last few
        // seconds, delay EoR receive event.
        if (channel_->LastReceived(kEndOfRibSendRetryTimeMsecs * 3) ||
                channel_->LastSent(kEndOfRibSendRetryTimeMsecs * 3)) {
            eor_receive_timer_->Reschedule(kEndOfRibSendRetryTimeMsecs);
            BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_INFO,
                         BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                         "EndOfRib Receive timer rescheduled to fire after " <<
                         kEndOfRibSendRetryTimeMsecs << " milliseconds ");
            return true;
        }
    }

    BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
                 BGP_PEER_DIR_IN, "EndOfRib Receive timer expired");
    ReceiveEndOfRIB(Address::UNSPEC);
    return false;
}

bool BgpXmppChannel::EndOfRibSendTimerExpired() {
    if (!peer_->IsReady())
        return false;

    uint32_t timeout = manager() && manager()->xmpp_server() ?
        manager()->xmpp_server()->GetEndOfRibSendTime() :
        BgpGlobalSystemConfig::kEndOfRibTime;

    // If max timeout has not reached yet, check if we can exit GR sooner by
    // looking at the activity in the channel.
    if (UTCTimestampUsec() - eor_send_timer_start_time_ <
            timeout * 1000000 * 10) {

        // If there is some send or receive activity in the channel in last few
        // seconds, delay EoR send event.
        if (channel_->LastReceived(kEndOfRibSendRetryTimeMsecs * 3) ||
                channel_->LastSent(kEndOfRibSendRetryTimeMsecs * 3)) {
            eor_send_timer_->Reschedule(kEndOfRibSendRetryTimeMsecs);
            BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_INFO,
                         BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                         "EndOfRib Send timer rescheduled to fire after " <<
                         kEndOfRibSendRetryTimeMsecs << " milliseconds ");
            return true;
        }
    }

    SendEndOfRIB();
    return false;
}

void BgpXmppChannel::StartEndOfRibReceiveTimer() {
    uint32_t timeout = manager() && manager()->xmpp_server() ?
                           manager()->xmpp_server()->GetEndOfRibReceiveTime() :
                           BgpGlobalSystemConfig::kEndOfRibTime;
    eor_receive_timer_start_time_ = UTCTimestampUsec();
    eor_receive_timer_->Cancel();

    BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
        BGP_PEER_DIR_IN, "EndOfRib Receive timer scheduled to fire after " <<
        timeout * 1000 << " milliseconds");
    eor_receive_timer_->Start(timeout * 1000,
        boost::bind(&BgpXmppChannel::EndOfRibReceiveTimerExpired, this),
        boost::bind(&BgpXmppChannel::EndOfRibTimerErrorHandler, this, _1, _2));
}

void BgpXmppChannel::ResetEndOfRibSendState() {
    if (eor_sent_)
        return;

    // If socket is blocked, then wait for it to get unblocked first.
    if (!peer_->send_ready())
        return;

    // If there is any outstanding subscribe pending, wait for its completion.
    if (channel_stats_.table_subscribe_complete !=
            channel_stats_.table_subscribe)
        return;

    uint32_t timeout = manager() && manager()->xmpp_server() ?
                           manager()->xmpp_server()->GetEndOfRibSendTime() :
                           BgpGlobalSystemConfig::kEndOfRibTime;
    eor_send_timer_start_time_ = UTCTimestampUsec();
    eor_send_timer_->Cancel();

    BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
        BGP_PEER_DIR_OUT, "EndOfRib Send timer scheduled to fire after " <<
        timeout * 1000 << " milliseconds");
    eor_send_timer_->Start(timeout * 1000,
        boost::bind(&BgpXmppChannel::EndOfRibSendTimerExpired, this),
        boost::bind(&BgpXmppChannel::EndOfRibTimerErrorHandler, this, _1, _2));
}

/*
 * Empty items list constitute eor marker.
 */
void BgpXmppChannel::SendEndOfRIB() {
    eor_send_timer_->Cancel();
    eor_sent_ = true;

    string msg;
    msg += "\n<message from=\"";
    msg += XmppInit::kControlNodeJID;
    msg += "\" to=\"";
    msg += peer_->ToString();
    msg += "/";
    msg += XmppInit::kBgpPeer;
    msg += "\">";
    msg += "\n\t<event xmlns=\"http://jabber.org/protocol/pubsub\">";
    msg = (msg + "\n<items node=\"") + XmppInit::kEndOfRibMarker +
          "\"></items>";
    msg += "\n\t</event>\n</message>\n";

    if (channel_->connection())
        channel_->connection()->Send((const uint8_t *) msg.data(), msg.size());

    stats_[TX].end_of_rib++;
    BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
                 BGP_PEER_DIR_OUT, "EndOfRib marker sent");
}

void BgpXmppChannel::ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
    CHECK_CONCURRENCY("xmpp::StateMachine");

    // Bail if the connection is being deleted. It's not safe to assert
    // because the Delete method can be called from the main thread.
    if (channel_->connection() && channel_->connection()->IsDeleted())
        return;

    // Make sure that peer is not set for closure already.
    assert(!defer_peer_close_);
    assert(!peer_deleted());

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
                xml_node item = pugi->FindNode("item");

                // Empty items-list can be considered as EOR Marker for all afis
                if (item == 0) {
                    BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_INFO,
                                 BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                                 "EndOfRib marker received");
                    stats_[RX].end_of_rib++;
                    ReceiveEndOfRIB(Address::UNSPEC);
                    return;
                }
                for (; item; item = item.next_sibling()) {
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
    if (!channel->deleted()) {
        channel->set_deleted(true);
        delete channel;
    }
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
      dscp_listener_id_(-1) {
    // Initialize the gen id counter
    subscription_gen_id_ = 1;
    deleting_count_ = 0;

    if (xmpp_server)
        xmpp_server->CreateConfigUpdater(server->config_manager());
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
    dscp_listener_id_ =
        server->RegisterDSCPUpdateCallback(boost::bind(
            &BgpXmppChannelManager::DSCPUpdateCallback, this, _1));

    id_ = server->routing_instance_mgr()->RegisterInstanceOpCallback(
        boost::bind(&BgpXmppChannelManager::RoutingInstanceCallback,
                    this, _1, _2));
}

BgpXmppChannelManager::~BgpXmppChannelManager() {
    assert(channel_map_.empty());
    assert(channel_name_map_.empty());
    assert(deleting_count_ == 0);
    if (xmpp_server_) {
        xmpp_server_->UnRegisterConnectionEvent(xmps::BGP);
    }

    queue_.Shutdown();
    bgp_server_->UnregisterAdminDownCallback(admin_down_listener_id_);
    bgp_server_->UnregisterASNUpdateCallback(asn_listener_id_);
    bgp_server_->routing_instance_mgr()->UnregisterInstanceOpCallback(id_);
    bgp_server_->UnregisterDSCPUpdateCallback(dscp_listener_id_);
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

void BgpXmppChannelManager::DSCPUpdateCallback(uint8_t dscp_value) {
    xmpp_server_->SetDscpValue(dscp_value);
}

void BgpXmppChannelManager::ASNUpdateCallback(as_t old_asn,
    as_t old_local_asn) {
    CHECK_CONCURRENCY("bgp::Config");
    BOOST_FOREACH(XmppChannelMap::value_type &i, channel_map_) {
        i.second->rtarget_manager()->ASNUpdateCallback(old_asn,
                                                                old_local_asn);
    }
    if (bgp_server_->autonomous_system() != old_asn) {
        xmpp_server_->ClearAllConnections();
    }
}

void BgpXmppChannelManager::IdentifierUpdateCallback(
    Ip4Address old_identifier) {
    CHECK_CONCURRENCY("bgp::Config");
    BOOST_FOREACH(XmppChannelMap::value_type &i, channel_map_) {
        i.second->rtarget_manager()->IdentifierUpdateCallback(
                old_identifier);
    }
}

void BgpXmppChannelManager::RoutingInstanceCallback(string vrf_name, int op) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");
    BOOST_FOREACH(XmppChannelMap::value_type &i, channel_map_) {
        i.second->RoutingInstanceCallback(vrf_name, op);
    }
}

void BgpXmppChannelManager::VisitChannels(BgpXmppChannelManager::VisitorFn fn) {
    tbb::mutex::scoped_lock lock(mutex_);
    BOOST_FOREACH(XmppChannelMap::value_type &i, channel_map_) {
        fn(i.second);
    }
}

void BgpXmppChannelManager::VisitChannels(BgpXmppChannelManager::VisitorFn fn)
        const {
    tbb::mutex::scoped_lock lock(mutex_);
    BOOST_FOREACH(const XmppChannelMap::value_type &i, channel_map_) {
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
    if (channel->connection() && !channel->connection()->IsActiveChannel()) {
        CHECK_CONCURRENCY("bgp::Config");
    }
    channel_map_.erase(channel);
    channel_name_map_.erase(channel->ToString());
}

BgpXmppChannel *BgpXmppChannelManager::CreateChannel(XmppChannel *channel) {
    CHECK_CONCURRENCY("xmpp::StateMachine");
    BgpXmppChannel *ch = new BgpXmppChannel(channel, bgp_server_, this);

    return ch;
}

void BgpXmppChannelManager::XmppHandleChannelEvent(XmppChannel *channel,
                                                   xmps::PeerState state) {
    tbb::mutex::scoped_lock lock(mutex_);

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
            if (bgp_xmpp_channel->peer_deleted())
                return;
            channel->RegisterReceive(xmps::BGP,
                boost::bind(&BgpXmppChannel::ReceiveUpdate, bgp_xmpp_channel,
                            _1));
        }

        bgp_xmpp_channel->eor_sent_ = false;
        bgp_xmpp_channel->StartEndOfRibReceiveTimer();
        bgp_xmpp_channel->ResetEndOfRibSendState();
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
            BGP_LOG_NOTICE(BgpMessage, BGP_LOG_FLAG_ALL,
                    "Peer not found on channel not ready event");
        }
    }
}

void BgpXmppChannelManager::FillPeerInfo(const BgpXmppChannel *channel) const {
    PeerStatsInfo stats;
    PeerStats::FillPeerDebugStats(channel->Peer()->peer_stats(), &stats);

    XmppPeerInfoData peer_info;
    peer_info.set_name(channel->Peer()->ToUVEKey());
    peer_info.set_peer_stats_info(stats);
    XMPPPeerInfo::Send(peer_info);

    PeerStatsData peer_stats_data;
    peer_stats_data.set_name(channel->Peer()->ToUVEKey());
    peer_stats_data.set_encoding("XMPP");
    PeerStats::FillPeerUpdateStats(channel->Peer()->peer_stats(),
                                   &peer_stats_data);
    PeerStatsUve::Send(peer_stats_data, "ObjectXmppPeerInfo");
}

bool BgpXmppChannelManager::CollectStats(BgpRouterState *state, bool first)
         const {
    CHECK_CONCURRENCY("bgp::ShowCommand");

    VisitChannels(boost::bind(&BgpXmppChannelManager::FillPeerInfo, this, _1));
    bool change = false;
    uint32_t num_xmpp = count();
    if (first || num_xmpp != state->get_num_xmpp_peer()) {
        state->set_num_xmpp_peer(num_xmpp);
        change = true;
    }

    uint32_t num_up_xmpp = NumUpPeer();
    if (first || num_up_xmpp != state->get_num_up_xmpp_peer()) {
        state->set_num_up_xmpp_peer(num_up_xmpp);
        change = true;
    }

    uint32_t num_deleting_xmpp = deleting_count();
    if (first || num_deleting_xmpp != state->get_num_deleting_xmpp_peer()) {
        state->set_num_deleting_xmpp_peer(num_deleting_xmpp);
        change = true;
    }

    return change;
}

void BgpXmppChannel::Close() {
    instance_membership_request_map_.clear();
    STLDeleteElements(&defer_q_);

    if (table_membership_requests()) {
        BGP_LOG_PEER(Event, peer_.get(), SandeshLevel::SYS_INFO,
            BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA, "Close procedure deferred");
        defer_peer_close_ = true;
        return;
    }
    peer_->Close(true);
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
//
void BgpXmppChannel::set_peer_closed(bool flag) {
    peer_->SetPeerClosed(flag);
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
uint64_t BgpXmppChannel::peer_closed_at() const {
    return peer_->closed_at();
}

bool BgpXmppChannel::IsSubscriptionGrStale(RoutingInstance *instance) const {
    SubscribedRoutingInstanceList::const_iterator it =
        routing_instances_.find(instance);
    assert(it != routing_instances_.end());
    return it->second.IsGrStale();
}

bool BgpXmppChannel::IsSubscriptionLlgrStale(RoutingInstance *instance) const {
    SubscribedRoutingInstanceList::const_iterator it =
        routing_instances_.find(instance);
    assert(it != routing_instances_.end());
    return it->second.IsLlgrStale();
}

bool BgpXmppChannel::IsSubscriptionEmpty() const {
    return routing_instances_.empty();
}

const RoutingInstance::RouteTargetList &BgpXmppChannel::GetSubscribedRTargets(
        RoutingInstance *instance) const {
    SubscribedRoutingInstanceList::const_iterator it =
        routing_instances_.find(instance);
    assert(it != routing_instances_.end());
    return it->second.targets;
}
