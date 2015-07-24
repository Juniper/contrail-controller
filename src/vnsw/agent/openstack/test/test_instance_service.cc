/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "base/logging.h"
#include <boost/assign/list_of.hpp>

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <openstack/instance_service_server.h>

#include <io/test/event_manager_test.h>
#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "vr_types.h"

#include <async/TAsioAsync.h>
#include <protocol/TBinaryProtocol.h>
#include <gen-cpp/InstanceService.h>

#include <async/TFuture.h>
#include <io/event_manager.h>
#include <db/db.h>
#include <cmn/agent_cmn.h>
#include <cfg/cfg_interface.h>
#include <cfg/cfg_types.h>
#include <base/test/task_test_util.h>

using namespace std;
using namespace boost::assign;
using namespace apache::thrift;
using boost::shared_ptr;

int8_t port_id []     = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int8_t instance_id [] = {0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int8_t vn_id []       = {0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
string mac("00:00:00:00:00:");
string ip_address("10.10.10.");
string tap_name("vmport");
string display_name("vm");
string host_name("vm");
string host("vm");

bool connection_complete = false;
bool port_resp_done  = false;

static tuuid MakeUuid(int8_t *data, int index) {
    int8_t tmp[16];
    bzero(tmp, sizeof(tmp));
    if (data) {
        memcpy(tmp, data, sizeof(tmp));
    }
    tmp[15] = index;

    tuuid u;
    u.insert(u.begin(), tmp, tmp + sizeof(port_id));
    return u;
}

static void InitPortInfo(Port &port, int8_t portid, int8_t instanceid,
                         int8_t vnid, int16_t port_type) {
    std::string id_hex_str = integerToString(portid);
    port.tap_name = (tap_name + id_hex_str);
    port.ip_address = (ip_address + integerToString(portid));
    port.mac_address = (mac + id_hex_str);
    port.__set_display_name(display_name + integerToString(instanceid));
    port.__set_hostname(host_name + integerToHexString(instanceid));
    port.__set_host(host + integerToHexString(instanceid));
    port.__set_port_type(port_type);
    
    port.port_id = MakeUuid(port_id, portid);
    port.instance_id = MakeUuid(instance_id, instanceid);
    port.vn_id = MakeUuid(vn_id, vnid);
}


class InstanceServiceTest : public ::testing::Test {
public:
    InstanceServiceTest() :
        agent_(Agent::GetInstance()), table_(agent_->interface_table()),
        cfg_table_(agent_->interface_config_table()), service_(agent_) {
    }

    void SetUp() {
        init_port_count = table_->Size();
        TASK_UTIL_EXPECT_EQ(0, cfg_table_->Size());
    }

    void TearDown() {
        TASK_UTIL_EXPECT_EQ(0, cfg_table_->Size());
        TASK_UTIL_EXPECT_EQ(init_port_count, table_->Size());
    }

    CfgIntEntry *GetConfigEntry(int index) {
        CfgIntKey key(MakeUuid(index));
        return static_cast<CfgIntEntry *>(cfg_table_->Find(&key));
    }

    int ConfigVersion() const {
        return service_.version();
    }

    void SetStaleTimeout(uint32_t msec = 100) {
        service_.interface_stale_cleaner()->set_timeout(msec);
    }

    bool DeletePort(int8_t portid) {
        service_.DeletePort(MakeUuid(port_id, portid));
        return true;
    }

    bool AddPort(std::vector<Port> pl) {
        service_.AddPort(pl);
        return true;
    }

    bool AddPort(Port &port) {
        std::vector<Port> pl;
        pl.push_back(port);
        return AddPort(pl);
    }


    bool AddPort(int8_t portid, int8_t instanceid, int8_t vnid,
                 int16_t port_type) {
        Port port;
        InitPortInfo(port, portid, instanceid, vnid, port_type);
        return AddPort(port);
    }

    bool SendKeepAlive() {
        service_.KeepAliveCheck();
        return true;
    }

    bool Connect() {
        service_.Connect();
    }

    Agent *agent_;
    InterfaceTable *table_;
    CfgIntTable *cfg_table_;
    InstanceServiceAsyncHandler service_;
    uint32_t init_port_count;
};

TEST_F(InstanceServiceTest, KeepAliveTest) {
    SendKeepAlive();
}

// Add and delete port check the status of the cfg intf db
TEST_F(InstanceServiceTest, PortAdd) {
    AddPort(1, 1, 1, PortTypes::NovaVMPort);
    client->WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, cfg_table_->Size());
    CfgIntEntry *cfg_entry = GetConfigEntry(1);
    EXPECT_TRUE(cfg_entry != NULL);
    EXPECT_EQ(cfg_entry->GetVersion(), ConfigVersion());

    DeletePort(1);
    client->WaitForIdle();
}

// 1) Add name space port
// 2) Add nova port
// 3) Run stale timer
// 4) Verifuy nova port deleted and name space port exists
// 5) Delete name space port
TEST_F(InstanceServiceTest, StaleTimer) {
    // Add name-space port
    AddPort(1, 1, 1, PortTypes::NameSpacePort);
    TASK_UTIL_EXPECT_EQ(1, cfg_table_->Size());
    CfgIntEntry *cfg_entry = GetConfigEntry(1);
    EXPECT_TRUE(cfg_entry != NULL);
    EXPECT_EQ(cfg_entry->GetVersion(), ConfigVersion());
    EXPECT_EQ(cfg_entry->port_type(), CfgIntEntry::CfgIntNameSpacePort);

    // Add nova port
    AddPort(2, 2, 2, PortTypes::NovaVMPort);
    TASK_UTIL_EXPECT_EQ(2, cfg_table_->Size());
    CfgIntEntry *cfg_entry1 = GetConfigEntry(2);
    EXPECT_TRUE(cfg_entry1 != NULL);
    EXPECT_EQ(cfg_entry1->GetVersion(), ConfigVersion());
    EXPECT_EQ(cfg_entry1->port_type(), CfgIntEntry::CfgIntVMPort);

    // Set stale-timeout and connect for audit to be done
    SetStaleTimeout();
    Connect();
    client->WaitForIdle();

    // Namespace port should not be deleted
    TASK_UTIL_EXPECT_EQ(1, cfg_table_->Size());
    cfg_entry1 = GetConfigEntry(1);
    EXPECT_TRUE(cfg_entry1 != NULL);
    EXPECT_EQ(cfg_entry1->port_type(), CfgIntEntry::CfgIntNameSpacePort);

    // Nova port must be deleted
    EXPECT_TRUE(GetConfigEntry(2) == NULL);

    // Cleanup
    DeletePort(1);
    client->WaitForIdle();
}

TEST_F(InstanceServiceTest, ReconnectVersionCheck) {
    // Add port with old-version
    AddPort(1, 1, 1, PortTypes::NovaVMPort);
    TASK_UTIL_EXPECT_EQ(1, cfg_table_->Size());
    CfgIntEntry *cfg_entry = GetConfigEntry(1);
    EXPECT_TRUE(cfg_entry != NULL);
    EXPECT_EQ(cfg_entry->GetVersion(), ConfigVersion());

    // Set stale-timeout and connect for audit to be done
    SetStaleTimeout();
    // Connect increases version number
    Connect();
    client->WaitForIdle();

    // Audit timer must kick-in and delete the stale entry
    TASK_UTIL_EXPECT_EQ(0, cfg_table_->Size());
    EXPECT_TRUE((cfg_entry = GetConfigEntry(1)) == NULL);
}

// Add Delete Multiple ports
TEST_F(InstanceServiceTest, MultiPortAddDelete) {
    Port port1;
    InitPortInfo(port1, 3, 3, 1, PortTypes::NovaVMPort);

    Port port2;
    InitPortInfo(port2, 4, 4, 1, PortTypes::NovaVMPort);

    Port port3;
    InitPortInfo(port3, 5, 5, 1, PortTypes::NovaVMPort);

    std::vector<Port> pl;
    pl.clear();
    pl.push_back(port1);
    pl.push_back(port2);
    pl.push_back(port3);

    AddPort(pl);
    TASK_UTIL_EXPECT_EQ(3, cfg_table_->Size());

    // Add again same muliple ports
    pl.clear();
    pl.push_back(port1);
    pl.push_back(port2);
    pl.push_back(port3);
    AddPort(pl);
    TASK_UTIL_EXPECT_EQ(3, cfg_table_->Size());

    // Delete Multiple ports
    DeletePort(3);
    DeletePort(4);
    DeletePort(5);
    TASK_UTIL_EXPECT_EQ(0, cfg_table_->Size());

    // Duplicate delete
    DeletePort(3);
    DeletePort(4);
    DeletePort(5);
    TASK_UTIL_EXPECT_EQ(0, cfg_table_->Size());
}

// Test negitive inputs
// Wrong IP address
TEST_F(InstanceServiceTest, AddPortWrongIP) {
    Port port;
    InitPortInfo(port, 2, 2, 2, PortTypes::NovaVMPort);
    port.ip_address = "TestIP";
    AddPort(port);
    client->WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, cfg_table_->Size());
}

// Wrong IP address : send IPv6 address
TEST_F(InstanceServiceTest, AddPortWrongIPIsv6) {
    Port port;
    InitPortInfo(port, 2, 2, 2, PortTypes::NovaVMPort);
    port.ip_address = "30:32::03";
    AddPort(port);
    client->WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, cfg_table_->Size());
}

// Null uuids, ports do get created
TEST_F(InstanceServiceTest, NullUUIDTest) {
    Port port;
    InitPortInfo(port, 1, 1, 1, PortTypes::NovaVMPort);
    port.port_id = MakeUuid(NULL, 0);
    AddPort(port);
    client->WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, cfg_table_->Size());

    DeletePort(0);
    TASK_UTIL_EXPECT_EQ(0, cfg_table_->Size());

    InitPortInfo(port, 1, 1, 1, PortTypes::NovaVMPort);
    port.instance_id = MakeUuid(NULL, 0);
    AddPort(port);
    client->WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, cfg_table_->Size());

    DeletePort(1);
    TASK_UTIL_EXPECT_EQ(0, cfg_table_->Size());

    InitPortInfo(port, 1, 1, 1, PortTypes::NovaVMPort);
    port.vn_id = MakeUuid(NULL, 0);
    AddPort(port);
    client->WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, cfg_table_->Size());

    DeletePort(1);
    TASK_UTIL_EXPECT_EQ(0, cfg_table_->Size());
}

// Send extra bytes in the uuid, and it shouldn't get added
TEST_F(InstanceServiceTest, Wrong_Port_UUIDTest1) {
    Port port;
    InitPortInfo(port, 1, 1, 1, PortTypes::NovaVMPort);
    port.port_id.push_back(0);
    AddPort(port);
    client->WaitForIdle();

    // There is no reliable way to check if port is not added
    // Just for validation, add a dummy port. Since DBRequest are processed in
    // sequence. If the new port is added, the old should be ignored
    AddPort(2, 2, 2, PortTypes::NovaVMPort);
    client->WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, cfg_table_->Size());
    CfgIntEntry *cfg_entry1 = GetConfigEntry(2);
    EXPECT_TRUE(cfg_entry1 != NULL);

    DeletePort(2);
    client->WaitForIdle();
}

// Send one byte less in the uuid, and it shouldn't get added
TEST_F(InstanceServiceTest, Wrong_Port_UUIDTest2) {
    Port port;
    InitPortInfo(port, 1, 1, 1, PortTypes::NovaVMPort);
    port.port_id.pop_back();
    AddPort(port);
    client->WaitForIdle();

    // There is no reliable way to check if port is not added
    // Just for validation, add a dummy port. Since DBRequest are processed in
    // sequence. If the new port is added, the old should be ignored
    AddPort(2, 2, 2, PortTypes::NovaVMPort);
    client->WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, cfg_table_->Size());
    CfgIntEntry *cfg_entry1 = GetConfigEntry(2);
    EXPECT_TRUE(cfg_entry1 != NULL);

    DeletePort(2);
    client->WaitForIdle();
}


TEST_F(InstanceServiceTest, Wrong_Instance_UUIDTest1) {
    Port port;
    InitPortInfo(port, 1, 1, 1, PortTypes::NovaVMPort);
    port.instance_id.push_back(0);
    AddPort(port);
    client->WaitForIdle();

    // There is no reliable way to check if port is not added
    // Just for validation, add a dummy port. Since DBRequest are processed in
    // sequence. If the new port is added, the old should be ignored
    AddPort(2, 2, 2, PortTypes::NovaVMPort);
    client->WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, cfg_table_->Size());
    CfgIntEntry *cfg_entry1 = GetConfigEntry(2);
    EXPECT_TRUE(cfg_entry1 != NULL);

    DeletePort(2);
    client->WaitForIdle();
}

TEST_F(InstanceServiceTest, Wrong_Instance_UUIDTest2) {
    Port port;
    InitPortInfo(port, 1, 1, 1, PortTypes::NovaVMPort);
    port.instance_id.pop_back();
    AddPort(port);
    client->WaitForIdle();

    // There is no reliable way to check if port is not added
    // Just for validation, add a dummy port. Since DBRequest are processed in
    // sequence. If the new port is added, the old should be ignored
    AddPort(2, 2, 2, PortTypes::NovaVMPort);
    client->WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, cfg_table_->Size());
    CfgIntEntry *cfg_entry1 = GetConfigEntry(2);
    EXPECT_TRUE(cfg_entry1 != NULL);

    DeletePort(2);
    client->WaitForIdle();
}

TEST_F(InstanceServiceTest, Wrong_Vn_UUIDTest1) {
    Port port;
    InitPortInfo(port, 1, 1, 1, PortTypes::NovaVMPort);
    port.vn_id.push_back(0);
    AddPort(port);
    client->WaitForIdle();

    // There is no reliable way to check if port is not added
    // Just for validation, add a dummy port. Since DBRequest are processed in
    // sequence. If the new port is added, the old should be ignored
    AddPort(2, 2, 2, PortTypes::NovaVMPort);
    client->WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, cfg_table_->Size());
    CfgIntEntry *cfg_entry1 = GetConfigEntry(2);
    EXPECT_TRUE(cfg_entry1 != NULL);

    DeletePort(2);
    client->WaitForIdle();
}

TEST_F(InstanceServiceTest, Wrong_Vn_UUIDTest2) {
    Port port;
    InitPortInfo(port, 1, 1, 1, PortTypes::NovaVMPort);
    port.vn_id.pop_back();
    AddPort(port);
    client->WaitForIdle();

    // There is no reliable way to check if port is not added
    // Just for validation, add a dummy port. Since DBRequest are processed in
    // sequence. If the new port is added, the old should be ignored
    AddPort(2, 2, 2, PortTypes::NovaVMPort);
    client->WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, cfg_table_->Size());
    CfgIntEntry *cfg_entry1 = GetConfigEntry(2);
    EXPECT_TRUE(cfg_entry1 != NULL);

    DeletePort(2);
    client->WaitForIdle();
}

int main (int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
