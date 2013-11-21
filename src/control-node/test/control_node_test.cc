/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "control_node_test.h"

#include "base/test/task_test_util.h"
#include "io/event_manager.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/ifmap_xmpp.h"
#include "xmpp/xmpp_server.h"

#include "schema/vnc_cfg_types.h"

using boost::asio::ip::address;

namespace test {

const char *ControlNodeTest::kNodeJID = "network-control@contrailsystems.com";
int ControlNodeTest::node_count_ = 0;

ControlNodeTest::ControlNodeTest(EventManager *evm, const std::string &hostname)
    : bgp_server_(new BgpServerTest(evm, hostname)),
      xmpp_server_(new XmppServer(evm, kNodeJID)),
      map_server_(new IFMapServer(bgp_server_->config_db(),
                                  bgp_server_->config_graph(),
                                  evm->io_service())),
      xmpp_manager_(
            new BgpXmppChannelManager(xmpp_server_, bgp_server_.get())),
      map_manager_(
            new IFMapChannelManager(xmpp_server_, map_server_.get())) {
    ControlNode::SetDefaultSchedulingPolicy();
    bgp_server_->session_manager()->Initialize(0);
    xmpp_server_->Initialize(0, false);

    SetUp();
    map_server_->Initialize();
}

XmppChannelConfig *ControlNodeTest::CreateChannelConfig() const {
    XmppChannelConfig *config = new XmppChannelConfig();
    config->endpoint.address(address::from_string("127.0.0.1"));
    config->endpoint.port(xmpp_port());
    config->FromAddr = kNodeJID;
    config->NodeAddr = kNodeJID;
    return config;
}

void ControlNodeTest::Shutdown() {

    //
    // Close XmppManager first before closing BgpServer, so that all Xmpp
    // sessions are properly deleted
    //
    xmpp_server_->Shutdown();
    task_util::WaitForIdle();

    TcpServerManager::DeleteServer(xmpp_server_);

    bgp_server_->Shutdown();
    map_server_->Shutdown();
    task_util::WaitForIdle();
    TASK_UTIL_ASSERT_EQ(0, bgp_server_->routing_instance_mgr()->count());
    map_manager_.reset();
    xmpp_manager_.reset();
    TearDown();
}

ControlNodeTest::~ControlNodeTest() {
    Shutdown();
}

const std::string &ControlNodeTest::localname() const {
    return bgp_server_->config_manager()->localname();
}

int ControlNodeTest::bgp_port() const {
    return bgp_server_->session_manager()->GetPort();
}

int ControlNodeTest::xmpp_port() const {
    return xmpp_server_->GetPort();
}

DB *ControlNodeTest::config_db() {
    return bgp_server_->config_db();
}

void ControlNodeTest::BgpConfig(const std::string &config) {
    bgp_server_->Configure(config);
}

void ControlNodeTest::IFMapMessage(const std::string &msg) {
    IFMapServerParser *parser = IFMapServerParser::GetInstance("vnc_cfg");
    parser->Receive(bgp_server_->config_db(), msg.data(), msg.length(), 0);
}

void ControlNodeTest::VerifyRoutingInstance(const std::string instance,
                                            bool verify_network_index) {
    const RoutingInstanceMgr *ri_mgr = bgp_server_->routing_instance_mgr();
    TASK_UTIL_WAIT_NE_NO_MSG(ri_mgr->GetRoutingInstance(instance),
        NULL, 1000, 10000, "Wait for routing instance..");
    const RoutingInstance *rti = ri_mgr->GetRoutingInstance(instance);

    if (verify_network_index) {
        TASK_UTIL_WAIT_NE_NO_MSG(rti->virtual_network_index(),
            0, 1000, 10000, "Wait for vn index..");
    }
}

int ControlNodeTest::BgpEstablishedCount() const {
    RoutingInstanceMgr *manager  = bgp_server_->routing_instance_mgr();
    RoutingInstance *rt_default = manager->GetRoutingInstance(
        BgpConfigManager::kMasterInstance);
    assert(rt_default != NULL);
    int count = 0;
    for (PeerManager::BgpPeerKeyMap::const_iterator iter =
         rt_default->peer_manager()->peer_map().begin();
         iter != rt_default->peer_manager()->peer_map().end(); ++iter) {
        const BgpPeer *peer = iter->second;
        if (peer->GetState() == StateMachine::ESTABLISHED) {
            count++;
        }
    }
    return count;
}

BgpServerTest *ControlNodeTest::bgp_server() {
    return bgp_server_.get();
}

XmppServer *ControlNodeTest::xmpp_server() {
    return xmpp_server_;
}

BgpXmppChannelManager *ControlNodeTest::xmpp_channel_manager() {
    return xmpp_manager_.get();
}

void ControlNodeTest::SetUp() {
    if (node_count_++) {
        return;
    }
    IFMapServerParser *parser = IFMapServerParser::GetInstance("vnc_cfg");
    vnc_cfg_ParserInit(parser);
    bgp_schema_ParserInit(parser);    
}

void ControlNodeTest::TearDown() {
    if (--node_count_) {
        return;
    }
    IFMapServerParser *parser = IFMapServerParser::GetInstance("vnc_cfg");
    parser->MetadataClear("vnc_cfg");
}

}  // namespace test
