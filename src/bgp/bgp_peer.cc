/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_peer.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>

#include "base/logging.h"
#include "base/task_annotations.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_proto.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session.h"
#include "bgp/state_machine.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/rtarget/rtarget_table.h"
#include "io/event_manager.h"
#include "net/address.h"
#include "net/bgp_af.h"

using boost::system::error_code;
using namespace std;

class BgpPeer::PeerClose : public IPeerClose {
  public:
    explicit PeerClose(BgpPeer *peer)
        : peer_(peer),
          is_closed_(false),
          manager_(BgpObjectFactory::Create<PeerCloseManager>(peer_)) {
    }

    virtual ~PeerClose() {
    }

    virtual std::string ToString() const {
        return peer_->ToString();
    }

    virtual bool IsCloseGraceful() {

        //
        // If the peer is deleted or administratively held down, do not attempt
        // graceful restart
        //
        if (peer_->IsDeleted() || peer_->IsAdminDown()) return false;

        return peer_->server()->IsPeerCloseGraceful();
    }

    virtual void CustomClose() {
        return peer_->CustomClose();
    }

    // CloseComplete
    //
    // Close process for this peer is complete. Restart the state machine and
    // attempt to bring up session with the neighbor
    //
    virtual bool CloseComplete(bool from_timer, bool gr_cancelled) {
        if (!peer_->IsDeleted()) {

            //
            // If this closure is off graceful restart timer, nothing else to
            // do as we retain the peer based on the configuration
            //
            if (from_timer) return false;

            //
            // Reset peer's state machine
            //
            if (!peer_->IsAdminDown()) peer_->state_machine_->Initialize();

            return false;
        }

        //
        // This peer is deleted. Timer should have already been cancelled
        //
        assert(!from_timer);

        peer_->deleter()->RetryDelete();
        is_closed_ = true;
        return true;
    }

    bool IsClosed() const {
        return is_closed_;
    }

    virtual PeerCloseManager *close_manager() {
        return manager_.get();
    }

    void Close() {
        if (!is_closed_) {
            manager_->Close();
        }
    }

private:
    BgpPeer *peer_;
    bool is_closed_;
    boost::scoped_ptr<PeerCloseManager> manager_;
};

class BgpPeer::PeerStats : public IPeerDebugStats {
public:
    explicit PeerStats(BgpPeer *peer)
        : peer_(peer) {
    }

    // Printable name
    virtual std::string ToString() const {
        return peer_->ToString();
    }
    // Previous State of the peer
    virtual std::string last_state() const {
        return peer_->state_machine()->LastStateName();
    }
    virtual std::string last_state_change_at() const {
        return peer_->state_machine()->last_state_change_at();
    }
    // Last error on this peer
    virtual std::string last_error() const {
        return peer_->state_machine()->last_notification_in_error();
    }
    // Last Event on this peer
    virtual std::string last_event() const {
        return peer_->state_machine()->last_event();
    }

    // When was the Last
    virtual std::string last_flap() const {
        return peer_->last_flap_at();
    }

    // Total number of flaps
    virtual uint32_t num_flaps() const {
        return peer_->flap_count();
    }

    virtual void GetRxProtoStats(ProtoStats &stats) const {
        stats = proto_stats_[0];
    }

    virtual void GetTxProtoStats(ProtoStats &stats) const {
        stats = proto_stats_[1];
    }

    virtual void GetRxRouteUpdateStats(UpdateStats &stats)  const {
        stats = update_stats_[0];
    }

    virtual void GetTxRouteUpdateStats(UpdateStats &stats)  const {
        stats = update_stats_[1];
    }

    virtual void GetRxSocketStats(SocketStats &stats) const {
        TcpServer::SocketStats socket_stats;

        if (peer_->session()) {
            socket_stats = peer_->session()->GetSocketStats();
            stats.calls = socket_stats.read_calls;
            stats.bytes = socket_stats.read_bytes;
        }
    }

    virtual void GetTxSocketStats(SocketStats &stats) const {
        TcpServer::SocketStats socket_stats;

        if (peer_->session()) {
            socket_stats = peer_->session()->GetSocketStats();
            stats.calls = socket_stats.write_calls;
            stats.bytes = socket_stats.write_bytes;
            stats.blocked_count = socket_stats.write_blocked;
            stats.blocked_duration_usecs =
                socket_stats.write_blocked_duration_usecs;
        }
    }

    virtual void UpdateTxUnreachRoute(uint32_t count) {
        update_stats_[1].unreach += count;
    }

    virtual void UpdateTxReachRoute(uint32_t count) {
        update_stats_[1].reach += count;
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
    DeleteActor(BgpPeer *peer)
        : LifetimeActor(peer->server_->lifetime_manager()),
          peer_(peer) {
    }

    virtual bool MayDelete() const {
        CHECK_CONCURRENCY("bgp::Config");
        if (!peer_->peer_close_->IsClosed())
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
        peer_->rtinstance_->peer_manager()->DestroyIPeer(peer_);
    }

private:
    BgpPeer *peer_;
};

void BgpPeer::SendEndOfRIB(Address::Family family) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);

    // Bail if there's no session for the peer anymore.
    if (!session_)
        return;

    BgpProto::Update update;
    uint16_t afi;
    uint8_t safi;
    BgpAf::FamilyToAfiSafi(family, afi, safi);
    BgpMpNlri *nlri = new BgpMpNlri(BgpAttribute::MPUnreachNlri, afi, safi);
    update.path_attributes.push_back(nlri);
    uint8_t data[256];
    int result = BgpProto::Encode(&update, data, sizeof(data));
    assert(result > BgpProto::kMinMessageSize);
    session_->Send(data, result, NULL);
}

void BgpPeer::MembershipRequestCallback(IPeer *ipeer, BgpTable *table, 
                                        bool established) {
    assert(membership_req_pending_);
    membership_req_pending_--;
    if (!membership_req_pending_ && defer_close_) {
        defer_close_ = false;
        trigger_.Set();
        return;
    }

    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_send_state("in sync");
    BGPPeerInfo::Send(peer_info);
    if (established) {
        // Generate End-Of-RIB message
        SendEndOfRIB(table->family());
    }
}

bool BgpPeer::ResumeClose() {
    peer_close_->Close();
    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_send_state("not advertising");
    BGPPeerInfo::Send(peer_info);
    return true;
}

BgpPeer::BgpPeer(BgpServer *server, RoutingInstance *instance,
                 const BgpNeighborConfig *config)
        : server_(server),
          rtinstance_(instance),
          peer_key_(config),
          peer_name_(config->name()),
          config_(config),
          index_(server->RegisterPeer(this)),
          trigger_(boost::bind(&BgpPeer::ResumeClose, this),
                   TaskScheduler::GetInstance()->GetTaskId("bgp::StateMachine"),
                   GetIndex()),
          session_(NULL),
          keepalive_timer_(TimerManager::CreateTimer(*server->ioservice(),
                     "BGP keepalive timer")),
          end_of_rib_timer_(TimerManager::CreateTimer(*server->ioservice(),
                   "BGP RTarget EndOfRib timer",
                   TaskScheduler::GetInstance()->GetTaskId("bgp::StateMachine"),
                   GetIndex())),
          send_ready_(true),
          control_node_(config_->vendor() == "contrail"),
          admin_down_(false),
          state_machine_(BgpObjectFactory::Create<StateMachine>(this)),
          membership_req_pending_(0),
          defer_close_(false),
          vpn_tables_registered_(false),
          local_as_(config->local_as()),
          peer_as_(config_->peer_as()),
          remote_bgp_id_(0),
          peer_type_((peer_as_ == local_as_) ? BgpProto::IBGP : BgpProto::EBGP),
          policy_(peer_type_, RibExportPolicy::BGP, peer_as_, -1, 0),
          peer_close_(new PeerClose(this)),
          peer_stats_(new PeerStats(this)),
          deleter_(new DeleteActor(this)),
          instance_delete_ref_(this, instance->deleter()),
          flap_count_(0),
          last_flap_(0) {

    boost::system::error_code ec;
    Ip4Address id = Ip4Address::from_string(config->local_identifier(), ec);
    local_bgp_id_ = id.to_ulong();

    refcount_ = 0;
    configured_families_ = config->address_families();
    sort(configured_families_.begin(), configured_families_.end());
    BOOST_FOREACH(string family, configured_families_) {
        Address::Family fmly = Address::FamilyFromString(family);
        assert(fmly != Address::UNSPEC);
        family_.insert(fmly);
    }

    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_peer_type(PeerType() == BgpProto::IBGP ? 
                            "internal" : "external");
    peer_info.set_local_asn(local_as_);
    peer_info.set_peer_asn(peer_as_);
    peer_info.set_local_id(local_bgp_id_);
    if (!config->address_families().empty())
        peer_info.set_configured_families(config->address_families());

    peer_info.set_peer_address(peer_key_.endpoint.address().to_string());
    BGPPeerInfo::Send(peer_info);
}

BgpPeer::~BgpPeer() {
    assert(GetRefCount() == 0);
    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_deleted(true);
    BGPPeerInfo::Send(peer_info);
}

void BgpPeer::Initialize() {
    state_machine_->Initialize();
}

void BgpPeer::BindLocalEndpoint(BgpSession *session) {
}

void BgpPeer::ConfigUpdate(const BgpNeighborConfig *config) {
    if (IsDeleted())
        return;

    config_ = config;

    // During peer deletion, configuration gets completely deleted. In that
    // case, there is no need to update the rest and flap the peer.
    if (!config_) return;

    AddressFamilyList new_families;
    configured_families_ = config->address_families();
    sort(configured_families_.begin(), configured_families_.end());
    BOOST_FOREACH(string family, configured_families_) {
        Address::Family fmly = Address::FamilyFromString(family);
        assert(fmly != Address::UNSPEC);
        new_families.insert(fmly);
    }

    bool clear_session = false;

    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());

    // Check if there is any change in the peer address.
    BgpPeerKey key(config);
    if (peer_key_ != key) {
        peer_key_ = key;
        peer_info.set_peer_address(peer_key_.endpoint.address().to_string());
        clear_session = true;
    }

    // Check if there is any change in the configured address families.
    if (family_ != new_families) {
        family_ = new_families;
        clear_session = true;
        peer_info.set_configured_families(config->address_families());
    }

    BgpProto::BgpPeerType old_type = PeerType();
    if (local_as_ != config->local_as()) {
        local_as_ = config->local_as();
        peer_info.set_local_asn(local_as_);
        clear_session = true;
    }

    if (peer_as_ != config->peer_as()) {
        peer_as_ = config->peer_as();
        peer_info.set_peer_asn(peer_as_);
        clear_session = true;
    }

    boost::system::error_code ec;
    Ip4Address id = Ip4Address::from_string(config->local_identifier(), ec);
    uint32_t local_bgp_id = id.to_ulong();
    if (local_bgp_id_ != local_bgp_id) {
        local_bgp_id_ = local_bgp_id;
        peer_info.set_local_id(local_bgp_id_);
        clear_session = true;
    }

    peer_type_ = (peer_as_ == local_as_) ? BgpProto::IBGP : BgpProto::EBGP;

    if (old_type != PeerType()) {
        peer_info.set_peer_type(PeerType() == BgpProto::IBGP ? 
                                "internal" : "external");
        policy_.type = peer_type_;
        policy_.as_number = peer_as_;
        clear_session = true;
    }

    if (clear_session) {
        BGP_LOG_PEER(Config, this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_NA,
                     "Session cleared due to configuration change");
        BGPPeerInfo::Send(peer_info);
        Clear(BgpProto::Notification::OtherConfigChange);
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
    switch (family) {
    case Address::INET:
        return MpNlriAllowed(BgpAf::IPv4, BgpAf::Unicast);
        break;
    case Address::INETVPN:
        return MpNlriAllowed(BgpAf::IPv4, BgpAf::Vpn);
        break;
    case Address::RTARGET:
        return MpNlriAllowed(BgpAf::IPv4, BgpAf::RTarget);
        break;
    case Address::EVPN:
        return MpNlriAllowed(BgpAf::L2Vpn, BgpAf::EVpn);
        break;
    case Address::ERMVPN:
        return MpNlriAllowed(BgpAf::IPv4, BgpAf::ErmVpn);
        break;
    default:
        break;
    }

    return false;
}

// Release resources for a peer that is going to be deleted.
void BgpPeer::PostCloseRelease() {
    if (index_ != -1) {
        server_->UnregisterPeer(this);
        index_ = -1;
    }
    TimerManager::DeleteTimer(keepalive_timer_);
    TimerManager::DeleteTimer(end_of_rib_timer_);
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

uint32_t BgpPeer::bgp_identifier() const {
    return remote_bgp_id_;
}

//
// Customized close routing for BgpPeers.
//
// Reset all stored capabilities information and cancel outstanding timers.
//
void BgpPeer::CustomClose() {
    assert(vpn_tables_registered_);
    negotiated_families_.clear();
    ResetCapabilities();
    keepalive_timer_->Cancel();
    end_of_rib_timer_->Cancel();
}

//
// Close this peer by closing all of it's RIBs.
//
// If we haven't registered to VPN tables, do it now before we kick off
// the peer close. This ensures that we will clean up all the VPN routes
// when we do eventually perform the peer close. This handles the corner
// case where the peer has to be closed before VPN tables are registered.
// Need to do this since we add VPN routes before we've registered to the
// VPN tables.
//
// Note that registering to VPN tables will bump membership_req_pending_
// in most normal cases i.e. when at least one VPN family is negotiated.
// Hence we must register to the VPN tables if needed before deciding if
// we need to defer peer close.
//
void BgpPeer::Close() {
    if (!vpn_tables_registered_) {
        RegisterToVpnTables(false);
    }

    if (membership_req_pending_) {
        defer_close_ = true;
        return;
    }

    peer_close_->Close();
    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_send_state("not advertising");
    BGPPeerInfo::Send(peer_info);
}

IPeerClose *BgpPeer::peer_close() {
    return peer_close_.get();
}

IPeerDebugStats *BgpPeer::peer_stats() {
    return peer_stats_.get();
}


void BgpPeer::Clear(int subcode) {
    state_machine_->Shutdown(subcode);
}

//
// Check whether this peer has been marked for deletion from configuration
//
bool BgpPeer::IsDeleted() const {
    return deleter_->IsDeleted();
}

bool BgpPeer::IsCloseInProgress() const {
    return (defer_close_ || peer_close_->close_manager()->IsCloseInProgress());
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

    BgpSession *bgp_session = static_cast<BgpSession *>(session);
    BindLocalEndpoint(bgp_session);
    bgp_session->SetPeer(this);
    return bgp_session;
}

void BgpPeer::SetAdminState(bool down) {
    if (admin_down_ != down) {
        admin_down_ = down;
        state_machine_->SetAdminState(down);
    }
}

bool BgpPeer::AcceptSession(BgpSession *session) {
    session->SetPeer(this);
    return state_machine_->PassiveOpen(session);
}

//
// Register to tables for negotiated address families.
//
// If the route-target family has been negotiated, defer registration to
// VPN tables till we receive an End-Of-RIB marker for the route-target
// NLRI or till the EndOfRibTimer expires.  This ensures that we do not
// start sending VPN routes to the peer till we know what route targets
// the peer is interested in.
//
// Note that received VPN routes are still processed normally even if we
// haven't registered this BgpPeer to the VPN table in question.
//
void BgpPeer::RegisterAllTables() {
    PeerRibMembershipManager *membership_mgr = server_->membership_mgr();
    RoutingInstance *instance = GetRoutingInstance();

    if (IsFamilyNegotiated(Address::INET)) {
        BgpTable *table = instance->GetTable(Address::INET);
        BGP_LOG_PEER_TABLE(this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                           table, "Register peer with the table");
        if (table) {
            membership_mgr->Register(this, table, policy_, -1,
                boost::bind(&BgpPeer::MembershipRequestCallback, this, _1, _2, 
                            true));
            membership_req_pending_++;
        }
    }

    vpn_tables_registered_ = false;
    if (IsFamilyNegotiated(Address::RTARGET)) {
        BgpTable *table = instance->GetTable(Address::RTARGET);
        BGP_LOG_PEER_TABLE(this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                           table, "Register peer with the table");
        if (table) {
            membership_mgr->Register(this, table, policy_, -1,
                boost::bind(&BgpPeer::MembershipRequestCallback, this, _1, _2,
                            true));
            membership_req_pending_++;
        }
        StartEndOfRibTimer();
    } else {
        RegisterToVpnTables(true);
    }

    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_send_state("not advertising");
    BGPPeerInfo::Send(peer_info);
}

void BgpPeer::SendOpen(TcpSession *session) {
    BgpSessionManager *session_mgr = 
        static_cast<BgpSessionManager *>(session->server());
    BgpServer *server = session_mgr->server();
    BgpProto::OpenMessage openmsg;
    openmsg.as_num = server->autonomous_system();
    openmsg.holdtime = state_machine_->GetConfiguredHoldTime();
    openmsg.identifier = local_bgp_id_;
    static const uint8_t cap_mp[5][4] = {
        { 0, BgpAf::IPv4,  0, BgpAf::Unicast },
        { 0, BgpAf::IPv4,  0, BgpAf::Vpn },
        { 0, BgpAf::L2Vpn, 0, BgpAf::EVpn },
        { 0, BgpAf::IPv4,  0, BgpAf::RTarget },
        { 0, BgpAf::IPv4,  0, BgpAf::ErmVpn },
    };

    BgpProto::OpenMessage::OptParam *opt_param =
            new BgpProto::OpenMessage::OptParam;
    if (LookupFamily(Address::INET)) {
        BgpProto::OpenMessage::Capability *cap =
                new BgpProto::OpenMessage::Capability(
                        BgpProto::OpenMessage::Capability::MpExtension,
                        cap_mp[0], 4);

        opt_param->capabilities.push_back(cap);
    }
    if (LookupFamily(Address::INETVPN)) {
        BgpProto::OpenMessage::Capability *cap =
                new BgpProto::OpenMessage::Capability(
                        BgpProto::OpenMessage::Capability::MpExtension,
                        cap_mp[1], 4);
        opt_param->capabilities.push_back(cap);
    }
    if (LookupFamily(Address::EVPN)) {
        BgpProto::OpenMessage::Capability *cap =
                new BgpProto::OpenMessage::Capability(
                        BgpProto::OpenMessage::Capability::MpExtension,
                        cap_mp[2], 4);
        opt_param->capabilities.push_back(cap);
    }
    if (LookupFamily(Address::RTARGET)) {
        BgpProto::OpenMessage::Capability *cap =
                new BgpProto::OpenMessage::Capability(
                        BgpProto::OpenMessage::Capability::MpExtension,
                        cap_mp[3], 4);
        opt_param->capabilities.push_back(cap);
    }
    if (LookupFamily(Address::ERMVPN)) {
        BgpProto::OpenMessage::Capability *cap =
                new BgpProto::OpenMessage::Capability(
                        BgpProto::OpenMessage::Capability::MpExtension,
                        cap_mp[4], 4);
        opt_param->capabilities.push_back(cap);
    }

    // Add restart capability for generating End-Of-Rib
    const uint8_t restart_cap[2] = { 0x0, 0x0 };
    BgpProto::OpenMessage::Capability *cap =
        new BgpProto::OpenMessage::Capability(
                          BgpProto::OpenMessage::Capability::GracefulRestart, 
                          restart_cap, 2);
    opt_param->capabilities.push_back(cap);

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

    peer_stats_->proto_stats_[1].open++;
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

    peer_stats_->proto_stats_[1].keepalive++;
}

static bool SkipUpdateSend() {
    static bool init_;
    static bool skip_;

    if (init_) return skip_;

    skip_ = getenv("BGP_SKIP_UPDATE_SEND") != NULL;
    init_ = true;

    return skip_;
}

bool BgpPeer::SendUpdate(const uint8_t *msg, size_t msgsize) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);

    // Bail if there's no session for the peer anymore.
    if (!session_)
        return true;

    if (!SkipUpdateSend()) {
        if (Sandesh::LoggingLevel() >= Sandesh::LoggingUtLevel()) {
            BGP_LOG_PEER(Message, this, Sandesh::LoggingUtLevel(),
                         BGP_LOG_FLAG_SYSLOG, BGP_PEER_DIR_OUT, "Update");
        }
        send_ready_ = session_->Send(msg, msgsize, NULL);
        if (send_ready_) {
            StartKeepaliveTimerUnlocked();
        } else {
            StopKeepaliveTimerUnlocked();
        }
    } else {
        send_ready_ = true;
    }
    peer_stats_->proto_stats_[1].update++;
    inc_tx_route_update();
    if (!send_ready_) {
        BGP_LOG_PEER(Event, this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_NA, "Send blocked");
        BgpPeerInfoData peer_info;
        peer_info.set_name(ToUVEKey());
        peer_info.set_send_state("not in sync");
        BGPPeerInfo::Send(peer_info);
    }
    return send_ready_;
}

void BgpPeer::SendNotification(BgpSession *session,
        int code, int subcode, const std::string &data) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    session->SendNotification(code, subcode, data);
    state_machine_->set_last_notification_out(code, subcode, data);
    peer_stats_->proto_stats_[1].notification++;
}

void BgpPeer::SetCapabilities(const BgpProto::OpenMessage *msg) {
    remote_bgp_id_ = msg->identifier;
    capabilities_.clear();
    std::vector<BgpProto::OpenMessage::OptParam *>::const_iterator it;
    for (it = msg->opt_params.begin(); it < msg->opt_params.end(); it++) {
        capabilities_.insert(capabilities_.end(), (*it)->capabilities.begin(),
                             (*it)->capabilities.end());
        (*it)->capabilities.clear();
    }

    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_peer_id(remote_bgp_id_);

    std::vector<std::string> families;
    std::vector<BgpProto::OpenMessage::Capability *>::iterator cap_it;
    for (cap_it = capabilities_.begin(); cap_it < capabilities_.end(); cap_it++) {
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
    BOOST_FOREACH(Address::Family family, family_) {
        uint16_t afi;
        uint8_t safi;
        BgpAf::FamilyToAfiSafi(family, afi, safi);
        if (!MpNlriAllowed(afi, safi))
            continue;
        negotiated_families_.push_back(Address::FamilyToString(family));
    }
    sort(negotiated_families_.begin(), negotiated_families_.end());
    peer_info.set_negotiated_families(negotiated_families_);

    BGPPeerInfo::Send(peer_info);
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
    std::vector<std::string> families = std::vector<std::string>();
    peer_info.set_families(families);
    std::vector<std::string> negotiated_families = std::vector<std::string>();
    peer_info.set_negotiated_families(negotiated_families);
    BGPPeerInfo::Send(peer_info);
}

bool BgpPeer::MpNlriAllowed(uint16_t afi, uint8_t safi) {
    std::vector<BgpProto::OpenMessage::Capability *>::iterator it;
    for (it = capabilities_.begin(); it < capabilities_.end(); it++) {
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

void BgpPeer::ProcessUpdate(const BgpProto::Update *msg) {
    BgpAttrPtr attr = server_->attr_db()->Locate(msg->path_attributes);
    // Check as path loop and neighbor-as 
    const BgpAttr *path_attr = attr.get(); 
    uint32_t flags = 0;

    inc_rx_route_update();

    if (path_attr->as_path() != NULL) {
        // Check whether neighbor has appended its AS to the AS_PATH
        if ((PeerType() == BgpProto::EBGP) && 
            (!path_attr->as_path()->path().AsLeftMostMatch(peer_as()))) {
            flags |= BgpPath::NoNeighborAs;
        }

        // Check for AS_PATH loop
        if (path_attr->as_path()->path().AsPathLoop(
                server_->autonomous_system())) {
            flags |= BgpPath::AsPathLooped;
        }
    }

    RoutingInstance *instance = GetRoutingInstance();
    if (msg->nlri.size() || msg->withdrawn_routes.size()) {
        InetTable *table =
            static_cast<InetTable *>(instance->GetTable(Address::INET));
        if (!table) {
            BGP_LOG_PEER(Message, this, SandeshLevel::SYS_CRIT, BGP_LOG_FLAG_ALL,
                         BGP_PEER_DIR_IN, "Cannot find inet table");
            return;
        }

        for (vector<BgpProtoPrefix *>::const_iterator it =
             msg->withdrawn_routes.begin(); it != msg->withdrawn_routes.end();
             ++it++) {
            DBRequest req;
            req.oper = DBRequest::DB_ENTRY_DELETE;
            req.data.reset(NULL);
            Ip4Prefix prefix = Ip4Prefix(**it);
            req.key.reset(new InetTable::RequestKey(prefix, this));
            table->Enqueue(&req);
            inc_rx_route_unreach();
        }

        for (vector<BgpProtoPrefix *>::const_iterator it = msg->nlri.begin();
             it != msg->nlri.end(); ++it) {
            DBRequest req;
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            req.data.reset(new InetTable::RequestData(attr, flags, 0));
            Ip4Prefix prefix = Ip4Prefix(**it);
            req.key.reset(new InetTable::RequestKey(prefix, this));
            table->Enqueue(&req);
            inc_rx_route_reach();
        }
    }

    for (std::vector<BgpAttribute *>::const_iterator ait =
            msg->path_attributes.begin();
            ait != msg->path_attributes.end(); ++ait) {
        DBRequest::DBOperation oper;
        if ((*ait)->code == BgpAttribute::MPReachNlri) {
            oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            inc_rx_route_reach();
        } else if ((*ait)->code == BgpAttribute::MPUnreachNlri) {
            oper = DBRequest::DB_ENTRY_DELETE;
            inc_rx_route_unreach();
        } else {
            continue;
        }

        BgpMpNlri *nlri = static_cast<BgpMpNlri *>(*ait);
        if (!nlri) continue;

        Address::Family family = BgpAf::AfiSafiToFamily(nlri->afi, nlri->safi);
        if (!IsFamilyNegotiated(family)) {
            BGP_LOG_PEER(Message, this, SandeshLevel::SYS_NOTICE, BGP_LOG_FLAG_ALL,
                         BGP_PEER_DIR_IN,
                         "AFI "<< nlri->afi << " SAFI " << (int) nlri->safi <<
                         " not allowed");
            continue;
        }

        if ((*ait)->code == BgpAttribute::MPReachNlri)
            attr = GetMpNlriNexthop(nlri, attr);

        switch (family) {
        case Address::INET: {
            InetTable *table =
                static_cast<InetTable *>(instance->GetTable(family));
            assert(table);

            vector<BgpProtoPrefix *>::const_iterator it;
            for (it = nlri->nlri.begin(); it < nlri->nlri.end(); it++) {
                DBRequest req;
                req.oper = oper;
                if (oper == DBRequest::DB_ENTRY_ADD_CHANGE)
                    req.data.reset(new InetTable::RequestData(attr, flags, 0));
                Ip4Prefix prefix = Ip4Prefix(**it);
                req.key.reset(new InetTable::RequestKey(prefix, this));
                table->Enqueue(&req);
            }
            break;
        }

        case Address::INETVPN: {
            InetVpnTable *table = 
              static_cast<InetVpnTable *>(instance->GetTable(family));
            assert(table);

            vector<BgpProtoPrefix *>::const_iterator it;
            for (it = nlri->nlri.begin(); it < nlri->nlri.end(); it++) {
                uint32_t label = ((*it)->prefix[0] << 16 |
                                  (*it)->prefix[1] << 8 |
                                  (*it)->prefix[2]) >> 4;
                DBRequest req;
                req.oper = oper;
                if (oper == DBRequest::DB_ENTRY_ADD_CHANGE)
                    req.data.reset(new InetVpnTable::RequestData(attr, flags, label));
                req.key.reset(new InetVpnTable::RequestKey(InetVpnPrefix(**it),
                                                           this));
                table->Enqueue(&req);
            }
            break;
        }

        case Address::EVPN: {
            EvpnTable *table =
              static_cast<EvpnTable *>(instance->GetTable(family));
            assert(table);

            vector<BgpProtoPrefix *>::const_iterator it;
            for (it = nlri->nlri.begin(); it < nlri->nlri.end(); it++) {
                if ((*it)->type != 2) {
                    BGP_LOG_PEER(Message, this, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                                 BGP_PEER_DIR_IN,
                                 "EVPN: Unsupported route type " << (*it)->type);
                    continue;
                }
                size_t label_offset = EvpnPrefix::label_offset(**it);
                uint32_t label = ((*it)->prefix[label_offset] << 16 |
                                  (*it)->prefix[label_offset + 1] << 8 |
                                  (*it)->prefix[label_offset + 2]) >> 4;
                DBRequest req;
                req.oper = oper;
                if (oper == DBRequest::DB_ENTRY_ADD_CHANGE)
                    req.data.reset(new EvpnTable::RequestData(attr, flags, label));
                req.key.reset(new EvpnTable::RequestKey(EvpnPrefix(**it), this));
                table->Enqueue(&req);
            }
            break;
        }

        case Address::RTARGET: {
            RTargetTable *table =
                static_cast<RTargetTable *>(instance->GetTable(Address::RTARGET));
            assert(table);
            if (oper == DBRequest::DB_ENTRY_DELETE && nlri->nlri.empty()) {
                // End-Of-RIB message
                end_of_rib_timer_->Cancel();
                RegisterToVpnTables(true);
                return;
            }
            vector<BgpProtoPrefix *>::const_iterator it;
            for (it = nlri->nlri.begin(); it < nlri->nlri.end(); it++) {
                DBRequest req;
                req.oper = oper;
                if (oper == DBRequest::DB_ENTRY_ADD_CHANGE)
                    req.data.reset(new RTargetTable::RequestData(attr, flags, 0));
                RTargetPrefix prefix = RTargetPrefix(**it);
                req.key.reset(new RTargetTable::RequestKey(prefix, this));
                table->Enqueue(&req);
            }
            break;
        }

        case Address::ERMVPN: {
            ErmVpnTable *table;
            table = static_cast<ErmVpnTable *>(instance->GetTable(family));
            assert(table);

            vector<BgpProtoPrefix *>::const_iterator it;
            for (it = nlri->nlri.begin(); it < nlri->nlri.end(); it++) {
                if (!ErmVpnPrefix::IsValidForBgp((*it)->type)) {
                    BGP_LOG_PEER(Message, this, SandeshLevel::SYS_WARN,
                        BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN,
                        "ERMVPN: Unsupported route type " << (*it)->type);
                    continue;
                }
                DBRequest req;
                req.oper = oper;
                if (oper == DBRequest::DB_ENTRY_ADD_CHANGE)
                    req.data.reset(new ErmVpnTable::RequestData(attr, flags,
                                                              0));
                req.key.reset(new ErmVpnTable::RequestKey(
                                  ErmVpnPrefix(**it), this));
                table->Enqueue(&req);
            }
            break;
        }

        default:
            continue;
        }
    }
}

void BgpPeer::EndOfRibTimerErrorHandler(string error_name,
                                        string error_message) {
    BGP_LOG_PEER(Timer, this, SandeshLevel::SYS_CRIT, BGP_LOG_FLAG_ALL,
                 BGP_PEER_DIR_NA,
                 "Timer error: " << error_name << " " << error_message);
}

void BgpPeer::RegisterToVpnTables(bool established) {
    CHECK_CONCURRENCY("bgp::StateMachine", "bgp::RTFilter");

    if (vpn_tables_registered_)
        return;
    vpn_tables_registered_ = true;

    PeerRibMembershipManager *membership_mgr = server_->membership_mgr();
    RoutingInstance *instance = GetRoutingInstance();

    if (IsFamilyNegotiated(Address::INETVPN)) {
        BgpTable *table = instance->GetTable(Address::INETVPN);
        BGP_LOG_PEER_TABLE(this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                           table, "Register peer with the table");
        if (table) {
            membership_mgr->Register(this, table, policy_, -1,
                boost::bind(&BgpPeer::MembershipRequestCallback, this, _1, _2, 
                            established));
            membership_req_pending_++;
        }
    }

    if (IsFamilyNegotiated(Address::ERMVPN)) {
        BgpTable *table = instance->GetTable(Address::ERMVPN);
        BGP_LOG_PEER_TABLE(this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                           table, "Register peer with the table");
        if (table) {
            membership_mgr->Register(this, table, policy_, -1,
                boost::bind(&BgpPeer::MembershipRequestCallback, this, _1, _2,
                            established));
            membership_req_pending_++;
        }
    }

    if (IsFamilyNegotiated(Address::EVPN)) {
        BgpTable *table = instance->GetTable(Address::EVPN);
        BGP_LOG_PEER_TABLE(this, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                           table, "Register peer with the table");
        if (table) {
            membership_mgr->Register(this, table, policy_, -1,
                boost::bind(&BgpPeer::MembershipRequestCallback, this, _1, _2,
                            established));
            membership_req_pending_++;
        }
    }
}



void BgpPeer::KeepaliveTimerErrorHandler(string error_name,
                                         string error_message) {
    BGP_LOG_PEER(Timer, this, SandeshLevel::SYS_CRIT, BGP_LOG_FLAG_ALL,
                 BGP_PEER_DIR_NA,
                 "Timer error: " << error_name << " " << error_message);
}

bool BgpPeer::EndOfRibTimerExpired() {
    RegisterToVpnTables(true);
    return false;
}

bool BgpPeer::KeepaliveTimerExpired() {
    SendKeepalive(true);

    //
    // Start the timer again, by returning true
    //
    return true;
}

void BgpPeer::StartEndOfRibTimer() {
    uint32_t timeout = 30 * 1000;
    char *time_str = getenv("BGP_RTFILTER_EOR_TIMEOUT");
    if (time_str) {
        timeout = strtoul(time_str, NULL, 0);
    }
    end_of_rib_timer_->Start(timeout, 
        boost::bind(&BgpPeer::EndOfRibTimerExpired, this),
        boost::bind(&BgpPeer::EndOfRibTimerErrorHandler, this, _1, _2));
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

void BgpPeer::StopKeepaliveTimer() {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    StopKeepaliveTimerUnlocked();
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
    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_send_state("in sync");
    BGPPeerInfo::Send(peer_info);
}

void BgpPeer::set_session(BgpSession *session) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    assert(session_ == NULL);
    session_ = session;
}

void BgpPeer::clear_session() {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (session_) {
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

void BgpPeer::ReceiveMsg(BgpSession *session, const u_int8_t *msg,
                         size_t size) {
    ParseErrorContext ec;
    BgpProto::BgpMessage *minfo = BgpProto::Decode(msg, size, &ec);

    if (minfo == NULL) {
        BGP_TRACE_PEER_PACKET(this, msg, size, SandeshLevel::SYS_WARN);
        BGP_LOG_PEER(Message, this, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_IN,
                     "Error while parsing message at " << ec.type_name);
        state_machine_->OnMessageError(session, &ec);
        return;
    }

    // Tracing periodic keepalive packets is not necessary.
    if (minfo->type != BgpProto::KEEPALIVE)
        BGP_TRACE_PEER_PACKET(this, msg, size, Sandesh::LoggingUtLevel());

    state_machine_->OnMessage(session, minfo);
}

string BgpPeer::ToString() const {
    ostringstream out;

    out << peer_key_.endpoint.address();
    if (peer_key_.endpoint.port() != BgpConfigManager::kDefaultPort) {
        out << ":" << static_cast<unsigned short>(peer_key_.endpoint.port());
    }
    return out.str();
}

string BgpPeer::ToUVEKey() const {
    ostringstream out;

    // XXX Skip default instance names from the logs and uves.
    if (true || !rtinstance_->IsDefaultRoutingInstance()) {
        out << rtinstance_->name() << ":";
    }
    // out << peer_key_.endpoint.address();
    out << server_->localname() << ":";
    out << peer_name();
    return out.str();
}


BgpAttrPtr BgpPeer::GetMpNlriNexthop(BgpMpNlri *nlri, BgpAttrPtr attr) {
    bool update_nh = false;
    IpAddress addr;
    Ip4Address::bytes_type bt = { { 0 } };

    if (nlri->afi == BgpAf::IPv4) {
        if (nlri->safi == BgpAf::Unicast || nlri->safi == BgpAf::RTarget) {
            std::copy(nlri->nexthop.begin(), nlri->nexthop.end(),
                      bt.begin());
            update_nh = true;
        } else if (nlri->safi == BgpAf::Vpn) {
            size_t rdsize = RouteDistinguisher::kSize;
            std::copy(nlri->nexthop.begin() + rdsize,
                      nlri->nexthop.end(), bt.begin());
            update_nh = true;
        }
    } else if (nlri->afi == BgpAf::L2Vpn) {
        if (nlri->safi == BgpAf::EVpn) {
            std::copy(nlri->nexthop.begin(), nlri->nexthop.end(),
                      bt.begin());
            update_nh = true;
        }
    }

    // Always update the nexthop in BgpAttr with MpReachNlri->nexthop.
    // NOP in cases <afi,safi> doesn't carry nexthop attribute.
    if (update_nh) {
        addr = Ip4Address(bt);
        attr = server_->attr_db()->UpdateNexthopAndLocate(attr.get(), nlri->afi,
                                                          nlri->safi, addr);
    }
    return attr;
}

void BgpPeer::ManagedDelete() {
    BGP_LOG_PEER(Config, this, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
                 BGP_PEER_DIR_NA, "Received request for deletion");
    deleter_->Delete();
}
 
void BgpPeer::RetryDelete() {
    if (!deleter_->IsDeleted())
        return;
    deleter_->RetryDelete();
}

void BgpPeer::SetDataCollectionKey(BgpPeerInfo *peer_info) const {
    if (rtinstance_) {
        peer_info->set_domain(rtinstance_->name());
    } else {
        peer_info->set_domain(BgpConfigManager::kMasterInstance);
    }

    peer_info->set_ip_address(peer_key_.endpoint.address().to_string());
}

static void FillProtoStats(IPeerDebugStats::ProtoStats &stats, 
                           PeerProtoStats &proto_stats) {
    proto_stats.open = stats.open;
    proto_stats.keepalive = stats.keepalive;
    proto_stats.close = stats.close;
    proto_stats.update = stats.update;
    proto_stats.notification = stats.notification;
    proto_stats.total = stats.open + stats.keepalive + stats.close + 
        stats.update + stats.notification;
}

static void FillRouteUpdateStats(IPeerDebugStats::UpdateStats &stats, 
                                 PeerUpdateStats &rt_stats) {
    rt_stats.total = stats.total;
    rt_stats.reach = stats.reach;
    rt_stats.unreach = stats.unreach;
}

static void FillSocketStats(IPeerDebugStats::SocketStats &socket_stats,
                            PeerSocketStats &peer_socket_stats) {
    peer_socket_stats.calls = socket_stats.calls;
    peer_socket_stats.bytes = socket_stats.bytes;
    if (socket_stats.calls) {
        peer_socket_stats.average_bytes = socket_stats.bytes/socket_stats.calls;
    }
    peer_socket_stats.blocked_count = socket_stats.blocked_count;
    ostringstream os;
    os << boost::posix_time::microseconds(socket_stats.blocked_duration_usecs);
    peer_socket_stats.blocked_duration = os.str();
    if (socket_stats.blocked_count) {
        os.str("");
        os << boost::posix_time::microseconds(
            socket_stats.blocked_duration_usecs/socket_stats.blocked_count);
        peer_socket_stats.average_blocked_duration = os.str();
    }
}

void BgpPeer::FillBgpNeighborDebugState(BgpNeighborResp &resp, 
                                        const IPeerDebugStats *peer_state) {
    resp.set_last_state(peer_state->last_state());
    resp.set_last_event(peer_state->last_event());
    resp.set_last_error(peer_state->last_error());
    resp.set_last_state_at(peer_state->last_state_change_at());
    resp.set_flap_count(peer_state->num_flaps());
    resp.set_flap_time(peer_state->last_flap());

    IPeerDebugStats::ProtoStats stats;
    PeerProtoStats proto_stats;
    peer_state->GetRxProtoStats(stats);
    FillProtoStats(stats, proto_stats);
    resp.set_rx_proto_stats(proto_stats);

    peer_state->GetTxProtoStats(stats);
    FillProtoStats(stats, proto_stats);
    resp.set_tx_proto_stats(proto_stats);


    IPeerDebugStats::UpdateStats update_stats;
    PeerUpdateStats rt_stats;
    peer_state->GetRxRouteUpdateStats(update_stats);
    FillRouteUpdateStats(update_stats, rt_stats);
    resp.set_rx_update_stats(rt_stats);

    peer_state->GetTxRouteUpdateStats(update_stats);
    FillRouteUpdateStats(update_stats, rt_stats);
    resp.set_tx_update_stats(rt_stats);

    IPeerDebugStats::SocketStats socket_stats;
    PeerSocketStats peer_socket_stats;

    peer_state->GetRxSocketStats(socket_stats);
    FillSocketStats(socket_stats, peer_socket_stats);
    resp.set_rx_socket_stats(peer_socket_stats);

    peer_state->GetTxSocketStats(socket_stats);
    FillSocketStats(socket_stats, peer_socket_stats);
    resp.set_tx_socket_stats(peer_socket_stats);
}

void BgpPeer::FillNeighborInfo(std::vector<BgpNeighborResp> &nbr_list) const {
    BgpNeighborResp nbr;
    nbr.set_peer(peer_name_.substr(rtinstance_->name().size() + 1));
    nbr.set_peer_address(peer_key_.endpoint.address().to_string());
    nbr.set_deleted(IsDeleted());
    nbr.set_peer_asn(peer_as());
    nbr.set_local_address(server_->ToString());
    nbr.set_local_asn(local_as());
    nbr.set_peer_type(PeerType() == BgpProto::IBGP ? "internal" : "external");
    nbr.set_encoding("BGP");
    nbr.set_state(state_machine_->StateName());
    nbr.set_peer_id(Ip4Address(remote_bgp_id_).to_string());
    nbr.set_local_id(Ip4Address(local_bgp_id_).to_string());
    nbr.set_configured_address_families(configured_families_);
    nbr.set_negotiated_address_families(negotiated_families_);
    nbr.set_configured_hold_time(state_machine_->GetConfiguredHoldTime());
    nbr.set_negotiated_hold_time(state_machine_->hold_time());

    FillBgpNeighborDebugState(nbr, peer_stats_.get());

    // TODO: Fill the rest of the fields
    PeerRibMembershipManager *mgr = server_->membership_mgr();
    mgr->FillPeerMembershipInfo(this, nbr);

    nbr_list.push_back(nbr);
}

void BgpPeer::inc_rx_open() {
    peer_stats_->proto_stats_[0].open++;
}

void BgpPeer::inc_rx_keepalive() {
    peer_stats_->proto_stats_[0].keepalive++;
}

size_t BgpPeer::get_rx_keepalive() {
    return peer_stats_->proto_stats_[0].keepalive;
}

size_t BgpPeer::get_tr_keepalive() {
    return peer_stats_->proto_stats_[1].keepalive;
}

void BgpPeer::inc_rx_update() {
    peer_stats_->proto_stats_[0].update++;
}

void BgpPeer::inc_rx_notification() {
    peer_stats_->proto_stats_[0].notification++;
}

size_t BgpPeer::get_rx_notification() {
    return peer_stats_->proto_stats_[0].notification;
}

void BgpPeer::inc_rx_route_update() {
    peer_stats_->update_stats_[0].total++;
}

void BgpPeer::inc_tx_route_update() {
    peer_stats_->update_stats_[1].total++;
}

void BgpPeer::inc_rx_route_reach() {
    peer_stats_->update_stats_[0].reach++;
}

void BgpPeer::inc_rx_route_unreach() {
    peer_stats_->update_stats_[0].unreach++;
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

size_t BgpPeer::get_connect_error() {
    return peer_stats_->error_stats_.connect_error;
}

size_t BgpPeer::get_connect_timer_expired() {
    return peer_stats_->error_stats_.connect_timer;
}

size_t BgpPeer::get_hold_timer_expired() {
    return peer_stats_->error_stats_.hold_timer;
}

size_t BgpPeer::get_open_error() {
    return peer_stats_->error_stats_.open_error;
}

size_t BgpPeer::get_update_error() {
    return peer_stats_->error_stats_.update_error;
}

std::string BgpPeer::last_flap_at() const { 
    if (last_flap_) {
        return integerToString(UTCUsecToPTime(last_flap_));
    } else {
        return "";
    }
}

void BgpPeer::increment_flap_count() {
    flap_count_++;
    last_flap_ = UTCTimestampUsec();

    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    PeerFlapInfo flap_info;
    flap_info.set_flap_count(flap_count_);
    flap_info.set_flap_time(last_flap_);
    peer_info.set_flap_info(flap_info);
    BGPPeerInfo::Send(peer_info);
}

void BgpPeer::reset_flap_count() {
    flap_count_ = 0;
    last_flap_ = 0;

    BgpPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    PeerFlapInfo flap_info;
    peer_info.set_flap_info(flap_info);
    BGPPeerInfo::Send(peer_info);
}
