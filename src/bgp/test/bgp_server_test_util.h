/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_SERVER_TEST_UTIL_H__
#define __BGP_SERVER_TEST_UTIL_H__

#include <boost/any.hpp>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>
#include <tbb/compat/condition_variable>
#include <tbb/mutex.h>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "base/util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_lifetime.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_server.h"
#include "bgp/peer_close_manager.h"
#include "bgp/state_machine.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "io/tcp_session.h"
#include "xmpp/xmpp_lifetime.h"
#include "xmpp/xmpp_server.h"

class BgpPeerTest;

class BgpInstanceConfigTest : public BgpInstanceConfig {
public:
    BgpInstanceConfigTest(const std::string &name) : BgpInstanceConfig(name) {
    }
    RouteTargetList *mutable_import_list() { return &import_list_; }
    RouteTargetList *mutable_export_list() { return &export_list_; }
    void set_virtual_network(std::string virtual_network) {
        virtual_network_ = virtual_network;
    }
    void set_virtual_network_index(int virtual_network_index) {
        virtual_network_index_ = virtual_network_index;
    }
};

class BgpTestUtil {
public:
    static BgpInstanceConfigTest *CreateBgpInstanceConfig(
            const std::string &name,
            const std::string import_targets = "",
            const std::string export_targets = "",
            const std::string virtual_network = "",
            int virtual_network_index = 0);
    static void UpdateBgpInstanceConfig(BgpInstanceConfigTest *inst,
            const std::string import_targets,
            const std::string export_targets);
    static void UpdateBgpInstanceConfig(BgpInstanceConfigTest *inst,
        const std::string virtual_network, int virtual_network_index);

    void SetUserData(std::string key, boost::any &value);
    boost::any GetUserData(std::string key);

private:
    std::map<std::string, boost::any> user_data_;
};

class XmppServerTest : public XmppServer {
public:

    XmppServerTest(EventManager *evm) : XmppServer(evm) {
    }
    XmppServerTest(EventManager *evm, const std::string &server_addr) :
            XmppServer(evm, server_addr) {
    }
    XmppServerTest(EventManager *evm, const std::string &server_addr,
                   const XmppChannelConfig *config) :
        XmppServer(evm, server_addr, config) {
    }
    virtual ~XmppServerTest() { }

    const ConnectionMap &connection_map() const { return connection_map_; }

    // Protect connection db with mutex as it is queried from main thread which
    // does not adhere to control-node scheduler policy.
    XmppServerConnection *FindConnection(const std::string &peer_addr) {
        tbb::mutex::scoped_lock lock(mutex_);
        return XmppServer::FindConnection(peer_addr);
    }

    void InsertConnection(XmppServerConnection *connection) {
        tbb::mutex::scoped_lock lock(mutex_);
        XmppServer::InsertConnection(connection);
    }

    void RemoveConnection(XmppServerConnection *connection) {
        tbb::mutex::scoped_lock lock(mutex_);
        XmppServer::RemoveConnection(connection);
    }

    void InsertDeletedConnection(XmppServerConnection *connection) {
        tbb::mutex::scoped_lock lock(mutex_);
        XmppServer::InsertDeletedConnection(connection);
    }

    void RemoveDeletedConnection(XmppServerConnection *connection) {
        tbb::mutex::scoped_lock lock(mutex_);
        XmppServer::RemoveDeletedConnection(connection);
    }

private:
    tbb::mutex mutex_;
};

class StateMachineTest : public StateMachine {
public:
    explicit StateMachineTest(BgpPeer *peer)
        : StateMachine(peer), gr_close_(false), bgpaas_client_(false),
          skip_bgp_notification_msg_(false) {
    }
    virtual ~StateMachineTest() { }

    void StartConnectTimer(int seconds) {
        connect_timer_->Start(100,
            boost::bind(&StateMachine::ConnectTimerExpired, this),
            boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
    }

    void StartOpenTimer(int seconds) {
        open_timer_->Start(1000,
            boost::bind(&StateMachine::OpenTimerExpired, this),
            boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
    }

    void StartIdleHoldTimer() {
        if (idle_hold_time_ <= 0)
            return;
        idle_hold_timer_->Start(10,
            boost::bind(&StateMachine::IdleHoldTimerExpired, this),
            boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
    }

    void StartHoldTimer() {
        if (hold_time_msecs_ <= 0) {
            StateMachine::StartHoldTimer();
            return;
        }
        hold_timer_->Start(hold_time_msecs_,
            boost::bind(&StateMachine::HoldTimerExpired, this),
            boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
    }

    static void set_hold_time_msecs(int hold_time_msecs) {
        hold_time_msecs_ = hold_time_msecs;
    }

    virtual const int GetIdleHoldTimeMSecs() const { return 10; }

    virtual int keepalive_time_msecs() const {
        if (keepalive_time_msecs_)
            return keepalive_time_msecs_;
        return StateMachine::keepalive_time_msecs();
    }

    static void set_keepalive_time_msecs(int keepalive_time_msecs) {
        keepalive_time_msecs_ = keepalive_time_msecs;
    }

    static TcpSession::Event skip_tcp_event() { return skip_tcp_event_; }
    static void set_skip_tcp_event(TcpSession::Event event) {
        skip_tcp_event_ = event;
    }

    virtual void OnSessionEvent(TcpSession *session, TcpSession::Event event) {
        if (skip_tcp_event_ != event)
            StateMachine::OnSessionEvent(session, event);
        else
            skip_tcp_event_ = TcpSession::EVENT_NONE;
    }

    virtual void OnNotificationMessage(BgpSession *session,
                                       BgpProto::BgpMessage *msg) {
        if (!skip_bgp_notification_msg_) {
            StateMachine::OnNotificationMessage(session, msg);
        } else {
            skip_bgp_notification_msg_ = false;
            delete msg;
        }
    }

    bool skip_bgp_notification_msg() const {
        return skip_bgp_notification_msg_;
    }

    void set_skip_bgp_notification_msg(bool flag) {
        skip_bgp_notification_msg_ = flag;
    }

    bool IsCloseGraceful() const {
        return gr_close_ ?: StateMachine::IsCloseGraceful();
    }
    void SetCloseGraceful(bool gr_close) { gr_close_ = gr_close; }

    bool IsRouterTypeBGPaaS() const {
        return bgpaas_client_ ?: StateMachine::IsRouterTypeBGPaaS();
    }
    void SetRouterTypeBGPaaS(bool bgpaas_client) {
        bgpaas_client_ = bgpaas_client;
    }
    bool IsPeerCloseInProgress() const {
        return gr_close_ ?: StateMachine::IsPeerCloseInProgress();
    }

private:
    static int hold_time_msecs_;
    static int keepalive_time_msecs_;
    static TcpSession::Event skip_tcp_event_;
    bool gr_close_;
    bool bgpaas_client_;
    bool skip_bgp_notification_msg_;
};

class BgpServerTest : public BgpServer {
public:
    BgpServerTest(EventManager *evm, const std::string &localname,
                  DB *config_db, DBGraph *config_graph);
    BgpServerTest(EventManager *evm, const std::string &localname);
    ~BgpServerTest();
    bool Configure(const std::string &config);
    BgpPeerTest *FindPeerByUuid(const char *routing_instance,
                                const std::string &uuid);
    BgpPeer *FindPeer(const char *routing_instance,
                      const std::string &peername);
    BgpPeer *FindPeer(TcpSession::Endpoint remote) const;
    BgpPeer *FindMatchingPeer(const std::string &routing_instance,
                              const std::string &name);
    void DisableAllPeers();
    void EnableAllPeers();
    void Shutdown(bool verify = true, bool wait_for_idle = true);
    void VerifyShutdown() const;

    DB *config_db() { return config_db_.get(); }
    DBGraph *config_graph() { return config_graph_.get(); }

    static void GlobalSetUp();
    void set_autonomous_system(as_t as) { autonomous_system_ = as; }
    void set_cluster_id(uint32_t cid) { cluster_id_ = cid; }
    void set_local_autonomous_system(as_t local_as) {
        local_autonomous_system_ = local_as;
    }
    void set_bgp_identifier(uint32_t bgp_id) {
        Ip4Address addr(bgp_id);
        bgp_identifier_ = addr;
    }

    virtual std::string ToString() const;
    uint16_t source_port() const { return source_port_; }
    void set_source_port(uint16_t source_port) { source_port_ = source_port; }
    bool peer_lookup_disable() const { return peer_lookup_disable_; }
    void set_peer_lookup_disable(bool flag) { peer_lookup_disable_ = flag; }

private:
    void PostShutdown();

    std::string name_;
    boost::scoped_ptr<DB> config_db_;
    boost::scoped_ptr<DBGraph> config_graph_;
    bool cleanup_config_;
    uint16_t source_port_;
    bool peer_lookup_disable_;
};

typedef boost::shared_ptr<BgpServerTest> BgpServerTestPtr;

class BgpPeerTest : public BgpPeer {
public:
    BgpPeerTest(BgpServer *server, RoutingInstance *rtinst,
                const BgpNeighborConfig *config);
    virtual ~BgpPeerTest();

    void BindLocalEndpoint(BgpSession *session);

    static void verbose_name(bool verbose) { verbose_name_ = verbose; }
    virtual const std::string &ToString() const;

    bool BgpPeerSendUpdate(const uint8_t *msg, size_t msgsize);
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        tbb::mutex::scoped_lock lock(fnc_mutex_);
        return send_update_fnc_(msg, msgsize);
    }

    void set_send_update_fnc(
            boost::function<bool(const uint8_t *, size_t)> send_update_fnc) {
        tbb::mutex::scoped_lock lock(fnc_mutex_);
        send_update_fnc_ = send_update_fnc;
    }

    bool BgpPeerMpNlriAllowed(uint16_t afi, uint8_t safi);
    virtual bool MpNlriAllowed(uint16_t afi, uint8_t safi) {
        tbb::mutex::scoped_lock lock(fnc_mutex_);
        return mp_nlri_allowed_fnc_(afi, safi);
    }

    void set_mp_nlri_allowed_fnc(
            boost::function<bool(uint16_t, uint8_t)> mp_nlri_allowed_fnc) {
        tbb::mutex::scoped_lock lock(fnc_mutex_);
        mp_nlri_allowed_fnc_ = mp_nlri_allowed_fnc;
    }

    void SetDataCollectionKey(BgpPeerInfo *peer_info) const;
    virtual void SendEndOfRIB(Address::Family family) {
        SendEndOfRIBActual(family);
    }

    void SendEndOfRIB() {
        BOOST_FOREACH(std::string family, negotiated_families()) {
            SendEndOfRIBActual(Address::FamilyFromString(family));
        }
    }

    bool BgpPeerIsReady() { return BgpPeer::IsReady(); }
    virtual bool IsReady() const {
        tbb::mutex::scoped_lock lock(fnc_mutex_);
        return is_ready_fnc_();
    }

    void set_is_ready_fnc(boost::function<bool()> is_ready_fnc) {
        tbb::mutex::scoped_lock lock(fnc_mutex_);
        is_ready_fnc_ = is_ready_fnc;
    }

    bool BgpPeerCheckSplitHorizon(uint32_t cluster_id, uint32_t ribout_cid) {
        return BgpPeer::CheckSplitHorizon(cluster_id, ribout_cid);
    }
    virtual bool CheckSplitHorizon(uint32_t cluster_id = 0,
                  uint32_t ribout_cid = 0) const {
        tbb::mutex::scoped_lock lock(fnc_mutex_);
        return check_split_horizon_fnc_(cluster_id, ribout_cid);
    }

    void set_check_split_horizon_fnc(
            boost::function<bool(uint32_t, uint32_t)> check_split_horizon_fnc) {
        tbb::mutex::scoped_lock lock(fnc_mutex_);
        check_split_horizon_fnc_ = check_split_horizon_fnc;
    }

    bool BgpPeerProcessSession() { return BgpPeer::ProcessSession(); }
    virtual bool ProcessSession() const {
        tbb::mutex::scoped_lock lock(fnc_mutex_);
        return process_session_fnc_();
    }

    void set_process_session_fnc(boost::function<bool()> process_session_fnc) {
        tbb::mutex::scoped_lock lock(fnc_mutex_);
        process_session_fnc_ = process_session_fnc;
    }

    void set_vpn_tables_registered(bool flag) { vpn_tables_registered_ = flag; }
    const int id() const { return id_; }
    void set_id(int id) { id_ = id; }

    virtual void SetAdminState(bool down,
        int subcode = BgpProto::Notification::AdminShutdown);
    void SetAdminStateActual(bool down, int subcode);

    BgpTestUtil util_;

private:
    enum RequestType { ADMIN_UP, ADMIN_DOWN };
    struct Request {
        Request() : result(false) { }
        RequestType type;
        int         subcode;
        bool        result;
    };

    static bool verbose_name_;
    int id_;
    mutable tbb::mutex fnc_mutex_;
    boost::function<bool(const uint8_t *, size_t)> send_update_fnc_;
    boost::function<bool(uint16_t, uint8_t)> mp_nlri_allowed_fnc_;
    boost::function<bool(uint32_t, uint32_t)> check_split_horizon_fnc_;
    boost::function<bool()> process_session_fnc_;
    boost::function<bool()> is_ready_fnc_;
};

class PeerManagerTest : public PeerManager {
public:
    PeerManagerTest(RoutingInstance *instance);
    virtual BgpPeer *PeerLocate(BgpServer *server,
                                const BgpNeighborConfig *config);
    virtual BgpPeer *PeerLookup(TcpSession::Endpoint remote_endpoint) const;
    virtual void DestroyIPeer(IPeer *ipeer);

private:
    typedef std::map<boost::uuids::uuid, BgpPeer *> PeerByUuidMap;
    PeerByUuidMap peers_by_uuid_;
};

class BgpLifetimeManagerTest : public BgpLifetimeManager {
public:
    BgpLifetimeManagerTest(BgpServer *server, int task_id)
        : BgpLifetimeManager(server, task_id), destroy_not_ok_(false) {
    }
    virtual ~BgpLifetimeManagerTest() {
    }

    virtual bool MayDestroy() { return !destroy_not_ok_; }
    virtual void SetQueueDisable(bool disabled) {
        LifetimeManager::SetQueueDisable(disabled);
    }

    void set_destroy_not_ok(bool destroy_not_ok) {
        destroy_not_ok_ = destroy_not_ok;
    }

private:
    bool destroy_not_ok_;
};

class XmppStateMachineTest : public XmppStateMachine {
public:
    XmppStateMachineTest(XmppConnection *connection, bool active,
                         bool auth_enabled = false)
        : XmppStateMachine(connection, active, auth_enabled) {
        if (!notify_fn_.empty())
            notify_fn_(this, true);
    }

    ~XmppStateMachineTest() {
        if (!notify_fn_.empty())
            notify_fn_(this, false);
    }

    void StartConnectTimer(int seconds) {
        connect_timer_->Start(100,
            boost::bind(&XmppStateMachine::ConnectTimerExpired, this),
            boost::bind(&XmppStateMachine::TimerErrorHandler, this, _1, _2));
    }

    void StartOpenTimer(int seconds) {
        open_timer_->Start(1000,
            boost::bind(&XmppStateMachine::OpenTimerExpired, this),
            boost::bind(&XmppStateMachine::TimerErrorHandler, this, _1, _2));
    }

    virtual int hold_time_msecs() const {
        if (hold_time_msecs_)
            return hold_time_msecs_;
        return XmppStateMachine::hold_time_msecs();
    }

    static void set_hold_time_msecs(int hold_time_msecs) {
        hold_time_msecs_ = hold_time_msecs;
    }

    static TcpSession::Event get_skip_tcp_event() { return skip_tcp_event_; }
    static void set_skip_tcp_event(TcpSession::Event event) {
        skip_tcp_event_ = event;
    }
    virtual void OnSessionEvent(TcpSession *session, TcpSession::Event event) {
        if (skip_tcp_event_ != event)
            XmppStateMachine::OnSessionEvent(session, event);
        else
            skip_tcp_event_ = TcpSession::EVENT_NONE;
    }
    size_t get_queue_length() const { return work_queue_.Length(); }
    void set_queue_disable(bool disable) { work_queue_.set_disable(disable); }
    typedef boost::function<void(XmppStateMachineTest *, bool)> NotifyFn;
    static void set_notify_fn(NotifyFn notify_fn) { notify_fn_ = notify_fn; }

private:
    static int hold_time_msecs_;
    static TcpSession::Event skip_tcp_event_;
    static NotifyFn notify_fn_;
};

class XmppLifetimeManagerTest : public XmppLifetimeManager {
public:
    explicit XmppLifetimeManagerTest(int task_id)
        : XmppLifetimeManager(task_id), destroy_not_ok_(false) {
    }
    virtual ~XmppLifetimeManagerTest() {
    }

    virtual bool MayDestroy() { return !destroy_not_ok_; }
    virtual void SetQueueDisable(bool disabled) {
        LifetimeManager::SetQueueDisable(disabled);
    }

    void set_destroy_not_ok(bool destroy_not_ok) {
        destroy_not_ok_ = destroy_not_ok;
    }

private:
    bool destroy_not_ok_;
};

#define BGP_WAIT_FOR_PEER_STATE(peer, state)                                   \
    TASK_UTIL_WAIT_EQ(state, (peer)->GetState(), task_util_wait_time(),        \
                      task_util_retry_count(), "Peer State")

#define BGP_WAIT_FOR_PEER_STATE_NE(peer, state)                                \
    TASK_UTIL_WAIT_NE(state, (peer)->GetState(), task_util_wait_time(),        \
                      task_util_retry_count(), "Peer State")

#define BGP_WAIT_FOR_PEER_DELETION(peer)  \
    TASK_UTIL_EXPECT_EQ_MSG(NULL, peer, "Peer Deletion")

#define BGP_VERIFY_ROUTE_COUNT(table, count)                                   \
    do {                                                                       \
        ostringstream _os;                                                     \
        _os << "Wait for route count in table " << (table)->name();            \
        TASK_UTIL_EXPECT_EQ_MSG(count, static_cast<int>((table)->Size()),      \
                                _os.str());                                    \
    } while (false)

#define BGP_VERIFY_ROUTE_PRESENCE(table, route) \
    TASK_UTIL_EXPECT_NE_MSG(static_cast<BgpRoute *>(NULL),                     \
                            (table)->Find(route), "Route Presence")

#define BGP_VERIFY_ROUTE_ABSENCE(table, route) \
    TASK_UTIL_EXPECT_EQ_MSG(static_cast<BgpRoute *>(NULL),                     \
                            (table)->Find(route), "Route Absence")

#endif // __BGP_SERVER_TEST_UTIL_H__
