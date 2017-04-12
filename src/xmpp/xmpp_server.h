/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_SERVER_H__
#define __XMPP_SERVER_H__

#include <boost/asio/ip/tcp.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/lifetime.h"
#include "base/queue_task.h"
#include "bgp/bgp_config.h"
#include "io/ssl_server.h"
#include "net/address.h"
#include "xmpp/xmpp_session.h"
#include "xmpp/xmpp_config.h"
#include "xmpp/xmpp_connection_manager.h"
#include "xmpp/xmpp_channel_mux.h"

class LifetimeActor;
class LifetimeManager;
class ShowXmppConnection;
class ShowXmppServerResp;
class TcpSession;
class XmppConnectionEndpoint;
class XmppConfigUpdater;
class XmppServerConnection;

// Class to represent Xmpp Server
class XmppServer : public XmppConnectionManager {
public:
    typedef boost::asio::ip::tcp::endpoint Endpoint;

    XmppServer(EventManager *evm, const std::string &server_addr,
               const XmppChannelConfig *config);
    XmppServer(EventManager *evm, const std::string &server_addr);
    explicit XmppServer(EventManager *evm);
    virtual ~XmppServer();

    typedef boost::function<void(XmppChannelMux *, xmps::PeerState)> ConnectionEventCb;
    void RegisterConnectionEvent(xmps::PeerId, ConnectionEventCb);
    void UnRegisterConnectionEvent(xmps::PeerId);
    void NotifyConnectionEvent(XmppChannelMux *, xmps::PeerState);
    size_t ConnectionEventCount() const;

    LifetimeManager *lifetime_manager();
    virtual LifetimeActor *deleter();
    virtual LifetimeActor *deleter() const;

    virtual TcpSession *CreateSession();
    virtual bool Initialize(short port);
    virtual bool Initialize(short port, bool logUVE);
    void SessionShutdown();
    bool MayDelete() const;
    void Shutdown();
    void Terminate();

    virtual XmppServerConnection *CreateConnection(XmppSession *session);
    virtual XmppServerConnection *FindConnection(Endpoint remote_endpoint);
    virtual XmppServerConnection *FindConnection(const std::string &address);
    virtual void InsertConnection(XmppServerConnection *connection);
    virtual void RemoveConnection(XmppServerConnection *connection);

    virtual void InsertDeletedConnection(XmppServerConnection *connection);
    virtual void RemoveDeletedConnection(XmppServerConnection *connection);

    bool ClearConnection(const std::string &hostname);
    void ClearAllConnections();

    const std::string &ServerAddr() const { return server_addr_; }
    size_t ConnectionCount() const;

    XmppConnectionEndpoint *FindConnectionEndpoint(
            const std::string &endpoint_name);
    XmppConnectionEndpoint *LocateConnectionEndpoint(
        XmppServerConnection *connection, bool &created);
    void ReleaseConnectionEndpoint(XmppServerConnection *connection);

    void FillShowConnections(
        std::vector<ShowXmppConnection> *show_connection_list) const;
    void FillShowServer(ShowXmppServerResp *resp) const;
    void CreateConfigUpdater(BgpConfigManager *config_manager);
    bool IsPeerCloseGraceful() const;
    uint16_t GetGracefulRestartTime() const;
    uint32_t GetLongLivedGracefulRestartTime() const;
    uint32_t GetEndOfRibReceiveTime() const;
    uint32_t GetEndOfRibSendTime() const;
    bool IsGRHelperModeEnabled() const;
    bool gr_helper_disable() const { return gr_helper_disable_; }
    void set_gr_helper_disable(bool gr_helper_disable) {
        gr_helper_disable_ = gr_helper_disable;
    }
    void SetDscpValue(uint8_t value);
    uint8_t dscp_value() const { return dscp_value_; }

protected:
    virtual SslSession *AllocSession(SslSocket *socket);
    virtual bool AcceptSession(TcpSession *session);

    typedef std::map<Endpoint, XmppServerConnection *> ConnectionMap;
    ConnectionMap connection_map_;

private:
    class DeleteActor;
    friend class BgpXmppBasicTest;
    friend class DeleteActor;
    friend class XmppStateMachineTest;

    typedef std::set<XmppServerConnection *> ConnectionSet;
    typedef std::map<std::string, XmppConnectionEndpoint *> ConnectionEndpointMap;
    typedef std::map<xmps::PeerId, ConnectionEventCb> ConnectionEventCbMap;

    bool DequeueConnection(XmppServerConnection *connection);
    size_t GetConnectionQueueSize() const;
    void SetConnectionQueueDisable(bool disabled);
    void WorkQueueExitCallback(bool done);

    ConnectionSet deleted_connection_set_;
    size_t max_connections_;

    tbb::mutex endpoint_map_mutex_;
    ConnectionEndpointMap connection_endpoint_map_;

    tbb::mutex deletion_mutex_;
    boost::scoped_ptr<LifetimeManager> lifetime_manager_;
    boost::scoped_ptr<DeleteActor> deleter_;

    ConnectionEventCbMap connection_event_map_;
    std::string server_addr_;
    bool log_uve_;
    bool auth_enabled_;
    int tcp_hold_time_;
    bool gr_helper_disable_;
    boost::scoped_ptr<XmppConfigUpdater> xmpp_config_updater_;
    uint8_t dscp_value_;
    WorkQueue<XmppServerConnection *> connection_queue_;

    DISALLOW_COPY_AND_ASSIGN(XmppServer);
};

#endif
