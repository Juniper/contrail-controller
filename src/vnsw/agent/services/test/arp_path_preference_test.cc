/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"

#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <base/logging.h>

#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <oper/vrf.h>
#include <oper/mirror_table.h>
#include <pugixml/pugixml.hpp>
#include <services/arp_proto.h>
#include <vr_interface.h>
#include <test/test_cmn_util.h>
#include <test/pkt_gen.h>
#include "vr_types.h"
#include "xmpp/test/xmpp_test_util.h"
#include <services/services_sandesh.h>
#include "oper/path_preference.h"

struct PortInfo input1[] = {
    {"vnet1", 1, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
};

struct PortInfo input2[] = {
    {"vnet2", 2, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
};

IpamInfo ipam_info[] = {
    {"8.1.1.0", 24, "8.1.1.10", true},
};

class ArpPathPreferenceTest : public ::testing::Test {
public:
    ArpPathPreferenceTest(): agent(Agent::GetInstance()) {}
    virtual void SetUp() {
        CreateVmportEnv(input1, 1);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DeleteVmportEnv(input1, 1, true);
        client->WaitForIdle();
        DelIPAM("vn1");
        EXPECT_TRUE(VrfGet("vrf1", true) == NULL);
    }

    ArpVrfState* GetVrfState(uint32_t vrf_id) {
        const VrfEntry *vrf = agent->vrf_table()->FindVrfFromId(vrf_id);
        return static_cast<ArpVrfState *>(vrf->GetState(agent->vrf_table(),
                                          agent->GetArpProto()->
                                          vrf_table_listener_id()));
    }

    bool CheckIpMap(uint32_t vrf_id, uint32_t id, const IpAddress &ip) {
        ArpVrfState *state = GetVrfState(vrf_id);
        return state->Get(ip)->IntfPresentInIpMap(id);
    }

    bool CheckEvpnMap(uint32_t vrf_id, uint32_t id, const IpAddress &ip) {
        ArpVrfState *state = GetVrfState(vrf_id);
        return state->Get(ip)->IntfPresentInEvpnMap(id);
    }

    uint32_t GetIpMapRetryCount(uint32_t vrf_id, uint32_t id, const IpAddress &ip) {
        ArpVrfState *state = GetVrfState(vrf_id);
        return state->Get(ip)->IntfRetryCountInIpMap(id);
    }

    uint32_t GetEvpnMapRetryCount(uint32_t vrf_id, uint32_t id, const IpAddress &ip) {
        ArpVrfState *state = GetVrfState(vrf_id);
        return state->Get(ip)->IntfRetryCountInEvpnMap(id);
    }
protected:
    Agent *agent;
};

//Check EVPN route and IP route list has
//interface ID
TEST_F(ArpPathPreferenceTest, Test1) {
    uint32_t vrf_id = VrfGet("vrf1")->vrf_id();
    uint32_t intf_id = VmPortGetId(1);
    const IpAddress ip = Ip4Address::from_string("8.1.1.1");
    EXPECT_TRUE(CheckIpMap(vrf_id, intf_id, ip));
    EXPECT_TRUE(CheckEvpnMap(vrf_id, intf_id, ip));

    EXPECT_TRUE(GetIpMapRetryCount(vrf_id, intf_id, ip) >= 1);
    EXPECT_TRUE(GetEvpnMapRetryCount(vrf_id, intf_id, ip) >= 1);
}

//Enqueue Traffic seen for IP route, verify
//EVPN route still has interface ID in map
TEST_F(ArpPathPreferenceTest, Test2) {
    CreateVmportEnv(input2, 1);
    client->WaitForIdle();

    uint32_t vrf_id = VrfGet("vrf1")->vrf_id();
    uint32_t intf_id = VmPortGetId(1);
    uint32_t intf_id1 = VmPortGetId(2);
    const IpAddress ip = Ip4Address::from_string("8.1.1.1");

    EXPECT_TRUE(CheckIpMap(vrf_id, intf_id, ip));
    EXPECT_TRUE(CheckEvpnMap(vrf_id, intf_id, ip));
    EXPECT_TRUE(GetIpMapRetryCount(vrf_id, intf_id, ip) >= 2);
    EXPECT_TRUE(GetEvpnMapRetryCount(vrf_id, intf_id, ip) >= 2);

    EXPECT_TRUE(CheckIpMap(vrf_id, intf_id1, ip));
    EXPECT_TRUE(CheckEvpnMap(vrf_id, intf_id1, ip));
    EXPECT_TRUE(GetIpMapRetryCount(vrf_id, intf_id1, ip) >= 1);
    EXPECT_TRUE(GetEvpnMapRetryCount(vrf_id, intf_id1, ip) >= 1);

    DeleteVmportEnv(input2, 1, false);
    client->WaitForIdle();
}

TEST_F(ArpPathPreferenceTest, Test3) {
    uint32_t vrf_id = VrfGet("vrf1")->vrf_id();
    uint32_t intf_id = VmPortGetId(1);
    const IpAddress ip = Ip4Address::from_string("8.1.1.1");
    EXPECT_TRUE(CheckIpMap(vrf_id, intf_id, ip));
    EXPECT_TRUE(CheckEvpnMap(vrf_id, intf_id, ip));

    EXPECT_TRUE(GetIpMapRetryCount(vrf_id, intf_id, ip) >= 1);
    EXPECT_TRUE(GetEvpnMapRetryCount(vrf_id, intf_id, ip) >= 1);

    agent->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 32, intf_id, vrf_id,
                           MacAddress::ZeroMac());
    client->WaitForIdle();

    EXPECT_TRUE(CheckIpMap(vrf_id, intf_id, ip));
    EXPECT_TRUE(CheckEvpnMap(vrf_id, intf_id, ip));
    EXPECT_TRUE(GetEvpnMapRetryCount(vrf_id, intf_id, ip) >= 1);
}

//Verify dependent route doesnt get added in
//path preference map
TEST_F(ArpPathPreferenceTest, Test4) {
    uint32_t vrf_id = VrfGet("vrf1")->vrf_id();
    uint32_t intf_id = VmPortGetId(1);
    struct TestIp4Prefix static_route[] = {
        { Ip4Address::from_string("24.1.1.1"), 32},
    };
    AddInterfaceRouteTable("static_route", 1, static_route, 1);
    AddLink("virtual-machine-interface", "vnet1",
            "interface-route-table", "static_route");
    client->WaitForIdle();

    const IpAddress ip = Ip4Address::from_string("24.1.1.1");

    EXPECT_FALSE(CheckIpMap(vrf_id, intf_id, ip));
    EXPECT_FALSE(CheckEvpnMap(vrf_id, intf_id, ip));
    client->WaitForIdle();

    DelLink("virtual-machine-interface", "vnet1",
            "interface-route-table", "static_route");
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, true);
    usleep(100000);
    client->WaitForIdle();

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
