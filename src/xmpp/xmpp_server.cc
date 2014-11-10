/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_server.h"

#include <boost/foreach.hpp>
#include <boost/tuple/tuple.hpp>

#include "base/task_annotations.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/xmpp_lifetime.h"
#include "xmpp/xmpp_log.h"
#include "xmpp/xmpp_sandesh.h"
#include "xmpp/xmpp_session.h"

#include "sandesh/request_pipeline.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include "sandesh/xmpp_server_types.h"
#include "sandesh/xmpp_trace_sandesh_types.h"
#include "sandesh/xmpp_client_server_sandesh_types.h"

using namespace std;
using namespace boost::asio;
using boost::tie;

class XmppServer::DeleteActor : public LifetimeActor {
public:
    DeleteActor(XmppServer *server)
        : LifetimeActor(server->lifetime_manager()), server_(server) {
    }
    virtual bool MayDelete() const {
        CHECK_CONCURRENCY("bgp::Config");
        return true;
    }
    virtual void Shutdown() {
        CHECK_CONCURRENCY("bgp::Config");
        server_->SessionShutdown();
    }
    virtual void Destroy() {
        CHECK_CONCURRENCY("bgp::Config");
        server_->Terminate();
    }

private:
    XmppServer *server_;
};

XmppServer::XmppServer(EventManager *evm, const string &server_addr) 
    : TcpServer(evm),
      lifetime_manager_(XmppObjectFactory::Create<XmppLifetimeManager>(
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"))),
      deleter_(new DeleteActor(this)), 
      server_addr_(server_addr),
      log_uve_(false),
      work_queue_(TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0,
          boost::bind(&XmppServer::DequeueConnection, this, _1)) {
}

XmppServer::XmppServer(EventManager *evm) 
    : TcpServer(evm),
      lifetime_manager_(new LifetimeManager(
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"))),
      deleter_(new DeleteActor(this)), 
      log_uve_(false),
      work_queue_(TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0,
          boost::bind(&XmppServer::DequeueConnection, this, _1)) {
}

bool XmppServer::IsPeerCloseGraceful() {
    // If the server is deleted, do not do graceful restart
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

//
// Can be removed after Shutdown is renamed to ManagedDelete.
//
void XmppServer::SessionShutdown() {
    TcpServer::Shutdown();
}

//
// Trigger deletion of the XmppServer.
//
// A mutex is used to ensure that we do not create new XmppServerConnections
// after this point.  Note that this routine and AcceptSession may be called
// concurrently from 2 different threads in tests.
//
void XmppServer::Shutdown() {
    tbb::mutex::scoped_lock lock(deletion_mutex_);
    deleter_->Delete();
}

//
// Called when the XmppServer delete actor is being destroyed.
//
void XmppServer::Terminate() {
    ClearSessions();
    work_queue_.Shutdown();
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

size_t XmppServer::ConnectionEventCount() const {
    return connection_event_map_.size();
}

size_t XmppServer::ConnectionCount() const {
    return connection_map_.size() + deleted_connection_set_.size();
}

XmppServerConnection *XmppServer::FindConnection(Endpoint remote_endpoint) {
    ConnectionMap::iterator loc = connection_map_.find(remote_endpoint);
    if (loc != connection_map_.end()) {
        return loc->second;
    }
    return NULL;
}

XmppServerConnection *XmppServer::FindConnection(const string &address) {
    BOOST_FOREACH(ConnectionMap::value_type &value, connection_map_) {
        if (value.second->ToString() == address)
            return value.second;
    }
    return NULL;
}

bool XmppServer::ClearConnection(const string &hostname) {
    BOOST_FOREACH(ConnectionMap::value_type &value, connection_map_) {
        if (value.second->GetComputeHostName() == hostname) {
            value.second->Clear();
            return true;
        }
    }
    return false;
}

void XmppServer::ClearAllConnections() {
    BOOST_FOREACH(ConnectionMap::value_type &value, connection_map_) {
        value.second->Clear();
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

//
// Accept newly formed passive tcp session by creating necessary xmpp data
// structures. We do so to make sure that if there is any error reported
// over this tcp session, it can still be correctly handled, even though
// the allocated xmpp data structures are not fully processed yet.
//
bool XmppServer::AcceptSession(TcpSession *tcp_session) {
    tbb::mutex::scoped_lock lock(deletion_mutex_);
    if (deleter_->IsDeleted())
        return false;

    XmppSession *session = dynamic_cast<XmppSession *>(tcp_session);
    XmppServerConnection *connection = CreateConnection(session);

    // Register event handler.
    tcp_session->set_observer(boost::bind(&XmppStateMachine::OnSessionEvent,
                                          connection->state_machine(), _1, _2));

    // set async_read_ready as false
    session->set_read_on_connect(false);
    connection->set_session(session);
    connection->set_on_work_queue();
    work_queue_.Enqueue(connection);
    return true;
}

//
// Remove the given XmppServerConnection from the ConnectionMap.
//
void XmppServer::RemoveConnection(XmppServerConnection *connection) {
    CHECK_CONCURRENCY("bgp::Config");

    assert(connection->IsDeleted());
    Endpoint endpoint = connection->endpoint();
    ConnectionMap::iterator loc = connection_map_.find(endpoint);
    assert(loc != connection_map_.end() && loc->second == connection);
    connection_map_.erase(loc);
}

//
// Insert the given XmppServerConnection into the ConnectionMap.
//
void XmppServer::InsertConnection(XmppServerConnection *connection) {
    CHECK_CONCURRENCY("bgp::Config");

    assert(!connection->IsDeleted());
    connection->Initialize();
    Endpoint endpoint = connection->endpoint();
    ConnectionMap::iterator loc;
    bool result;
    tie(loc, result) = connection_map_.insert(make_pair(endpoint, connection));
    assert(result);
}

//
// Create XmppConnnection and its associated data structures. This API is
// only used to allocate data structures and initialize necessary fields.
// The data structures are not populated to any maps in the XmppServer at
// this point.  However, the newly created XmppServerConnection does add
// itself as a dependent of the XmppServer via LifetimeManager linkage.
//
XmppServerConnection *XmppServer::CreateConnection(XmppSession *session) {
    XmppServerConnection  *connection;

    XmppChannelConfig cfg(false);
    cfg.endpoint = session->remote_endpoint();
    cfg.local_endpoint = session->local_endpoint();
    cfg.FromAddr = server_addr_;
    cfg.logUVE = log_uve_;

    XMPP_DEBUG(XmppCreateConnection, session->ToString());
    connection = XmppObjectFactory::Create<XmppServerConnection>(this, &cfg);

    return connection;
}

//
// Handler for XmppServerConnections that are dequeued from the WorkQueue.
//
// Since the XmppServerConnections on the WorkQueue are dependents of the
// XmppServer, we are guaranteed that the XmppServer won't get destroyed
// before the WorkQueue is drained.  Hence we don't need to check for the
// WorkQueue being empty in DeleteActor::MayDelete.
//
bool XmppServer::DequeueConnection(XmppServerConnection *connection) {
    CHECK_CONCURRENCY("bgp::Config"); 
    connection->clear_on_work_queue();

    // This happens if the XmppServer got deleted while the XmppConnnection
    // was on the WorkQueue.
    if (connection->IsDeleted()) {
        connection->RetryDelete();
        return true;
    }

    XmppSession *session = connection->session();
    connection->set_session(NULL);
    Endpoint remote_endpoint = session->remote_endpoint();
    XmppServerConnection *old_connection = FindConnection(remote_endpoint);

    // Close as duplicate if we have a connection from the same Endpoint.
    // Otherwise go ahead and insert into the ConnectionMap. We may find
    // it has a conflicting Endpoint name and decide to terminate it when
    // we process the Open message.
    if (old_connection) {
        XMPP_DEBUG(XmppCreateConnection, "Close duplicate connection " +
            session->ToString());
        DeleteSession(session);
        connection->set_duplicate();
        connection->ManagedDelete();
        InsertDeletedConnection(connection);
    } else {
        InsertConnection(connection);
        connection->AcceptSession(session);
    }

    return true;
}


size_t XmppServer::GetQueueSize() const {
    return work_queue_.Length();
}

void XmppServer::SetQueueDisable(bool disabled) {
    work_queue_.set_disable(disabled);
}

//
// Connection is marked deleted, add it to the ConnectionSet.
//
void XmppServer::InsertDeletedConnection(XmppServerConnection *connection) {
    CHECK_CONCURRENCY("bgp::Config");

    assert(connection->IsDeleted());
    ConnectionSet::iterator it;
    bool result;
    tie(it, result) = deleted_connection_set_.insert(connection);
    assert(result);
}

//
// Connection is being destroyed, remove it from the ConnectionSet.
//
void XmppServer::RemoveDeletedConnection(XmppServerConnection *connection) {
    CHECK_CONCURRENCY("bgp::Config");

    assert(connection->IsDeleted());
    ConnectionSet::iterator it = deleted_connection_set_.find(connection);
    assert(it != deleted_connection_set_.end());
    deleted_connection_set_.erase(it);
}

const XmppConnectionEndpoint *XmppServer::FindConnectionEndpoint(
    const string &endpoint_name) const {
    ConnectionEndpointMap::const_iterator loc =
        connection_endpoint_map_.find(endpoint_name);
    return (loc != connection_endpoint_map_.end() ? loc->second : NULL);
}

//
// Find or create an XmppConnectionEndpoint for the Endpoint of the given
// XmppConnnection. If XmppConnectionEndpoint is already associated with a
// XmppConnection, return NULL to indicate that the XmppConnection should
// be terminated. Otherwise, associate it with the given XmppConnection.
//
XmppConnectionEndpoint *XmppServer::LocateConnectionEndpoint(
    XmppServerConnection *connection) {
    tbb::mutex::scoped_lock lock(endpoint_map_mutex_);

    const string &endpoint_name = connection->ToString();
    XmppConnectionEndpoint *conn_endpoint;
    ConnectionEndpointMap::iterator loc =
        connection_endpoint_map_.find(endpoint_name);
    if (loc == connection_endpoint_map_.end()) {
        conn_endpoint = new XmppConnectionEndpoint(endpoint_name);
        bool result;
        tie(loc, result) = connection_endpoint_map_.insert(
            make_pair(endpoint_name, conn_endpoint));
        assert(result);
    } else {
        conn_endpoint = loc->second;
    }

    if (conn_endpoint->connection())
        return NULL;
    conn_endpoint->set_connection(connection);
    return conn_endpoint;
}

//
// Remove association of the given XmppConnectionEndpoint with XmppConnection.
// This method is provided just to make things symmetrical - the caller could
// simply have called XmppConnectionEndpoint::reset_connection directly.
//
void XmppServer::ReleaseConnectionEndpoint(XmppServerConnection *connection) {
    tbb::mutex::scoped_lock lock(endpoint_map_mutex_);

    if (!connection->conn_endpoint())
        return;
    assert(connection->conn_endpoint()->connection() == connection);
    connection->conn_endpoint()->reset_connection();
}

void XmppServer::FillShowConnections(
    vector<ShowXmppConnection> *show_connection_list) const {
    BOOST_FOREACH(const ConnectionMap::value_type &value, connection_map_) {
        const XmppServerConnection *connection = value.second;
        ShowXmppConnection show_connection;
        connection->FillShowInfo(&show_connection);
        show_connection_list->push_back(show_connection);
    }
    BOOST_FOREACH(const XmppServerConnection *connection,
        deleted_connection_set_) {
        ShowXmppConnection show_connection;
        connection->FillShowInfo(&show_connection);
        show_connection_list->push_back(show_connection);
    }
}

class ShowXmppConnectionHandler {
public:
    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps, int stage, int instNum,
            RequestPipeline::InstData *data) {
        const ShowXmppConnectionReq *req =
            static_cast<const ShowXmppConnectionReq *>(ps.snhRequest_.get());
        XmppSandeshContext *xsc =
            dynamic_cast<XmppSandeshContext *>(req->client_context());
        const XmppServer *xmpp_server = xsc->xmpp_server;

        ShowXmppConnectionResp *resp = new ShowXmppConnectionResp;
        vector<ShowXmppConnection> connections;
        xmpp_server->FillShowConnections(&connections);
        resp->set_connections(connections);
        resp->set_context(req->context());
        resp->Response();
        return true;
    }
};

void ShowXmppConnectionReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);

    // Request pipeline has single stage to collect connection info and
    // respond to the request.
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = ShowXmppConnectionHandler::CallbackS1;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

class ClearXmppConnectionHandler {
public:
    static bool CallbackS1(const Sandesh *sr,
        const RequestPipeline::PipeSpec ps, int stage, int instNum,
        RequestPipeline::InstData *data) {

        const ClearXmppConnectionReq *req =
            static_cast<const ClearXmppConnectionReq *>(ps.snhRequest_.get());
        XmppSandeshContext *xsc =
            dynamic_cast<XmppSandeshContext *>(req->client_context());
        XmppServer *server = xsc->xmpp_server;

        ClearXmppConnectionResp *resp = new ClearXmppConnectionResp;
        if (!xsc->test_mode) {
            resp->set_success(false);
        } else if (req->get_hostname_or_all() != "all") {
            if (server->ClearConnection(req->get_hostname_or_all())) {
                resp->set_success(true);
            } else {
                resp->set_success(false);
            }
        } else {
            if (server->ConnectionCount()) {
                server->ClearAllConnections();
                resp->set_success(true);
            } else {
                resp->set_success(false);
            }
        }

        resp->set_context(req->context());
        resp->Response();
        return true;
    }
};

void ClearXmppConnectionReq::HandleRequest() const {

    // config task is used to create and delete connection objects.
    // hence use the same task to find the connection
    RequestPipeline::StageSpec s1;
    s1.taskId_ = TaskScheduler::GetInstance()->GetTaskId("bgp::Config");
    s1.instances_.push_back(0);
    s1.cbFn_ = ClearXmppConnectionHandler::CallbackS1;

    RequestPipeline::PipeSpec ps(this);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
