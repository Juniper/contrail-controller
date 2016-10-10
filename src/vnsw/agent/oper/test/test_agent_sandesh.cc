/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <sys/socket.h>

#include <net/if.h>

#ifdef __linux__
#include <linux/netlink.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>
#endif

#ifdef __FreeBSD__
#include <sys/sockio.h>
#include <ifaddrs.h>
#endif

#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "oper/agent_sandesh.h"
#include "filter/acl.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h>
#include <ksync/ksync_sock_user.h>
#include <boost/assign/list_of.hpp>

using namespace boost::assign;

class AgentSandeshTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        table_ = agent_->interface_table();
    }

    virtual void TearDown() {
    }

    static bool AddInterface(uint32_t max_count);
    static bool DelInterface(uint32_t max_count);

    Agent *agent_;
    DBTable *table_;
};

static void ValidateSandeshResponse(Sandesh *sandesh, uint32_t expect_count,
                                    bool expect_error) {

    if (expect_error) {
        ErrorResp *resp = dynamic_cast<ErrorResp *>(sandesh);
        EXPECT_TRUE(resp != NULL);
        return;
    }

    ItfResp *resp = dynamic_cast<ItfResp *>(sandesh);
    if (resp == NULL)
        return;

    const std::vector<ItfSandeshData> itf_list = resp->get_itf_list();
    EXPECT_EQ(itf_list.size(), expect_count);
}

void DoInterfaceSandesh(int start, int end, int expect_count) {
    ItfReq *itf_req = new ItfReq();
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1,
                                               expect_count, false));
    itf_req->HandleRequest();
    client->WaitForIdle();
    itf_req->Release();
    client->WaitForIdle();
}

string MakePageRequest(const string &name, int start, int end) {
    AgentSandeshArguments args;
    args.Add("table", name);
    args.Add("begin", start);
    args.Add("end", end);
    string s;
    args.Encode(&s);
    return s;
}

void DoPageRequest(const string &req_str, int expect_count, bool expect_err) {
    PageReq *req = new PageReq();
    req->set_key(req_str);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1,
                                               expect_count, expect_err));

    req->HandleRequest();
    client->WaitForIdle();
    req->Release();
    client->WaitForIdle();
}

void DoPageRequest(const string &name, int start, int end, int expect_count) {
    string s = MakePageRequest(name, start, end);
    DoPageRequest(s, expect_count, false);
}

void MakePortInfo(struct PortInfo *info, uint32_t id) {
    sprintf(info->name, "vnet-%d", id);
    info->intf_id = id;
    sprintf(info->addr, "1.1.1.%d", id);
    sprintf(info->mac, "00:00:00:00:00:%d", id);
    info->vn_id = id;
    info->vm_id = id;
}

bool AgentSandeshTest::AddInterface(uint32_t max_count) {
    struct PortInfo info[] = {
        {"vnet", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    uint32_t count = 0;
    for (count = 1; count <= max_count; count++) {
        MakePortInfo(info, count);
        AddPort(info[0].name, info[0].intf_id);
        CreateVmportEnv(info, 1);
        client->WaitForIdle();
    }

    for (count = 1; count <= max_count; count++) {
        WAIT_FOR(1000, 1000, (VmPortActive(count) == true));
    }
    return true;
}

bool AgentSandeshTest::DelInterface(uint32_t max_count) {
    uint32_t count = 0;
    struct PortInfo info[] = {
        {"vnet", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    for (count = 1; count <= max_count; count++) {
        MakePortInfo(info, count);
        DeleteVmportEnv(info, 1, true);
        client->WaitForIdle();
    }

    for (count = 1; count <= max_count; count++) {
        WAIT_FOR(1000, 1000, (VmPortFind(count) == false));
    }
    return true;
}

// Start=-1, end=-1. Should give full table
TEST_F(AgentSandeshTest, test_all) {
    DoInterfaceSandesh(-1, -1, table_->Size());
}

// Start=1, end=10. Should give 10 entries
TEST_F(AgentSandeshTest, test_1_10) {
    DoPageRequest(table_->name(), 1, 10, 10);
}

// Start=0, end=100. Should give full table
TEST_F(AgentSandeshTest, test_0_100) {
    DoPageRequest(table_->name(), 0, 100, table_->Size());
}

// Start=200, end=300. Range beyond the size of DBTable. Must give first page
TEST_F(AgentSandeshTest, test_200_300) {
    DoPageRequest(table_->name(), 200, 300, table_->Size());
}

// Invalid request. There is no validator for this
TEST_F(AgentSandeshTest, invalid_request) {
    DoPageRequest("invalid.table", table_->Size(), true);
}

// Invalid table-name. There is no validator for this
TEST_F(AgentSandeshTest, invalid_table) {
    AgentSandeshArguments args;
    args.Add("table", table_->name());
    string s;
    args.Encode(&s);
    DoPageRequest(s, table_->Size(), true);
}

// Invalid 'begin' arg. There is no validator for this
TEST_F(AgentSandeshTest, invalid_begin) {
    AgentSandeshArguments args;
    args.Add("table", table_->name());
    args.Add("end", 10);
    string s;
    args.Encode(&s);
    DoPageRequest(s, table_->Size(), true);
}

// Invalid 'end' arg. There is no validator for this
TEST_F(AgentSandeshTest, invalid_end) {
    AgentSandeshArguments args;
    args.Add("table", table_->name());
    args.Add("begin", 10);
    string s;
    args.Encode(&s);
    DoPageRequest(s, table_->Size(), true);
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    AgentSandeshTest::AddInterface(64);
    int ret = RUN_ALL_TESTS();
    AgentSandeshTest::DelInterface(64);
    TestShutdown();
    delete client;
    return ret;
}
