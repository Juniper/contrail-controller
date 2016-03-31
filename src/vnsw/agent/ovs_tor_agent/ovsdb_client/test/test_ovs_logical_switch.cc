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
#include "oper/physical_device_vn.h"
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
#include "test-xml/test_xml.h"
#include "test-xml/test_xml_oper.h"
#include "test_xml_physical_device.h"
#include "test_xml_ovsdb.h"

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

TEST_F(OvsBaseTest, LogicalSwitchBasic) {
    AgentUtXmlTest test("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_client/test/xml/logical-switch-base.xml");
    // set current session in test context
    OvsdbTestSetSessionContext(tcp_session_);
    AgentUtXmlOperInit(&test);
    AgentUtXmlPhysicalDeviceInit(&test);
    AgentUtXmlOvsdbInit(&test);
    if (test.Load() == true) {
        test.ReadXml();
        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
}

TEST_F(OvsBaseTest, PhysicalDeviceVnWithNullDevice) {
    AddPhysicalDeviceVn(agent_, 1, 1, true);

    VnAddReq(1, "vn1");
    WAIT_FOR(100, 10000, (VnGet(1) != NULL));

    DelPhysicalDeviceVn(agent_, 1, 1, true);

    VnDelReq(1);
    WAIT_FOR(100, 10000, (VnGet(1) == NULL));
}

TEST_F(OvsBaseTest, PhysicalLocatorCreateWait) {
    DBRequest device_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    device_req.key.reset(new PhysicalDeviceKey(MakeUuid(1)));
    device_req.data.reset(new PhysicalDeviceData(agent_, "test-router",
                                                 "test-router", "",
                                                 Ip4Address::from_string("1.1.1.1"),
                                                 Ip4Address::from_string("2.2.2.2"),
                                                 "OVS", NULL));
    agent_->physical_device_table()->Enqueue(&device_req);
    VnAddReq(1, "vn1");
    WAIT_FOR(100, 10000, (VnGet(1) != NULL));
    VnAddReq(2, "vn2");
    WAIT_FOR(100, 10000, (VnGet(2) != NULL));

    uint64_t txn_failures = tcp_session_->client_idl()->stats().txn_failed;
    // hold Db task to execute both logical switch  add requests in
    // single db task run
    TestTaskHold *hold =
        new TestTaskHold(TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0);
    AddPhysicalDeviceVn(agent_, 1, 1, false);
    AddPhysicalDeviceVn(agent_, 1, 2, false);

    delete hold;
    hold = NULL;
    client->WaitForIdle();

    LogicalSwitchTable *ls_table =
        tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key1(ls_table, UuidToString(MakeUuid(1)));
    LogicalSwitchEntry key2(ls_table, UuidToString(MakeUuid(2)));
    LogicalSwitchEntry *entry;
    WAIT_FOR(100, 10000, ((entry = static_cast<LogicalSwitchEntry *>
                    (ls_table->Find(&key1))) != NULL &&
                entry->GetState() == KSyncEntry::IN_SYNC));
    WAIT_FOR(100, 10000, ((entry = static_cast<LogicalSwitchEntry *>
                    (ls_table->Find(&key2))) != NULL &&
                entry->GetState() == KSyncEntry::IN_SYNC));

    hold = new TestTaskHold(TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0);

    // create delete and trigger change on entry to cancel del task.
    entry->DeleteOvs(false);
    EXPECT_TRUE(entry->IsDeleteOvsInProgress());
    entry->table()->Change(entry);
    EXPECT_FALSE(entry->IsDeleteOvsInProgress());

    // trigger delete
    entry->DeleteOvs(false);
    EXPECT_TRUE(entry->IsDeleteOvsInProgress());
    delete hold;

    WAIT_FOR(1000, 10000, (entry->IsDeleteOvsInProgress() == false));
    WAIT_FOR(1000, 10000, (entry->IsResolved() == true));

    // there should not be any more transaction failures
    EXPECT_EQ(txn_failures, tcp_session_->client_idl()->stats().txn_failed);

    DelPhysicalDeviceVn(agent_, 1, 1, true);
    DelPhysicalDeviceVn(agent_, 1, 2, true);
    VnDelReq(1);
    VnDelReq(2);

    DBRequest del_dev_req(DBRequest::DB_ENTRY_DELETE);
    del_dev_req.key.reset(new PhysicalDeviceVnKey(MakeUuid(1),
                                                  MakeUuid(1)));
    agent_->physical_device_table()->Enqueue(&del_dev_req);
}

TEST_F(OvsBaseTest, RenewLogicalSwitchWithVxlanAcquireFailure) {
    DBRequest device_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    device_req.key.reset(new PhysicalDeviceKey(MakeUuid(1)));
    device_req.data.reset(new PhysicalDeviceData(agent_, "test-router",
                                                 "test-router", "",
                                                 Ip4Address::from_string("1.1.1.1"),
                                                 Ip4Address::from_string("2.2.2.2"),
                                                 "OVS", NULL));
    agent_->physical_device_table()->Enqueue(&device_req);
    VnAddReq(1, "vn1");
    WAIT_FOR(100, 10000, (VnGet(1) != NULL));

    AddPhysicalDeviceVn(agent_, 1, 1, false);
    client->WaitForIdle();

    LogicalSwitchTable *ls_table =
        tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key1(ls_table, UuidToString(MakeUuid(1)));
    LogicalSwitchEntry *entry;
    WAIT_FOR(100, 10000, ((entry = static_cast<LogicalSwitchEntry *>
                    (ls_table->Find(&key1))) != NULL &&
                entry->GetState() == KSyncEntry::IN_SYNC));
    WAIT_FOR(1000, 10000, (entry->IsResolved() == true));

    // hold reference to object to avoid deletion
    KSyncEntry::KSyncEntryPtr ref = entry;
    DelPhysicalDeviceVn(agent_, 1, 1, false);
    client->WaitForIdle();

    // acquire and hold vxlan id 200
    LogicalSwitchEntry tmp_entry(ls_table, "tmp_entry");
    assert(tmp_entry.res_vxlan_id().AcquireVxLanId(200));

    // change vxlan id to 200
    VnVxlanAddReq(1, "vn1", 200);
    client->WaitForIdle();

    // re-add dev-vn entry
    AddPhysicalDeviceVn(agent_, 1, 1, false);
    client->WaitForIdle();

    WAIT_FOR(100, 10000, ((entry = static_cast<LogicalSwitchEntry *>
                    (ls_table->Find(&key1))) != NULL));
    // release the reference to logical switch entry
    ref = NULL;

    // release vxlan id 200
    tmp_entry.res_vxlan_id().ReleaseVxLanId(false);
    client->WaitForIdle();

    WAIT_FOR(100, 10000, ((entry = static_cast<LogicalSwitchEntry *>
                    (ls_table->Find(&key1))) != NULL &&
                entry->GetState() == KSyncEntry::IN_SYNC));
    WAIT_FOR(1000, 10000, (entry->IsResolved() == true));

    DelPhysicalDeviceVn(agent_, 1, 1, true);
    VnDelReq(1);

    DBRequest del_dev_req(DBRequest::DB_ENTRY_DELETE);
    del_dev_req.key.reset(new PhysicalDeviceVnKey(MakeUuid(1),
                                                  MakeUuid(1)));
    agent_->physical_device_table()->Enqueue(&del_dev_req);
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
