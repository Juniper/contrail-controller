/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_server.h"
#include <boost/bind.hpp>
#include <boost/tuple/tuple.hpp>

#include "base/task_annotations.h"

#include "base/task_annotations.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/xmpp_log.h"
#include "xmpp/xmpp_session.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include "sandesh/xmpp_trace_sandesh_types.h"
#include "sandesh/xmpp_client_server_sandesh_types.h"

using namespace std;
using namespace boost::asio;
using boost::tie;

class XmppServer::DeleteActor : public LifetimeActor {
public:
    DeleteActor(XmppServer *server)
        : LifetimeActor(server->lifetime_manager()), server_(server) { }
    virtual bool MayDelete() const {
        return true;
    }
    virtual void Shutdown() {
        CHECK_CONCURRENCY("bgp::Config");
    }
    virtual void Destroy() {
        CHECK_CONCURRENCY("bgp::Config");
    }

private:
    XmppServer *server_;
};

XmppServer::XmppServer(EventManager *evm, const string &server_addr) 
    : TcpServer(evm), lifetime_manager_(new LifetimeManager(
            TaskScheduler::GetInstance()->GetTaskId("bgp::Config"))),
      deleter_(new DeleteActor(this)), 
      work_queue_(TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0,
                  boost::bind(&XmppServer::DequeueConnection, this, _1)) {
    server_addr_ = server_addr;
    log_uve_ = false;
}

XmppServer::XmppServer(EventManager *evm) 
    : TcpServer(evm), lifetime_manager_(new LifetimeManager(
            TaskScheduler::GetInstance()->GetTaskId("bgp::Config"))),
      deleter_(new DeleteActor(this)), 
      work_queue_(TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0,
                  boost::bind(&XmppServer::DequeueConnection, this, _1)) {
    log_uve_ = false;
}

bool XmppServer::IsPeerCloseGraceful() {

    //
    // If the server is deleted, do not do graceful restart
    //
    if (deleter()->IsDeleted()) return false;

    static bool init = false;
    static bool enabled = false;

    if (!init) {
        init = true;
        char *p = getenv("XMPP_GRACEFUL_RESTART_ENABLE");
        if (p && !strcasecmp(p, "true")) enabled = true;
    }
    return enabled;
}

XmppServer::~XmppServer() {
    STLDeleteElements(&connection_endpoint_map_);
    TcpServer::ClearSessions();
}

bool XmppServer::Initialize(short port) {
    TcpServer::Initialize(port);
    log_uve_ = false;

    return true;
}

void XmppServer::Initialize(short port, bool logUVE) {
    TcpServer::Initialize(port);
    log_uve_ = logUVE;
}

LifetimeActor *XmppServer::deleter() {
    return deleter_.get();
}

LifetimeManager *XmppServer::lifetime_manager() {
    return lifetime_manager_.get();
}

TcpSession *XmppServer::CreateSession() {
    typedef boost::asio::detail::socket_option::boolean<
#ifdef __APPLE__
        SOL_SOCKET, SO_REUSEPORT> reuse_port_t;
#else
        SOL_SOCKET, SO_REUSEADDR> reuse_addr_t;
#endif 
    TcpSession *session = TcpServer::CreateSession();
    Socket *socket = session->socket();

    boost::system::error_code err;
    socket->open(ip::tcp::v4(), err);
    if (err) {
        XMPP_WARNING(ServerOpenFail, err.message());
    }

#ifdef __APPLE__    
    socket->set_option(reuse_port_t(true), err);
#else
    socket->set_option(reuse_addr_t(true), err);
#endif
    if (err) {
        XMPP_WARNING(SetSockOptFail, err.message());
        return session;
    }

    socket->bind(LocalEndpoint(), err);
    if (err) {
        XMPP_WARNING(ServerBindFailure, err.message());
    }

    return session;
}

void XmppServer::Shutdown() {
    TcpServer::Shutdown();
    work_queue_.Shutdown();
    deleter_->Delete();
}

bool XmppServer::Compare(const string &peer_addr, 
                         const XmppConnectionPair &p) const {
    return (peer_addr.compare(p.second->ToString()) ?  false : true);
}

size_t XmppServer::ConnectionEventCount() const {
    return connection_event_map_.size();
}

size_t XmppServer::ConnectionsCount() const {
    return connection_map_.size() + deleted_connection_set_.size();
}

XmppConnection *XmppServer::FindConnection(Endpoint remote_endpoint) {
    XmppConnectionMap::iterator loc = connection_map_.find(remote_endpoint);
    if (loc != connection_map_.end()) {
        return loc->second;
    }
    return NULL;
}

XmppConnection *XmppServer::FindConnection(const string &peer_addr) {
    XmppConnectionMap::iterator loc = find_if(connection_map_.begin(),
            connection_map_.end(), boost::bind(&XmppServer::Compare, this,
                                           boost::ref(peer_addr), _1));
    if (loc != connection_map_.end()) {
        return loc->second;
    }
    return NULL;
}

XmppConnection *XmppServer::FindConnectionbyHostName(const string hostname) {
    for (XmppConnectionMap::iterator iter = connection_map_.begin(), next = iter;
                                     iter != connection_map_.end(); 
                                     iter = next) {
        next++;
        XmppConnection *conn = iter->second;
        if (hostname.compare(conn->GetComputeHostName()) == 0) {
            return conn;
        }
    }
    return NULL;
}

void XmppServer::ClearAllConnections() {
    for (XmppConnectionMap::iterator iter = connection_map_.begin(), next = iter;
                                     iter != connection_map_.end(); 
                                     iter = next) {
        next++;
        XmppConnection *conn = iter->second;
        conn->Clear();
    }
}

void XmppServer::ClearConnection(XmppConnection *conn) {
    if (conn) {
        conn->Clear();
    }
}

void XmppServer::RegisterConnectionEvent(xmps::PeerId id, 
                                         ConnectionEventCb cb) {
    connection_event_map_.insert(make_pair(id, cb));
}

void XmppServer::UnRegisterConnectionEvent(xmps::PeerId id) {
    ConnectionEventCbMap::iterator it =  connection_event_map_.find(id);
    if (it != connection_event_map_.end())
        connection_event_map_.erase(it);
}

void XmppServer::NotifyConnectionEvent(XmppChannelMux *mux, 
                                       xmps::PeerState state) {
    ConnectionEventCbMap::iterator iter = connection_event_map_.begin();
    for (; iter != connection_event_map_.end(); ++iter) {
        ConnectionEventCb cb = iter->second;
        cb(mux, state);
    }
}

TcpSession *XmppServer::AllocSession(Socket *socket) {
    TcpSession *session = new XmppSession(this, socket);
    return session;
}

// Accept newly formed passive tcp session by creating necessary xmpp data
// structures. We do so to make sure that if there is any error reported
// over this tcp session, it can still be correctly handled, even though
// the allocated xmpp data structures are not fully processed yet.
bool XmppServer::AcceptSession(TcpSession *tcp_session) {
    XmppSession *session = dynamic_cast<XmppSession *>(tcp_session);
    XmppConnection *connection = CreateConnection(session);

    // Register event handler.
    tcp_session->set_observer(boost::bind(&XmppStateMachine::OnSessionEvent,
                                          connection->state_machine(), _1, _2));

    // set async_read_ready as false
    session->set_read_on_connect(false);
    connection->set_session(session);
    work_queue_.Enqueue(connection);
    return true;
}

//
// Remove the given XmppConnection from the XmppConnectionMap.
//
// Return true if the XmppConnection was in the map, false otherwise.
// Note that the map may have a different incarnation of the XmppConnection
// i.e. same endpoint but different XmppConnection.
//
bool XmppServer::RemoveConnection(XmppConnection *connection) {
    CHECK_CONCURRENCY("bgp::Config");

    boost::asio::ip::tcp::endpoint endpoint = connection->endpoint();
    XmppConnectionMap::iterator loc = connection_map_.find(endpoint);
    if (loc != connection_map_.end() && loc->second == connection) {
        connection_map_.erase(loc);
        return true;
    }
    return false;
}

//
// Insert the given XmppConnection into the XmppConnectionMap.
//
void XmppServer::InsertConnection(XmppConnection *connection) {
    CHECK_CONCURRENCY("bgp::Config");

    connection->Initialize();
    boost::asio::ip::tcp::endpoint endpoint = connection->endpoint();
    XmppConnectionMap::iterator loc;
    bool result;
    tie(loc, result) = connection_map_.insert(make_pair(endpoint, connection));
    assert(result);
}

//
// Connection is being deleted, add it to the XmppConnectionSet.
//
void XmppServer::DeleteConnection(XmppConnection *connection) {
    CHECK_CONCURRENCY("bgp::Config");

    XmppConnectionSet::iterator it;
    bool result;
    tie(it, result) = deleted_connection_set_.insert(connection);
    assert(result);
}

//
// Connection is being destroyed, remove it from the XmppConnectionSet.
//
void XmppServer::DestroyConnection(XmppConnection *connection) {
    CHECK_CONCURRENCY("bgp::Config");

    XmppConnectionSet::iterator it = deleted_connection_set_.find(connection);
    assert(it != deleted_connection_set_.end());
    deleted_connection_set_.erase(it);
    delete connection;
}

// Create XmppConnnection and its associated data structures. This API is
// only used to allocate data structures and initialize necessary fields.
// However, the data structures are not populated to any maps yet.
XmppConnection *XmppServer::CreateConnection(XmppSession *session) {
    XmppConnection   *connection;
    ip::tcp::endpoint remote_endpoint;

    remote_endpoint.address(session->remote_endpoint().address());
    remote_endpoint.port(0);

    // Create a connection.
    XmppChannelConfig cfg(false);
    cfg.endpoint = remote_endpoint;
    cfg.FromAddr = this->ServerAddr();
    cfg.logUVE = this->log_uve_;

    XMPP_DEBUG(XmppCreateConnection,
               session->remote_endpoint().address().to_string());
    connection = XmppObjectFactory::Create<XmppServerConnection>(this, &cfg);

    return connection;
}

bool XmppServer::DequeueConnection(XmppConnection *connection) {
    CHECK_CONCURRENCY("bgp::Config"); 

    XmppSession *session = connection->session();
    connection->set_session(NULL);

    Endpoint remote_endpoint;
    remote_endpoint.address(session->remote_endpoint().address());
    remote_endpoint.port(0);

    XmppConnection *old_connection = FindConnection(remote_endpoint);
    if (old_connection == NULL) {
        InsertConnection(connection);
        connection->AcceptSession(session);
    } else {
        if (!IsPeerCloseGraceful()) {

            // Delete the newly created connection and session.
            XMPP_DEBUG(XmppCreateConnection, "Close duplicate connection " +
                       session->ToString());
            DeleteSession(session);
            DeleteConnection(connection);
            connection->ManagedDelete();

        } else {

            DeleteConnection(connection);
            connection->ManagedDelete();

            // TODO GR case: associate the new session
            // ShutdownPending() is set in bgp::config task context via LTM
            // TCP Close event enqueues an event to xmpp::StateMachine task, which
            // may have not yet run, which enqueues event to LTM to set the
            // ShutdownPending() flag. 
            // In such a case  where xmpp::StateMachine task did not
            // run, connection->ShutdownPending() will not be set.
            // 
            // Hence ShutdownPending() should be set in ioReader task on
            // TCP close event and also while calling XmppServer Shutdown()
            // Also appropriately take care of asserts in bgp_xmpp_channel.cc
            // for ReceiveUpdate
            //
            connection = old_connection;
            if (connection->session()) {
                DeleteSession(connection->session());
            }
            connection->Initialize();
            connection->AcceptSession(session);
        }
    }

    return true;
}

XmppConnectionEndpoint *XmppServer::LocateConnectionEndpoint(
    Ip4Address address) {
    XmppConnectionEndpointMap::iterator loc =
        connection_endpoint_map_.find(address);
    if (loc != connection_endpoint_map_.end())
        return loc->second;

    XmppConnectionEndpoint *conn_endpoint = new XmppConnectionEndpoint(address);
    bool result;
    tie(loc, result) =
        connection_endpoint_map_.insert(make_pair(address, conn_endpoint));
    assert(result);
    return conn_endpoint;
}
