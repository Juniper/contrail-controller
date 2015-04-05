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
#include "cfg/cfg_interface.h"
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
#include "filter/acl.h"
#include "openstack/instance_service_server.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h>
#include <ksync/ksync_sock_user.h>
#include <boost/assign/list_of.hpp>

using namespace boost::assign;

#define NULL_VRF ""
#define ZERO_IP "0.0.0.0"

void RouterIdDepInit(Agent *agent) {
}

class IntfTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent = Agent::GetInstance();
        agent->set_tor_agent_enabled(true);
        intf_count = agent->interface_table()->Size();
        client->WaitForIdle();
    }

    virtual void TearDown() {
        client->WaitForIdle();
        WAIT_FOR(100, 1000, (agent->interface_table()->Size() == intf_count));
        WAIT_FOR(100, 1000, (agent->vrf_table()->Size() == 1U));
        WAIT_FOR(100, 1000, (agent->vm_table()->Size() == 0U));
        WAIT_FOR(100, 1000, (agent->vn_table()->Size() == 0U));
    }

    unsigned int intf_count;
    Agent *agent;
};

void AddVmi(int name_id, int id) {
    char buff[128];
    sprintf(buff, "vmi%d", name_id);
    AddNode("virtual-machine-interface", buff, id);
}

void DelVmi(int id) {
    char buff[128];
    sprintf(buff, "vmi%d", id);
    DelNode("virtual-machine-interface", buff);
}

void AddLink(int lif_id, int vmi_id) {
    char vmi_name[128];
    sprintf(vmi_name, "vmi%d", vmi_id);
    char lif_name[128];
    sprintf(lif_name, "li%d", lif_id);
    AddLink("virtual-machine-interface", vmi_name, "logical-interface",
            lif_name);
}

void DelLink(int lif_id, int vmi_id) {
    char vmi_name[128];
    sprintf(vmi_name, "vmi%d", vmi_id);
    char lif_name[128];
    sprintf(lif_name, "li%d", lif_id);
    DelLink("virtual-machine-interface", vmi_name, "logical-interface",
            lif_name);
}

TEST_F(IntfTest, basic_1) {
    AddPhysicalInterface("pi1", 1, "pi1");
    AddLogicalInterface("li1", 0, "li1", 100);
    AddLink("physical-interface", "pi1", "logical-interface", "li1");

    for (int i = 1; i < 64; i++) {
        AddVmi(i, i);
    }
    client->WaitForIdle();

    for (int i = 4; i < 64; i++) {
        AddLink(1, i);
    }
    client->WaitForIdle();

    AddLogicalInterface("li2", 2, "li2", 200);
    client->WaitForIdle();

    AddLink("physical-interface", "pi1", "logical-interface", "li2");
    AddLogicalInterface("li1", 1, "li1", 100);
    AddLink(1, 1);
    AddLink(1, 2);
    client->WaitForIdle();

    WAIT_FOR(1000, 100, (LogicalInterfaceGet(1, "li1") != NULL));
    client->WaitForIdle();
    LogicalInterface *li = LogicalInterfaceGet(1, "li1");

    WAIT_FOR(1000, 100, (li->vm_interface() != NULL));
    client->WaitForIdle();

    for (int i = 1; i < 64; i++) {
        DelLink(1, i);
    }
    DelLink("physical-interface", "pi1", "logical-interface", "li1");
    DelLink("physical-interface", "pi1", "logical-interface", "li2");

    for (int i = 1; i < 64; i++) {
        DelVmi(i);
    }
    DeleteLogicalInterface("li1");
    DeleteLogicalInterface("li2");
    DeletePhysicalInterface("pi1");
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, false, false, false);

    int ret = RUN_ALL_TESTS();

    usleep(10000);
    client->WaitForIdle();
    usleep(10000);
    TestShutdown();
    delete client;

    return ret;
}
