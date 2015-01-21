/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
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

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "vr_types.h"

#include <io/event_manager.h>
#include <db/db.h>
#include <cmn/agent_cmn.h>
#include <cfg/cfg_interface.h>
#include <port_ipc/port_ipc_types.h>

using namespace std;

void RouterIdDepInit(Agent *agent) {
}

class PortIpcTest : public ::testing::Test {
protected:
};


bool port_resp_done  = false;

static void PortReqResponse(Sandesh *sandesh, string &resp_str) {
    PortResp *resp = dynamic_cast<PortResp *>(sandesh);
    std::string s_resp_str = resp->get_resp();
    EXPECT_EQ(resp_str, s_resp_str);
    port_resp_done = true;
}

TEST_F(PortIpcTest, IntrospecPortAdd) {
    AddPortReq *req = new AddPortReq();
    req->set_port_uuid(std::string("00000000-0000-0000-0000-000000000001"));
    req->set_instance_uuid(std::string("00000000-0000-0000-0000-000000000001"));
    req->set_tap_name(std::string("tap1"));
    req->set_ip_address(std::string("1.1.1.1"));
    req->set_vn_uuid(std::string("00000000-0000-0000-0000-000000000001"));
    req->set_mac_address(std::string("00:00:00:00:00:1"));
    req->set_vm_name(std::string("vm1"));
    req->set_rx_vlan_id(100);
    req->set_tx_vlan_id(100);
    req->set_vm_project_uuid("00000000-0000-0000-0000-000000000001");
    port_resp_done = false;
    Sandesh::set_response_callback(boost::bind(PortReqResponse, _1,
        std::string("Success")));
    req->HandleRequest();
    client->WaitForIdle();
    EXPECT_EQ(1, Agent::GetInstance()->interface_config_table()->Size());
    req->Release();
}

TEST_F(PortIpcTest, IntrospecPortDelete) {
    DeletePortReq *req = new DeletePortReq();
    req->set_port_uuid(std::string("00000000-0000-0000-0000-000000000001"));
    port_resp_done = false;
    Sandesh::set_response_callback(boost::bind(PortReqResponse, _1,
        std::string("Success")));
    req->HandleRequest();
    client->WaitForIdle();
    EXPECT_EQ(0, Agent::GetInstance()->interface_config_table()->Size());
    req->Release();
}

TEST_F(PortIpcTest, IntrospectErrorPortDelete) {
    DeletePortReq *req = new DeletePortReq();
    req->set_port_uuid(std::string("wrong-uuid"));
    port_resp_done = false;
    Sandesh::set_response_callback(boost::bind(PortReqResponse, _1,
        std::string("Port uuid is not correct.")));
    req->HandleRequest();
    client->WaitForIdle();
    EXPECT_EQ(0, Agent::GetInstance()->interface_config_table()->Size());
    req->Release();
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
