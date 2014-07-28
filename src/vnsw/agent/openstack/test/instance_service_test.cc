/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>

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

extern void InstanceInfoServiceServerInit(Agent *agent);

void RouterIdDepInit(Agent *agent) {
}

//namespace {
class NovaInfoServerTest : public ::testing::Test {
protected:
};


DB *db;

uint32_t base_port_count = 3;
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

boost::shared_ptr<InstanceServiceAsyncClient> client_service;

void AddPortCallback(tuuid port_id, std::string tap_name, bool ret) 
{
    std::cout << "Device " << tap_name << ", Return value " << ret << std::endl;
}

void AddPortErrback(const InstanceService_AddPort_result& result) 
{
    std::cout << "Exception caught " << __FUNCTION__ << std::endl;
}

void DeletePortCallback(tuuid port_id, bool ret)
{
    std::cout << "DeletePort " << ret << std::endl;
}

void DeletePortErrback(const InstanceService_DeletePort_result& result) 
{
    std::cout << "Exception caught " << __FUNCTION__ << std::endl;
}

void ConnectCallback(bool ret) 
{
    std::cout << "Connect, " << "Return value " << ret << std::endl;
}

void KeepAliveCallback(bool ret) 
{
    std::cout << "Keepalive, " << "Return value " << ret << std::endl;
}

static inline const std::string integerToHexString(const int8_t &num)
{
    std::stringstream ss;
    ss << std::hex << (uint16_t)num;
    return ss.str();
}

void CreatePort(Port &port, int8_t portid, int8_t instanceid, int8_t vnid) {
    std::string id_hex_str = integerToString(portid);
    port.tap_name = (tap_name + id_hex_str);
    port.ip_address = (ip_address + integerToString(portid));
    port.mac_address = (mac + id_hex_str);
    port.__set_display_name(display_name + integerToString(instanceid));
    port.__set_hostname(host_name + integerToHexString(instanceid));
    port.__set_host(host + integerToHexString(instanceid));
    
    tuuid port_tuuid;
    int8_t t_port_id[sizeof(port_id)];
    memcpy(t_port_id, port_id, sizeof(t_port_id));
    t_port_id[15] = portid;
    port_tuuid.insert(port_tuuid.begin(), t_port_id, 
                      t_port_id + (sizeof(t_port_id)/sizeof(int8_t)));
    port.port_id = port_tuuid;

    tuuid instance_tuuid;
    int8_t t_instance_id[sizeof(instance_id)];
    memcpy(t_instance_id, instance_id, sizeof(t_instance_id));
    t_instance_id[15] = instanceid;
    instance_tuuid.insert(instance_tuuid.begin(), t_instance_id, 
                          t_instance_id + (sizeof(t_instance_id)/sizeof(int8_t)));
    port.instance_id = instance_tuuid;

    tuuid vn_tuuid;
    int8_t t_vn_id[sizeof(vn_id)];
    memcpy(t_vn_id, vn_id, sizeof(t_vn_id));
    t_vn_id[15] = vnid;
    vn_tuuid.insert(vn_tuuid.begin(), t_vn_id, 
                    t_vn_id + (sizeof(t_vn_id)/sizeof(int8_t)));
    port.vn_id = vn_tuuid;
}

void DelVmPort(boost::shared_ptr<InstanceServiceAsyncClient> inst_client, int8_t portid) {
    tuuid port_tuuid;
    int8_t t_port_id[sizeof(port_id)];
    memcpy(t_port_id, port_id, sizeof(t_port_id));
    t_port_id[15] = portid;
    port_tuuid.insert(port_tuuid.begin(), t_port_id, 
                      t_port_id + (sizeof(t_port_id)/sizeof(int8_t)));
    inst_client->DeletePort(port_tuuid).setCallback(
        boost::bind(&DeletePortCallback, port_tuuid, _1)).setErrback(DeletePortErrback);
}

void AddVmPort(boost::shared_ptr<InstanceServiceAsyncClient> inst_client, int8_t portid,
               int8_t instanceid, int8_t vnid) {
    std::vector<Port> pl;
    Port port;
    CreatePort(port, portid, instanceid, vnid);
    pl.push_back(port);
    inst_client->AddPort(pl).setCallback(
        boost::bind(&AddPortCallback, 
                    port.port_id, port.tap_name, _1)).setErrback(AddPortErrback);
}

void ConnectToServer(boost::shared_ptr<InstanceServiceAsyncClient> inst_client) {
    inst_client->Connect().setCallback(
        boost::bind(&ConnectCallback, _1));
}

void SendKeepAlive(boost::shared_ptr<InstanceServiceAsyncClient> inst_client) {
    inst_client->KeepAliveCheck().setCallback(
        boost::bind(&KeepAliveCallback, _1));
}

void connected(boost::shared_ptr<InstanceServiceAsyncClient> inst_client) {
    std::cout << "connected!!!" << std::endl;
    client_service = inst_client;
    connection_complete = true;
}

static void PortReqResponse(Sandesh *sandesh, string &resp_str) {
    PortResp *resp = dynamic_cast<PortResp *>(sandesh);
    std::string s_resp_str = resp->get_resp();
    EXPECT_EQ(resp_str, s_resp_str);
    port_resp_done = true;
}

TEST_F(NovaInfoServerTest, IntrospecPortAdd) {
    AddPortReq *req = new AddPortReq();
    req->set_port_uuid(std::string("00000000-0000-0000-0000-000000000001"));
    req->set_instance_uuid(std::string("00000000-0000-0000-0000-000000000001"));
    req->set_tap_name(std::string("tap1"));
    req->set_ip_address(std::string("1.1.1.1"));
    req->set_vn_uuid(std::string("00000000-0000-0000-0000-000000000001"));
    req->set_mac_address(std::string("00:00:00:00:00:1"));
    req->set_vm_name(std::string("vm1"));
    req->set_vlan_id(100);
    req->set_vm_project_uuid("00000000-0000-0000-0000-000000000001");
    port_resp_done = false;
    Sandesh::set_response_callback(boost::bind(PortReqResponse, _1, std::string("Success")));
    req->HandleRequest();
    client->WaitForIdle();
    EXPECT_EQ(1, Agent::GetInstance()->interface_config_table()->Size());
    req->Release();
}

TEST_F(NovaInfoServerTest, IntrospecPortDelete) {
    DeletePortReq *req = new DeletePortReq();
    req->set_port_uuid(std::string("00000000-0000-0000-0000-000000000001"));
    port_resp_done = false;
    Sandesh::set_response_callback(boost::bind(PortReqResponse, _1, std::string("Success")));
    req->HandleRequest();
    client->WaitForIdle();
    EXPECT_EQ(0, Agent::GetInstance()->interface_config_table()->Size());
    req->Release();
}

EventManager *client_evm;
ServerThread *client_thread;

class NovaInfoClientServerTest : public ::testing::Test {
protected:
};

TEST_F(NovaInfoClientServerTest, ConnectionStart) {
    client_evm = new EventManager();
    client_thread = new ServerThread(client_evm);
    boost::shared_ptr<protocol::TProtocolFactory> 
            protocolFactory(new protocol::TBinaryProtocolFactory());
    boost::shared_ptr<async::TAsioClient> nova_client (
        new async::TAsioClient(
            *(client_evm->io_service()),
            protocolFactory,
            protocolFactory));
    client_thread->Start();
    nova_client->connect("localhost", 9090, connected);
    TASK_UTIL_EXPECT_EQ(true, connection_complete);
}

TEST_F(NovaInfoClientServerTest, ConnectTest) {
    ConnectToServer(client_service);
}

TEST_F(NovaInfoClientServerTest, KeepAliveTest) {
    SendKeepAlive(client_service);
}

TEST_F(NovaInfoClientServerTest, PortAdd) {
    // Add and delete port check the status of the cfg intf db
    AddVmPort(client_service, 1, 1, 1);
    CfgIntTable *cfgtable = Agent::GetInstance()->interface_config_table();
    CfgIntKey key(MakeUuid(1));
    TASK_UTIL_EXPECT_EQ(1, Agent::GetInstance()->interface_config_table()->Size());
    CfgIntEntry *cfg_entry;
    TASK_UTIL_EXPECT_NE((CfgIntEntry *)NULL, 
                        (cfg_entry = 
                         static_cast<CfgIntEntry *>(Agent::GetInstance()->interface_config_table()->Find(&key))));
    EXPECT_EQ(cfg_entry->GetVersion(), 1);
}

TEST_F(NovaInfoClientServerTest, PortDelete) {
    CfgIntTable *cfgtable = Agent::GetInstance()->interface_config_table();
    CfgIntKey key(MakeUuid(1));
    TASK_UTIL_EXPECT_EQ(1, Agent::GetInstance()->interface_config_table()->Size());
    CfgIntEntry *cfg_entry;
    TASK_UTIL_EXPECT_NE((CfgIntEntry *)NULL, 
                        (cfg_entry = 
                         static_cast<CfgIntEntry *>(Agent::GetInstance()->interface_config_table()->Find(&key))));
    EXPECT_EQ(cfg_entry->GetVersion(), 1);
    DelVmPort(client_service, 1);
    TASK_UTIL_EXPECT_EQ(0, Agent::GetInstance()->interface_config_table()->Size());
    TASK_UTIL_EXPECT_EQ(base_port_count, Agent::GetInstance()->interface_table()->Size());
}

TEST_F(NovaInfoClientServerTest, ReconnectVersionCheck) {
    AddVmPort(client_service, 1, 1, 1);
    ConnectToServer(client_service);
    CfgIntTable *cfgtable = Agent::GetInstance()->interface_config_table();
    CfgIntKey key(MakeUuid(1));
    TASK_UTIL_EXPECT_EQ(1, Agent::GetInstance()->interface_config_table()->Size());
    CfgIntEntry *cfg_entry;
    TASK_UTIL_EXPECT_NE((CfgIntEntry *)NULL, 
                        (cfg_entry = 
                         static_cast<CfgIntEntry *>(Agent::GetInstance()->interface_config_table()->Find(&key))));
    EXPECT_EQ(cfg_entry->GetVersion(), 1);
    DelVmPort(client_service, 1);
    TASK_UTIL_EXPECT_EQ(0, Agent::GetInstance()->interface_config_table()->Size());
    TASK_UTIL_EXPECT_EQ(base_port_count, Agent::GetInstance()->interface_table()->Size());
}

TEST_F(NovaInfoClientServerTest, MultiPortAddDelete) {
    // Add Multiple ports
    Port port1;
    Port port2;
    Port port3;
    CreatePort(port1, 3, 3, 1);
    CreatePort(port2, 4, 4, 1);
    CreatePort(port3, 5, 5, 1);
    std::vector<Port> pl;
    pl.clear();
    pl.push_back(port1);
    pl.push_back(port2);
    pl.push_back(port3);
    client_service->AddPort(pl).setCallback(
        boost::bind(&AddPortCallback,
                    port3.port_id, port3.tap_name, _1)).setErrback(AddPortErrback);
    TASK_UTIL_EXPECT_EQ(3, Agent::GetInstance()->interface_config_table()->Size());
    // Add again same muliple ports
    pl.clear();
    pl.push_back(port1);
    pl.push_back(port2);
    pl.push_back(port3);
    client_service->AddPort(pl).setCallback(
        boost::bind(&AddPortCallback,
                    port3.port_id, port3.tap_name, _1)).setErrback(AddPortErrback);
    TASK_UTIL_EXPECT_EQ(3, Agent::GetInstance()->interface_config_table()->Size());
    // Delete Multiple ports
    DelVmPort(client_service, 3);
    DelVmPort(client_service, 4);
    DelVmPort(client_service, 5);
    TASK_UTIL_EXPECT_EQ(0, Agent::GetInstance()->interface_config_table()->Size());
    TASK_UTIL_EXPECT_EQ(base_port_count, Agent::GetInstance()->interface_table()->Size());
}

TEST_F(NovaInfoClientServerTest, AddPortWrongIP) {
    // Test negitive inputs
    // Wrong IP address
    Port port;
    std::vector<Port> pl;
    CreatePort(port, 2, 2, 2);
    port.ip_address = "TestIP";
    pl.push_back(port);
    client_service->AddPort(pl).setCallback(
        boost::bind(&AddPortCallback,
                    port.port_id, port.tap_name, _1)).setErrback(AddPortErrback);
    TASK_UTIL_EXPECT_EQ(0, Agent::GetInstance()->interface_config_table()->Size());
    TASK_UTIL_EXPECT_EQ(base_port_count, Agent::GetInstance()->interface_table()->Size());
}

TEST_F(NovaInfoClientServerTest, NullUUIDTest) {
    Port port;
    // Null uuids, we are allowing them
    CreatePort(port, 3, 3, 3);
    tuuid null_tuuid;
    int8_t t_port_id[sizeof(port_id)];
    memcpy(t_port_id, port_id, sizeof(t_port_id));
    null_tuuid.insert(null_tuuid.begin(), t_port_id, 
                      t_port_id + (sizeof(t_port_id)/sizeof(int8_t)));
    port.port_id = null_tuuid;
    port.instance_id = null_tuuid;
    port.vn_id = null_tuuid;
    std::vector<Port> pl;
    pl.clear();
    pl.push_back(port);
    client_service->AddPort(pl).setCallback(
        boost::bind(&AddPortCallback,
                    port.port_id, port.tap_name, _1)).setErrback(AddPortErrback);
    TASK_UTIL_EXPECT_EQ(1, Agent::GetInstance()->interface_config_table()->Size());
    client_service->DeletePort(null_tuuid).setCallback(
        boost::bind(&DeletePortCallback, null_tuuid, _1)).setErrback(DeletePortErrback);
    TASK_UTIL_EXPECT_EQ(0, Agent::GetInstance()->interface_config_table()->Size());
    TASK_UTIL_EXPECT_EQ(base_port_count, Agent::GetInstance()->interface_table()->Size());
}

TEST_F(NovaInfoClientServerTest, WrongUUIDTest1) {
    // Send extra bytes in the uuid, and it shouldn't get added
    Port port;
    CreatePort(port, 4, 4, 4);
    tuuid extra_tuuid;
    int8_t t_extraport_id[sizeof(port_id) + 1];
    memcpy(t_extraport_id, port_id, sizeof(port_id));
    extra_tuuid.insert(extra_tuuid.begin(), t_extraport_id,
                       t_extraport_id + (sizeof(t_extraport_id)/sizeof(int8_t)));
    port.port_id = extra_tuuid;
    port.instance_id = extra_tuuid;
    port.vn_id = extra_tuuid;
    std::vector<Port> pl;
    pl.clear();
    pl.push_back(port);
    client_service->AddPort(pl).setCallback(
        boost::bind(&AddPortCallback,
                    port.port_id, port.tap_name, _1)).setErrback(AddPortErrback);
    TASK_UTIL_EXPECT_EQ(0, Agent::GetInstance()->interface_config_table()->Size());
    TASK_UTIL_EXPECT_EQ(base_port_count, Agent::GetInstance()->interface_table()->Size());
}

TEST_F(NovaInfoClientServerTest, WrongUUIDTest2) {
    // Send less number of bytes in uuids, and it shouldn't get added
    Port port;
    CreatePort(port, 5, 5, 5);
    tuuid small_tuuid;
    int8_t s_port_id[sizeof(port_id)];
    memcpy(s_port_id, port_id, sizeof(port_id));
    small_tuuid.insert(small_tuuid.begin(), s_port_id,
                       s_port_id + (sizeof(s_port_id)/sizeof(int8_t) - 1));
    port.port_id = small_tuuid;
    port.instance_id = small_tuuid;
    tuuid vn_tuuid;
    int8_t t_vn_id[sizeof(vn_id)];
    memcpy(t_vn_id, vn_id, sizeof(t_vn_id));
    vn_tuuid.insert(vn_tuuid.begin(), t_vn_id, 
                    t_vn_id + (sizeof(t_vn_id)/sizeof(int8_t)));
    port.vn_id = vn_tuuid;
    std::vector<Port> pl;
    pl.clear();
    pl.push_back(port);
    client_service->AddPort(pl).setCallback(
        boost::bind(&AddPortCallback,
                    port.port_id, port.tap_name, _1)).setErrback(AddPortErrback);
    TASK_UTIL_EXPECT_EQ(0, Agent::GetInstance()->interface_config_table()->Size());
    TASK_UTIL_EXPECT_EQ(base_port_count, Agent::GetInstance()->interface_table()->Size());
}

TEST_F(NovaInfoClientServerTest, ConnectionDelete) {
    client_evm->Shutdown();
    client_thread->Join();
    delete client_evm;
    delete client_thread;
}

int main (int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    EventManager *evm = Agent::GetInstance()->event_manager();
    InstanceInfoServiceServerInit(Agent::GetInstance());
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
