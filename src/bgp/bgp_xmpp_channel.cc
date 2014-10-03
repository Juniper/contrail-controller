/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_xmpp_channel.h"

#include <sstream>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <pugixml/pugixml.hpp>

#include "base/label_block.h"
#include "base/logging.h"
#include "base/task_annotations.h"
#include "base/util.h"

#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_server.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_route.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/extended-community/mac_mobility.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/ipeer.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/rtarget/rtarget_prefix.h"
#include "bgp/rtarget/rtarget_table.h"
#include "bgp/scheduling_group.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"

#include "net/bgp_af.h"
#include "net/mac_address.h"

#include "schema/xmpp_unicast_types.h"
#include "schema/xmpp_multicast_types.h"
#include "schema/xmpp_enet_types.h"

#include "xml/xml_pugi.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/sandesh/xmpp_peer_info_types.h"

using pugi::xml_node;
using std::auto_ptr;
using std::string;
using std::vector;
using boost::system::error_code;

using namespace std;

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

int BgpXmppChannel::ErrorStats::get_inet6_rx_bad_xml_token_count() const {
    return inet6_rx_bad_xml_token_count;
}

int BgpXmppChannel::ErrorStats::get_inet6_rx_bad_prefix_count() const {
    return inet6_rx_bad_prefix_count;
}

int BgpXmppChannel::ErrorStats::get_inet6_rx_bad_nexthop_count() const {
    return inet6_rx_bad_nexthop_count;
}

int BgpXmppChannel::ErrorStats::get_inet6_rx_bad_afi_safi_count() const {
    return inet6_rx_bad_afi_safi_count;
}

BgpXmppChannel::Stats::Stats()
    : rt_updates(0), reach(0), unreach(0) {
}

class BgpXmppChannel::PeerClose : public IPeerClose {
public:
    PeerClose(BgpXmppChannel *channel)
       : parent_(channel),
         manager_(BgpObjectFactory::Create<PeerCloseManager>(channel->Peer())) {
    }
    virtual ~PeerClose() {
    }
    virtual std::string ToString() const {
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
        return static_cast<XmppServer *>(connection->server())->IsPeerCloseGraceful();
    }

    virtual void CustomClose() {
        if (parent_->rtarget_routes_.empty()) return;
        RoutingInstanceMgr *instance_mgr = 
            parent_->bgp_server_->routing_instance_mgr();
        RoutingInstance *master = 
            instance_mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
        assert(master);
        BgpTable *rtarget_table = master->GetTable(Address::RTARGET);
        assert(rtarget_table);

        for (PublishedRTargetRoutes::iterator
             it = parent_->rtarget_routes_.begin();
             it != parent_->rtarget_routes_.end(); it++) {
            parent_->RTargetRouteOp(rtarget_table,
                                    parent_->bgp_server_->autonomous_system(),
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

        // TODO: This needs to be cleaned up properly by clearly separting GR
        // entry and exit steps. Avoid duplicate channel deletions.
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
    std::auto_ptr<PeerCloseManager> manager_;
};

class BgpXmppChannel::PeerStats : public IPeerDebugStats {
public:
    explicit PeerStats(BgpXmppChannel *peer)
        : peer_(peer) {
    }

    // Printable name
    virtual std::string ToString() const {
        return peer_->ToString();
    }

    // Previous State of the peer
    virtual std::string last_state() const {
        return (peer_->channel_->LastStateName());
    }
    // Last state change occurred at
    virtual std::string last_state_change_at() const {
        return (peer_->channel_->LastStateChangeAt());
    }

    // Last error on this peer
    virtual std::string last_error() const {
        return "";
    }

    // Last Event on this peer
    virtual std::string last_event() const {
        return (peer_->channel_->LastEvent());
    }

    // When was the Last
    virtual std::string last_flap() const {
        return (peer_->channel_->LastFlap());
    }

    // Total number of flaps
    virtual uint32_t num_flaps() const {
        return (peer_->channel_->FlapCount());
    }

    virtual void GetRxProtoStats(ProtoStats &stats) const {
        stats.open = peer_->channel_->rx_open();
        stats.close = peer_->channel_->rx_close();
        stats.keepalive = peer_->channel_->rx_keepalive();
        stats.update = peer_->channel_->rx_update();
    }

    virtual void GetTxProtoStats(ProtoStats &stats) const {
        stats.open = peer_->channel_->tx_open();
        stats.close = peer_->channel_->tx_close();
        stats.keepalive = peer_->channel_->tx_keepalive();
        stats.update = peer_->channel_->tx_update();
    }

    virtual void GetRxRouteUpdateStats(UpdateStats &stats)  const {
        stats.total = peer_->stats_[RX].rt_updates;
        stats.reach = peer_->stats_[RX].reach;
        stats.unreach = peer_->stats_[RX].unreach;
    }

    virtual void GetTxRouteUpdateStats(UpdateStats &stats)  const {
        stats.total = peer_->stats_[TX].rt_updates;
        stats.reach = peer_->stats_[TX].reach;
        stats.unreach = peer_->stats_[TX].unreach;
    }

    virtual void GetRxSocketStats(IPeerDebugStats::SocketStats &stats) const {
        const XmppSession *session = peer_->GetSession();
        if (session) {
            io::SocketStats socket_stats(session->GetSocketStats());
            stats.calls = socket_stats.read_calls;
            stats.bytes = socket_stats.read_bytes;
        }
    }

    virtual void GetTxSocketStats(IPeerDebugStats::SocketStats &stats) const {
        const XmppSession *session = peer_->GetSession();
        if (session) {
            io::SocketStats socket_stats(session->GetSocketStats());
            stats.calls = socket_stats.write_calls;
            stats.bytes = socket_stats.write_bytes;
            stats.blocked_count = socket_stats.write_blocked;
            stats.blocked_duration_usecs =
                socket_stats.write_blocked_duration_usecs;
        }
    }

    virtual void GetRxErrorStats(RxErrorStats &stats) const {
        BgpXmppChannel::ErrorStats &err_stats = peer_->error_stats();
        stats.inet6_bad_xml_token_count =
            err_stats.get_inet6_rx_bad_xml_token_count();
        stats.inet6_bad_prefix_count =
            err_stats.get_inet6_rx_bad_prefix_count();
        stats.inet6_bad_nexthop_count =
            err_stats.get_inet6_rx_bad_nexthop_count();
        stats.inet6_bad_afi_safi_count =
            err_stats.get_inet6_rx_bad_afi_safi_count();
    }

    virtual void UpdateTxUnreachRoute(uint32_t count) {
        peer_->stats_[TX].unreach += count;
    }

    virtual void UpdateTxReachRoute(uint32_t count) {
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
          send_ready_(true) {
        refcount_ = 0;
    }

    virtual ~XmppPeer() {
        assert(GetRefCount() == 0);
    }

    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize);
    virtual std::string ToString() const {
        return parent_->ToString();
    }

    virtual std::string ToUVEKey() const {
        if (!parent_->channel_->connection()) return "";
        return parent_->channel_->connection()->ToUVEKey();
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
    virtual bool IsReady() const {
        return (parent_->channel_->GetPeerState() == xmps::READY);
    }
    virtual const string GetStateName() const {
        switch (parent_->channel_->GetPeerState()) {
            case xmps::UNKNOWN: return "UNKNOWN";
            case xmps::READY: return "READY";
            case xmps::NOT_READY: return "NOT_READY";
        }
        return "UNKNOWN";
    }
    virtual bool IsXmppPeer() const {
        return true;
    }
    virtual void Close();

    const bool IsDeleted() const { return is_deleted_; }
    void SetDeleted(bool deleted) { is_deleted_ = deleted; }

    virtual BgpProto::BgpPeerType PeerType() const {
        return BgpProto::XMPP;
    }

    virtual uint32_t bgp_identifier() const {
        const XmppConnection *connection = parent_->channel_->connection();
        const boost::asio::ip::tcp::endpoint &remote = connection->endpoint();
        if (remote.address().is_v4()) {
            return remote.address().to_v4().to_ulong();
        }
        return 0;
    }

    virtual void UpdateRefCount(int count) const { refcount_ += count; }
    virtual tbb::atomic<int> GetRefCount() const { return refcount_; }

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
    bool is_deleted_;
    bool send_ready_;
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
        parent_->stats_[TX].rt_updates ++;
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
}

BgpXmppChannel::~BgpXmppChannel() {
    if (channel_->connection() && !channel_->connection()->IsActiveChannel()) {
        CHECK_CONCURRENCY("bgp::Config");
    }

    if (manager_)
        manager_->RemoveChannel(channel_);
    STLDeleteElements(&defer_q_);
    assert(peer_->IsDeleted());
    channel_->UnRegisterReceive(peer_id_);
}

const XmppSession *BgpXmppChannel::GetSession() const {
    if (channel_ && channel_->connection()) {
        return channel_->connection()->session();
    }
    return NULL;
}

std::string BgpXmppChannel::ToString() const {
    return channel_->ToString();
}

std::string BgpXmppChannel::StateName() const {
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

void BgpXmppChannel::ASNUpdateCallback(as_t old_asn) {
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

    // Delete the route and add with new ASN
    for (PublishedRTargetRoutes::iterator it = rtarget_routes_.begin();
         it != rtarget_routes_.end(); it++) {
        RTargetRouteOp(rtarget_table, old_asn, it->first, NULL, false);
        RTargetRouteOp(rtarget_table, bgp_server_->autonomous_system(),
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
        std::pair<PublishedRTargetRoutes::iterator, bool> ret =
         rtarget_routes_.insert(std::make_pair(rtarget, RoutingInstanceList()));

        rt_loc = ret.first;
        // Send rtarget route ADD
        RTargetRouteOp(rtarget_table,
                       bgp_server_->autonomous_system(),
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
                       bgp_server_->autonomous_system(),
                       rtarget, NULL, false);
    }
}

void BgpXmppChannel::RoutingInstanceCallback(std::string vrf_name, int op) {
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

        std::pair<RoutingInstance::RouteTargetList::iterator, bool> r;
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

bool BgpXmppChannel::XmppDecodeAddress(int af, const string &address,
                                       IpAddress *addrp) {
    switch (af) {
    case BgpAf::IPv4:
        break;
    default:
        BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_IN, "Unsupported address family:" << af);
        return false;
    }

    error_code error;
    *addrp = IpAddress::from_string(address, error);
    if (error) {
        return false;
    }
    return true;
}

void BgpXmppChannel::ProcessMcastItem(std::string vrf_name,
                                      const pugi::xml_node &node,
                                      bool add_change) {
    autogen::McastItemType item;
    item.Clear();

    if (!item.XmlParse(node)) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
                              BGP_LOG_FLAG_ALL,
                              "Invalid multicast message received");
        return;
    }

    // NLRI ipaddress/mask
    if (item.entry.nlri.af != BgpAf::IPv4) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
           BGP_LOG_FLAG_ALL, "Unsupported address family:" << item.entry.nlri.af
           << " for multicast route");
        return;
    }

    if (item.entry.nlri.safi != BgpAf::Mcast) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
            BGP_LOG_FLAG_ALL, "Unsupported safi:" << item.entry.nlri.safi <<
            " for multicast route");
        return;
    }

    error_code error;
    IpAddress grp_address = IpAddress::from_string("0.0.0.0", error);
    if (!item.entry.nlri.group.empty()) {
        if (!(XmppDecodeAddress(item.entry.nlri.af,
                                item.entry.nlri.group, &grp_address))) {
            BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN,
                BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                "Error parsing group address:" << item.entry.nlri.group <<
                " for family:" << item.entry.nlri.af);
            return;
        }
    }

    IpAddress src_address = IpAddress::from_string("0.0.0.0", error);
    if (!item.entry.nlri.source.empty()) {
        if (!(XmppDecodeAddress(item.entry.nlri.af,
                                item.entry.nlri.source, &src_address))) {
            BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN,
                BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                "Error parsing source address:" << item.entry.nlri.source <<
                " for family:" << item.entry.nlri.af);
            return;
        }
    }

    RoutingInstanceMgr *instance_mgr = bgp_server_->routing_instance_mgr();
    if (!instance_mgr) {
        BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN,
              BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
              " ProcessMcastItem: Routing Instance Manager not found");
        return;
    }
    RoutingInstance *rt_instance = instance_mgr->GetRoutingInstance(vrf_name);
    bool subscribe_pending = false;
    int instance_id = -1;
    BgpTable *table = NULL;
    //Build the key to the Multicast DBTable
    PeerRibMembershipManager *mgr = bgp_server_->membership_mgr();
    if (rt_instance != NULL && !rt_instance->deleted()) {
        table = rt_instance->GetTable(Address::ERMVPN);
        if (table == NULL) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                    SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                    "Inet Multicast table not found");
            return;
        }


        //check if Registration is pending
        RoutingTableMembershipRequestMap::iterator loc =
            routingtable_membership_request_map_.find(table->name());
        if (loc != routingtable_membership_request_map_.end()) {
            if (loc->second.pending_req == SUBSCRIBE) {
                instance_id = loc->second.instance_id;
                subscribe_pending = true;
            } else {
                BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                   SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                   "Inet Multicast Route not processed as no subscription pending");
                return;
            }
        } else {
            if (IPeerRib *rib = mgr->IPeerRibFind(peer_.get(), table)) {
                instance_id = rib->instance_id();
            } else {
                BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                   SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                   "Inet Multicast Route not processed as peer is not registered");
                return;
            }
        }
    } else {
        //check if Registration is pending before routing instance create
        VrfMembershipRequestMap::iterator loc =
            vrf_membership_request_map_.find(vrf_name);
        if (loc != vrf_membership_request_map_.end()) {
            subscribe_pending = true;
            instance_id = loc->second;
        } else {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
               SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
               "Inet Multicast Route not processed as no subscription pending");
            return;
        }
    }

    RouteDistinguisher mc_rd(peer_->bgp_identifier(), instance_id);
    ErmVpnPrefix mc_prefix(ErmVpnPrefix::NativeRoute, mc_rd,
        grp_address.to_v4(), src_address.to_v4());

    //Build and enqueue a DB request for route-addition
    DBRequest req;
    req.key.reset(new ErmVpnTable::RequestKey(mc_prefix, peer_.get()));

    uint32_t flags = 0;
    ExtCommunitySpec ext;

    if (add_change) {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        vector<uint32_t> labels;

        // Agents should send only one next-hop in the item
        if (item.entry.next_hops.next_hop.size() != 1) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                    SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                    "More than one nexthop received for the group:"
                    << item.entry.nlri.group);
                return;
        }

        // Label Allocation item.entry.label by parsing the range
        if (!stringToIntegerList(item.entry.next_hops.next_hop[0].label, "-", labels) ||
            labels.size() != 2) {
            BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN,
                         BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                "Bad label block range:" << item.entry.next_hops.next_hop[0].label);
            return;
        }

        BgpAttrSpec attrs;
        LabelBlockPtr lbptr = lb_mgr_->LocateBlock(labels[0], labels[1]);

        BgpAttrLabelBlock attr_label(lbptr);
        attrs.push_back(&attr_label);

        //Next-hop ipaddress
        IpAddress nh_address;
        if (!(XmppDecodeAddress(item.entry.next_hops.next_hop[0].af,
                                item.entry.next_hops.next_hop[0].address, &nh_address))) {
            BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                "Error parsing nexthop address:" <<
                 item.entry.next_hops.next_hop[0].address <<
                " family:" << item.entry.next_hops.next_hop[0].af <<
                " for multicast route");
            return;
        }
        BgpAttrNextHop nexthop(nh_address.to_v4().to_ulong());
        attrs.push_back(&nexthop);

        // Tunnel Encap list
        bool no_valid_tunnel_encap = true;
        for (std::vector<std::string>::const_iterator it =
             item.entry.next_hops.next_hop[0].tunnel_encapsulation_list.begin();
             it !=
             item.entry.next_hops.next_hop[0].tunnel_encapsulation_list.end();
             it++) {
             TunnelEncap tun_encap(*it);
             if (tun_encap.tunnel_encap() != TunnelEncapType::UNSPEC) {
                 no_valid_tunnel_encap = false;
                 ext.communities.push_back(tun_encap.GetExtCommunityValue());
             }
        }

        // If all of the tunnel encaps published by the agent is invalid,
        // mark the path as infeasible
        // If agent has not published any tunnel encap, default the tunnel
        // encap to "gre"
        //
        if (!item.entry.next_hops.next_hop[0].tunnel_encapsulation_list.tunnel_encapsulation.empty() &&
            no_valid_tunnel_encap) {
            flags = BgpPath::NoTunnelEncap;
        }

        // We may have extended communities for tunnel encapsulation.
        if (!ext.communities.empty())
            attrs.push_back(&ext);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attrs);
        req.data.reset(new ErmVpnTable::RequestData(attr, flags, 0));
        stats_[RX].reach++;
    } else {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        stats_[RX].unreach++;
    }


    if (subscribe_pending) {
        //
        // We will Q all route request till register request is processed
        //
        DBRequest *request_entry = new DBRequest();
        request_entry->Swap(&req);
        std::string table_name =
            RoutingInstance::GetTableName(vrf_name, Address::ERMVPN);
        defer_q_.insert(std::make_pair(std::make_pair(vrf_name, table_name),
                                       request_entry));
        return;
    }

    assert(table);
    if (mgr && !mgr->PeerRegistered(peer_.get(), table)) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
            BGP_LOG_FLAG_ALL,
            "Peer:" << peer_.get() << " not subscribed to table " <<
            table->name());
        return;
    }

    BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                          SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                          "Inet Multicast Group" 
                               << item.entry.nlri.group <<
                               " Source " << item.entry.nlri.source <<
                               " Label Range: " <<
                               ((item.entry.next_hops.next_hop.size() == 1) ?
                                 item.entry.next_hops.next_hop[0].label.c_str():
                                 "Invalid Label Range")
                               << " from peer:" << peer_->ToString() <<
                               " is enqueued for " <<
                               (add_change ? "add/change" : "delete"));
    table->Enqueue(&req);
}

void BgpXmppChannel::ProcessItem(string vrf_name,
                                 const pugi::xml_node &node, bool add_change) {
    autogen::ItemType item;
    item.Clear();

    if (!item.XmlParse(node)) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
                                   BGP_LOG_FLAG_ALL,
                                   "Invalid message received");
        return;
    }

    // NLRI ipaddress/mask
    if (item.entry.nlri.af != BgpAf::IPv4) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
                                   BGP_LOG_FLAG_ALL,
                                   "Unsupported address family");
        return;
    }

    error_code error;
    Ip4Prefix rt_prefix = Ip4Prefix::FromString(item.entry.nlri.address,
                                                &error);
    if (error) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
                                   BGP_LOG_FLAG_ALL,
                                   "Bad address string: " <<
                                   item.entry.nlri.address);
        return;
    }

    RoutingInstanceMgr *instance_mgr = bgp_server_->routing_instance_mgr();
    if (!instance_mgr) {
        BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN,
              BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
              " ProcessItem: Routing Instance Manager not found");
        return;
    }

    RoutingInstance *rt_instance = instance_mgr->GetRoutingInstance(vrf_name);
    bool subscribe_pending = false;
    int instance_id = -1;
    BgpTable *table = NULL;
    if (rt_instance != NULL && !rt_instance->deleted()) {
        table = rt_instance->GetTable(Address::INET);
        if (table == NULL) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
                                       BGP_LOG_FLAG_ALL, "Inet table not found");
            return;
        }

        RoutingTableMembershipRequestMap::iterator loc =
            routingtable_membership_request_map_.find(table->name());
        if (loc != routingtable_membership_request_map_.end()) {
            // We have rxed unregister request for a table and
            // receiving route update for the same table
            if (loc->second.pending_req != SUBSCRIBE) {
                BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN,
                                  BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                                  "Received route update after unregister req : "
                                  << table->name());
                return;
            }
            subscribe_pending = true;
            instance_id = loc->second.instance_id;
        } else {
            // Bail if we are not subscribed to the table
            PeerRibMembershipManager *mgr = bgp_server_->membership_mgr();
            const IPeerRib *peer_rib = mgr->IPeerRibFind(peer_.get(), table);
            if (!peer_rib) {
                BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                   SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                   "Peer:" << peer_.get() << " not subscribed to table " <<
                   table->name());
                return;
            }
            instance_id = peer_rib->instance_id();
        }
    } else {
        //check if Registration is pending before routing instance create
        VrfMembershipRequestMap::iterator loc =
            vrf_membership_request_map_.find(vrf_name);
        if (loc != vrf_membership_request_map_.end()) {
            subscribe_pending = true;
            instance_id = loc->second;
        } else {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
               SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
               "Inet Route not processed as no subscription pending");
            return;
        }
    }

    if (instance_id == -1)
        instance_id = rt_instance->index();

    InetTable::RequestData::NextHops nexthops;
    DBRequest req;
    req.key.reset(new InetTable::RequestKey(rt_prefix, peer_.get()));

    IpAddress nh_address(Ip4Address(0));
    uint32_t label = 0;
    uint32_t flags = 0;
    ExtCommunitySpec ext;

    if (add_change) {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        BgpAttrSpec attrs;

        if (!item.entry.next_hops.next_hop.empty()) {
            for (size_t i = 0; i < item.entry.next_hops.next_hop.size(); i++) {
                InetTable::RequestData::NextHop nexthop;

                IpAddress nhop_address(Ip4Address(0));
                if (!(XmppDecodeAddress(
                          item.entry.next_hops.next_hop[i].af,
                          item.entry.next_hops.next_hop[i].address,
                          &nhop_address))) {
                    BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN,
                        BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                        "Error parsing nexthop address:" <<
                        item.entry.next_hops.next_hop[i].address <<
                        " family:" << item.entry.next_hops.next_hop[i].af <<
                        " for unicast route");
                    return;
                }

                if (i == 0) {
                    nh_address = nhop_address;
                    label = item.entry.next_hops.next_hop[0].label;
                }

                bool no_valid_tunnel_encap = true;

                // Tunnel Encap list
                for (std::vector<std::string>::const_iterator it =
                     item.entry.next_hops.next_hop[i].tunnel_encapsulation_list.begin();
                     it !=
                     item.entry.next_hops.next_hop[i].tunnel_encapsulation_list.end();
                     it++) {
                    TunnelEncap tun_encap(*it);
                    if (tun_encap.tunnel_encap() != TunnelEncapType::UNSPEC) {
                        no_valid_tunnel_encap = false;
                        if (i == 0) {
                            ext.communities.push_back(tun_encap.GetExtCommunityValue());
                        }
                        nexthop.tunnel_encapsulations_.push_back(tun_encap.GetExtCommunity());
                    }
                }

                // If all of the tunnel encaps published by the agent are
                // invalid, mark the path as infeasible. If agent has not
                // published any tunnel encap, default the tunnel encap to "gre"
                if (!item.entry.next_hops.next_hop[i].tunnel_encapsulation_list.tunnel_encapsulation.empty() &&
                    no_valid_tunnel_encap) {
                    flags = BgpPath::NoTunnelEncap;
                }

                nexthop.flags_ = flags;
                nexthop.address_ = nhop_address;
                nexthop.label_ = item.entry.next_hops.next_hop[i].label;
                nexthop.source_rd_ = RouteDistinguisher(
                                         nhop_address.to_v4().to_ulong(),
                                         instance_id);
                nexthops.push_back(nexthop);
            }
        }

        BgpAttrLocalPref local_pref(item.entry.local_preference);
        if (local_pref.local_pref != 0)
            attrs.push_back(&local_pref);

        BgpAttrNextHop nexthop(nh_address.to_v4().to_ulong());
        attrs.push_back(&nexthop);

        BgpAttrSourceRd source_rd(
            RouteDistinguisher(nh_address.to_v4().to_ulong(), instance_id));
        attrs.push_back(&source_rd);

        // SGID list
        for (std::vector<int>::iterator it =
             item.entry.security_group_list.security_group.begin();
             it != item.entry.security_group_list.security_group.end();
             it++) {
            SecurityGroup sg(bgp_server_->autonomous_system(), *it);
            ext.communities.push_back(sg.GetExtCommunityValue());
        }

        // Seq number
        if (item.entry.sequence_number) {
            MacMobility mm(item.entry.sequence_number);
            ext.communities.push_back(mm.GetExtCommunityValue());
        }

        if (rt_instance) {
            OriginVn origin_vn(bgp_server_->autonomous_system(),
                rt_instance->virtual_network_index());
            ext.communities.push_back(origin_vn.GetExtCommunityValue());
        }

        // We may have extended communities for tunnel encapsulation, security
        // groups and origin vn.
        if (!ext.communities.empty())
            attrs.push_back(&ext);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attrs);

        req.data.reset(new InetTable::RequestData(attr, nexthops));
        stats_[RX].reach++;
    } else {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        stats_[RX].unreach++;
    }

    // Defer all route requests till register request is processed
    if (subscribe_pending) {
        DBRequest *request_entry = new DBRequest();
        request_entry->Swap(&req);
        std::string table_name =
            RoutingInstance::GetTableName(vrf_name, Address::INET);
        defer_q_.insert(std::make_pair(std::make_pair(vrf_name, table_name),
                                       request_entry));
        return;
    }

    assert(table);

    BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, 
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                               "Inet route " << item.entry.nlri.address <<
                               " with next-hop " << nh_address
                               << " and label " << label
                               <<  " is enqueued for "
                               << (add_change ? "add/change" : "delete"));
    table->Enqueue(&req);
}

void BgpXmppChannel::ProcessInet6Item(string vrf_name,
        const pugi::xml_node &node, bool add_change) {
    autogen::ItemType item;
    item.Clear();

    if (!item.XmlParse(node)) {
        error_stats().incr_inet6_rx_bad_xml_token_count();
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
                              BGP_LOG_FLAG_ALL, "Invalid message received");
        return;
    }

    // NLRI ipaddress/mask
    if ((item.entry.nlri.af != BgpAf::IPv6) ||
        (item.entry.nlri.safi != BgpAf::Unicast)) {
        error_stats().incr_inet6_rx_bad_afi_safi_count();
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
                              BGP_LOG_FLAG_ALL, "Unsupported address family");
        return;
    }

    error_code error;
    Inet6Prefix rt_prefix = Inet6Prefix::FromString(item.entry.nlri.address,
                                                    &error);
    if (error) {
        error_stats().incr_inet6_rx_bad_prefix_count();
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
           BGP_LOG_FLAG_ALL, "Bad address string: " << item.entry.nlri.address);
        return;
    }

    RoutingInstanceMgr *instance_mgr = bgp_server_->routing_instance_mgr();
    if (!instance_mgr) {
        BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
           BGP_PEER_DIR_IN, " ProcessItem: Routing Instance Manager not found");
        return;
    }

    RoutingInstance *rt_instance = instance_mgr->GetRoutingInstance(vrf_name);
    bool subscribe_pending = false;
    int instance_id = -1;
    BgpTable *table = NULL;
    if (rt_instance != NULL && !rt_instance->deleted()) {
        table = rt_instance->GetTable(Address::INET6);
        if (table == NULL) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
                                  BGP_LOG_FLAG_ALL, "Inet6 table not found");
            return;
        }

        RoutingTableMembershipRequestMap::iterator loc =
            routingtable_membership_request_map_.find(table->name());
        if (loc != routingtable_membership_request_map_.end()) {
            // We have rxed unregister request for a table and
            // receiving route update for the same table
            if (loc->second.pending_req != SUBSCRIBE) {
                BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN,
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                             "Received route update after unregister req : "
                             << table->name());
                return;
            }
            subscribe_pending = true;
            instance_id = loc->second.instance_id;
        } else {
            // Bail if we are not subscribed to the table
            PeerRibMembershipManager *mgr = bgp_server_->membership_mgr();
            const IPeerRib *peer_rib = mgr->IPeerRibFind(peer_.get(), table);
            if (!peer_rib) {
                BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                    SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                    "Peer:" << peer_.get() << " not subscribed to table " <<
                    table->name());
                return;
            }
            instance_id = peer_rib->instance_id();
        }
    } else {
        //check if Registration is pending before routing instance create
        VrfMembershipRequestMap::iterator loc =
            vrf_membership_request_map_.find(vrf_name);
        if (loc != vrf_membership_request_map_.end()) {
            subscribe_pending = true;
            instance_id = loc->second;
        } else {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Inet6 Route not processed as no subscription pending");
            return;
        }
    }

    if (instance_id == -1) {
        instance_id = rt_instance->index();
    }

    Inet6Table::RequestData::NextHops nexthops;
    DBRequest req;
    req.key.reset(new Inet6Table::RequestKey(rt_prefix, peer_.get()));

    IpAddress nh_address(Ip4Address(0));
    uint32_t label = 0;
    uint32_t flags = 0;
    ExtCommunitySpec ext;

    if (add_change) {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        BgpAttrSpec attrs;

        if (!item.entry.next_hops.next_hop.empty()) {
            for (size_t i = 0; i < item.entry.next_hops.next_hop.size(); ++i) {
                Inet6Table::RequestData::NextHop nexthop;

                IpAddress nhop_address(Ip4Address(0));
                if (!(XmppDecodeAddress(
                          item.entry.next_hops.next_hop[i].af,
                          item.entry.next_hops.next_hop[i].address,
                          &nhop_address))) {
                    error_stats().incr_inet6_rx_bad_nexthop_count();
                    BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN,
                        BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                        "Error parsing nexthop address:" <<
                        item.entry.next_hops.next_hop[i].address <<
                        " family:" << item.entry.next_hops.next_hop[i].af <<
                        " for unicast route");
                    return;
                }

                if (i == 0) {
                    nh_address = nhop_address;
                    label = item.entry.next_hops.next_hop[0].label;
                }

                bool no_valid_tunnel_encap = true;

                // Tunnel Encap list
                for (std::vector<std::string>::const_iterator it =
                     item.entry.next_hops.next_hop[i].
                                        tunnel_encapsulation_list.begin();
                     it !=
                     item.entry.next_hops.next_hop[i].
                                        tunnel_encapsulation_list.end();
                     ++it) {
                    TunnelEncap tun_encap(*it);
                    if (tun_encap.tunnel_encap() != TunnelEncapType::UNSPEC) {
                        no_valid_tunnel_encap = false;
                        if (i == 0) {
                            ext.communities.push_back(
                                tun_encap.GetExtCommunityValue());
                        }
                        nexthop.tunnel_encapsulations_.push_back(
                            tun_encap.GetExtCommunity());
                    }
                }

                // If all of the tunnel encaps published by the agent are 
                // invalid, mark the path as infeasible. If agent has not 
                // published any tunnel encap, default the tunnel encap to "gre"
                if (!item.entry.next_hops.next_hop[i].tunnel_encapsulation_list.
                        tunnel_encapsulation.empty() && no_valid_tunnel_encap) {
                    flags = BgpPath::NoTunnelEncap;
                }

                nexthop.flags_ = flags;
                nexthop.address_ = nhop_address;
                nexthop.label_ = item.entry.next_hops.next_hop[i].label;
                nexthop.source_rd_ = 
                    RouteDistinguisher(nhop_address.to_v4().to_ulong(),
                                       instance_id);
                nexthops.push_back(nexthop);
            }
        }

        BgpAttrLocalPref local_pref(item.entry.local_preference);
        if (local_pref.local_pref != 0) {
            attrs.push_back(&local_pref);
        }

        BgpAttrNextHop nexthop(nh_address.to_v4().to_ulong());
        attrs.push_back(&nexthop);

        BgpAttrSourceRd source_rd(
            RouteDistinguisher(nh_address.to_v4().to_ulong(), instance_id));
        attrs.push_back(&source_rd);

        // SGID list
        for (std::vector<int>::iterator it =
                 item.entry.security_group_list.security_group.begin();
             it != item.entry.security_group_list.security_group.end();
             ++it) {
            SecurityGroup sg(bgp_server_->autonomous_system(), *it);
            ext.communities.push_back(sg.GetExtCommunityValue());
        }

        if (item.entry.sequence_number) {
            MacMobility mm(item.entry.sequence_number);
            ext.communities.push_back(mm.GetExtCommunityValue());
        }

        if (rt_instance) {
            OriginVn origin_vn(bgp_server_->autonomous_system(),
                rt_instance->virtual_network_index());
            ext.communities.push_back(origin_vn.GetExtCommunityValue());
        }

        // We may have extended communities for tunnel encapsulation, security
        // groups and origin vn.
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

    // Defer all route requests till register request is processed
    if (subscribe_pending) {
        DBRequest *request_entry = new DBRequest();
        request_entry->Swap(&req);
        std::string table_name =
            RoutingInstance::GetTableName(vrf_name, Address::INET6);
        defer_q_.insert(std::make_pair(std::make_pair(vrf_name, table_name),
                                       request_entry));
        return;
    }

    assert(table);

    BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, 
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE, "Inet6 route " 
        << item.entry.nlri.address << " with next-hop " << nh_address 
        << " and label " << label <<  " is enqueued for "
        << (add_change ? "add/change" : "delete"));
    table->Enqueue(&req);
}

void BgpXmppChannel::ProcessEnetItem(string vrf_name,
                                     const pugi::xml_node &node,
                                     bool add_change) {
    autogen::EnetItemType item;
    item.Clear();

    if (!item.XmlParse(node)) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
                                   BGP_LOG_FLAG_ALL,
                                   "Invalid message received");
        return;
    }

    // NLRI ipaddress/mask
    if (item.entry.nlri.af != BgpAf::L2Vpn) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
                                   BGP_LOG_FLAG_ALL,
                                   "Unsupported address family");
        return;
    }

    error_code error;
    MacAddress mac_addr = MacAddress::FromString(item.entry.nlri.mac, &error);

    if (error) {
        BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
                                   BGP_LOG_FLAG_ALL,
                                   "Bad mac address string: " <<
                                   item.entry.nlri.mac);
        return;
    }

    Ip4Prefix ip_prefix;
    if (!mac_addr.IsBroadcast() && !item.entry.nlri.address.empty()) {
        ip_prefix = Ip4Prefix::FromString(item.entry.nlri.address, &error);
        if (error || ip_prefix.prefixlen() != 32) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
                                       BGP_LOG_FLAG_ALL,
                                       "Bad address string: " <<
                                       item.entry.nlri.address);
            return;
        }
    }

    RoutingInstanceMgr *instance_mgr = bgp_server_->routing_instance_mgr();
    if (!instance_mgr) {
        BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN,
                     BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                     " ProcessEnetItem: Routing Instance Manager not found");
        return;
    }

    RoutingInstance *rt_instance = instance_mgr->GetRoutingInstance(vrf_name);
    bool subscribe_pending = false;
    int instance_id = -1;
    BgpTable *table = NULL;
    if (rt_instance != NULL && !rt_instance->deleted()) {
        table = rt_instance->GetTable(Address::EVPN);
        if (table == NULL) {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name, SandeshLevel::SYS_WARN,
                                       BGP_LOG_FLAG_ALL, "Evpn table not found");
            return;
        }

        RoutingTableMembershipRequestMap::iterator loc =
            routingtable_membership_request_map_.find(table->name());
        if (loc != routingtable_membership_request_map_.end()) {
            // We have rxed unregister request for a table and
            // receiving route update for the same table
            if (loc->second.pending_req != SUBSCRIBE) {
                BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN,
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                             "Received route update after unregister req : "
                                 << table->name());
                return;
            }
            subscribe_pending = true;
            instance_id = loc->second.instance_id;
        } else {
            // Bail if we are not subscribed to the table
            PeerRibMembershipManager *mgr = bgp_server_->membership_mgr();
            const IPeerRib *peer_rib = mgr->IPeerRibFind(peer_.get(), table);
            if (!peer_rib) {
                BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
                   SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                   "Peer:" << peer_.get() << " not subscribed to table " <<
                   table->name());
                return;
            }
            instance_id = peer_rib->instance_id();
        }
    } else {
        //check if Registration is pending before routing instance create
        VrfMembershipRequestMap::iterator loc =
            vrf_membership_request_map_.find(vrf_name);
        if (loc != vrf_membership_request_map_.end()) {
            subscribe_pending = true;
            instance_id = loc->second;
        } else {
            BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
               SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
               "Evpn Route not processed as no subscription pending");
            return;
        }
    }

    if (instance_id == -1)
        instance_id = rt_instance->index();

    RouteDistinguisher rd;
    if (mac_addr.IsBroadcast()) {
        rd = RouteDistinguisher(peer_->bgp_identifier(), instance_id);
    } else {
        rd = RouteDistinguisher::kZeroRd;
    }

    uint32_t ethernet_tag = item.entry.nlri.ethernet_tag;
    EvpnPrefix evpn_prefix(rd, ethernet_tag, mac_addr, ip_prefix.ip4_addr());

    EvpnTable::RequestData::NextHops nexthops;
    DBRequest req;
    ExtCommunitySpec ext;
    req.key.reset(new EvpnTable::RequestKey(evpn_prefix, peer_.get()));

    IpAddress nh_address(Ip4Address(0));
    uint32_t label = 0;
    uint32_t flags = 0;

    if (add_change) {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        BgpAttrSpec attrs;

        if (!item.entry.next_hops.next_hop.empty()) {
            for (size_t i = 0; i < item.entry.next_hops.next_hop.size(); i++) {
                EvpnTable::RequestData::NextHop nexthop;
                IpAddress nhop_address(Ip4Address(0));

                if (!(XmppDecodeAddress(
                          item.entry.next_hops.next_hop[i].af,
                          item.entry.next_hops.next_hop[i].address,
                          &nhop_address))) {
                    BGP_LOG_PEER(Message, Peer(), SandeshLevel::SYS_WARN,
                                 BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                                 "Error parsing nexthop address:" <<
                                 item.entry.next_hops.next_hop[i].address <<
                                 " family:" <<
                                 item.entry.next_hops.next_hop[i].af <<
                                 " for evpn route");
                    return;
                }
                if (i == 0) {
                    nh_address = nhop_address;
                    label = item.entry.next_hops.next_hop[0].label;
                }

                // Tunnel Encap list
                bool no_valid_tunnel_encap = true;
                for (std::vector<std::string>::const_iterator it =
                     item.entry.next_hops.next_hop[i].tunnel_encapsulation_list.begin();
                     it !=
                     item.entry.next_hops.next_hop[i].tunnel_encapsulation_list.end();
                     it++) {
                    TunnelEncap tun_encap(*it);
                    if (tun_encap.tunnel_encap() != TunnelEncapType::UNSPEC) {
                        no_valid_tunnel_encap = false;
                        if (i == 0) {
                            ext.communities.push_back(tun_encap.GetExtCommunityValue());
                        }
                        nexthop.tunnel_encapsulations_.push_back(tun_encap.GetExtCommunity());
                    }
                }
                //
                // If all of the tunnel encaps published by the agent is invalid,
                // mark the path as infeasible
                // If agent has not published any tunnel encap, default the tunnel
                // encap to "gre"
                //
                if (!item.entry.next_hops.next_hop[i].tunnel_encapsulation_list.tunnel_encapsulation.empty() &&
                    no_valid_tunnel_encap)
                    flags = BgpPath::NoTunnelEncap;

                nexthop.flags_ = flags;
                nexthop.address_ = nhop_address;
                nexthop.label_ = item.entry.next_hops.next_hop[i].label;
                nexthop.source_rd_ = RouteDistinguisher(
                                         nhop_address.to_v4().to_ulong(),
                                         instance_id);
                nexthops.push_back(nexthop);
            }
        }

        BgpAttrNextHop nexthop(nh_address.to_v4().to_ulong());
        attrs.push_back(&nexthop);

        BgpAttrSourceRd source_rd(
            RouteDistinguisher(nh_address.to_v4().to_ulong(), instance_id));
        attrs.push_back(&source_rd);

        if (rt_instance) {
            OriginVn origin_vn(bgp_server_->autonomous_system(),
                rt_instance->virtual_network_index());
            ext.communities.push_back(origin_vn.GetExtCommunityValue());
        }

        if (!ext.communities.empty())
            attrs.push_back(&ext);

        BgpAttrParams params_spec;
        if (item.entry.edge_replication_not_supported) {
            params_spec.params |= BgpAttrParams::EdgeReplicationNotSupported;
            attrs.push_back(&params_spec);
        }

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attrs);

        req.data.reset(new EvpnTable::RequestData(attr, nexthops));
        stats_[0].reach++;
    } else {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        stats_[RX].unreach++;
    }

    // Defer all route requests till register request is processed
    if (subscribe_pending) {
        DBRequest *request_entry = new DBRequest();
        request_entry->Swap(&req);
        std::string table_name =
            RoutingInstance::GetTableName(vrf_name, Address::EVPN);
        defer_q_.insert(std::make_pair(std::make_pair(vrf_name, table_name),
                                       request_entry));
        return;
    }

    assert(table);

    BGP_LOG_PEER_INSTANCE(Peer(), vrf_name,
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                               "Evpn route " << item.entry.nlri.mac << ","
                               << item.entry.nlri.address
                               << " with next-hop " << nh_address
                               << " and label " << label
                               <<  " is enqueued for "
                               << (add_change ? "add/change" : "delete"));
    table->Enqueue(&req);
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
        BGP_LOG_PEER(Event, Peer(), SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_NA, "Peer:" << peer_->ToString()
                         << " not subscribed to instance " << table->name());
        return;
    }

    if (request->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
        // In cases where RoutingInstance is not yet created when RouteAdd
        // request is received from agent, the origin_vn is not set in the
        // DBRequest. Fill the origin_vn info in the route attribute.
        BgpTable::RequestData *data =
            static_cast<BgpTable::RequestData *>(request->data.get());
        BgpAttrPtr attr =  data->attrs();
        RoutingInstance *rt_instance = table->routing_instance();
        assert(rt_instance);
        OriginVn origin_vn(bgp_server_->autonomous_system(),
            rt_instance->virtual_network_index());
        ExtCommunityPtr ext_community =
            bgp_server_->extcomm_db()->ReplaceOriginVnAndLocate(
                attr->ext_community(), origin_vn.GetExtCommunity());
        BgpAttrPtr new_attr =
            bgp_server_->attr_db()->ReplaceExtCommunityAndLocate(
                attr.get(), ext_community);
        data->set_attrs(new_attr);
    }

    table->Enqueue(ptr.get());
}

bool BgpXmppChannel::ResumeClose() {
    peer_->Close();
    return true;
}

void BgpXmppChannel::RegisterTable(BgpTable *table, int instance_id) {
    PeerRibMembershipManager *mgr = bgp_server_->membership_mgr();
    mgr->Register(peer_.get(), table, bgp_policy_, instance_id,
            boost::bind(&BgpXmppChannel::MembershipRequestCallback,
                    this, _1, _2));
}

void BgpXmppChannel::UnregisterTable(BgpTable *table) {
    PeerRibMembershipManager *mgr = bgp_server_->membership_mgr();
    mgr->Unregister(peer_.get(), table,
            boost::bind(&BgpXmppChannel::MembershipRequestCallback,
                    this, _1, _2));
}

bool BgpXmppChannel::MembershipResponseHandler(std::string table_name) {
    BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                 BGP_PEER_DIR_NA,
                 "MembershipResponseHandler for table " << table_name);

    RoutingTableMembershipRequestMap::iterator loc =
        routingtable_membership_request_map_.find(table_name);
    if (loc == routingtable_membership_request_map_.end()) {
        BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                     BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA, 
                     "table " << table_name << " not in request queue");
        assert(0);
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
    if (table == NULL) {
        routingtable_membership_request_map_.erase(loc);
        return true;
    }

    MembershipRequestState state = loc->second;
    BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_NA,
            "MembershipResponseHandler for table " << table_name <<
            " current req = " <<
            ((state.current_req == SUBSCRIBE) ? "subscribe" : "unsubscribe") <<
            " pending req = " <<
            ((state.pending_req == SUBSCRIBE) ? "subscribe" : "unsubscribe"));

    PeerRibMembershipManager *mgr = bgp_server_->membership_mgr();
    if ((state.current_req == UNSUBSCRIBE) &&
        (state.pending_req == SUBSCRIBE)) {
        //
        // Rxed Register while processing unregister
        //
        RegisterTable(table, state.instance_id);
        loc->second.current_req = SUBSCRIBE;
        return true;
    } else if ((state.current_req == SUBSCRIBE) &&
               (state.pending_req == UNSUBSCRIBE)) {
        //
        // Rxed UnRegister while processing register
        //
        UnregisterTable(table);
        loc->second.current_req = UNSUBSCRIBE;
        return true;
    }

    routingtable_membership_request_map_.erase(loc);

    VrfTableName vrf_n_table =
        std::make_pair(table->routing_instance()->name(), table_name);

    if (state.pending_req == UNSUBSCRIBE) {
        assert(!defer_q_.count(vrf_n_table));
        return true;
    } else if (state.pending_req == SUBSCRIBE) {
        IPeerRib *rib = mgr->IPeerRibFind(peer_.get(), table);
        if (rib)
            rib->set_instance_id(state.instance_id);
    }

    for(DeferQ::iterator it = defer_q_.find(vrf_n_table);
        (it != defer_q_.end() && it->first.second == table_name); it++) {
        DequeueRequest(table_name, it->second);
    }
    // Erase all elements for the table
    defer_q_.erase(vrf_n_table);

    std::vector<std::string> registered_tables;
    mgr->FillRegisteredTable(peer_.get(), registered_tables);

    if (registered_tables.empty()) return true;

    XmppPeerInfoData peer_info;
    peer_info.set_name(peer_->ToUVEKey());
    peer_info.set_routing_tables(registered_tables);
    peer_info.set_send_state("in sync");
    XMPPPeerInfo::Send(peer_info);

    return true;
}

void BgpXmppChannel::MembershipRequestCallback(IPeer *ipeer, BgpTable *table) {
    membership_response_worker_.Enqueue(table->name());
}

void BgpXmppChannel::FillInstanceMembershipInfo(BgpNeighborResp *resp) {
    vector<BgpNeighborRoutingInstance> instance_list;
    BOOST_FOREACH(SubscribedRoutingInstanceList::value_type &entry,
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
    BOOST_FOREACH(VrfMembershipRequestMap::value_type &entry,
        vrf_membership_request_map_) {
        BgpNeighborRoutingInstance instance;
        instance.set_name(entry.first);
        instance.set_state("pending");
        instance.set_index(entry.second);
        instance_list.push_back(instance);
    }
    resp->set_routing_instances(instance_list);
}

void BgpXmppChannel::FillTableMembershipInfo(BgpNeighborResp *resp) {
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

    BOOST_FOREACH(RoutingTableMembershipRequestMap::value_type &entry,
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
void BgpXmppChannel::FlushDeferQ(std::string vrf_name, std::string table_name) {
    for (DeferQ::iterator it =
        defer_q_.find(std::make_pair(vrf_name, table_name)), itnext;
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
void BgpXmppChannel::FlushDeferQ(std::string vrf_name) {
    for (DeferQ::iterator it =
        defer_q_.lower_bound(std::make_pair(vrf_name, std::string())), itnext;
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
        std::pair<SubscribedRoutingInstanceList::iterator, bool> ret =
            routing_instances_.insert(
                  std::pair<RoutingInstance *, SubscriptionState>
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
        routingtable_membership_request_map_.insert(make_pair(table->name(),
                                                    state));
    }
}

void BgpXmppChannel::ProcessSubscriptionRequest(
        std::string vrf_name, const XmppStanza::XmppMessageIq *iq,
        bool add_change) {
    PeerRibMembershipManager *mgr = bgp_server_->membership_mgr();
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
    if (!instance_mgr) {
        BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                     BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                     "Routing Instance Manager not found");
        return;
    }
    RoutingInstance *rt_instance = instance_mgr->GetRoutingInstance(vrf_name);
    if (rt_instance == NULL) {
        BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                     BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                     "Routing Instance " << vrf_name <<
                     " not found when processing " <<
                     (add_change ? "subscribe" : "unsubscribe"));
        if (add_change) {
            vrf_membership_request_map_[vrf_name] = instance_id;
        } else {
            if (vrf_membership_request_map_.erase(vrf_name)) {
                FlushDeferQ(vrf_name);
            }
        }

        return;
    }

    // If the instance is being deleted and agent is trying to unsubscribe
    // we need to process the unsubscribe if the vrf is not in the request
    // map.  This would be the normal case where we wait for the agent to
    // unsubscribe in order to remove routes added by it.
    if (rt_instance->deleted()) {
        BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_DEBUG,
                     BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                     "Routing Instance " << vrf_name <<
                     " is being deleted when processing " <<
                     (add_change ? "subscribe" : "unsubscribe"));
        if (add_change) {
            vrf_membership_request_map_[vrf_name] = instance_id;
            return;
        } else {
            if (vrf_membership_request_map_.erase(vrf_name)) {
                FlushDeferQ(vrf_name);
                return;
            }
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
                IPeerRib *rib = mgr->IPeerRibFind(peer_.get(), table);
                if (rib && !rib->IsStale()) {
                    BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                                 BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                                 "Received registration req without " <<
                                 "unregister : " << table->name());
                    continue;
                }
                MembershipRequestState state(SUBSCRIBE, instance_id);
                routingtable_membership_request_map_.insert(
                                        make_pair(table->name(), state));
            } else {
                if (loc->second.pending_req == SUBSCRIBE) {
                    BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                                 BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                                 "Received registration req without " <<
                                 "unregister : " << table->name());
                    continue;
                }
                loc->second.instance_id = instance_id;
                loc->second.pending_req = SUBSCRIBE;
                continue;
            }

            RegisterTable(table, instance_id);
        } else {
            if (defer_q_.count(std::make_pair(vrf_name, table->name()))) {
                BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                             BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                             "Flush the DBRoute request on unregister :" <<
                             table->name());
            }

            // Erase all elements for the table
            FlushDeferQ(vrf_name, table->name());

            RoutingTableMembershipRequestMap::iterator loc =
                routingtable_membership_request_map_.find(table->name());
            if (loc == routingtable_membership_request_map_.end()) {
                IPeerRib *rib = mgr->IPeerRibFind(peer_.get(), table);
                if (!rib) {
                    BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                                 BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                                 "Received back to back unregister req:" <<
                                 table->name());
                    continue;
                } else {
                    MembershipRequestState state(UNSUBSCRIBE, instance_id);
                    routingtable_membership_request_map_.insert(
                                            make_pair(table->name(), state));
                }
            } else {
                if (loc->second.pending_req == UNSUBSCRIBE) {
                    BGP_LOG_PEER(Membership, Peer(), SandeshLevel::SYS_WARN,
                                 BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                                 "Received back to back unregister req:" <<
                                 table->name());
                    continue;
                }
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

                        std::string id(iq->as_node.c_str());
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

    //
    // TODO: Enqueue an event to the deleter() and deleted this peer and the
    // channel from a different thread to solve concurrency issues
    //
    delete channel;
    return true;
}


// BgpXmppChannelManager routines.

BgpXmppChannelManager::BgpXmppChannelManager(XmppServer *xmpp_server,
                                             BgpServer *server)
    : xmpp_server_(xmpp_server),
      bgp_server_(server),
      queue_(TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0,
             boost::bind(&BgpXmppChannelManager::DeleteExecutor, this, _1)) {
    queue_.SetEntryCallback(
            boost::bind(&BgpXmppChannelManager::IsReadyForDeletion, this));
    if (xmpp_server) {
        xmpp_server->RegisterConnectionEvent(xmps::BGP,
               boost::bind(&BgpXmppChannelManager::XmppHandleChannelEvent,
                           this, _1, _2));
    }
    asn_listener_id_ = server->RegisterASNUpdateCallback(
        boost::bind(&BgpXmppChannelManager::ASNUpdateCallback, this, _1));

    id_ = server->routing_instance_mgr()->RegisterInstanceOpCallback(
        boost::bind(&BgpXmppChannelManager::RoutingInstanceCallback, 
                    this, _1, _2));
}

BgpXmppChannelManager::~BgpXmppChannelManager() {
    assert(channel_map_.empty());
    if (xmpp_server_) {
        xmpp_server_->UnRegisterConnectionEvent(xmps::BGP);
    }

    queue_.Shutdown();
    channel_map_.clear();
    bgp_server_->UnregisterASNUpdateCallback(asn_listener_id_);
    bgp_server_->routing_instance_mgr()->UnregisterInstanceOpCallback(id_);
}

bool BgpXmppChannelManager::IsReadyForDeletion() {
    return bgp_server_->IsReadyForDeletion();
}


void BgpXmppChannelManager::ASNUpdateCallback(as_t old_asn) {
    BOOST_FOREACH(XmppChannelMap::value_type &i, channel_map_) {
        i.second->ASNUpdateCallback(old_asn);
    }
    xmpp_server_->ClearAllConnections();
}

void BgpXmppChannelManager::RoutingInstanceCallback(std::string vrf_name,
                                                          int op) {
    BOOST_FOREACH(XmppChannelMap::value_type &i, channel_map_) {
        i.second->RoutingInstanceCallback(vrf_name, op);
    }
}


void BgpXmppChannelManager::VisitChannels(BgpXmppChannelManager::VisitorFn fn) {
    BOOST_FOREACH(XmppChannelMap::value_type &i, channel_map_) {
        fn(i.second);
    }
}

BgpXmppChannel *BgpXmppChannelManager::FindChannel(std::string client) {
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


void BgpXmppChannelManager::RemoveChannel(XmppChannel *ch) {
    channel_map_.erase(ch);
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
            channel_map_.insert(std::make_pair(channel, bgp_xmpp_channel));
            BGP_LOG_PEER(Message, bgp_xmpp_channel->Peer(),
                         Sandesh::LoggingUtLevel(), BGP_LOG_FLAG_SYSLOG,
                         BGP_PEER_DIR_IN,
                         "Received XmppChannel up event");
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

            //
            // Trigger closure of this channel
            //
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
    close_in_progress_ = true;
    vrf_membership_request_map_.clear();
    STLDeleteElements(&defer_q_);

    if (routingtable_membership_request_map_.size()) {
        BGP_LOG(BgpMessage, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Peer Close with pending membership request");
        defer_peer_close_ = true;
        return;
    }
    peer_->Close();
}

//
// Return connection's remote tcp endpoint if available
//
boost::asio::ip::tcp::endpoint BgpXmppChannel::remote_endpoint() {
    const XmppSession *session = GetSession();
    if (session) {
        return session->remote_endpoint();
    }
    return boost::asio::ip::tcp::endpoint();
}

//
// Return connection's local tcp endpoint if available
//
boost::asio::ip::tcp::endpoint BgpXmppChannel::local_endpoint() {
    const XmppSession *session = GetSession();
    if (session) {
        return session->local_endpoint();
    }
    return boost::asio::ip::tcp::endpoint();
}

//
// Return true if the XmppPeer is deleted.
//
bool BgpXmppChannel::peer_deleted() {
    return peer_->IsDeleted();
}
