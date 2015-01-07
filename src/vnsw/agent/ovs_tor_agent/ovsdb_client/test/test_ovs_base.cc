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
#include "cfg/cfg_interface.h"
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
#include "openstack/instance_service_server.h"
#include "test_cmn_util.h"
#include "vr_types.h"

#include "openstack/instance_service_server.h"
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
#include "test_ovs_agent_init.h"

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
                  (init_->ovsdb_client()->next_session(NULL))) != NULL);
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
}

TEST_F(OvsBaseTest, physical_router) {
    PhysicalSwitchTable *table =
        tcp_session_->client_idl()->physical_switch_table();
    PhysicalSwitchEntry key(table, "test-router");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key) != NULL));
}

TEST_F(OvsBaseTest, physical_port) {
    PhysicalPortTable *table =
        tcp_session_->client_idl()->physical_port_table();

    PhysicalPortEntry key(table, "ge-0/0/0");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key) != NULL));

    PhysicalPortEntry key1(table, "ge-0/0/1");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key1) != NULL));

    PhysicalPortEntry key2(table, "ge-0/0/2");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key2) != NULL));

    PhysicalPortEntry key3(table, "ge-0/0/3");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key3) != NULL));

    PhysicalPortEntry key4(table, "ge-0/0/4");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key4) != NULL));

    PhysicalPortEntry key5(table, "ge-0/0/47");
    WAIT_FOR(100, 10000, (table->FindActiveEntry(&key5) != NULL));
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    // override with true to initialize ovsdb server and client
    ksync_init = true;
    client = OvsTestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestOvsAgentInit *init = static_cast<TestOvsAgentInit *>(client->agent_init());
    TestShutdown();
    return ret;
}
