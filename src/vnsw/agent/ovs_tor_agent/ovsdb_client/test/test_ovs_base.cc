/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <io/test/event_manager_test.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "filter/acl.h"
#include "test_cmn_util.h"
#include "vr_types.h"

#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"
#include "control_node_mock.h"
#include "xml/xml_pugi.h"
#include "controller/controller_peer.h"
#include "controller/controller_export.h"
#include "controller/controller_vrf_export.h"

#include "ovs_tor_agent/ovsdb_client/ovsdb_route_peer.h"
#include "ovs_tor_agent/ovsdb_client/physical_switch_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/logical_switch_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/physical_port_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/vrf_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/vlan_port_binding_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/unicast_mac_local_ovsdb.h"
#include "test_ovs_agent_init.h"

#include <ovsdb_types.h>

using namespace pugi;
using namespace OVSDB;

EventManager evm1;
ServerThread *thread1;
test::ControlNodeMock *bgp_peer1;

EventManager evm2;
ServerThread *thread2;
test::ControlNodeMock *bgp_peer2;

void RouterIdDepInit(Agent *agent) {
    Agent::GetInstance()->controller()->Connect();
}

class OvsBaseTest : public ::testing::Test {
protected:
    OvsBaseTest() {
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        init_ = static_cast<TestOvsAgentInit *>(client->agent_init());
        peer_manager_ = init_->ovs_peer_manager();
        WAIT_FOR(100, 10000,
                 (tcp_session_ = static_cast<OvsdbClientTcpSession *>
                  (init_->ovsdb_client()->NextSession(NULL))) != NULL);
        WAIT_FOR(100, 10000,
                 (tcp_session_->client_idl() != NULL));
        WAIT_FOR(100, 10000, (tcp_session_->status() == string("Established")));
        client->WaitForIdle();
        WAIT_FOR(100, 10000, (!tcp_session_->client_idl()->IsMonitorInProcess()));
        client->WaitForIdle();
    }

    virtual void TearDown() {
    }

    Agent *agent_;
    TestOvsAgentInit *init_;
    OvsPeerManager *peer_manager_;
    OvsdbClientTcpSession *tcp_session_;
};

TEST_F(OvsBaseTest, connection) {
    WAIT_FOR(100, 10000, (tcp_session_->status() == string("Established")));

    OvsdbClientReq *req = new OvsdbClientReq();
    req->HandleRequest();
    client->WaitForIdle();
    req->Release();
}

TEST_F(OvsBaseTest, physical_router) {
    PhysicalSwitchTable *table =
        tcp_session_->client_idl()->physical_switch_table();
    PhysicalSwitchEntry key(table, "test-router");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key) != NULL));

    OvsdbPhysicalSwitchReq *req = new OvsdbPhysicalSwitchReq();
    req->HandleRequest();
    client->WaitForIdle();
    req->Release();
}

TEST_F(OvsBaseTest, physical_port) {
    PhysicalPortTable *table =
        tcp_session_->client_idl()->physical_port_table();

    PhysicalPortEntry key(table, "test-router", "ge-0/0/0");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key) != NULL));

    PhysicalPortEntry key1(table, "test-router", "ge-0/0/1");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key1) != NULL));

    PhysicalPortEntry key2(table, "test-router", "ge-0/0/2");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key2) != NULL));

    PhysicalPortEntry key3(table, "test-router", "ge-0/0/3");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key3) != NULL));

    PhysicalPortEntry key4(table, "test-router", "ge-0/0/4");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key4) != NULL));

    PhysicalPortEntry key5(table, "test-router", "ge-0/0/47");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key5) != NULL));

    OvsdbPhysicalPortReq *req = new OvsdbPhysicalPortReq();
    req->HandleRequest();
    client->WaitForIdle();
    req->Release();
}

TEST_F(OvsBaseTest, VrfAuditRead) {
    VrfOvsdbObject *table = tcp_session_->client_idl()->vrf_ovsdb();
    VrfOvsdbEntry vrf_key(table, UuidToString(MakeUuid(1)));
    WAIT_FOR(1000, 10000, (table->Find(&vrf_key) != NULL));

    OvsdbLogicalSwitchReq *req = new OvsdbLogicalSwitchReq();
    req->HandleRequest();
    client->WaitForIdle();
    req->Release();

    OvsdbMulticastMacLocalReq *mcast_req = new OvsdbMulticastMacLocalReq();
    mcast_req->HandleRequest();
    client->WaitForIdle();
    mcast_req->Release();

    OvsdbUnicastMacRemoteReq *mac_req = new OvsdbUnicastMacRemoteReq();
    mac_req->HandleRequest();
    client->WaitForIdle();
    mac_req->Release();
}

TEST_F(OvsBaseTest, VlanPortBindingAuditRead) {
    VlanPortBindingTable *table = tcp_session_->client_idl()->vlan_port_table();
    VlanPortBindingEntry key(table, "test-router", "ge-0/0/0", 100, "");
    WAIT_FOR(1000, 10000, (table->Find(&key) != NULL));

    OvsdbVlanPortBindingReq *req = new OvsdbVlanPortBindingReq();
    req->HandleRequest();
    client->WaitForIdle();
    req->Release();
}

TEST_F(OvsBaseTest, connection_close) {
    // Take reference to idl so that session object itself is not deleted.
    OvsdbClientIdlPtr tcp_idl = tcp_session_->client_idl();

    tcp_session_->TriggerClose();
    client->WaitForIdle();

    // Validate that keepalive timer has stopped, for the idl
    WAIT_FOR(100, 10000,
             (tcp_idl->IsKeepAliveTimerActive() == false));

    UnicastMacLocalOvsdb *ucast_local_table =
        tcp_idl->unicast_mac_local_ovsdb();
    // Validate that vrf re-eval queue of unicast mac local
    // table has stopped
    WAIT_FOR(100, 10000,
             (ucast_local_table->IsVrfReEvalQueueActive() == false));

    // release idl reference
    tcp_idl = NULL;

    WAIT_FOR(100, 10000,
             (tcp_session_ = static_cast<OvsdbClientTcpSession *>
              (init_->ovsdb_client()->NextSession(NULL))) != NULL);
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    // override with true to initialize ovsdb server and client
    ksync_init = true;
    client = OvsTestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    return ret;
}
