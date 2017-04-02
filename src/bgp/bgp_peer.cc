/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_peer.h"

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>
#include <boost/tuple/tuple.hpp>

#include <algorithm>
#include <map>

#include "base/task_annotations.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_peer_close.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/inet6vpn/inet6vpn_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/peer_close_manager.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/rtarget/rtarget_table.h"

using boost::assign::list_of;
using boost::assign::map_list_of;
using boost::system::error_code;
using boost::tie;
using std::copy;
using std::dec;
using std::map;
using std::numeric_limits;
using std::ostringstream;
using std::string;
using std::vector;

class BgpPeer::PeerStats : public IPeerDebugStats {
public:
    explicit PeerStats(BgpPeer *peer)
        : peer_(peer) {
    }

    // Used when peer flaps.
    // Reset all counters.
    // Socket counters are implicitly cleared because we use a new socket.
    virtual void Clear() {
        error_stats_ = ErrorStats();
        proto_stats_[0] = ProtoStats();
        proto_stats_[1] = ProtoStats();
        update_stats_[0] = UpdateStats();
        update_stats_[1] = UpdateStats();
    }

    // Printable name
    virtual string ToString() const {
        return peer_->ToString();
    }
    // Previous State of the peer
    virtual string last_state() const {
        return peer_->state_machine()->LastStateName();
    }
    virtual string last_state_change_at() const {
        return peer_->state_machine()->last_state_change_at();
    }
    // Last error on this peer
    virtual string last_error() const {
        return peer_->state_machine()->last_notification_in_error();
    }
    // Last Event on this peer
    virtual string last_event() const {
        return peer_->state_machine()->last_event();
    }

    // When was the Last
    virtual string last_flap() const {
        return peer_->last_flap_at();
    }

    // Total number of flaps
    virtual uint64_t num_flaps() const {
        return peer_->flap_count();
    }

    virtual void GetRxProtoStats(ProtoStats *stats) const {
        *stats = proto_stats_[0];
    }

    virtual void GetTxProtoStats(ProtoStats *stats) const {
        *stats = proto_stats_[1];
    }

    virtual void GetRxRouteUpdateStats(UpdateStats *stats)  const {
        *stats = update_stats_[0];
    }

    virtual void GetTxRouteUpdateStats(UpdateStats *stats)  const {
        *stats = update_stats_[1];
    }

    virtual void GetRxSocketStats(IPeerDebugStats::SocketStats *stats) const {
        if (peer_->session()) {
            io::SocketStats socket_stats(peer_->session()->GetSocketStats());
            stats->calls = socket_stats.read_calls;
            stats->bytes = socket_stats.read_bytes;
        }
    }

    virtual void GetTxSocketStats(IPeerDebugStats::SocketStats *stats) const {
        if (peer_->session()) {
            io::SocketStats socket_stats(peer_->session()->GetSocketStats());
            stats->calls = socket_stats.write_calls;
            stats->bytes = socket_stats.write_bytes;
            stats->blocked_count = socket_stats.write_blocked;
            stats->blocked_duration_usecs =
                socket_stats.write_blocked_duration_usecs;
        }
    }

    virtual void UpdateTxUnreachRoute(uint64_t count) {
        update_stats_[1].unreach += count;
    }

    virtual void UpdateTxReachRoute(uint64_t count) {
        update_stats_[1].reach += count;
    }

    // Do nothing for bgp peers.
    virtual void GetRxErrorStats(RxErrorStats *stats) const {
    }

    virtual void GetRxRouteStats(RxRouteStats *stats) const {
        stats->total_path_count = peer_->GetTotalPathCount();
        stats->primary_path_count = peer_->GetPrimaryPathCount();
    }

private:
    friend class BgpPeer;

    BgpPeer *peer_;
    ErrorStats error_stats_;
    ProtoStats proto_stats_[2];
    UpdateStats update_stats_[2];
};

class BgpPeer::DeleteActor : public LifetimeActor {
public:
    explicit DeleteActor(BgpPeer *peer)
        : LifetimeActor(peer->server_->lifetime_manager()),
          peer_(peer) {
    }

    virtual bool MayDelete() const {
        CHECK_CONCURRENCY("bgp::Config");
        if (!peer_->close_manager_->IsQueueEmpty())
            return false;
        if (peer_->IsCloseInProgress())
            return false;
        if (!peer_->state_machine_->IsQueueEmpty())
            return false;
        return true;
    }

    virtual void Shutdown() {
        CHECK_CONCURRENCY("bgp::Config");
        peer_->Clear(BgpProto::Notification::PeerDeconfigured);
    }

    virtual void Destroy() {
        CHECK_CONCURRENCY("bgp::Config");
        peer_->PostCloseRelease();
        if (peer_->router_type() == "bgpaas-client") {
            peer_->server()->decrement_deleting_bgpaas_count();
        } else {
            peer_->server()->decrement_deleting_count();
        }
        assert(!peer_->membership_req_pending());
        assert(!peer_->close_manager_->IsMembershipInUse());
        peer_->rtinstance_->peer_manager()->DestroyIPeer(peer_);
    }

private:
    BgpPeer *peer_;
};

//
// Constructor for BgpPeerFamilyAttributes.
//
BgpPeerFamilyAttributes::BgpPeerFamilyAttributes(
    const BgpNeighborConfig *config,
    const BgpFamilyAttributesConfig &family_config) {
    if (family_config.loop_count) {
        loop_count = family_config.loop_count;
    } else {
        loop_count = config->loop_count();
    }
    prefix_limit = family_config.prefix_limit;

    if (config->router_type() == "bgpaas-client") {
        if (family_config.family == "inet") {
            gateway_address = config->gateway_address(Address::INET);
        } else if (family_config.family == "inet6") {
            gateway_address = config->gateway_address(Address::INET6);
        }
    }
}

RibExportPolicy BgpPeer::BuildRibExportPolicy(Address::Family family) const {
    RibExportPolicy policy;
    BgpPeerFamilyAttributes *family_attributes =
        family_attributes_list_[family];
    if (!family_attributes ||
        family_attributes->gateway_address.is_unspecified()) {
        policy = RibExportPolicy(peer_type_, RibExportPolicy::BGP, peer_as_,
            as_override_, peer_close_->IsCloseLongLivedGraceful(), -1, 0);
    } else {
        IpAddress nexthop = family_attributes->gateway_address;
        policy = RibExportPolicy(peer_type_, RibExportPolicy::BGP, peer_as_,
            as_override_, peer_close_->IsCloseLongLivedGraceful(), nexthop,
            -1, 0);
    }

    if (private_as_action_ == "remove") {
        policy.SetRemovePrivatePolicy(false, false, true);
    } else if (private_as_action_ == "remove-all") {
        policy.SetRemovePrivatePolicy(true, false, true);
    } else if (private_as_action_ == "replace-all") {
        policy.SetRemovePrivatePolicy(true, true, true);
    }

    return policy;
}

void BgpPeer::ReceiveEndOfRIB(Address::Family family, size_t msgsize) {
    close_manager_->ProcessEORMarkerReceived(family);
    eor_receive_timer_[family]->Cancel();

    // If EoR for RTarget is received, start registration for other families.
    if (family == Address::RTARGET)
        RegisterToVpnTables();
}

void BgpPeer::SendEndOfRIBActual(Address::Family family) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);

    // Bail if there's no session for the peer anymore.
    if (!session_)
        return;

    BgpProto::Update update;
    uint16_t afi;
    uint8_t safi;
    tie(afi, safi) = BgpAf::FamilyToAfiSafi(family);
    BgpMpNlri *nlri = new BgpMpNlri(BgpAttribute::MPUnreachNlri, afi, safi);
    update.path_attributes.push_back(nlri);
    uint8_t data[256];
    int msgsize = BgpProto::Encode(&update, data, sizeof(data));
    assert(msgsize > BgpProto::kMinMessageSize);
    session_->Send(data, msgsize, NULL);
    inc_tx_end_of_rib();
    inc_tx_update();
    BGP_LOG_PEER(Message, this, SandeshLevel::SYS_INFO,
        BGP_LOG_FLAG_SYSLOG, BGP_PEER_DIR_OUT,
        "EndOfRib marker family " << Address::FamilyToString(family) <<
        " size " << msgsize);
}

uint32_t BgpPeer::GetOutputQueueDepth(Address::Family family) const {
    BgpTable *table = GetRoutingInstance()->GetTable(family);
    return server_->membership_mgr()->GetRibOutQueueDepth(this, table);
}

uint64_t BgpPeer::GetEorSendTimerElapsedTimeUsecs() const {
    return UTCTimestampUsec() - eor_send_timer_start_time_;
}

// For RTargets, send eor right away.
uint32_t BgpPeer::GetEndOfRibSendTime(Address::Family family) const {
    return family == Address::RTARGET ? 0 : server_->GetEndOfRibSendTime();
}

uint32_t BgpPeer::GetEndOfRibReceiveTime(Address::Family family) const {
    return family == Address::RTARGET ?
        kRouteTargetEndOfRibTimeSecs : server_->GetEndOfRibReceiveTime();
}

bool BgpPeer::EndOfRibSendTimerExpired(Address::Family family) {
    if (!IsReady())
        return false;

    // Retry if wait time has not exceeded the max (10 times configured) and
    // the output queue has not been fully drained yet.
    if (GetEorSendTimerElapsedTimeUsecs() <
            GetEndOfRibSendTime(family) * 1000000 * 10) {
        uint32_t output_depth = GetOutputQueueDepth(family);
        if (output_depth) {
            eor_send_timer_[family]->Reschedule(kEndOfRibSendRetryTimeMsecs);
            BGP_LOG_PEER(Message, this, SandeshLevel::SYS_INFO,
                    BGP_LOG_FLAG_SYSLOG, BGP_PEER_DIR_OUT,
                    "EndOfRib Send Timer rescheduled for family " <<
                    Address::FamilyToString(family) << " to fire after " <<
                    kEndOfRibSendRetryTimeMsecs/1000 << " seconds " <<
                    "due to non-empty output queue (" << output_depth << ")");
            return true;
        }
    }

    SendEndOfRIBActual(family);
    return false;
}

void BgpPeer::SendEndOfRIB(Address::Family family) {
    uint32_t timeout = GetEndOfRibSendTime(family);

    eor_send_timer_start_time_ = UTCTimestampUsec();
    BGP_LOG_PEER(Message, this, SandeshLevel::SYS_INFO,
        BGP_LOG_FLAG_SYSLOG, BGP_PEER_DIR_OUT,
        "EndOfRib Send Timer scheduled for family " <<
        Address::FamilyToString(family) <<
        " to fire after " << timeout * 1000 << " milliseconds");
    eor_send_timer_[family]->Start(timeout * 1000,
        boost::bind(&BgpPeer::EndOfRibSendTimerExpired, this, family),
        boost::bind(&BgpPeer::EndOfRibTimerErrorHandler, this, _1, _2));
}

void BgpPeer::BGPPeerInfoSend(const BgpPeerInfoData &peer_info) const {
    BGPPeerInfo::Send(peer_info);
}

bool BgpPeer::CanUseMembershipManager() const {
    return !membership_req_pending_;
}

//
// Callback from BgpMembershipManager.
// Update pending membership request count and send EndOfRib for the family
// in question.
//
void BgpPeer::MembershipRequestCallback(BgpTable *table) {
    if (close_manager_->IsMembershipInUse()) {
        close_manager_->MembershipRequestCallback();
        return;
    }

    assert(membership_req_pending_ > 0);
    membership_req_pending_--;

    // Resume if CloseManager is waiting to use membership manager.
    if (!membership_req_pending_ &&
        close_manager_->IsMembershipInWait()) {
        close_manager_->MembershipRequest();
    }

    // Resume close if it was deferred and this is the last pending callback.
    // Don't bother sending EndOfRib if close is deferred.
    if (defer_close_) {
        if (!membership_req_pending_) {
            defer_close_ = false;
            trigger_.Set();
        }
        return;
    }

    SendEndOfRIB(table->family());
}

bool BgpPeer::MembershipPathCallback(DBTablePartBase *tpart, BgpRoute *route,
                                     BgpPath *path) {
    return close_manager_->MembershipPathCallback(tpart, route, path);
}

bool BgpPeer::ResumeClose() {
    peer_close_->Close(graceful_close_);
    graceful_close_ = true;
    return true;
}

BgpPeer::BgpPeer(BgpServer *server, RoutingInstance *instance,
                 const BgpNeighborConfig *config)
        : server_(server),
          rtinstance_(instance),
          peer_key_(config),
          peer_port_(config->source_port()),
          peer_name_(config->name()),
          router_type_(config->router_type()),
          config_(config),
          index_(server->RegisterPeer(this)),
          trigger_(boost::bind(&BgpPeer::ResumeClose, this),
                   TaskScheduler::GetInstance()->GetTaskId("bgp::StateMachine"),
                   GetTaskInstance()),
          buffer_len_(0),
          session_(NULL),
          keepalive_timer_(TimerManager::CreateTimer(*server->ioservice(),
                     "BGP keepalive timer",
                   TaskScheduler::GetInstance()->GetTaskId("bgp::StateMachine"),
                   GetTaskInstance())),
          eor_send_timer_start_time_(0),
          send_ready_(true),
          admin_down_(config->admin_down()),
          passive_(config->passive()),
          resolve_paths_(config->router_type() == "bgpaas-client"),
          as_override_(config->as_override()),
          defer_close_(false),
          graceful_close_(true),
          vpn_tables_registered_(false),
          hold_time_(config->hold_time()),
          local_as_(config->local_as()),
          peer_as_(config->peer_as()),
          local_bgp_id_(config->local_identifier()),
          peer_bgp_id_(0),
          peer_type_((config->peer_as() == config->local_as()) ?
                         BgpProto::IBGP : BgpProto::EBGP),
          state_machine_(BgpObjectFactory::Create<StateMachine>(this)),
          peer_close_(BgpObjectFactory::Create<BgpPeerClose>(this)),
          peer_stats_(new PeerStats(this)),
          deleter_(new DeleteActor(this)),
          instance_delete_ref_(this, instance ? instance->deleter() : NULL),
          flap_count_(0),
          total_flap_count_(0),
          last_flap_(0),
          inuse_authkey_type_(AuthenticationData::NIL) {
    close_manager_.reset(
        BgpObjectFactory::Create<PeerCloseManager>(peer_close_.get()));
    ostringstream oss1;
    oss1 << peer_key_.endpoint.address();
    if (peer_key_.endpoint.port() != BgpConfigManager::kDefaultPort)
        oss1 << ":" << dec << peer_key_.endpoint.port();
    to_str_ = oss1.str();

    ostringstream oss2;
    if (rtinstance_)
        oss2 << rtinstance_->name() << ":";
    oss2 << server_->localname() << ":";
    oss2 << peer_name();
    uve_key_str_ = oss2.str();

    if (router_type_ == "control-node" ||
        router_type_ == "external-control-node") {
        peer_is_control_node_ = true;
    } else {
        peer_is_control_node_ = false;
    }

    membership_req_pending_ = 0;
    BGP_LOG_PEER(Event, this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
        BGP_PEER_DIR_NA, "Created");

    if (rtinstance_ && peer_name_.find(rtinstance_->name()) == 0) {
        peer_basename_ = peer_name_.substr(rtinstance_->name().size() + 1);
    } else {
        peer_basename_ = peer_name_;
    }

    for (Address::Family family = Address::UNSPEC;
            family < Address::NUM_FAMILIES;
            family = static_cast<Address::Family>(family + 1)) {
         eor_send_timer_[family] =
             TimerManager::CreateTimer(*server->ioservice(),
                 "BGP EoR Send timer for " + Address::FamilyToString(family),
                 TaskScheduler::GetInstance()->GetTaskId("bgp::StateMachine"),
                 GetTaskInstance());

         eor_receive_timer_[family] =
             TimerManager::CreateTimer(*server->ioservice(),
                 "BGP EoR Receive timer for " + Address::FamilyToString(family),
                 TaskScheduler::GetInstance()->GetTaskId("bgp::StateMachine"),
                 GetTaskInstance());
    }

    total_path_count_ = 0;
    primary_path_count_ = 0;

    if (resolve_paths_) {
        rtinstance_->GetTable(Address::INET)->LocatePathResolver();
        rtinstance_->GetTable(Address::INET6)->LocatePathResolver();
    }

    ProcessEndpointConfig(config);
    ProcessAuthKeyChainConfig(config);
    ProcessFamilyAttributesConfig(config);

    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_admin_down(admin_down_);
    peer_info.set_passive(passive_);
    peer_info.set_as_override(as_override_);
    peer_info.set_router_type(router_type_);
    peer_info.set_peer_type(
        PeerType() == BgpProto::IBGP ? "internal" : "external");
    peer_info.set_local_asn(local_as_);
    peer_info.set_peer_asn(peer_as_);
    peer_info.set_peer_port(peer_port_);
    peer_info.set_hold_time(hold_time_);
    peer_info.set_local_id(local_bgp_id_);
    peer_info.set_configured_families(configured_families_);
    peer_info.set_peer_address(peer_key_.endpoint.address().to_string());
    BGPPeerInfoSend(peer_info);
}

BgpPeer::~BgpPeer() {
    assert(!close_manager()->IsCloseInProgress());
    assert(!IsCloseInProgress());
    assert(GetTotalPathCount() == 0);
    STLDeleteValues(&family_attributes_list_);
    ClearListenSocketAuthKey();
    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_deleted(true);
    BGPPeerInfoSend(peer_info);

    PeerStatsData peer_stats_data;
    peer_stats_data.set_name(ToUVEKey());
    peer_stats_data.set_deleted(true);
    PeerStatsUve::Send(peer_stats_data, "ObjectBgpPeer");

    PeerFlapData peer_flap_data;
    peer_flap_data.set_name(ToUVEKey());
    peer_flap_data.set_deleted(true);
    PeerFlap::Send(peer_flap_data, "ObjectBgpPeer");

    BGP_LOG_PEER(Event, this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
        BGP_PEER_DIR_NA, "Deleted");
}

void BgpPeer::Initialize() {
    if (!admin_down_)
        state_machine_->Initialize();
}

void BgpPeer::NotifyEstablished(bool established) {
    if (established) {
        if (router_type_ == "bgpaas-client") {
            server_->IncrementUpBgpaasPeerCount();
        } else {
            server_->IncrementUpPeerCount();
        }
    } else {
        if (router_type_ == "bgpaas-client") {
            server_->DecrementUpBgpaasPeerCount();
        } else {
            server_->DecrementUpPeerCount();
        }
    }
}

void BgpPeer::BindLocalEndpoint(BgpSession *session) {
}

// Just return the first entry for now.
bool BgpPeer::GetBestAuthKey(AuthenticationKey *auth_key,
    KeyType *key_type) const {
    if (auth_data_.Empty()) {
        return false;
    }
    assert(auth_key);
    AuthenticationData::const_iterator iter = auth_data_.begin();
    *auth_key = *iter;
    *key_type = auth_data_.key_type();
    return true;
}

bool BgpPeer::ProcessAuthKeyChainConfig(const BgpNeighborConfig *config) {
    const AuthenticationData &input_auth_data = config->auth_data();

    if (auth_data_ == input_auth_data) {
        return false;
    }

    auth_data_ = input_auth_data;
    return InstallAuthKeys();
}

bool BgpPeer::InstallAuthKeys() {
    if (!PeerAddress()) {
        return false;
    }

    AuthenticationKey auth_key;
    KeyType key_type;
    bool valid = GetBestAuthKey(&auth_key, &key_type);
    if (valid) {
        if (key_type == AuthenticationData::MD5) {
            LogInstallAuthKeys("Listen", "add", auth_key, key_type);
            SetListenSocketAuthKey(auth_key, key_type);
            SetInuseAuthKeyInfo(auth_key, key_type);
        }
    } else {
        // If there are no valid available keys but an older one is currently
        // installed, un-install it.
        if (inuse_authkey_type_ == AuthenticationData::MD5) {
            LogInstallAuthKeys("Listen", "delete", inuse_auth_key_,
                               inuse_authkey_type_);
            ClearListenSocketAuthKey();
            // Resetting the key information must be done last.
            ResetInuseAuthKeyInfo();
        }
    }
    return true;
}

void BgpPeer::SetInuseAuthKeyInfo(const AuthenticationKey &key, KeyType type) {
    inuse_auth_key_ = key;
    inuse_authkey_type_ = type;
}

void BgpPeer::ResetInuseAuthKeyInfo() {
    inuse_auth_key_.Reset();
    inuse_authkey_type_ = AuthenticationData::NIL;
}

void BgpPeer::SetListenSocketAuthKey(const AuthenticationKey &auth_key,
                                     KeyType key_type) {
    if (key_type == AuthenticationData::MD5) {
        server_->session_manager()->
            SetListenSocketMd5Option(PeerAddress(), auth_key.value);
    }
}

void BgpPeer::ClearListenSocketAuthKey() {
    if (inuse_authkey_type_ == AuthenticationData::MD5) {
        server_->session_manager()->SetListenSocketMd5Option(PeerAddress(), "");
    }
}

void BgpPeer::SetSessionSocketAuthKey(TcpSession *session) {
    if ((inuse_authkey_type_ == AuthenticationData::MD5) && PeerAddress()) {
        assert(!inuse_auth_key_.value.empty());
        LogInstallAuthKeys("Session", "add", inuse_auth_key_,
                           inuse_authkey_type_);
        session->SetMd5SocketOption(PeerAddress(), inuse_auth_key_.value);
    }
}

string BgpPeer::GetInuseAuthKeyValue() const {
    return inuse_auth_key_.value;
}

void BgpPeer::LogInstallAuthKeys(const string &socket_name,
        const string &oper, const AuthenticationKey &auth_key,
        KeyType key_type) {
    string logstr = socket_name + " socket kernel " + oper + " of key id "
                          + integerToString(auth_key.id) + ", type "
                          + AuthenticationData::KeyTypeToString(key_type)
                          + ", peer " + peer_name_;
    BGP_LOG_PEER(Config, this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
                 BGP_PEER_DIR_NA, logstr);
}

//
// Process family attributes configuration and update the family attributes
// list.
//
// Return true is there's a change, false otherwise.
//
bool BgpPeer::ProcessFamilyAttributesConfig(const BgpNeighborConfig *config) {
    FamilyAttributesList family_attributes_list(Address::NUM_FAMILIES);
    BOOST_FOREACH(const BgpFamilyAttributesConfig family_config,
        config->family_attributes_list()) {
        Address::Family family =
            Address::FamilyFromString(family_config.family);
        assert(family != Address::UNSPEC);
        BgpPeerFamilyAttributes *family_attributes =
            new BgpPeerFamilyAttributes(config, family_config);
        family_attributes_list[family] = family_attributes;
    }

    int ret = STLSortedCompare(
        family_attributes_list.begin(), family_attributes_list.end(),
        family_attributes_list_.begin(), family_attributes_list_.end(),
        BgpPeerFamilyAttributesCompare());
    STLDeleteValues(&family_attributes_list_);
    family_attributes_list_ = family_attributes_list;
    configured_families_ = config->GetAddressFamilies();
    return (ret != 0);
}

void BgpPeer::ProcessEndpointConfig(const BgpNeighborConfig *config) {
    if (config->router_type() == "bgpaas-client") {
        endpoint_ = TcpSession::Endpoint(Ip4Address(), config->source_port());
    } else {
        endpoint_ = TcpSession::Endpoint();
    }
}

void BgpPeer::ConfigUpdate(const BgpNeighborConfig *config) {
    if (IsDeleted())
        return;

    config_ = config;

    // During peer deletion, configuration gets completely deleted. In that
    // case, there is no need to update the rest and flap the peer.
    if (!config_)
        return;

    bool clear_session = false;
    bool admin_down_changed = false;
    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());

    if (admin_down_ != config->admin_down()) {
        SetAdminState(config->admin_down());
        peer_info.set_admin_down(admin_down_);
        admin_down_changed = true;
    }

    if (passive_ != config->passive()) {
        passive_ = config->passive();
        peer_info.set_passive(passive_);
        clear_session = true;
    }

    if (as_override_ != config->as_override()) {
        as_override_ = config->as_override();
        peer_info.set_as_override(as_override_);
        clear_session = true;
    }

    if (router_type_ != config->router_type()) {
        router_type_ = config->router_type();
        peer_info.set_router_type(router_type_);
        resolve_paths_ = (config->router_type() == "bgpaas-client");
        clear_session = true;
    }
    if (router_type_ == "control-node" ||
        router_type_ == "external-control-node") {
        peer_is_control_node_ = true;
    } else {
        peer_is_control_node_ = false;
    }

    // Check if there is any change in the peer address.
    // If the peer address is changing, remove the key for the older address.
    // Update with the new peer address and then process the key chain info
    // for the new peer below.
    BgpPeerKey key(config);
    if (peer_key_ != key) {
        ClearListenSocketAuthKey();
        peer_key_ = key;
        peer_info.set_peer_address(peer_key_.endpoint.address().to_string());
        clear_session = true;
    }
    if (ProcessAuthKeyChainConfig(config)) {
        clear_session = true;
    }

    if (peer_port_ != config->source_port()) {
        peer_port_ = config->source_port();
        peer_info.set_peer_port(peer_port_);
        clear_session = true;
    }
    ProcessEndpointConfig(config);

    BgpProto::BgpPeerType old_type = PeerType();
    if (local_as_ != config->local_as()) {
        local_as_ = config->local_as();
        peer_info.set_local_asn(local_as_);
        clear_session = true;
    }

    if (hold_time_ != config->hold_time()) {
        hold_time_ = config->hold_time();
        peer_info.set_hold_time(hold_time_);
        clear_session = true;
    }

    if (peer_as_ != config->peer_as()) {
        peer_as_ = config->peer_as();
        peer_info.set_peer_asn(peer_as_);
        clear_session = true;
    }

    boost::system::error_code ec;
    uint32_t local_bgp_id = config->local_identifier();
    if (local_bgp_id_ != local_bgp_id) {
        local_bgp_id_ = local_bgp_id;
        peer_info.set_local_id(local_bgp_id_);
        clear_session = true;
    }

    peer_type_ = (peer_as_ == local_as_) ? BgpProto::IBGP : BgpProto::EBGP;
    if (old_type != PeerType()) {
        peer_info.set_peer_type(
            PeerType() == BgpProto::IBGP ? "internal" : "external");
        clear_session = true;
    }

    // Check if there is any change in private-as-action configuration.
    if (private_as_action_ != config->private_as_action()) {
        private_as_action_ = config->private_as_action();
        clear_session = true;
    }

    // Check if there is any change in the configured address families.
    if (ProcessFamilyAttributesConfig(config)) {
        peer_info.set_configured_families(configured_families_);
        clear_session = true;
    }

    // Note that the state machine would have been stopped via SetAdminDown
    // if admin down was set to true above.  Further, it's not necessary to
    // clear the peer if it's already admin down.
    if (!admin_down_changed && !admin_down_ && clear_session) {
        BGP_LOG_PEER(Config, this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_NA,
                     "Session cleared due to configuration change");
        Clear(BgpProto::Notification::OtherConfigChange);
    }

    // Send the UVE as appropriate.
    if (admin_down_changed || clear_session) {
        BGPPeerInfoSend(peer_info);
    }
}

void BgpPeer::ClearConfig() {
    CHECK_CONCURRENCY("bgp::Config");
    config_ = NULL;
}

LifetimeActor *BgpPeer::deleter() {
    return deleter_.get();
}

//
// Check if the given address family has been negotiated with the peer.
//
bool BgpPeer::IsFamilyNegotiated(Address::Family family) {
    // Bail if the family is not configured locally.
    if (!LookupFamily(family))
        return false;

    // Check if the peer advertised it in his Open message.
    uint16_t afi;
    uint8_t safi;
    tie(afi, safi) = BgpAf::FamilyToAfiSafi(family);
    return MpNlriAllowed(afi, safi);
}

// Release resources for a peer that is going to be deleted.
void BgpPeer::PostCloseRelease() {
    if (index_ != -1) {
        server_->UnregisterPeer(this);
        index_ = -1;
    }
    TimerManager::DeleteTimer(keepalive_timer_);

    for (Address::Family family = Address::UNSPEC;
            family < Address::NUM_FAMILIES;
            family = static_cast<Address::Family>(family + 1)) {
        TimerManager::DeleteTimer(eor_send_timer_[family]);
        TimerManager::DeleteTimer(eor_receive_timer_[family]);
    }
}

// IsReady
//
// Check whether this peer is up and ready
//
bool BgpPeer::IsReady() const {
    return state_machine_->get_state() == StateMachine::ESTABLISHED;
}

bool BgpPeer::IsXmppPeer() const {
    return false;
}

uint32_t BgpPeer::local_bgp_identifier() const {
    return ntohl(local_bgp_id_);
}

string BgpPeer::local_bgp_identifier_string() const {
    return Ip4Address(ntohl(local_bgp_id_)).to_string();
}

uint32_t BgpPeer::bgp_identifier() const {
    return ntohl(peer_bgp_id_);
}

string BgpPeer::bgp_identifier_string() const {
    return Ip4Address(ntohl(peer_bgp_id_)).to_string();
}

string BgpPeer::transport_address_string() const {
    TcpSession::Endpoint endpoint;
    ostringstream oss;
    if (session_)
        endpoint = session_->remote_endpoint();
    oss << endpoint;
    return oss.str();
}

string BgpPeer::gateway_address_string(Address::Family family) const {
    if (!family_attributes_list_[family])
        return string();
    return family_attributes_list_[family]->gateway_address.to_string();
}

//
// Customized close routing for BgpPeers.
//
// Reset all stored capabilities information and cancel outstanding timers.
//
void BgpPeer::CustomClose() {
    negotiated_families_.clear();
    ResetCapabilities();
    keepalive_timer_->Cancel();

    for (Address::Family family = Address::UNSPEC;
            family < Address::NUM_FAMILIES;
            family = static_cast<Address::Family>(family + 1)) {
        eor_send_timer_[family]->Cancel();
        eor_receive_timer_[family]->Cancel();
    }
}

//
// Close this peer by closing all of it's RIBs.
//
void BgpPeer::Close(bool graceful) {
    send_ready_ = true;
    if (membership_req_pending_ && !close_manager_->IsMembershipInUse()) {
        BGP_LOG_PEER(Event, this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_NA, "Close procedure deferred");
        defer_close_ = true;

        // Note down non-graceful closures. Once a close is non-graceful,
        // it shall remain as non-graceful.
        graceful_close_ &= graceful;
        return;
    }

    peer_close_->Close(graceful);
}

IPeerClose *BgpPeer::peer_close() {
    return peer_close_.get();
}

IPeerClose *BgpPeer::peer_close() const {
    return peer_close_.get();
}

void BgpPeer::UpdateCloseRouteStats(Address::Family family,
        const BgpPath *old_path, uint32_t path_flags) const {
    peer_close()->UpdateRouteStats(family, old_path, path_flags);
}

IPeerDebugStats *BgpPeer::peer_stats() {
    return peer_stats_.get();
}

const IPeerDebugStats *BgpPeer::peer_stats() const {
    return peer_stats_.get();
}

void BgpPeer::Clear(int subcode) {
    CHECK_CONCURRENCY("bgp::Config");
    state_machine_->Shutdown(subcode);
}

//
// Check whether this peer has been marked for deletion from configuration
//
bool BgpPeer::IsDeleted() const {
    return deleter_->IsDeleted();
}

bool BgpPeer::IsInGRTimerWaitState() const {
    return close_manager_->IsInGRTimerWaitState();
}

bool BgpPeer::IsCloseInProgress() const {
    CHECK_CONCURRENCY("bgp::Config");

    // trigger is set only after defer_close is reset
    assert(!(defer_close_ && trigger_.IsSet()));
    return defer_close_ || trigger_.IsSet() ||
            (close_manager_->IsCloseInProgress() &&
             !IsInGRTimerWaitState());
}

StateMachine::State BgpPeer::GetState() const {
    return state_machine_->get_state();
}

const string BgpPeer::GetStateName() const {
    return state_machine_->StateName();
}

BgpSession *BgpPeer::CreateSession() {
    TcpSession *session = server_->session_manager()->CreateSession();
    if (session == NULL) {
        return NULL;
    }

    // Set valid keys, if any, in the socket.
    SetSessionSocketAuthKey(session);

    BgpSession *bgp_session = static_cast<BgpSession *>(session);
    BindLocalEndpoint(bgp_session);
    bgp_session->set_peer(this);
    return bgp_session;
}

void BgpPeer::SetAdminState(bool down) {
    if (admin_down_ == down)
        return;
    admin_down_ = down;
    state_machine_->SetAdminState(down);
    if (admin_down_) {
        BGP_LOG_PEER(Config, this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_NA, "Session cleared due to admin down");
    }
}

bool BgpPeer::AcceptSession(BgpSession *session) {
    session->set_peer(this);

    // Set valid keys, if any, in the socket.
    SetSessionSocketAuthKey(session);

    return state_machine_->PassiveOpen(session);
}

void BgpPeer::Register(BgpTable *table, const RibExportPolicy &policy) {
    // In a highly corner case scenario, GR timer could fire right after a
    // session comes back up. In that case, CloseManager possibly could still
    // be using membership manager. Instead of creating a queue of these
    // register requests until close manager is done processing and process
    // them later, we could just as well just reset the session.
    if (close_manager_->IsMembershipInUse()) {
        BGP_LOG_PEER(Config, this, SandeshLevel::SYS_NOTICE, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_IN, "Session cleared due to GR not ready");
        Close(true);
    }

    if (close_manager_->IsMembershipInWait())
        assert(membership_req_pending_ > 0);
    BgpMembershipManager *membership_mgr = server_->membership_mgr();
    membership_req_pending_++;
    membership_mgr->Register(this, table, policy);

    // Start EndOfRib receive timer.
    StartEndOfRibReceiveTimer(table->family());
}

void BgpPeer::Register(BgpTable *table) {
    if (close_manager_->IsMembershipInUse()) {
        BGP_LOG_PEER(Config, this, SandeshLevel::SYS_NOTICE, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_IN, "Session cleared due to GR not ready");
        Close(true);
    }

    if (close_manager_->IsMembershipInWait())
        assert(membership_req_pending_ > 0);
    BgpMembershipManager *membership_mgr = server_->membership_mgr();
    membership_mgr->RegisterRibIn(this, table);

    // Start EndOfRib receive timer.
    StartEndOfRibReceiveTimer(table->family());
}

//
// Register to tables for negotiated address families.
//
// If the route-target family is negotiated, defer ribout registration
// to VPN tables till we receive End-Of-RIB marker for the route-target
// NLRI or till the EndOfRibTimer expires.  This ensures that we do not
// start sending VPN routes to the peer till we know what route targets
// the peer is interested in.
//
// Note that we do ribin registration right away even if the route-target
// family is negotiated. This allows received VPN routes to be processed
// normally before ribout registration to VPN tables is completed.
//
void BgpPeer::RegisterAllTables() {
    RoutingInstance *instance = GetRoutingInstance();

    BGP_LOG_PEER(Event, this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
        BGP_PEER_DIR_NA, "Established");

    vector<Address::Family> family_list = list_of
        (Address::INET)(Address::INET6);
    BOOST_FOREACH(Address::Family family, family_list) {
        if (!IsFamilyNegotiated(family))
            continue;
        BgpTable *table = instance->GetTable(family);
        BGP_LOG_PEER_TABLE(this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                           table, "Register peer with the table");
        Register(table, BuildRibExportPolicy(family));
    }

    vpn_tables_registered_ = false;
    if (!IsFamilyNegotiated(Address::RTARGET)) {
        RegisterToVpnTables();
        return;
    }

    Address::Family family = Address::RTARGET;
    BgpTable *table = instance->GetTable(family);
    BGP_LOG_PEER_TABLE(this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
        table, "Register peer with the table");
    Register(table, BuildRibExportPolicy(family));

    vector<Address::Family> vpn_family_list = list_of
        (Address::INETVPN)(Address::INET6VPN)(Address::ERMVPN)(Address::EVPN);
    BOOST_FOREACH(Address::Family vpn_family, vpn_family_list) {
        if (!IsFamilyNegotiated(vpn_family))
            continue;
        BgpTable *table = instance->GetTable(vpn_family);
        BGP_LOG_PEER_TABLE(this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_TRACE,
            table, "Register ribin for peer with the table");
        Register(table);
    }
}

const vector<Address::Family> BgpPeer::supported_families_ = list_of
    (Address::INET)
    (Address::INETVPN)
    (Address::EVPN)
    (Address::RTARGET)
    (Address::ERMVPN)
    (Address::INET6)
    (Address::INET6VPN);

void BgpPeer::SendOpen(TcpSession *session) {
    BgpProto::OpenMessage openmsg;
    openmsg.as_num = local_as_;
    openmsg.holdtime = state_machine_->GetConfiguredHoldTime();
    openmsg.identifier = ntohl(local_bgp_id_);
    BgpProto::OpenMessage::OptParam *opt_param =
        new BgpProto::OpenMessage::OptParam;

    static const uint8_t cap_mp[][4] = {
        { 0, (uint8_t) BgpAf::FamilyToAfi(supported_families_[0]), 0,
             BgpAf::FamilyToSafi(supported_families_[0]) },
        { 0, (uint8_t) BgpAf::FamilyToAfi(supported_families_[1]), 0,
             BgpAf::FamilyToSafi(supported_families_[1]) },
        { 0, (uint8_t) BgpAf::FamilyToAfi(supported_families_[2]), 0,
             BgpAf::FamilyToSafi(supported_families_[2]) },
        { 0, (uint8_t) BgpAf::FamilyToAfi(supported_families_[3]), 0,
             BgpAf::FamilyToSafi(supported_families_[3]) },
        { 0, (uint8_t) BgpAf::FamilyToAfi(supported_families_[4]), 0,
             BgpAf::FamilyToSafi(supported_families_[4]) },
        { 0, (uint8_t) BgpAf::FamilyToAfi(supported_families_[5]), 0,
             BgpAf::FamilyToSafi(supported_families_[5]) },
        { 0, (uint8_t) BgpAf::FamilyToAfi(supported_families_[6]), 0,
             BgpAf::FamilyToSafi(supported_families_[6]) },
    };

    static const FamilyToCapabilityMap family_to_cap_map = map_list_of
        (supported_families_[0], cap_mp[0])
        (supported_families_[1], cap_mp[1])
        (supported_families_[2], cap_mp[2])
        (supported_families_[3], cap_mp[3])
        (supported_families_[4], cap_mp[4])
        (supported_families_[5], cap_mp[5])
        (supported_families_[6], cap_mp[6]);

    // Add capabilities for configured address families.
    BOOST_FOREACH(const FamilyToCapabilityMap::value_type &val,
        family_to_cap_map) {
        if (!LookupFamily(val.first))
            continue;
        BgpProto::OpenMessage::Capability *cap =
            new BgpProto::OpenMessage::Capability(
                BgpProto::OpenMessage::Capability::MpExtension, val.second, 4);
        opt_param->capabilities.push_back(cap);
    }

    peer_close_->AddGRCapabilities(opt_param);
    peer_close_->AddLLGRCapabilities(opt_param);

    if (opt_param->capabilities.size()) {
        openmsg.opt_params.push_back(opt_param);
    } else {
        delete opt_param;
    }
    uint8_t data[256];
    int result = BgpProto::Encode(&openmsg, data, sizeof(data));
    assert(result > BgpProto::kMinMessageSize);
    BGP_LOG_PEER(Message, this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
                 BGP_PEER_DIR_OUT, "Open "  << openmsg.ToString());
    session->Send(data, result, NULL);
    inc_tx_open();
}

void BgpPeer::SendKeepalive(bool from_timer) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);

    // Bail if there's no session for the peer anymore.
    if (!session_)
        return;

    BgpProto::Keepalive msg;
    uint8_t data[BgpProto::kMinMessageSize];
    int result = BgpProto::Encode(&msg, data, sizeof(data));
    assert(result == BgpProto::kMinMessageSize);
    SandeshLevel::type log_level = from_timer ? Sandesh::LoggingUtLevel() :
                                                SandeshLevel::SYS_INFO;
    BGP_LOG_PEER(Message, this, log_level, BGP_LOG_FLAG_SYSLOG,
                 BGP_PEER_DIR_OUT, "Keepalive");
    send_ready_ = session_->Send(data, result, NULL);
    inc_tx_keepalive();
}

static bool SkipUpdateSend() {
    static bool init_;
    static bool skip_;

    if (init_) return skip_;

    skip_ = getenv("BGP_SKIP_UPDATE_SEND") != NULL;
    init_ = true;

    return skip_;
}

//
// Accumulate the message in the update buffer.
// Flush the existing buffer if the message can't fit.
// Note that FlushUpdateUnlocked resets buffer_len_ to 0.
//
bool BgpPeer::SendUpdate(const uint8_t *msg, size_t msgsize,
    const string *msg_str) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    bool send_ready = true;
    if (buffer_len_ + msgsize > kBufferSize) {
        send_ready = FlushUpdateUnlocked();
        assert(buffer_len_ == 0);
    }
    copy(msg, msg + msgsize, buffer_ + buffer_len_);
    buffer_len_ += msgsize;
    inc_tx_update();
    return send_ready;
}

bool BgpPeer::FlushUpdateUnlocked() {
    // Bail if the update buffer is empty.
    if (buffer_len_ == 0)
        return true;

    // Bail if there's no session for the peer anymore.
    if (!session_) {
        buffer_len_ = 0;
        return true;
    }

    if (!SkipUpdateSend()) {
        send_ready_ = session_->Send(buffer_, buffer_len_, NULL);
        buffer_len_ = 0;
        if (send_ready_) {
            StartKeepaliveTimerUnlocked();
        } else {
            StopKeepaliveTimerUnlocked();
        }
    } else {
        send_ready_ = true;
    }

    if (!send_ready_) {
        BGP_LOG_PEER(Event, this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_NA, "Send blocked");
    }
    return send_ready_;
}

bool BgpPeer::FlushUpdate() {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    return FlushUpdateUnlocked();
}

//
// Check if hard close can be skipped upon notification message reception
// based on code. Typically, when we want to avail the use of GR/LLGR, it is
// better not to do hard close and do GR instead for as many cases as possible.
//
bool BgpPeer::SkipNotificationReceive(int code, int subcode) const {
    switch (static_cast<BgpProto::Notification::Code>(code)) {
        case BgpProto::Notification::MsgHdrErr:
            switch (static_cast<BgpProto::Notification::MsgHdrSubCode>(
                        subcode)) {
            case BgpProto::Notification::ConnNotSync:
                break;
            case BgpProto::Notification::BadMsgLength:
                break;
            case BgpProto::Notification::BadMsgType:
                break;
            }
            break;
        case BgpProto::Notification::OpenMsgErr:
            switch (static_cast<BgpProto::Notification::OpenMsgSubCode>(
                        subcode)) {
            case BgpProto::Notification::UnsupportedVersion:
                break;
            case BgpProto::Notification::BadPeerAS:
                break;
            case BgpProto::Notification::BadBgpId:
                break;
            case BgpProto::Notification::UnsupportedOptionalParam:
                break;
            case BgpProto::Notification::AuthenticationFailure:
                break;
            case BgpProto::Notification::UnacceptableHoldTime:
                break;
            case BgpProto::Notification::UnsupportedCapability:
                break;
            }
            break;
        case BgpProto::Notification::UpdateMsgErr:
            switch (static_cast<BgpProto::Notification::UpdateMsgSubCode>(
                        subcode)) {
            case BgpProto::Notification::MalformedAttributeList:
                break;
            case BgpProto::Notification::UnrecognizedWellKnownAttrib:
                break;
            case BgpProto::Notification::MissingWellKnownAttrib:
                break;
            case BgpProto::Notification::AttribFlagsError:
                break;
            case BgpProto::Notification::AttribLengthError:
                break;
            case BgpProto::Notification::InvalidOrigin:
                break;
            case BgpProto::Notification::InvalidNH:
                break;
            case BgpProto::Notification::OptionalAttribError:
                break;
            case BgpProto::Notification::InvalidNetworkField:
                break;
            case BgpProto::Notification::MalformedASPath:
                break;
            }
            break;
        case BgpProto::Notification::HoldTimerExp:
            return true;
        case BgpProto::Notification::FSMErr:
            switch (static_cast<BgpProto::Notification::FsmSubcode>(subcode)) {
            case BgpProto::Notification::UnspecifiedError:
                break;
            case BgpProto::Notification::OpenSentError:
                break;
            case BgpProto::Notification::OpenConfirmError:
                break;
            case BgpProto::Notification::EstablishedError:
                break;
            }
            break;
        case BgpProto::Notification::Cease:
            switch (static_cast<BgpProto::Notification::CeaseSubCode>(
                        subcode)) {
            case BgpProto::Notification::Unknown:
                return true;
            case BgpProto::Notification::MaxPrefixes:
                return true;
            case BgpProto::Notification::AdminShutdown:
                break;
            case BgpProto::Notification::PeerDeconfigured:
                break;
            case BgpProto::Notification::AdminReset:
                break;
            case BgpProto::Notification::ConnectionRejected:
                break;
            case BgpProto::Notification::OtherConfigChange:
                return true;
            case BgpProto::Notification::ConnectionCollision:
                break;
            case BgpProto::Notification::OutOfResources:
                return true;
            }
            break;
    }
    return false;
}

//
// Check if notification send can be skipped. Typically, when we want to avail
// the use of GR/LLGR, it is better not to send a notification message so that
// peer retains the routes (as stale) we have already sent over this session.
//
bool BgpPeer::SkipNotificationSend(int code, int subcode) const {
    switch (static_cast<BgpProto::Notification::Code>(code)) {
        case BgpProto::Notification::MsgHdrErr:
            switch (static_cast<BgpProto::Notification::MsgHdrSubCode>(
                        subcode)) {
            case BgpProto::Notification::ConnNotSync:
                break;
            case BgpProto::Notification::BadMsgLength:
                break;
            case BgpProto::Notification::BadMsgType:
                break;
            }
            break;
        case BgpProto::Notification::OpenMsgErr:
            switch (static_cast<BgpProto::Notification::OpenMsgSubCode>(
                        subcode)) {
            case BgpProto::Notification::UnsupportedVersion:
                break;
            case BgpProto::Notification::BadPeerAS:
                break;
            case BgpProto::Notification::BadBgpId:
                break;
            case BgpProto::Notification::UnsupportedOptionalParam:
                break;
            case BgpProto::Notification::AuthenticationFailure:
                break;
            case BgpProto::Notification::UnacceptableHoldTime:
                break;
            case BgpProto::Notification::UnsupportedCapability:
                break;
            }
            break;
        case BgpProto::Notification::UpdateMsgErr:
            switch (static_cast<BgpProto::Notification::UpdateMsgSubCode>(
                        subcode)) {
            case BgpProto::Notification::MalformedAttributeList:
                break;
            case BgpProto::Notification::UnrecognizedWellKnownAttrib:
                break;
            case BgpProto::Notification::MissingWellKnownAttrib:
                break;
            case BgpProto::Notification::AttribFlagsError:
                break;
            case BgpProto::Notification::AttribLengthError:
                break;
            case BgpProto::Notification::InvalidOrigin:
                break;
            case BgpProto::Notification::InvalidNH:
                break;
            case BgpProto::Notification::OptionalAttribError:
                break;
            case BgpProto::Notification::InvalidNetworkField:
                break;
            case BgpProto::Notification::MalformedASPath:
                break;
            }
            break;
        case BgpProto::Notification::HoldTimerExp:
            return true;
        case BgpProto::Notification::FSMErr:
            switch (static_cast<BgpProto::Notification::FsmSubcode>(subcode)) {
            case BgpProto::Notification::UnspecifiedError:
                break;
            case BgpProto::Notification::OpenSentError:
                break;
            case BgpProto::Notification::OpenConfirmError:
                break;
            case BgpProto::Notification::EstablishedError:
                break;
            }
            break;
        case BgpProto::Notification::Cease:
            switch (static_cast<BgpProto::Notification::CeaseSubCode>(
                        subcode)) {
            case BgpProto::Notification::Unknown:
                return true;
            case BgpProto::Notification::MaxPrefixes:
                return true;
            case BgpProto::Notification::AdminShutdown:
                break;
            case BgpProto::Notification::PeerDeconfigured:
                break;
            case BgpProto::Notification::AdminReset:
                break;
            case BgpProto::Notification::ConnectionRejected:
                break;
            case BgpProto::Notification::OtherConfigChange:
                return true;
            case BgpProto::Notification::ConnectionCollision:
                break;
            case BgpProto::Notification::OutOfResources:
                return true;
            }
            break;
    }
    return false;
}

void BgpPeer::SendNotification(BgpSession *session,
        int code, int subcode, const string &data) {
    // Check if we can skip sending this notification message.
    if (peer_close_->IsCloseGraceful() && SkipNotificationSend(code, subcode))
        return;

    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    session->SendNotification(code, subcode, data);
    state_machine_->set_last_notification_out(code, subcode, data);
    inc_tx_notification();
}

bool BgpPeer::SetCapabilities(const BgpProto::OpenMessage *msg) {
    peer_bgp_id_ = htonl(msg->identifier);
    capabilities_.clear();
    vector<BgpProto::OpenMessage::OptParam *>::const_iterator it;
    for (it = msg->opt_params.begin(); it < msg->opt_params.end(); ++it) {
        capabilities_.insert(capabilities_.end(), (*it)->capabilities.begin(),
                             (*it)->capabilities.end());
        (*it)->capabilities.clear();
    }

    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_peer_id(peer_bgp_id_);

    vector<string> families;
    vector<BgpProto::OpenMessage::Capability *>::iterator cap_it;
    for (cap_it = capabilities_.begin(); cap_it < capabilities_.end();
         ++cap_it) {
        if ((*cap_it)->code != BgpProto::OpenMessage::Capability::MpExtension)
            continue;
        uint8_t *data = (*cap_it)->capability.data();
        uint16_t afi = get_value(data, 2);
        uint8_t safi = get_value(data + 3, 1);
        Address::Family family = BgpAf::AfiSafiToFamily(afi, safi);
        if (family == Address::UNSPEC) {
            families.push_back(BgpAf::ToString(afi, safi));
        } else {
            families.push_back(Address::FamilyToString(family));
        }
    }
    peer_info.set_families(families);

    negotiated_families_.clear();
    for (int idx = Address::UNSPEC; idx < Address::NUM_FAMILIES; ++idx) {
        if (!family_attributes_list_[idx])
            continue;
        Address::Family family = static_cast<Address::Family>(idx);
        uint16_t afi;
        uint8_t safi;
        tie(afi, safi) = BgpAf::FamilyToAfiSafi(family);
        if (!MpNlriAllowed(afi, safi))
            continue;
        negotiated_families_.push_back(Address::FamilyToString(family));
    }
    sort(negotiated_families_.begin(), negotiated_families_.end());
    peer_info.set_negotiated_families(negotiated_families_);

    // Process GR/LLGR capabilities and close non-gracefully if any ongoing GR
    // needs be aborted.
    if (!peer_close_->SetGRCapabilities(&peer_info)) {
        BGP_LOG_PEER(Message, this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_IN, "Close non-gracefully");
        Close(false);
        return false;
    }
    return true;
}

// Reset capabilities stored inside peer structure.
//
// When open message is processed, we directly take the capabilities off the
// open message and store inside the peer structure.
//
void BgpPeer::ResetCapabilities() {
    STLDeleteValues(&capabilities_);
    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    vector<string> families = vector<string>();
    peer_info.set_families(families);
    vector<string> negotiated_families = vector<string>();
    peer_info.set_negotiated_families(negotiated_families);
    peer_info.set_graceful_restart_families(vector<string>());
    BGPPeerInfoSend(peer_info);
}

bool BgpPeer::MpNlriAllowed(uint16_t afi, uint8_t safi) {
    vector<BgpProto::OpenMessage::Capability *>::iterator it;
    for (it = capabilities_.begin(); it < capabilities_.end(); ++it) {
        if ((*it)->code != BgpProto::OpenMessage::Capability::MpExtension)
            continue;
        uint8_t *data = (*it)->capability.data();
        uint16_t af_value = get_value(data, 2);
        uint8_t safi_value = get_value(data + 3, 1);
        if (afi == af_value && safi == safi_value) {
            return true;
        }
    }
    return false;
}

template <typename TableT, typename PrefixT>
void BgpPeer::ProcessNlri(Address::Family family, DBRequest::DBOperation oper,
    const BgpMpNlri *nlri, BgpAttrPtr attr, uint32_t flags) {
    TableT *table = static_cast<TableT *>(rtinstance_->GetTable(family));
    assert(table);

    for (vector<BgpProtoPrefix *>::const_iterator it = nlri->nlri.begin();
         it != nlri->nlri.end(); ++it) {
        PrefixT prefix;
        BgpAttrPtr new_attr(attr);
        uint32_t label = 0;
        uint32_t l3_label = 0;
        int result = PrefixT::FromProtoPrefix(server_, **it,
            (oper == DBRequest::DB_ENTRY_ADD_CHANGE ? attr.get() : NULL),
            &prefix, &new_attr, &label, &l3_label);
        if (result) {
            BGP_LOG_PEER_WARNING(Message, this,
                BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                "MP NLRI parse error for " <<
                Address::FamilyToString(family) << " route");
            continue;
        }

        DBRequest req;
        req.oper = oper;
        if (oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
            req.data.reset(new typename TableT::RequestData(
                new_attr, flags, label, l3_label, 0));
        }
        req.key.reset(new typename TableT::RequestKey(prefix, this));
        table->Enqueue(&req);
    }
}

uint32_t BgpPeer::GetPathFlags(Address::Family family,
    const BgpAttr *attr) const {
    uint32_t flags = resolve_paths_ ? BgpPath::ResolveNexthop : 0;

    // Check for OriginatorId loop in case we are an RR client.
    if (peer_type_ == BgpProto::IBGP &&
        attr->originator_id().to_ulong() == ntohl(local_bgp_id_)) {
        flags |= BgpPath::OriginatorIdLooped;
    }

    if (!attr->as_path())
        return flags;

    // Check whether neighbor has appended its AS to the AS_PATH.
    if ((PeerType() == BgpProto::EBGP) &&
        (!attr->as_path()->path().AsLeftMostMatch(peer_as()))) {
        flags |= BgpPath::NoNeighborAs;
    }

    // Check for AS_PATH loop.
    uint8_t max_loop_count = family_attributes_list_[family]->loop_count;
    if (attr->as_path()->path().AsPathLoop(local_as_, max_loop_count)) {
        flags |= BgpPath::AsPathLooped;
    }

    return flags;
}

uint32_t BgpPeer::GetLocalPrefFromMed(uint32_t med) const {
    if (peer_type_ != BgpProto::EBGP)
        return 0;
    if (!peer_is_control_node_)
        return 0;
    if (med == 0)
        return 0;
    if (med == 100)
        return 200;
    if (med == 200)
        return 100;
    return numeric_limits<uint32_t>::max() - med;
}

void BgpPeer::ProcessUpdate(const BgpProto::Update *msg, size_t msgsize) {
    BgpAttrPtr attr = server_->attr_db()->Locate(msg->path_attributes);

    uint32_t local_pref = GetLocalPrefFromMed(attr->med());
    if (local_pref) {
        attr = server_->attr_db()->ReplaceLocalPreferenceAndLocate(attr.get(),
            local_pref);
    }

    uint32_t reach_count = 0, unreach_count = 0;
    RoutingInstance *instance = GetRoutingInstance();
    if (msg->nlri.size() || msg->withdrawn_routes.size()) {
        InetTable *table =
            static_cast<InetTable *>(instance->GetTable(Address::INET));
        if (!table) {
            BGP_LOG_WARNING_STR(BgpConfig, BGP_LOG_FLAG_ALL,
                                "Cannot find inet table");
            return;
        }

        unreach_count += msg->withdrawn_routes.size();
        for (vector<BgpProtoPrefix *>::const_iterator it =
             msg->withdrawn_routes.begin(); it != msg->withdrawn_routes.end();
             ++it) {
            Ip4Prefix prefix;
            int result = Ip4Prefix::FromProtoPrefix((**it), &prefix);
            if (result) {
                BGP_LOG_PEER_WARNING(Message, this,
                    BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                    "Withdrawn route parse error for inet route");
                continue;
            }

            DBRequest req;
            req.oper = DBRequest::DB_ENTRY_DELETE;
            req.data.reset(NULL);
            req.key.reset(new InetTable::RequestKey(prefix, this));
            table->Enqueue(&req);
        }

        uint32_t flags = GetPathFlags(Address::INET, attr.get());
        reach_count += msg->nlri.size();
        for (vector<BgpProtoPrefix *>::const_iterator it = msg->nlri.begin();
             it != msg->nlri.end(); ++it) {
            Ip4Prefix prefix;
            int result = Ip4Prefix::FromProtoPrefix((**it), &prefix);
            if (result) {
                BGP_LOG_PEER_WARNING(Message, this,
                    BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                    "NLRI parse error for inet route");
                continue;
            }

            DBRequest req;
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            req.data.reset(new InetTable::RequestData(attr, flags, 0, 0, 0));
            req.key.reset(new InetTable::RequestKey(prefix, this));
            table->Enqueue(&req);
        }
    }

    for (vector<BgpAttribute *>::const_iterator ait =
            msg->path_attributes.begin();
            ait != msg->path_attributes.end(); ++ait) {
        DBRequest::DBOperation oper;
        if ((*ait)->code == BgpAttribute::MPReachNlri) {
            oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        } else if ((*ait)->code == BgpAttribute::MPUnreachNlri) {
            oper = DBRequest::DB_ENTRY_DELETE;
        } else {
            continue;
        }

        BgpMpNlri *nlri = static_cast<BgpMpNlri *>(*ait);
        assert(nlri);
        if (oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
            reach_count += nlri->nlri.size();
        } else {
            unreach_count += nlri->nlri.size();
        }

        Address::Family family = BgpAf::AfiSafiToFamily(nlri->afi, nlri->safi);
        if (!IsFamilyNegotiated(family)) {
            BGP_LOG_PEER_NOTICE(Message, this,
                BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                "AFI "<< nlri->afi << " SAFI " << (int) nlri->safi <<
                " not allowed");
            continue;
        }

        // Handle EndOfRib marker.
        if (oper == DBRequest::DB_ENTRY_DELETE && nlri->nlri.empty()) {
            inc_rx_end_of_rib();
            BGP_LOG_PEER(Message, this, SandeshLevel::SYS_INFO,
                         BGP_LOG_FLAG_SYSLOG, BGP_PEER_DIR_IN,
                         "EndOfRib marker family " <<
                         Address::FamilyToString(family) <<
                         " size " << msgsize);
            ReceiveEndOfRIB(family, msgsize);
            return;
        }

        uint32_t flags = 0;
        if ((*ait)->code == BgpAttribute::MPReachNlri) {
            flags = GetPathFlags(family, attr.get());
            attr = GetMpNlriNexthop(nlri, attr);
        }

        switch (family) {
        case Address::INET:
            ProcessNlri<InetTable, Ip4Prefix>(
                family, oper, nlri, attr, flags);
            break;
        case Address::INETVPN:
            ProcessNlri<InetVpnTable, InetVpnPrefix>(
                family, oper, nlri, attr, flags);
            break;
        case Address::INET6:
            ProcessNlri<Inet6Table, Inet6Prefix>(
                family, oper, nlri, attr, flags);
            break;
        case Address::INET6VPN:
            ProcessNlri<Inet6VpnTable, Inet6VpnPrefix>(
                family, oper, nlri, attr, flags);
            break;
        case Address::EVPN:
            ProcessNlri<EvpnTable, EvpnPrefix>(
                family, oper, nlri, attr, flags);
            break;
        case Address::ERMVPN:
            ProcessNlri<ErmVpnTable, ErmVpnPrefix>(
                family, oper, nlri, attr, flags);
            break;
        case Address::RTARGET:
            ProcessNlri<RTargetTable, RTargetPrefix>(
                family, oper, nlri, attr, flags);
            break;
        default:
            break;
        }
    }

    inc_rx_route_reach(reach_count);
    inc_rx_route_unreach(unreach_count);
    if (Sandesh::LoggingLevel() >= Sandesh::LoggingUtLevel()) {
        BGP_LOG_PEER(Message, this, Sandesh::LoggingUtLevel(),
            BGP_LOG_FLAG_SYSLOG, BGP_PEER_DIR_IN,
            "Update size " << msgsize <<
            " reach " << reach_count << " unreach " << unreach_count);
    }
}

void BgpPeer::EndOfRibTimerErrorHandler(string error_name,
                                        string error_message) {
    BGP_LOG_PEER_CRITICAL(Timer, this, BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                 "Timer error: " << error_name << " " << error_message);
}

void BgpPeer::RegisterToVpnTables() {
    CHECK_CONCURRENCY("bgp::StateMachine", "bgp::RTFilter");

    if (vpn_tables_registered_)
        return;
    vpn_tables_registered_ = true;

    RoutingInstance *instance = GetRoutingInstance();
    vector<Address::Family> vpn_family_list = list_of
        (Address::INETVPN)(Address::INET6VPN)(Address::ERMVPN)(Address::EVPN);
    BOOST_FOREACH(Address::Family vpn_family, vpn_family_list) {
        if (!IsFamilyNegotiated(vpn_family))
            continue;
        BgpTable *table = instance->GetTable(vpn_family);
        BGP_LOG_PEER_TABLE(this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_TRACE,
            table, "Register peer with the table");
        Register(table, BuildRibExportPolicy(vpn_family));
    }
}

void BgpPeer::StartEndOfRibReceiveTimer(Address::Family family) {
    uint32_t timeout = GetEndOfRibReceiveTime(family);

    BGP_LOG_PEER(Message, this, SandeshLevel::SYS_INFO,
        BGP_LOG_FLAG_SYSLOG, BGP_PEER_DIR_OUT,
        "EndOfRib Receive Timer scheduled for family " <<
        Address::FamilyToString(family) <<
        " to fire after " << timeout * 1000 << " milliseconds");
    eor_receive_timer_[family]->Start(timeout * 1000,
        boost::bind(&BgpPeer::EndOfRibReceiveTimerExpired, this, family),
        boost::bind(&BgpPeer::EndOfRibTimerErrorHandler, this, _1, _2));
}

void BgpPeer::KeepaliveTimerErrorHandler(string error_name,
                                         string error_message) {
    BGP_LOG_PEER_CRITICAL(Timer, this, BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,
                 "Timer error: " << error_name << " " << error_message);
}

bool BgpPeer::EndOfRibReceiveTimerExpired(Address::Family family) {
    if (family == Address::RTARGET)
        RegisterToVpnTables();

    // Fake reception of EoRs to exit from GR states and sweep all stale routes.
    close_manager_->ProcessEORMarkerReceived(family);
    return false;
}

bool BgpPeer::KeepaliveTimerExpired() {
    if (!IsReady())
        return false;

    SendKeepalive(true);

    //
    // Start the timer again, by returning true
    //
    return true;
}

void BgpPeer::StartKeepaliveTimerUnlocked() {
    int keepalive_time_msecs = state_machine_->keepalive_time_msecs();
    if (keepalive_time_msecs <= 0)
        return;

    keepalive_timer_->Start(keepalive_time_msecs,
        boost::bind(&BgpPeer::KeepaliveTimerExpired, this),
        boost::bind(&BgpPeer::KeepaliveTimerErrorHandler, this, _1, _2));
}

void BgpPeer::StartKeepaliveTimer() {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (session_ && send_ready_)
        StartKeepaliveTimerUnlocked();
}

void BgpPeer::StopKeepaliveTimerUnlocked() {
    keepalive_timer_->Cancel();
}

bool BgpPeer::KeepaliveTimerRunning() {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    return keepalive_timer_->running();
}

void BgpPeer::SetSendReady() {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    BGP_LOG_PEER(Event, this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                 BGP_PEER_DIR_NA, "Send ready");
    send_ready_ = true;
    if (session_ != NULL)
        StartKeepaliveTimerUnlocked();
}

void BgpPeer::set_session(BgpSession *session) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    assert(session_ == NULL);
    session_ = session;
}

void BgpPeer::clear_session() {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (session_) {
        session_->clear_peer();
        session_->set_observer(NULL);
        session_->Close();
    }
    session_ = NULL;
}

BgpSession *BgpPeer::session() {
    return session_;
}

string BgpPeer::BytesToHexString(const u_int8_t *msg, size_t size) {
    ostringstream out;
    char buf[4];

    for (size_t i = 0; i < size; i ++) {
        if (!(i % 32)) out << "\n";
        if (!(i %  4)) out << " 0x";
        snprintf(buf, sizeof(buf), "%02X", msg[i]);
        out << buf;
    }

    out << "\n";

    return out.str();
}

bool BgpPeer::ReceiveMsg(BgpSession *session, const u_int8_t *msg,
                         size_t size) {
    ParseErrorContext ec;
    BgpProto::BgpMessage *minfo = BgpProto::Decode(msg, size, &ec);

    if (minfo == NULL) {
        BGP_TRACE_PEER_PACKET(this, msg, size, SandeshLevel::SYS_WARN);
        BGP_LOG_PEER_WARNING(Message, this, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_IN,
                     "Error while parsing message at " << ec.type_name);
        state_machine_->OnMessageError(session, &ec);
        return false;
    }

    // Tracing periodic keepalive packets is not necessary.
    if (minfo->type != BgpProto::KEEPALIVE)
        BGP_TRACE_PEER_PACKET(this, msg, size, Sandesh::LoggingUtLevel());

    state_machine_->OnMessage(session, minfo, size);
    return true;
}

//
// Extract nexthop address from BgpMpNlri if appropriate and return updated
// BgpAttrPtr. The original attribute is returned for cases where there's no
// nexthop attribute in the BgpMpNlri.
//
BgpAttrPtr BgpPeer::GetMpNlriNexthop(BgpMpNlri *nlri, BgpAttrPtr attr) {
    bool update_nh = false;
    IpAddress addr;

    if (nlri->afi == BgpAf::IPv4) {
        if (nlri->safi == BgpAf::Unicast || nlri->safi == BgpAf::RTarget) {
            Ip4Address::bytes_type bt = { { 0 } };
            copy(nlri->nexthop.begin(),
                nlri->nexthop.begin() + sizeof(bt), bt.begin());
            addr = Ip4Address(bt);
            update_nh = true;
        } else if (nlri->safi == BgpAf::Vpn) {
            Ip4Address::bytes_type bt = { { 0 } };
            size_t rdsize = RouteDistinguisher::kSize;
            copy(nlri->nexthop.begin() + rdsize,
                nlri->nexthop.begin() + rdsize + sizeof(bt), bt.begin());
            addr = Ip4Address(bt);
            update_nh = true;
        }
    } else if (nlri->afi == BgpAf::L2Vpn) {
        if (nlri->safi == BgpAf::EVpn) {
            Ip4Address::bytes_type bt = { { 0 } };
            copy(nlri->nexthop.begin(),
                nlri->nexthop.begin() + sizeof(bt), bt.begin());
            addr = Ip4Address(bt);
            update_nh = true;
        }
    } else if (nlri->afi == BgpAf::IPv6) {
        if (nlri->safi == BgpAf::Unicast) {
            // There could be either 1 or 2 v6 addresses in the nexthop field.
            // The first one is supposed to be global and the optional second
            // one, if present, is link local. We will be liberal and find the
            // global address, whether it's first or second. Further, if the
            // address is a v4-mapped v6 address, we use the corresponding v4
            // address as the nexthop.
            for (int idx = 0; idx < 2; ++idx) {
                Ip6Address::bytes_type bt = { { 0 } };
                if ((idx + 1) * sizeof(bt) > nlri->nexthop.size())
                    break;
                copy(nlri->nexthop.begin() + idx * sizeof(bt),
                    nlri->nexthop.begin() + (idx + 1) * sizeof(bt), bt.begin());
                Ip6Address v6_addr(bt);
                if (v6_addr.is_v4_mapped()) {
                    addr = Address::V4FromV4MappedV6(v6_addr);
                    update_nh = true;
                    break;
                }
                if (!v6_addr.is_link_local()) {
                    addr = v6_addr;
                    update_nh = true;
                    break;
                }
            }
        } else if (nlri->safi == BgpAf::Vpn) {
            Ip6Address::bytes_type bt = { { 0 } };
            size_t rdsize = RouteDistinguisher::kSize;
            copy(nlri->nexthop.begin() + rdsize,
                nlri->nexthop.begin() + rdsize + sizeof(bt), bt.begin());
            Ip6Address v6_addr(bt);
            if (v6_addr.is_v4_mapped()) {
                addr = Address::V4FromV4MappedV6(v6_addr);
                update_nh = true;
            }
        }
    }

    // Always update the nexthop in BgpAttr with MpReachNlri->nexthop.
    // NOP in cases <afi,safi> doesn't carry nexthop attribute.
    if (update_nh) {
        attr = server_->attr_db()->ReplaceNexthopAndLocate(attr.get(), addr);
    }
    return attr;
}

void BgpPeer::ManagedDelete() {
    if (deleter_->IsDeleted())
        return;
    BGP_LOG_PEER(Config, this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
                 BGP_PEER_DIR_NA, "Received request for deletion");
    if (router_type_ == "bgpaas-client") {
        server()->increment_deleting_bgpaas_count();
    } else {
        server()->increment_deleting_count();
    }
    deleter_->Delete();
}

void BgpPeer::RetryDelete() {
    if (!deleter_->IsDeleted())
        return;
    deleter_->RetryDelete();
}

int BgpPeer::GetTaskInstance() const {
    return index_ % TaskScheduler::GetInstance()->HardwareThreadCount();
}

void BgpPeer::SetDataCollectionKey(BgpPeerInfo *peer_info) const {
    if (rtinstance_) {
        peer_info->set_domain(rtinstance_->name());
    } else {
        peer_info->set_domain(BgpConfigManager::kMasterInstance);
    }

    peer_info->set_ip_address(peer_key_.endpoint.address().to_string());
}

static void FillProtoStats(const IPeerDebugStats::ProtoStats &stats,
                           PeerProtoStats *proto_stats) {
    proto_stats->set_open(stats.open);
    proto_stats->set_keepalive(stats.keepalive);
    proto_stats->set_close(stats.close);
    proto_stats->set_update(stats.update);
    proto_stats->set_notification(stats.notification);
    proto_stats->set_total(stats.open + stats.keepalive + stats.close +
        stats.update + stats.notification);
}

static void FillRouteUpdateStats(const IPeerDebugStats::UpdateStats &stats,
                                 PeerUpdateStats *rt_stats) {
    rt_stats->set_reach(stats.reach);
    rt_stats->set_unreach(stats.unreach);
    rt_stats->set_end_of_rib(stats.end_of_rib);
    rt_stats->set_total(stats.reach + stats.unreach + stats.end_of_rib);
}

static void FillSocketStats(const IPeerDebugStats::SocketStats &socket_stats,
                            PeerSocketStats *peer_socket_stats) {
    peer_socket_stats->set_calls(socket_stats.calls);
    peer_socket_stats->set_bytes(socket_stats.bytes);
    if (socket_stats.calls) {
        peer_socket_stats->set_average_bytes(
            socket_stats.bytes/socket_stats.calls);
    }
    peer_socket_stats->set_blocked_count(socket_stats.blocked_count);
    ostringstream os;
    os << boost::posix_time::microseconds(socket_stats.blocked_duration_usecs);
    peer_socket_stats->set_blocked_duration(os.str());
    if (socket_stats.blocked_count) {
        os.str("");
        os << boost::posix_time::microseconds(
            socket_stats.blocked_duration_usecs/socket_stats.blocked_count);
        peer_socket_stats->set_average_blocked_duration(os.str());
    }
}

void BgpPeer::FillCloseInfo(BgpNeighborResp *resp) const {
    close_manager_->FillCloseInfo(resp);
}

void BgpPeer::FillBgpNeighborDebugState(BgpNeighborResp *bnr,
                                        const IPeerDebugStats *peer_stats) {
    bnr->set_last_state(peer_stats->last_state());
    bnr->set_last_event(peer_stats->last_event());
    bnr->set_last_error(peer_stats->last_error());
    bnr->set_last_state_at(peer_stats->last_state_change_at());
    bnr->set_flap_count(peer_stats->num_flaps());
    bnr->set_flap_time(peer_stats->last_flap());

    IPeerDebugStats::ProtoStats stats;
    PeerProtoStats proto_stats;
    peer_stats->GetRxProtoStats(&stats);
    FillProtoStats(stats, &proto_stats);
    bnr->set_rx_proto_stats(proto_stats);

    peer_stats->GetTxProtoStats(&stats);
    FillProtoStats(stats, &proto_stats);
    bnr->set_tx_proto_stats(proto_stats);

    IPeerDebugStats::UpdateStats update_stats;
    PeerUpdateStats rt_stats;
    peer_stats->GetRxRouteUpdateStats(&update_stats);
    FillRouteUpdateStats(update_stats, &rt_stats);
    bnr->set_rx_update_stats(rt_stats);

    peer_stats->GetTxRouteUpdateStats(&update_stats);
    FillRouteUpdateStats(update_stats, &rt_stats);
    bnr->set_tx_update_stats(rt_stats);

    IPeerDebugStats::SocketStats socket_stats;
    PeerSocketStats peer_socket_stats;

    peer_stats->GetRxSocketStats(&socket_stats);
    FillSocketStats(socket_stats, &peer_socket_stats);
    bnr->set_rx_socket_stats(peer_socket_stats);

    peer_stats->GetTxSocketStats(&socket_stats);
    FillSocketStats(socket_stats, &peer_socket_stats);
    bnr->set_tx_socket_stats(peer_socket_stats);
}

void BgpPeer::FillBgpNeighborFamilyAttributes(BgpNeighborResp *nbr) const {
    vector<ShowBgpNeighborFamily> show_family_attributes_list;
    for (int idx = Address::UNSPEC; idx < Address::NUM_FAMILIES; ++idx) {
        if (!family_attributes_list_[idx])
            continue;
        ShowBgpNeighborFamily show_family_attributes;
        show_family_attributes.set_family(
            Address::FamilyToString(static_cast<Address::Family>(idx)));
        show_family_attributes.set_loop_count(
            family_attributes_list_[idx]->loop_count);
        show_family_attributes.set_prefix_limit(
            family_attributes_list_[idx]->prefix_limit);
        if (!family_attributes_list_[idx]->gateway_address.is_unspecified()) {
            show_family_attributes.set_gateway_address(
                family_attributes_list_[idx]->gateway_address.to_string());
        }
        show_family_attributes_list.push_back(show_family_attributes);
    }
    nbr->set_family_attributes_list(show_family_attributes_list);
}

void BgpPeer::FillNeighborInfo(const BgpSandeshContext *bsc,
    BgpNeighborResp *bnr, bool summary) const {
    bnr->set_instance_name(rtinstance_->name());
    bnr->set_peer(peer_basename_);
    bnr->set_deleted(IsDeleted());
    bnr->set_closed_at(UTCUsecToString(deleter_->delete_time_stamp_usecs()));
    bnr->set_admin_down(admin_down_);
    bnr->set_passive(passive_);
    bnr->set_as_override(as_override_);
    bnr->set_private_as_action(private_as_action_);
    bnr->set_peer_address(peer_address_string());
    bnr->set_peer_id(bgp_identifier_string());
    bnr->set_peer_asn(peer_as());
    bnr->set_peer_port(peer_port());
    bnr->set_transport_address(transport_address_string());
    bnr->set_encoding("BGP");
    bnr->set_peer_type(PeerType() == BgpProto::IBGP ? "internal" : "external");
    bnr->set_router_type(router_type_);
    bnr->set_state(state_machine_->StateName());
    bnr->set_local_address(server_->ToString());
    bnr->set_local_id(Ip4Address(ntohl(local_bgp_id_)).to_string());
    bnr->set_local_asn(local_as());
    bnr->set_negotiated_hold_time(state_machine_->hold_time());
    bnr->set_primary_path_count(GetPrimaryPathCount());
    bnr->set_task_instance(GetTaskInstance());
    bnr->set_send_ready(send_ready_);
    bnr->set_flap_count(peer_stats_->num_flaps());
    bnr->set_flap_time(peer_stats_->last_flap());
    bnr->set_auth_type(
        AuthenticationData::KeyTypeToString(inuse_authkey_type_));
    if (bsc->test_mode()) {
        bnr->set_auth_keys(auth_data_.KeysToStringDetail());
    }

    if (summary)
        return;

    bnr->set_configured_address_families(configured_families_);
    bnr->set_negotiated_address_families(negotiated_families_);

    peer_close_->FillNeighborInfo(bnr);

    bnr->set_configured_hold_time(state_machine_->GetConfiguredHoldTime());
    FillBgpNeighborFamilyAttributes(bnr);
    FillBgpNeighborDebugState(bnr, peer_stats_.get());
    BgpMembershipManager *mgr = server_->membership_mgr();
    mgr->FillPeerMembershipInfo(this, bnr);
    bnr->set_routing_instances(vector<BgpNeighborRoutingInstance>());
    FillCloseInfo(bnr);
}

void BgpPeer::inc_rx_open() {
    peer_stats_->proto_stats_[0].open++;
}

void BgpPeer::inc_tx_open() {
    peer_stats_->proto_stats_[1].open++;
}

void BgpPeer::inc_rx_keepalive() {
    peer_stats_->proto_stats_[0].keepalive++;
}

uint64_t BgpPeer::get_rx_keepalive() const {
    return peer_stats_->proto_stats_[0].keepalive;
}

void BgpPeer::inc_tx_keepalive() {
    peer_stats_->proto_stats_[1].keepalive++;
}

uint64_t BgpPeer::get_tx_keepalive() const {
    return peer_stats_->proto_stats_[1].keepalive;
}

void BgpPeer::inc_rx_update() {
    peer_stats_->proto_stats_[0].update++;
}

uint64_t BgpPeer::get_rx_update() const {
    return peer_stats_->proto_stats_[0].update;
}

void BgpPeer::inc_tx_update() {
    peer_stats_->proto_stats_[1].update++;
}

uint64_t BgpPeer::get_tx_update() const {
    return peer_stats_->proto_stats_[1].update;
}

void BgpPeer::inc_rx_notification() {
    peer_stats_->proto_stats_[0].notification++;
}

uint64_t BgpPeer::get_rx_notification() const {
    return peer_stats_->proto_stats_[0].notification;
}

void BgpPeer::inc_tx_notification() {
    peer_stats_->proto_stats_[1].notification++;
}

void BgpPeer::inc_rx_end_of_rib() {
    peer_stats_->update_stats_[0].end_of_rib++;
}

uint64_t BgpPeer::get_rx_end_of_rib() const {
    return peer_stats_->update_stats_[0].end_of_rib;
}

void BgpPeer::inc_tx_end_of_rib() {
    peer_stats_->update_stats_[1].end_of_rib++;
}

uint64_t BgpPeer::get_tx_end_of_rib() const {
    return peer_stats_->update_stats_[1].end_of_rib;
}

void BgpPeer::inc_rx_route_reach(uint64_t count) {
    peer_stats_->update_stats_[0].reach += count;
}

uint64_t BgpPeer::get_rx_route_reach() const {
    return peer_stats_->update_stats_[0].reach;
}

uint64_t BgpPeer::get_tx_route_reach() const {
    return peer_stats_->update_stats_[1].reach;
}

void BgpPeer::inc_rx_route_unreach(uint64_t count) {
    peer_stats_->update_stats_[0].unreach += count;
}

uint64_t BgpPeer::get_rx_route_unreach() const {
    return peer_stats_->update_stats_[0].unreach;
}

uint64_t BgpPeer::get_tx_route_unreach() const {
    return peer_stats_->update_stats_[1].unreach;
}

uint64_t BgpPeer::get_rx_route_total() const {
    return peer_stats_->update_stats_[0].reach +
        peer_stats_->update_stats_[0].unreach +
        peer_stats_->update_stats_[0].end_of_rib;
}

uint64_t BgpPeer::get_tx_route_total() const {
    return peer_stats_->update_stats_[1].reach +
        peer_stats_->update_stats_[1].unreach +
        peer_stats_->update_stats_[1].end_of_rib;
}

void BgpPeer::inc_connect_error() {
    peer_stats_->error_stats_.connect_error++;
}

void BgpPeer::inc_connect_timer_expired() {
    peer_stats_->error_stats_.connect_timer++;
}

void BgpPeer::inc_hold_timer_expired() {
    peer_stats_->error_stats_.hold_timer++;
}

void BgpPeer::inc_open_error() {
    peer_stats_->error_stats_.open_error++;
}

void BgpPeer::inc_update_error() {
    peer_stats_->error_stats_.update_error++;
}

uint64_t BgpPeer::get_connect_error() const {
    return peer_stats_->error_stats_.connect_error;
}

uint64_t BgpPeer::get_connect_timer_expired() const {
    return peer_stats_->error_stats_.connect_timer;
}

uint64_t BgpPeer::get_hold_timer_expired() const {
    return peer_stats_->error_stats_.hold_timer;
}

uint64_t BgpPeer::get_open_error() const {
    return peer_stats_->error_stats_.open_error;
}

uint64_t BgpPeer::get_update_error() const {
    return peer_stats_->error_stats_.update_error;
}

uint64_t BgpPeer::get_socket_reads() const {
    IPeerDebugStats::SocketStats stats;
    peer_stats_->GetRxSocketStats(&stats);
    return stats.calls;
}

uint64_t BgpPeer::get_socket_writes() const {
    IPeerDebugStats::SocketStats stats;
    peer_stats_->GetTxSocketStats(&stats);
    return stats.calls;
}

string BgpPeer::last_flap_at() const {
    if (last_flap_) {
        return integerToString(UTCUsecToPTime(last_flap_));
    } else {
        return "";
    }
}

void BgpPeer::increment_flap_count() {
    flap_count_++;
    total_flap_count_++;
    last_flap_ = UTCTimestampUsec();

    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    PeerFlapInfo flap_info;
    flap_info.set_flap_count(flap_count_);
    flap_info.set_flap_time(last_flap_);
    peer_info.set_flap_info(flap_info);
    BGPPeerInfoSend(peer_info);

    PeerFlapData peer_flap_data;
    peer_flap_data.set_name(ToUVEKey());
    peer_flap_data.set_flap_info(flap_info);
    PeerFlap::Send(peer_flap_data, "ObjectBgpPeer");
}

void BgpPeer::reset_flap_count() {
    flap_count_ = 0;
    last_flap_ = 0;

    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    PeerFlapInfo flap_info;
    peer_info.set_flap_info(flap_info);
    BGPPeerInfoSend(peer_info);

    PeerFlapData peer_flap_data;
    peer_flap_data.set_name(ToUVEKey());
    peer_flap_data.set_flap_info(flap_info);
    PeerFlap::Send(peer_flap_data, "ObjectBgpPeer");
}
