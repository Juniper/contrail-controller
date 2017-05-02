/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_PEER_H__
#define SRC_BGP_BGP_PEER_H__

#include <boost/asio/ip/tcp.hpp>
#include <boost/scoped_ptr.hpp>
#include <tbb/spin_mutex.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/lifetime.h"
#include "base/util.h"
#include "base/task_trigger.h"
#include "base/timer.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_debug.h"
#include "bgp/bgp_peer_key.h"
#include "bgp/bgp_proto.h"
#include "bgp/bgp_rib_policy.h"
#include "bgp/ipeer.h"
#include "bgp/state_machine.h"
#include "db/db_table.h"
#include "net/address.h"

class BgpNeighborConfig;
class BgpPeerClose;
class BgpPeerInfo;
class BgpPeerInfoData;
class BgpServer;
class BgpSession;
class BgpSession;
class BgpNeighborResp;
class BgpSandeshContext;
class PeerCloseManager;
class RoutingInstance;

//
// This contains per address family attributes.
// A BgpPeer contains a vector of pointers to this structure, with an entry
// for each value in Address::Family i.e. the vector can be indexed using a
// Address::Family value.
//
struct BgpPeerFamilyAttributes {
    BgpPeerFamilyAttributes(const BgpNeighborConfig *config,
        const BgpFamilyAttributesConfig &family_config);
    uint8_t loop_count;
    uint32_t prefix_limit;
    IpAddress gateway_address;
};

//
// Comparator for BgpPeerFamilyAttributes.
//
struct BgpPeerFamilyAttributesCompare {
    int operator()(const BgpPeerFamilyAttributes *lhs,
                   const BgpPeerFamilyAttributes *rhs) const {
        if (lhs && rhs) {
            KEY_COMPARE(lhs->loop_count, rhs->loop_count);
            KEY_COMPARE(lhs->prefix_limit, rhs->prefix_limit);
            KEY_COMPARE(lhs->gateway_address, rhs->gateway_address);
        } else {
            KEY_COMPARE(lhs, rhs);
        }
        return 0;
    }
};

// A BGP peer along with its session and state machine.
class BgpPeer : public IPeer {
public:
    static const int kMinEndOfRibSendTimeUsecs = 10000000;  // 10 Seconds
    static const int kMaxEndOfRibSendTimeUsecs = 60000000;  // 60 Seconds
    static const int kEndOfRibSendRetryTimeMsecs = 2000;    // 2 Seconds
    static const int kRouteTargetEndOfRibTimeSecs = 30;     // Seconds
    static const size_t kBufferSize = 32768;

    typedef std::set<Address::Family> AddressFamilyList;
    typedef AuthenticationData::KeyType KeyType;

    BgpPeer(BgpServer *server, RoutingInstance *instance,
            const BgpNeighborConfig *config);
    virtual ~BgpPeer();

    // Interface methods

    // thread-safe
    virtual const std::string &ToString() const { return to_str_; }
    virtual const std::string &ToUVEKey() const { return uve_key_str_; }

    // Task: bgp::SendUpdate
    // Used to send an UPDATE message on the socket.
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize,
                            const std::string *msg_str);
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        return SendUpdate(msg, msgsize, NULL);
    }
    virtual bool FlushUpdate();

    // Task: bgp::Config
    void ConfigUpdate(const BgpNeighborConfig *config);
    void ClearConfig();

    // thread: event manager thread.
    // Invoked from BgpServer when a session is accepted.
    bool AcceptSession(BgpSession *session);

    BgpSession *CreateSession();

    virtual void SetAdminState(bool down);

    // Messages

    // thread: bgp::StateMachine
    void SendOpen(TcpSession *session);

    // thread: bgp::StateMachine, KA timer handler
    void SendKeepalive(bool from_timer);

    // thread: bgp::StateMachine, io::ReaderTask
    void SendNotification(BgpSession *, int code, int subcode = 0,
                          const std::string &data = std::string());

    // thread: io::ReaderTask
    void ProcessUpdate(const BgpProto::Update *msg, size_t msgsize = 0);

    // thread: io::ReaderTask
    virtual bool ReceiveMsg(BgpSession *session, const u_int8_t *msg,
                            size_t size);

    void StartKeepaliveTimer();
    bool KeepaliveTimerRunning();
    void SetSendReady();

    // thread: io::ReaderTask
    bool SetCapabilities(const BgpProto::OpenMessage *msg);
    void ResetCapabilities();

    // Table registration.
    void RegisterAllTables();

    // accessors
    virtual BgpServer *server() { return server_; }
    virtual BgpServer *server() const { return server_; }

    uint32_t PeerAddress() const { return peer_key_.address(); }
    const std::string peer_address_string() const {
        return peer_key_.endpoint.address().to_string();
    }
    const BgpPeerKey &peer_key() const { return peer_key_; }
    uint16_t peer_port() const { return peer_port_; }
    std::string transport_address_string() const;
    std::string gateway_address_string(Address::Family family) const;
    const std::string &peer_name() const { return peer_name_; }
    const std::string &peer_basename() const { return peer_basename_; }
    std::string router_type() const { return router_type_; }
    TcpSession::Endpoint endpoint() const { return endpoint_; }

    StateMachine::State GetState() const;
    virtual const std::string GetStateName() const;

    void set_session(BgpSession *session);
    void clear_session();
    BgpSession *session();

    uint16_t hold_time() const { return hold_time_; }
    as_t local_as() const { return local_as_; }
    as_t peer_as() const { return peer_as_; }
    size_t buffer_len() const { return buffer_len_; }

    // The BGP Identifier in host byte order.
    virtual uint32_t local_bgp_identifier() const;
    std::string local_bgp_identifier_string() const;
    virtual uint32_t bgp_identifier() const;
    std::string bgp_identifier_string() const;

    const std::vector<std::string> &configured_families() const {
        return configured_families_;
    }

    bool LookupFamily(Address::Family family) {
        return (family_attributes_list_[family] != NULL);
    }

    bool IsFamilyNegotiated(Address::Family family);
    RoutingInstance *GetRoutingInstance() { return rtinstance_; }
    RoutingInstance *GetRoutingInstance() const { return rtinstance_; }

    int GetIndex() const { return index_; }
    int GetTaskInstance() const;

    virtual BgpProto::BgpPeerType PeerType() const {
        return peer_type_;
    }
    const string &private_as_action() const { return private_as_action_; }

    const BgpNeighborConfig *config() const { return config_; }

    virtual void SetDataCollectionKey(BgpPeerInfo *peer_info) const;
    void FillNeighborInfo(const BgpSandeshContext *bsc, BgpNeighborResp *bnr,
        bool summary) const;

    // thread-safe
    bool IsDeleted() const;
    bool IsAdminDown() const { return admin_down_; }
    bool IsPassive() const { return passive_; }
    bool IsCloseInProgress() const;
    virtual bool IsReady() const;
    virtual bool IsXmppPeer() const;
    virtual bool CanUseMembershipManager() const;
    virtual bool IsRegistrationRequired() const { return true; }
    virtual uint64_t GetEorSendTimerElapsedTimeUsecs() const;
    virtual bool send_ready() const { return send_ready_; }

    void Close(bool graceful);
    void Clear(int subcode);

    virtual IPeerClose *peer_close();
    virtual IPeerClose *peer_close() const;
    virtual void UpdateCloseRouteStats(Address::Family family,
        const BgpPath *old_path, uint32_t path_flags) const;
    virtual IPeerDebugStats *peer_stats();
    virtual const IPeerDebugStats *peer_stats() const;
    void ManagedDelete();
    void RetryDelete();
    LifetimeActor *deleter();
    void Initialize();

    void NotifyEstablished(bool established);

    void increment_flap_count();
    void reset_flap_count();
    uint64_t flap_count() const { return flap_count_; }
    uint64_t last_flap() const { return last_flap_; }
    uint64_t total_flap_count() const { return total_flap_count_; }

    std::string last_flap_at() const;

    void inc_rx_open();
    void inc_rx_keepalive();
    void inc_rx_update();
    void inc_rx_notification();

    void inc_tx_open();
    void inc_tx_keepalive();
    void inc_tx_update();
    void inc_tx_notification();

    void inc_rx_end_of_rib();
    void inc_rx_route_reach(uint64_t count);
    void inc_rx_route_unreach(uint64_t count);

    void inc_tx_end_of_rib();

    uint64_t get_rx_keepalive() const;
    uint64_t get_rx_update() const;
    uint64_t get_rx_notification() const;
    uint64_t get_tx_keepalive() const;
    uint64_t get_tx_update() const;

    uint64_t get_rx_end_of_rib() const;
    uint64_t get_rx_route_reach() const;
    uint64_t get_rx_route_unreach() const;
    uint64_t get_rx_route_total() const;

    uint64_t get_tx_end_of_rib() const;
    uint64_t get_tx_route_reach() const;
    uint64_t get_tx_route_unreach() const;
    uint64_t get_tx_route_total() const;

    void inc_connect_error();
    void inc_connect_timer_expired();
    void inc_hold_timer_expired();
    void inc_open_error();
    void inc_update_error();

    uint64_t get_connect_error() const;
    uint64_t get_connect_timer_expired() const;
    uint64_t get_hold_timer_expired() const;
    uint64_t get_open_error() const;
    uint64_t get_update_error() const;

    uint64_t get_socket_reads() const;
    uint64_t get_socket_writes() const;

    static void FillBgpNeighborDebugState(BgpNeighborResp *bnr,
        const IPeerDebugStats *peer);

    bool ResumeClose();
    void MembershipRequestCallback(BgpTable *table);

    virtual void UpdateTotalPathCount(int count) const {
        total_path_count_ += count;
    }
    virtual int GetTotalPathCount() const { return total_path_count_; }
    virtual void UpdatePrimaryPathCount(int count) const {
        primary_path_count_ += count;
    }
    virtual int GetPrimaryPathCount() const { return primary_path_count_; }

    void RegisterToVpnTables();

    StateMachine *state_machine() { return state_machine_.get(); }
    const StateMachine *state_machine() const { return state_machine_.get(); }

    bool GetBestAuthKeyItem(AuthenticationKey *auth_key);
    bool InstallAuthKeys();
    std::string GetInuseAuthKeyValue() const;
    void SetListenSocketAuthKey(const AuthenticationKey &auth_key,
                                KeyType key_type);
    void ClearListenSocketAuthKey();
    void SetSessionSocketAuthKey(TcpSession *session);
    virtual bool AttemptGRHelperMode(int code, int subcode) const;
    void Register(BgpTable *table, const RibExportPolicy &policy);
    void Register(BgpTable *table);
    bool EndOfRibSendTimerExpired(Address::Family family);
    void CustomClose();
    const std::vector<std::string> &negotiated_families() const {
        return negotiated_families_;
    }
    void ReceiveEndOfRIB(Address::Family family, size_t msgsize);
    const std::vector<BgpProto::OpenMessage::Capability *> &
        capabilities() const {
        return capabilities_;
    }

    static const std::vector<Address::Family> &supported_families() {
        return supported_families_;
    }
    virtual bool IsInGRTimerWaitState() const;
    PeerCloseManager *close_manager() { return close_manager_.get(); }

protected:
    virtual void SendEndOfRIBActual(Address::Family family);
    virtual void SendEndOfRIB(Address::Family family);
    int membership_req_pending() const { return membership_req_pending_; }
    virtual bool notification() const;

private:
    friend class BgpConfigTest;
    friend class BgpPeerTest;
    friend class BgpServerUnitTest;
    friend class StateMachineUnitTest;

    class DeleteActor;
    class PeerStats;

    typedef std::map<Address::Family, const uint8_t *> FamilyToCapabilityMap;
    typedef std::vector<BgpPeerFamilyAttributes *> FamilyAttributesList;

    bool FlushUpdateUnlocked();
    void KeepaliveTimerErrorHandler(std::string error_name,
                                    std::string error_message);
    virtual void StartKeepaliveTimerUnlocked();
    void StopKeepaliveTimerUnlocked();
    bool KeepaliveTimerExpired();

    RibExportPolicy BuildRibExportPolicy(Address::Family family) const;
    void StartEndOfRibReceiveTimer(Address::Family family);
    bool EndOfRibReceiveTimerExpired(Address::Family family);
    void EndOfRibTimerErrorHandler(std::string error_name,
                                   std::string error_message);
    uint32_t GetEndOfRibReceiveTime(Address::Family family) const;
    uint32_t GetEndOfRibSendTime(Address::Family family) const;

    virtual void BindLocalEndpoint(BgpSession *session);
    void UnregisterAllTables();
    void BGPPeerInfoSend(const BgpPeerInfoData &peer_info) const;

    virtual bool MembershipPathCallback(DBTablePartBase *tpart,
                                        BgpRoute *route, BgpPath *path);
    uint32_t GetPathFlags(Address::Family family, const BgpAttr *attr) const;
    uint32_t GetLocalPrefFromMed(uint32_t med) const;
    virtual bool MpNlriAllowed(uint16_t afi, uint8_t safi);
    BgpAttrPtr GetMpNlriNexthop(BgpMpNlri *nlri, BgpAttrPtr attr);
    template <typename TableT, typename PrefixT>
    void ProcessNlri(Address::Family family, DBRequest::DBOperation oper,
        const BgpMpNlri *nlri, BgpAttrPtr attr, uint32_t flags);

    bool GetBestAuthKey(AuthenticationKey *auth_key, KeyType *key_type) const;
    bool ProcessAuthKeyChainConfig(const BgpNeighborConfig *config);
    void LogInstallAuthKeys(const std::string &socket_name,
        const std::string &oper, const AuthenticationKey &auth_key,
        KeyType key_type);
    void SetInuseAuthKeyInfo(const AuthenticationKey &key, KeyType type);
    void ResetInuseAuthKeyInfo();

    bool ProcessFamilyAttributesConfig(const BgpNeighborConfig *config);
    void ProcessEndpointConfig(const BgpNeighborConfig *config);

    void PostCloseRelease();

    void FillBgpNeighborFamilyAttributes(BgpNeighborResp *nbr) const;
    void FillCloseInfo(BgpNeighborResp *resp) const;

    std::string BytesToHexString(const u_int8_t *msg, size_t size);
    virtual uint32_t GetOutputQueueDepth(Address::Family family) const;

    static const std::vector<Address::Family> supported_families_;
    BgpServer *server_;
    RoutingInstance *rtinstance_;
    TcpSession::Endpoint endpoint_;
    BgpPeerKey peer_key_;
    uint16_t peer_port_;
    std::string peer_name_;
    std::string peer_basename_;
    std::string router_type_;         // bgp_schema.xsd:BgpRouterType
    bool peer_is_control_node_;
    mutable std::string to_str_;
    mutable std::string uve_key_str_;
    const BgpNeighborConfig *config_;

    // Global peer index
    int index_;
    TaskTrigger trigger_;

    // The mutex is used to protect the session, keepalive timer and the
    // send ready state.
    //
    // The session is accessed from the bgp::Send task and the keepalive
    // timer handler and gets set/cleared from the bgp::StateMachine task.
    //
    // The keepalive timer can get started from the io thread (either via
    // the SetSendReady callback or from the timer handler) or from the
    // bgp::Send task or the bgp::StateMachine task.  It can get stopped
    // from the bgp::Send task or the bgp::StateMachine task.
    //
    // The send ready state gets modified from the bgp::Send task or from
    // the io thread (either via the the SetSendReady callback or from the
    // timer handler).
    //
    // Note that the mutex will not be heavily contended since we expect
    // the bgp::Send task to lock it most frequently while all other tasks
    // and the io thread should need to lock it once every few seconds at
    // most.  Hence we choose a spin_mutex.
    tbb::spin_mutex spin_mutex_;
    uint8_t buffer_[kBufferSize];
    size_t buffer_len_;
    BgpSession *session_;
    Timer *keepalive_timer_;
    Timer *eor_receive_timer_[Address::NUM_FAMILIES];
    Timer *eor_send_timer_[Address::NUM_FAMILIES];
    uint64_t eor_send_timer_start_time_;
    bool send_ready_;
    bool admin_down_;
    bool passive_;
    bool resolve_paths_;
    bool as_override_;
    string private_as_action_;

    tbb::atomic<int> membership_req_pending_;
    bool defer_close_;
    bool graceful_close_;
    bool vpn_tables_registered_;
    std::vector<BgpProto::OpenMessage::Capability *> capabilities_;
    uint16_t hold_time_;
    as_t local_as_;
    as_t peer_as_;
    uint32_t local_bgp_id_;     // network order
    uint32_t peer_bgp_id_;      // network order
    FamilyAttributesList family_attributes_list_;
    std::vector<std::string> configured_families_;
    std::vector<std::string> negotiated_families_;
    BgpProto::BgpPeerType peer_type_;
    boost::scoped_ptr<StateMachine> state_machine_;
    boost::scoped_ptr<BgpPeerClose> peer_close_;
    boost::scoped_ptr<PeerCloseManager> close_manager_;
    boost::scoped_ptr<PeerStats> peer_stats_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<BgpPeer> instance_delete_ref_;
    mutable tbb::atomic<int> total_path_count_;
    mutable tbb::atomic<int> primary_path_count_;
    uint64_t flap_count_;
    uint64_t total_flap_count_;
    uint64_t last_flap_;
    AuthenticationData auth_data_;
    AuthenticationKey inuse_auth_key_;
    KeyType inuse_authkey_type_;

    DISALLOW_COPY_AND_ASSIGN(BgpPeer);
};

#endif  // SRC_BGP_BGP_PEER_H__
