/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_PEER_H__
#define __BGP_PEER_H__

#include <set>
#include <memory>
#include <boost/asio/ip/tcp.hpp>
#include <boost/scoped_ptr.hpp>
#include <tbb/spin_mutex.h>

#include "base/lifetime.h"
#include "base/util.h"
#include "base/task_trigger.h"
#include "base/timer.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_debug.h"
#include "bgp/bgp_peer_key.h"
#include "bgp/bgp_proto.h"
#include "bgp/bgp_ribout.h"
#include "bgp/ipeer.h"
#include "bgp/bgp_peer_close.h"
#include "bgp/state_machine.h"
#include "net/address.h"

class BgpNeighborConfig;
class BgpPeerInfo;
class BgpServer;
class BgpSession;
class RoutingInstance;
class StateMachine;
class BgpSession;
class BgpPeerInfo;
class BgpNeighborResp;
class BgpSandeshContext;

// A BGP peer along with its session and state machine.
class BgpPeer : public IPeer {
public:
    typedef std::set<Address::Family> AddressFamilyList;
    typedef AuthenticationData::KeyType KeyType;

    BgpPeer(BgpServer *server, RoutingInstance *instance,
            const BgpNeighborConfig *config);
    virtual ~BgpPeer();

    // Interface methods

    // thread-safe
    virtual std::string ToString() const;
    virtual std::string ToUVEKey() const;

    // thread: bgp::SendTask
    // Used to send an UPDATE message on the socket.
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize);

    // thread: bgp::config
    void ConfigUpdate(const BgpNeighborConfig *config);
    void ClearConfig();

    // thread: event manager thread.
    // Invoked from BgpServer when a session is accepted.
    bool AcceptSession(BgpSession *session);

    BgpSession *CreateSession();

    void SetAdminState(bool down);

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
    void StopKeepaliveTimer();
    bool KeepaliveTimerRunning();
    void SetSendReady();

    // thread: io::ReaderTask
    void SetCapabilities(const BgpProto::OpenMessage *msg);
    void ResetCapabilities();

    // Table registration.
    void RegisterAllTables();

    // accessors
    virtual BgpServer *server() { return server_; }
    const BgpServer *server() const { return server_; }

    unsigned long PeerAddress() const { return peer_key_.Address(); }
    const std::string peer_address_string() const {
        return peer_key_.endpoint.address().to_string();
    }
    const BgpPeerKey &peer_key() const { return peer_key_; }
    const std::string &peer_name() const { return peer_name_; }
    const std::string &peer_basename() const { return peer_basename_; }

    StateMachine::State GetState() const;
    virtual const std::string GetStateName() const;

    void set_session(BgpSession *session);
    void clear_session();
    BgpSession *session();

    as_t local_as() const { return local_as_; }
    as_t peer_as() const { return peer_as_; }

    // The BGP Identifier in host byte order.
    virtual uint32_t bgp_identifier() const;
    std::string bgp_identifier_string() const;

    const AddressFamilyList &families() const {
        return family_;
    }

    void AddFamily(Address::Family family) {
        family_.insert(family);
    }

    bool LookupFamily(Address::Family family) {
        return (family_.find(family) != family_.end());
    }

    bool IsFamilyNegotiated(Address::Family family);

    RoutingInstance *GetRoutingInstance() {
        return rtinstance_;
    }

    int GetIndex() const {
        return index_;
    }

    virtual BgpProto::BgpPeerType PeerType() const {
        return peer_type_;
    }

    const BgpNeighborConfig *config() const { return config_; }

    virtual void SetDataCollectionKey(BgpPeerInfo *peer_info) const;
    void FillNeighborInfo(const BgpSandeshContext *bsc,
            std::vector<BgpNeighborResp> *nbr_list, bool summary) const;

    // thread-safe
    bool IsDeleted() const;
    bool IsAdminDown() const { return admin_down_; }
    bool IsCloseInProgress() const;
    virtual bool IsReady() const;
    virtual bool IsXmppPeer() const;

    void Close();
    void Clear(int subcode);

    virtual IPeerClose *peer_close();
    virtual IPeerDebugStats *peer_stats();
    virtual const IPeerDebugStats *peer_stats() const;
    void ManagedDelete();
    void RetryDelete();
    LifetimeActor *deleter();
    void Initialize();

    void increment_flap_count();
    void reset_flap_count();
    uint32_t flap_count() const { return flap_count_; };

    std::string last_flap_at() const;

    void inc_rx_open();
    void inc_rx_keepalive();
    void inc_rx_update();
    void inc_rx_notification();

    void inc_rx_end_of_rib();
    void inc_rx_route_reach(uint32_t count);
    void inc_rx_route_unreach(uint32_t count);

    void inc_tx_end_of_rib();

    size_t get_rx_keepalive() const;
    size_t get_rx_notification() const;
    size_t get_tx_keepalive() const;

    size_t get_rx_end_of_rib() const;
    size_t get_rx_route_reach() const;
    size_t get_rx_route_unreach() const;
    size_t get_rx_route_total() const;

    size_t get_tx_end_of_rib() const;
    size_t get_tx_route_reach() const;
    size_t get_tx_route_unreach() const;
    size_t get_tx_route_total() const;

    void inc_connect_error();
    void inc_connect_timer_expired();
    void inc_hold_timer_expired();
    void inc_open_error();
    void inc_update_error();

    size_t get_connect_error() const;
    size_t get_connect_timer_expired() const;
    size_t get_hold_timer_expired() const;
    size_t get_open_error() const;
    size_t get_update_error() const;

    static void FillBgpNeighborDebugState(BgpNeighborResp &resp, const IPeerDebugStats *peer);

    bool ResumeClose();
    void MembershipRequestCallback(IPeer *ipeer, BgpTable *table);

    virtual void UpdateRefCount(int count) const { refcount_ += count; }
    virtual tbb::atomic<int> GetRefCount() const { return refcount_; }
    virtual void UpdatePrimaryPathCount(int count) const {
        primary_path_count_ += count;
    }
    virtual int GetPrimaryPathCount() const {
        return primary_path_count_;
    }

    void RegisterToVpnTables();

    StateMachine *state_machine() { return state_machine_.get(); }
    const StateMachine *state_machine() const { return state_machine_.get(); }

    bool GetBestAuthKeyItem(AuthenticationKey *auth_key);
    void InstallAuthKeys(TcpSession *session);
    std::string GetInuseAuthKeyValue() const;
    void SetListenSocketAuthKey(const AuthenticationKey &auth_key);
    void ClearListenSocketAuthKey();

private:
    friend class BgpConfigTest;
    friend class BgpPeerTest;
    friend class BgpServerUnitTest;
    friend class StateMachineUnitTest;

    class DeleteActor;
    class PeerClose;
    class PeerStats;

    void KeepaliveTimerErrorHandler(std::string error_name,
                                    std::string error_message);
    virtual void StartKeepaliveTimerUnlocked();
    void StopKeepaliveTimerUnlocked();
    bool KeepaliveTimerExpired();
 
    void ReceiveEndOfRIB(Address::Family family, size_t msgsize);
    void SendEndOfRIB(Address::Family family);
    void StartEndOfRibTimer();
    bool EndOfRibTimerExpired();
    void EndOfRibTimerErrorHandler(std::string error_name,
                                   std::string error_message);

    virtual void BindLocalEndpoint(BgpSession *session);

    void UnregisterAllTables();

    virtual bool MpNlriAllowed(uint16_t afi, uint8_t safi);
    BgpAttrPtr GetMpNlriNexthop(BgpMpNlri *nlri, BgpAttrPtr attr);

    bool GetBestAuthKey(AuthenticationKey *auth_key, KeyType *key_type) const;
    void ProcessAuthKeyChainConfig(const BgpNeighborConfig *config);
    void LogInstallAuthKeys(const std::string &oper,
                            const AuthenticationKey &auth_key, KeyType key_type);
    void SetInuseAuthKeyInfo(const AuthenticationKey &key, KeyType type);
    void ResetInuseAuthKeyInfo();

    void PostCloseRelease();
    void CustomClose();

    std::string BytesToHexString(const u_int8_t *msg, size_t size);

    BgpServer *server_;
    // Backpointer to routing instance
    RoutingInstance *rtinstance_;
    BgpPeerKey peer_key_;
    std::string peer_name_;
    std::string peer_basename_;
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
    BgpSession *session_;
    Timer *keepalive_timer_;
    Timer *end_of_rib_timer_;
    bool send_ready_;
    bool admin_down_;

    boost::scoped_ptr<StateMachine> state_machine_;
    uint32_t membership_req_pending_;
    bool defer_close_;
    bool vpn_tables_registered_;
    std::vector<BgpProto::OpenMessage::Capability *> capabilities_;
    as_t local_as_;
    as_t peer_as_;
    uint32_t local_bgp_id_;     // network order
    uint32_t peer_bgp_id_;      // network order
    AddressFamilyList family_;
    std::vector<std::string> configured_families_;
    std::vector<std::string> negotiated_families_;
    BgpProto::BgpPeerType peer_type_;
    RibExportPolicy policy_;
    boost::scoped_ptr<PeerClose> peer_close_;
    boost::scoped_ptr<PeerStats> peer_stats_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<BgpPeer> instance_delete_ref_;
    mutable tbb::atomic<int> refcount_;
    mutable tbb::atomic<int> primary_path_count_;
    uint32_t flap_count_;
    uint64_t last_flap_;
    AuthenticationData auth_data_;
    AuthenticationKey inuse_auth_key_;
    KeyType inuse_authkey_type_;

    DISALLOW_COPY_AND_ASSIGN(BgpPeer);
};

#endif
