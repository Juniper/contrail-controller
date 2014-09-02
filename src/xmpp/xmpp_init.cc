/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/util.h"
#include "xmpp/xmpp_config.h"
#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_log.h"
#include "xmpp/xmpp_server.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "sandesh/xmpp_trace_sandesh_types.h"

using namespace std;

const char *XmppInit::kControlNodeJID =
    "network-control@contrailsystems.com";
const char *XmppInit::kAgentNodeJID =
    "agent@contrailsystems.com";
const char *XmppInit::kDnsNodeJID =
    "network-dns@contrailsystems.com";
const char *XmppInit::kPubSubNS =
    "http://jabber.org/protocol/pubsub";
const char *XmppInit::kJIDControlBgp = 
    "network-control@contrailsystems.com/bgp-peer";
const char *XmppInit::kJIDControlDns = 
    "network-control@contrailsystems.com/dns-peer";
const char *XmppInit::kFqnPrependAgentNodeJID = 
    "default-global-system-config:";
const char *XmppInit::kConfigPeer = "config";
const char *XmppInit::kBgpPeer = "bgp-peer";
const char *XmppInit::kDnsPeer = "dns-peer";
const char *XmppInit::kOtherPeer = "other-peer";

SandeshTraceBufferPtr XmppMessageTraceBuf(SandeshTraceBufferCreate(XMPP_MESSAGE_TRACE_BUF, 1000));
SandeshTraceBufferPtr XmppTraceBuf(SandeshTraceBufferCreate(XMPP_TRACE_BUF, 1000));

XmppInit::XmppInit() 
   : g_server_(NULL), g_client_(NULL), cfg_(new XmppConfigData) {
}

XmppInit::~XmppInit() {
    if (g_client_) {
        g_client_->Shutdown();
        TcpServerManager::DeleteServer(g_client_);
        g_client_ = NULL;
    }

    if (g_server_) {
        g_server_->Shutdown();
        TcpServerManager::DeleteServer(g_server_);
        g_server_ = NULL;
    }

    if (cfg_) {
        delete cfg_;
        cfg_ = NULL;
    }
}

void XmppInit::Reset(bool keep_config) {
    g_server_ = NULL;
    g_client_ = NULL;
    if (!keep_config) {
        cfg_ = NULL;
    }
}

void XmppInit::InitClient(XmppClient *client) {
    g_client_ = client;
    g_client_->ConfigUpdate(cfg_);
}

void XmppInit::InitServer(XmppServer *server, int port, bool logUVE) {
    g_server_ = server;
    g_server_->Initialize(port, logUVE);
}

XmppChannelConfig *XmppInit::AllocChannelConfig(
    const string &peer_ip, int port, const string &from, const string &to, 
    const string &node, bool isClient) {
    boost::system::error_code ec;
    boost::asio::ip::address peer_addr =
            boost::asio::ip::address::from_string(peer_ip, ec);
    if (ec) {
        return NULL;
    }
    XmppChannelConfig *cc = new XmppChannelConfig(isClient);
    cc->endpoint.address(peer_addr);
    cc->endpoint.port(port);
    cc->ToAddr = to;
    cc->FromAddr = from;
    cc->NodeAddr = node;
    return cc;
}

void XmppInit::AddXmppChannelConfig(XmppChannelConfig *cc) {
    if (cc) {
        cfg_->AddXmppChannelConfig(cc);
    }
    return;
}
