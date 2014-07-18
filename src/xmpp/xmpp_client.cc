/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_client.h"

#include "base/task_annotations.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/xmpp_log.h"
#include "xmpp/xmpp_session.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include "sandesh/xmpp_client_server_sandesh_types.h"

using namespace std;
using namespace boost::asio;

class XmppClient::DeleteActor : public LifetimeActor {
public:
    DeleteActor(XmppClient *client)
        : LifetimeActor(client->lifetime_manager()), client_(client) { }
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
    XmppClient *client_;
};

XmppClient::XmppClient(EventManager *evm) 
    : TcpServer(evm), config_mgr_(new XmppConfigManager), 
      lifetime_manager_(new LifetimeManager(
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"))),
      deleter_(new DeleteActor(this)) {
}

XmppClient::~XmppClient() {
}

bool XmppClient::Initialize(short port) {
    TcpServer::Initialize(port);
    return true;
}

LifetimeActor *XmppClient::deleter() {
    return deleter_.get();
}
 
LifetimeManager *XmppClient::lifetime_manager() {
    return lifetime_manager_.get();
}

TcpSession *XmppClient::CreateSession() {
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
        XMPP_WARNING(ClientOpenFail, err.message());
        DeleteSession(session);
        return NULL;
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

    err = session->SetSocketOptions();
    if (err) {
        DeleteSession(session);
        assert(0); 
    }

    return session;
}

void XmppClient::Shutdown() {
    TcpServer::Shutdown();
    deleter_->Delete();
}

XmppConnection *XmppClient::CreateConnection(const XmppChannelConfig *config) {
    XmppConnection *connection =
        XmppObjectFactory::Create<XmppClientConnection>(this, config);
    boost::asio::ip::tcp::endpoint ep = connection->endpoint();
    connection_map_.insert(ep, connection);

    return connection;
}

void 
XmppClient::ProcessConfigUpdate(XmppConfigManager::DiffType delta, 
                                    const XmppChannelConfig *current,
                                    const XmppChannelConfig *future) {
    if (delta == XmppConfigManager::DF_ADD) {
        XmppConnection *connection = CreateConnection(future);
        connection->Initialize();
    }
    if (delta == XmppConfigManager::DF_DELETE) {
        XmppConnectionMap::iterator loc =
                connection_map_.find(current->endpoint);
        if (loc != connection_map_.end()) {
            loc->second->ManagedDelete(); 
        }
    }
}

void
XmppClient::ConfigUpdate(const XmppConfigData *cfg) {
    config_mgr_->SetFuture(cfg);

    config_mgr_->PeerConfigDiff(boost::bind(&XmppClient::ProcessConfigUpdate,
                                            this, _1, _2, _3));
    config_mgr_->AcceptFuture();
}

void XmppClient::RegisterConnectionEvent(xmps::PeerId id, ConnectionEventCb cb) {
    connection_event_map_.insert(make_pair(id, cb));
}

void XmppClient::UnRegisterConnectionEvent(xmps::PeerId id) {
    ConnectionEventCbMap::iterator it =  connection_event_map_.find(id);
    if (it != connection_event_map_.end())
        connection_event_map_.erase(it);
}

void XmppClient::NotifyConnectionEvent(XmppChannelMux *mux,
                                       xmps::PeerState state) {
    ConnectionEventCbMap::iterator iter = connection_event_map_.begin();
    for (; iter != connection_event_map_.end(); ++iter) {
        ConnectionEventCb cb = iter->second;
        cb(mux, state);
    }
}

size_t XmppClient::ConnectionEventCount() const {
    return connection_event_map_.size();
}

size_t XmppClient::ConnectionsCount() const {
    return connection_map_.size();
}

TcpSession *XmppClient::AllocSession(Socket *socket) {
    TcpSession *session = new XmppSession(this, socket);
    return session;
}

bool XmppClient::Compare(const string &server_addr, 
                         const XmppConnectionPair &p) const {
    return (server_addr.compare(p.second->ToString()) ? false : true);
}

XmppConnection *XmppClient::FindConnection(const string &server_addr) {
    XmppConnectionMap::iterator loc =
            find_if(connection_map_.begin(), connection_map_.end(),
                    boost::bind(&XmppClient::Compare, this, server_addr, _1));
    if (loc != connection_map_.end()) {
        return loc->second;
    }
    return NULL;
}

void XmppClient::RemoveConnection(XmppConnection *connection) {
    CHECK_CONCURRENCY("bgp::Config");
    boost::asio::ip::tcp::endpoint endpoint = connection->endpoint();
    connection_map_.erase(endpoint);
}

XmppChannel *XmppClient::FindChannel(const string &server_addr) {
    XmppConnection *connection = FindConnection(server_addr);
    if (connection == NULL) 
        return NULL;

    return connection->ChannelMux();
}
