/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_client.h"

#include <boost/foreach.hpp>
#include <boost/tuple/tuple.hpp>

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
using boost::tie;

class XmppClient::DeleteActor : public LifetimeActor {
public:
    DeleteActor(XmppClient *client)
        : LifetimeActor(client->lifetime_manager()), client_(client) { }
    virtual bool MayDelete() const {
        CHECK_CONCURRENCY("bgp::Config");
        return (client_->GetSessionQueueSize() == 0);
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
    : XmppConnectionManager(evm, ssl::context::tlsv1_client, false, false),
      config_mgr_(new XmppConfigManager),
      lifetime_manager_(new LifetimeManager(
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"))),
      deleter_(new DeleteActor(this)),
      auth_enabled_(false),
      tcp_hold_time_(XmppChannelConfig::kTcpHoldTime) {
}

XmppClient::XmppClient(EventManager *evm, const XmppChannelConfig *config)
    : XmppConnectionManager(
          evm, ssl::context::tlsv1_client, config->auth_enabled, true),
      config_mgr_(new XmppConfigManager),
      lifetime_manager_(new LifetimeManager(
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"))),
      deleter_(new DeleteActor(this)),
      auth_enabled_(config->auth_enabled),
      tcp_hold_time_(config->tcp_hold_time) {

    if (config->auth_enabled) {

        // Get SSL context from base class and update
        boost::asio::ssl::context *ctx = context();
        boost::system::error_code ec;

        //set mode
        ctx->set_options(ssl::context::default_workarounds |
                         ssl::context::no_sslv3 | ssl::context::no_sslv2, ec);
        if (ec.value() != 0) {
            LOG(ERROR, "Error : " << ec.message() << ", setting ssl options");
            exit(EINVAL);
        }

        // CA certificate, used to verify if the peer certificate
        // is signed by a trusted CA
        std::string ca_cert_filename = config->path_to_ca_cert;
        if (!ca_cert_filename.empty()) {

            // Verify peer has CA signed certificate
            ctx->set_verify_mode(boost::asio::ssl::verify_peer, ec);
            if (ec.value() != 0) {
                LOG(ERROR, "Error : " << ec.message()
                    << ", while setting ssl verification mode");
                exit(EINVAL);
            }

            ctx->load_verify_file(config->path_to_ca_cert, ec);
            if (ec.value() != 0) {
                LOG(ERROR, "Error : " << ec.message()
                    << ", while using cacert file : "
                    << config->path_to_ca_cert);
                exit(EINVAL);
            }
        }
     
        // server certificate
        ctx->use_certificate_chain_file(config->path_to_server_cert, ec);
        if (ec.value() != 0) {
            LOG(ERROR, "Error : " << ec.message() <<
                ", while using server cert file : "
                << config->path_to_server_cert);
            exit(EINVAL);
        }

        // server private key
        ctx->use_private_key_file(config->path_to_server_priv_key,
                                  boost::asio::ssl::context::pem, ec);
        if (ec.value() != 0) {
            LOG(ERROR, "Error : " << ec.message()
                << ", while using privkey file : "
                << config->path_to_server_priv_key);
            exit(EINVAL);
        }
    }
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
    XmppConnectionManager::Shutdown();
    deleter_->Delete();
}

void 
XmppClient::ProcessConfigUpdate(XmppConfigManager::DiffType delta, 
    const XmppChannelConfig *current, const XmppChannelConfig *future) {
    if (delta == XmppConfigManager::DF_ADD) {
        XmppClientConnection *connection = CreateConnection(future);
        connection->Initialize(); // trigger state-machine
    }
    if (delta == XmppConfigManager::DF_DELETE) {
        ConnectionMap::iterator loc = connection_map_.find(current->endpoint);
        if (loc != connection_map_.end()) {
            loc->second->ManagedDelete(); 
        }
    }
}

void
XmppClient::ConfigUpdate(const XmppConfigData *cfg) {
    config_mgr_->SetFuture(cfg);
    config_mgr_->PeerConfigDiff(
        boost::bind(&XmppClient::ProcessConfigUpdate, this, _1, _2, _3));
    config_mgr_->AcceptFuture();
}

void XmppClient::RegisterConnectionEvent(xmps::PeerId id,
    ConnectionEventCb cb) {
    tbb::mutex::scoped_lock lock(connection_event_map_mutex_);
    connection_event_map_.insert(make_pair(id, cb));
}

void XmppClient::UnRegisterConnectionEvent(xmps::PeerId id) {
    tbb::mutex::scoped_lock lock(connection_event_map_mutex_);
    ConnectionEventCbMap::iterator it =  connection_event_map_.find(id);
    if (it != connection_event_map_.end())
        connection_event_map_.erase(it);
}

void XmppClient::NotifyConnectionEvent(XmppChannelMux *mux,
    xmps::PeerState state) {
    tbb::mutex::scoped_lock lock(connection_event_map_mutex_);
    ConnectionEventCbMap::iterator iter = connection_event_map_.begin();
    for (; iter != connection_event_map_.end(); ++iter) {
        ConnectionEventCb cb = iter->second;
        cb(mux, state);
    }
}

size_t XmppClient::ConnectionEventCount() const {
    return connection_event_map_.size();
}

size_t XmppClient::ConnectionCount() const {
    return connection_map_.size();
}

SslSession *XmppClient::AllocSession(SslSocket *socket) {
    SslSession *session = new XmppSession(this, socket);
    return session;
}

XmppClientConnection *XmppClient::FindConnection(const string &address) {
    BOOST_FOREACH(ConnectionMap::value_type &value, connection_map_) {
        if (value.second->ToString() == address)
            return value.second;
    }
    return NULL;
}

XmppClientConnection *XmppClient::CreateConnection(
    const XmppChannelConfig *config) {
    XmppClientConnection *connection =
        XmppObjectFactory::Create<XmppClientConnection>(this, config);
    Endpoint endpoint = connection->endpoint();
    ConnectionMap::iterator loc;
    bool result;
    tie(loc, result) = connection_map_.insert(make_pair(endpoint, connection));
    assert(result);

    return connection;
}

void XmppClient::InsertConnection(XmppClientConnection *connection) {
    assert(!connection->IsDeleted());
    Endpoint endpoint = connection->endpoint();
    ConnectionMap::iterator loc;
    bool result;
    tie(loc, result) = connection_map_.insert(make_pair(endpoint, connection));
    assert(result);
}

void XmppClient::RemoveConnection(XmppClientConnection *connection) {
    assert(connection->IsDeleted());
    Endpoint endpoint = connection->endpoint();
    ConnectionMap::iterator loc = connection_map_.find(endpoint);
    assert(loc != connection_map_.end() && loc->second == connection);
    connection_map_.erase(loc);
}

XmppChannel *XmppClient::FindChannel(const string &address) {
    XmppClientConnection *connection = FindConnection(address);
    return (connection ? connection->ChannelMux() : NULL);
}

int XmppClient::SetDscpValue(uint8_t value) {
    XmppClientConnection *connection =
        FindConnection(XmppInit::kControlNodeJID);
    if (connection) {
        return connection->SetDscpValue(value);
    }
    return 0;
}
