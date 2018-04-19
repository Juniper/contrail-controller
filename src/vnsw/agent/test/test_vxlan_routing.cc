/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
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
#include "oper/physical_device_vn.h"
#include "filter/acl.h"
#include "test_cmn_util.h"
#include "vr_types.h"

#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"
#include "control_node_mock.h"
#include "xml/xml_pugi.h"
#include "oper/vxlan_routing_manager.h"

using namespace pugi;
#define L3_VRF_OFFSET 100

struct PortInfo input1[] = {
    {"vnet10", 10, "1.1.1.10", "00:00:01:01:01:10", 1, 10},
    {"vnet11", 11, "1.1.1.11", "00:00:01:01:01:11", 1, 11},
};

struct PortInfo input2[] = {
    {"vnet20", 20, "2.2.2.20", "00:00:02:02:02:20", 2, 20},
};

IpamInfo ipam_1[] = {
    {"1.1.1.0", 24, "1.1.1.200", true},
};

IpamInfo ipam_2[] = {
    {"2.2.2.0", 24, "2.2.2.200", true},
};

void RouterIdDepInit(Agent *agent) {
}

class VxlanRoutingTest : public ::testing::Test {
protected:
    VxlanRoutingTest() {
    }

    virtual void SetUp() {
        client->Reset();
        agent_ = Agent::GetInstance();
        bgp_peer_ = NULL;
    }

    virtual void TearDown() {
    }

    void SetupEnvironment(bool vxlan_routing_enabled) {
        bgp_peer_ = CreateBgpPeer("127.0.0.1", "remote");
        client->WaitForIdle();
        AddIPAM("vn1", ipam_1, 1);
        AddIPAM("vn2", ipam_2, 1);
        std::stringstream vxlan_routing_enabled_str;
        if (vxlan_routing_enabled) {
            vxlan_routing_enabled_str << "<vxlan-routing>true</vxlan-routing>";
        } else {
            vxlan_routing_enabled_str << "<vxlan-routing>false</vxlan-routing>";
        }
        AddNode("project", "project-admin", 1,
                vxlan_routing_enabled_str.str().c_str());
        client->WaitForIdle();
        CreateVmportEnv(input1, 2);
        CreateVmportEnv(input2, 1);
        AddLrVmiPort("lr-vmi-vn1", 91, "1.1.1.99", "vrf1", "vn1",
                    "instance_ip_1", 1);
        AddLrVmiPort("lr-vmi-vn2", 92, "2.2.2.99", "vrf2", "vn2",
                    "instance_ip_2", 2);
        client->WaitForIdle(5);
    }

    void DeleteEnvironment(bool vxlan_enabled) {
        DelIPAM("vn1");
        DelIPAM("vn2");
        DelNode("project", "admin");
        DeleteVmportEnv(input1, 2, true);
        DeleteVmportEnv(input2, 1, true);
        DelLrVmiPort("lr-vmi-vn1", 91, "1.1.1.99", "vrf1", "vn1",
                    "instance_ip_1", 1);
        DelLrVmiPort("lr-vmi-vn2", 92, "2.2.2.99", "vrf2", "vn2",
                    "instance_ip_2", 2);
        DeleteBgpPeer(bgp_peer_);
        client->WaitForIdle(5);
        EXPECT_TRUE(VrfGet("vrf1") == NULL);
        EXPECT_TRUE(VrfGet("vrf2") == NULL);
        EXPECT_TRUE(agent_->oper_db()->vxlan_routing_manager()->vrf_mapper().
                    IsEmpty());
        EXPECT_TRUE(agent_->oper_db()->vxlan_routing_manager()->vrf_mapper().
                    IsEmpty());
    }

    void AddRoutingVrf(int lr_id) {
        std::stringstream name_ss;
        name_ss << "l3evpn_" << lr_id;
        AddVrf(name_ss.str().c_str(), (L3_VRF_OFFSET + lr_id));
        AddVn(name_ss.str().c_str(), (L3_VRF_OFFSET + lr_id));
        AddNode("logical-router", name_ss.str().c_str(), lr_id);
        std::stringstream node_str;
        node_str << "<logical-router-virtual-network-type>"
            << "InternalVirtualNetwork"
            << "</logical-router-virtual-network-type>";
        AddLinkNode("logical-router-virtual-network",
                    name_ss.str().c_str(),
                    node_str.str().c_str());
        AddLink("logical-router-virtual-network",
                name_ss.str().c_str(),
                "logical-router",
                name_ss.str().c_str(),
                "logical-router-virtual-network");
        AddLink("logical-router-virtual-network",
                name_ss.str().c_str(),
                "virtual-network",
                name_ss.str().c_str(),
                "logical-router-virtual-network");
        AddLink("virtual-network",
                name_ss.str().c_str(),
                "routing-instance",
                name_ss.str().c_str());
        client->WaitForIdle();
    }

    void DelRoutingVrf(int lr_id) {
        std::stringstream name_ss;
        name_ss << "l3evpn_" << lr_id;
        DelLink("logical-router-virtual-network",
                name_ss.str().c_str(),
                "logical-router",
                name_ss.str().c_str(),
                "logical-router-virtual-network");
        DelLink("logical-router-virtual-network",
                name_ss.str().c_str(),
                "virtual-network",
                name_ss.str().c_str(),
                "logical-router-virtual-network");
        DelLink("virtual-network",
                name_ss.str().c_str(),
                "routing-instance",
                name_ss.str().c_str());
        DelVrf(name_ss.str().c_str());
        DelVn(name_ss.str().c_str());
        DelNode("logical-router", name_ss.str().c_str());
        DelNode("logical-router-virtual-network",
                    name_ss.str().c_str());
        client->WaitForIdle();
        EXPECT_TRUE(VrfGet(name_ss.str().c_str()) == NULL);
    }

    void AddBridgeVrf(const std::string &vmi_name,
                      int lr_id) {
        std::stringstream name_ss;
        std::string metadata = "logical-router-interface";
        name_ss << "l3evpn_" << lr_id;
        AddNode("logical-router", name_ss.str().c_str(), lr_id);
        std::stringstream lr_vmi_name;
        lr_vmi_name << "lr-vmi-" << vmi_name;
        AddLink("logical-router",
                name_ss.str().c_str(),
                "virtual-machine-interface",
                lr_vmi_name.str().c_str(),
                "logical-router-interface");
        client->WaitForIdle();
    }

    void DelBridgeVrf(const std::string &vmi_name,
                      int lr_id) {
        std::stringstream name_ss;
        std::string metadata = "logical-router-interface";
        name_ss << "l3evpn_" << lr_id;
        DelNode("logical-router", name_ss.str().c_str());
        std::stringstream lr_vmi_name;
        lr_vmi_name << "lr-vmi-" << vmi_name;
        DelLink("logical-router",
                name_ss.str().c_str(),
                "virtual-machine-interface",
                lr_vmi_name.str().c_str(),
                "logical-router-interface");
        client->WaitForIdle();
    }

    void ValidateBridge(const std::string &bridge_vrf,
                        const std::string &routing_vrf,
                        const Ip4Address &addr,
                        uint8_t plen,
                        bool participate) {
        InetUnicastRouteEntry *rt =
            RouteGet(bridge_vrf, addr, plen);
        if (participate) {
            EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() ==
                        Peer::EVPN_ROUTING_PEER);
            const VrfNH *nh = dynamic_cast<const VrfNH *>
                (rt->GetActiveNextHop());
            EXPECT_TRUE(nh->GetVrf()->GetName() == routing_vrf);
        } else {
            if (rt == NULL)
                return;

            EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() !=
                        Peer::EVPN_ROUTING_PEER);
            const VrfNH *nh = dynamic_cast<const VrfNH *>
                (rt->GetActiveNextHop());
            EXPECT_TRUE(nh == NULL);
        }
    }

    void ValidateBridgeRemote(const std::string &bridge_vrf,
                              const std::string &routing_vrf,
                              const Ip4Address &addr,
                              uint8_t plen,
                              bool participate) {
        InetUnicastRouteEntry *rt =
            RouteGet(bridge_vrf, addr, plen);
        if (participate) {
            EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() ==
                        Peer::BGP_PEER);
            const VrfNH *nh = dynamic_cast<const VrfNH *>
                (rt->GetActiveNextHop());
            EXPECT_TRUE(nh->GetVrf()->GetName() == routing_vrf);
        } else {
            if (rt == NULL)
                return;

            EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() !=
                        Peer::BGP_PEER);
            const VrfNH *nh = dynamic_cast<const VrfNH *>
                (rt->GetActiveNextHop());
            EXPECT_TRUE(nh == NULL);
        }
    }

    void ValidateRouting(const std::string &routing_vrf,
                         const Ip4Address &addr,
                         uint8_t plen,
                         const std::string &dest_name,
                         bool present) {
        InetUnicastRouteEntry *rt =
            RouteGet(routing_vrf, addr, plen);
        if (present) {
            EXPECT_TRUE(rt != NULL);
            const InterfaceNH *intf_nh =
                dynamic_cast<const InterfaceNH *>(rt->GetActiveNextHop());
            if (intf_nh) {
                EXPECT_TRUE(intf_nh->GetInterface()->name() == dest_name);
                EXPECT_TRUE(intf_nh->IsVxlanRouting());
            }
            const TunnelNH *tunnel_nh =
                dynamic_cast<const TunnelNH *>(rt->GetActiveNextHop());
            if (tunnel_nh) {
                EXPECT_TRUE(tunnel_nh->GetDip()->to_string() == dest_name);
                EXPECT_TRUE(tunnel_nh->rewrite_dmac().IsZero() == false);
            }
        } else {
            EXPECT_TRUE(rt == NULL);
        }
    }

    BgpPeer *bgp_peer_;
    Agent *agent_;
};

TEST_F(VxlanRoutingTest, Basic) {
    SetupEnvironment(true);
    AddRoutingVrf(1);
    EXPECT_TRUE(VmInterfaceGet(10)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(11)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(20)->logical_router_uuid() == nil_uuid());
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.10"), 32,
                    "vnet10", false);
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.11"), 32,
                    "vnet11", false);
    ValidateRouting("l3evpn_1", Ip4Address::from_string("2.2.2.20"), 32,
                    "vnet20", false);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("0.0.0.0"), 0, false);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("1.1.1.10"), 32, false);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("1.1.1.11"), 32, false);
    ValidateBridge("vrf2", "l3evpn_1",
                   Ip4Address::from_string("2.2.2.20"), 32, false);
    DelRoutingVrf(1);
    DeleteEnvironment(true);
}

TEST_F(VxlanRoutingTest, Route_1) {
    SetupEnvironment(true);
    AddRoutingVrf(1);
    AddBridgeVrf("vn1", 1);
    EXPECT_TRUE(VmInterfaceGet(10)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(11)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(20)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(91)->logical_router_uuid() != nil_uuid());
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.10"), 32,
                    "vnet10", true);
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.11"), 32,
                    "vnet11", true);
    ValidateRouting("l3evpn_1", Ip4Address::from_string("2.2.2.20"), 32,
                    "vnet20", false);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("0.0.0.0"), 0, true);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("1.1.1.10"), 32, true);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("1.1.1.11"), 32, true);
    ValidateBridge("vrf2", "l3evpn_1",
                   Ip4Address::from_string("2.2.2.20"), 32, false);
    DelBridgeVrf("vn1", 1);
    DelRoutingVrf(1);
    DeleteEnvironment(true);
}

TEST_F(VxlanRoutingTest, Route_2) {
    SetupEnvironment(true);
    AddRoutingVrf(1);
    AddBridgeVrf("vn1", 1);
    EXPECT_TRUE(VmInterfaceGet(10)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(11)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(20)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(91)->logical_router_uuid() != nil_uuid());
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.10"), 32,
                    "vnet10", true);
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.11"), 32,
                    "vnet11", true);
    ValidateRouting("l3evpn_1", Ip4Address::from_string("2.2.2.20"), 32,
                    "vnet20", false);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("0.0.0.0"), 0, true);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("1.1.1.10"), 32, true);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("1.1.1.11"), 32, true);
    ValidateBridge("vrf2", "l3evpn_1",
                   Ip4Address::from_string("2.2.2.20"), 32, false);
    DelBridgeVrf("vn1", 1);
    DelRoutingVrf(1);
    DeleteEnvironment(true);
}

TEST_F(VxlanRoutingTest, Route_3) {
    SetupEnvironment(true);
    AddRoutingVrf(1);
    AddBridgeVrf("vn1", 1);
    AddBridgeVrf("vn2", 1);
    EXPECT_TRUE(VmInterfaceGet(10)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(11)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(20)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(91)->logical_router_uuid() != nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(92)->logical_router_uuid() != nil_uuid());
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.10"), 32,
                    "vnet10", true);
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.11"), 32,
                    "vnet11", true);
    ValidateRouting("l3evpn_1", Ip4Address::from_string("2.2.2.20"), 32,
                    "vnet20", true);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("0.0.0.0"), 0, true);
    ValidateBridge("vrf2", "l3evpn_1",
                   Ip4Address::from_string("0.0.0.0"), 0, true);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("1.1.1.10"), 32, true);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("1.1.1.11"), 32, true);
    ValidateBridge("vrf2", "l3evpn_1",
                   Ip4Address::from_string("2.2.2.20"), 32, true);
    DelBridgeVrf("vn1", 1);
    DelBridgeVrf("vn2", 1);
    DelRoutingVrf(1);
    DeleteEnvironment(true);
}

TEST_F(VxlanRoutingTest, Route_4) {
    SetupEnvironment(true);
    AddRoutingVrf(1);
    AddRoutingVrf(2);
    AddBridgeVrf("vn1", 2);
    AddBridgeVrf("vn2", 1);
    EXPECT_TRUE(VmInterfaceGet(10)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(11)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(20)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(91)->logical_router_uuid() != nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(92)->logical_router_uuid() != nil_uuid());
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.10"), 32,
                    "vnet10", false);
    ValidateRouting("l3evpn_2", Ip4Address::from_string("1.1.1.10"), 32,
                    "vnet10", true);
    ValidateRouting("l3evpn_2", Ip4Address::from_string("1.1.1.11"), 32,
                    "vnet11", true);
    ValidateRouting("l3evpn_1", Ip4Address::from_string("2.2.2.20"), 32,
                    "vnet20", true);
    ValidateBridge("vrf1", "l3evpn_2",
                   Ip4Address::from_string("0.0.0.0"), 0, true);
    ValidateBridge("vrf2", "l3evpn_1",
                   Ip4Address::from_string("0.0.0.0"), 0, true);
    ValidateBridge("vrf1", "l3evpn_2",
                   Ip4Address::from_string("1.1.1.10"), 32, true);
    ValidateBridge("vrf1", "l3evpn_2",
                   Ip4Address::from_string("1.1.1.11"), 32, true);
    ValidateBridge("vrf2", "l3evpn_1",
                   Ip4Address::from_string("2.2.2.20"), 32, true);
    DelBridgeVrf("vn1", 2);
    DelBridgeVrf("vn2", 1);
    DelRoutingVrf(1);
    DelRoutingVrf(2);
    DeleteEnvironment(true);
}

TEST_F(VxlanRoutingTest, Route_5) {
    SetupEnvironment(true);
    AddRoutingVrf(1);
    AddBridgeVrf("vn1", 1);
    EXPECT_TRUE(VmInterfaceGet(10)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(11)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(20)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(91)->logical_router_uuid() != nil_uuid());
    MacAddress dummy_mac;
    BridgeTunnelRouteAdd(bgp_peer_, "l3evpn_1", TunnelType::VxlanType(),
                         Ip4Address::from_string("100.1.1.11"),
                         101, dummy_mac,
                         Ip4Address::from_string("1.1.1.20"),
                         32, "00:00:99:99:99:99");
    client->WaitForIdle();
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.10"), 32,
                    "vnet10", true);
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.11"), 32,
                    "vnet11", true);
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.20"), 32,
                    "100.1.1.11", true);
    ValidateRouting("l3evpn_1", Ip4Address::from_string("2.2.2.20"), 32,
                    "vnet20", false);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("0.0.0.0"), 0, true);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("1.1.1.10"), 32, true);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("1.1.1.11"), 32, true);
    ValidateBridge("vrf2", "l3evpn_1",
                   Ip4Address::from_string("2.2.2.20"), 32, false);
    EvpnAgentRouteTable::DeleteReq(bgp_peer_, "l3evpn_1",
                                   MacAddress(),
                                   Ip4Address::from_string("1.1.1.20"), 0, NULL);
    DelBridgeVrf("vn1", 1);
    DelRoutingVrf(1);
    DeleteEnvironment(true);
}

TEST_F(VxlanRoutingTest, Route_6) {
    SetupEnvironment(true);
    AddRoutingVrf(1);
    AddRoutingVrf(2);
    AddBridgeVrf("vn1", 1);
    EXPECT_TRUE(VmInterfaceGet(10)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(11)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(20)->logical_router_uuid() == nil_uuid());
    EXPECT_TRUE(VmInterfaceGet(91)->logical_router_uuid() != nil_uuid());
#if 0
    InetUnicastRouteEntry *rt1 =
        RouteGet("l3evpn_1", Ip4Address::from_string("1.1.1.10"), 32);
    InetUnicastRouteEntry *rt2 =
        RouteGet("l3evpn_1", Ip4Address::from_string("1.1.1.11"), 32);
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.10"), 32,
                    "vnet10", (rt2 == NULL));
    ValidateRouting("l3evpn_1", Ip4Address::from_string("1.1.1.11"), 32,
                    "vnet11", (rt1 == NULL));
    ValidateRouting("l3evpn_1", Ip4Address::from_string("2.2.2.20"), 32,
                    "vnet20", false);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("0.0.0.0"), 0, true);
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("1.1.1.10"), 32, (rt2 == NULL));
    ValidateBridge("vrf1", "l3evpn_1",
                   Ip4Address::from_string("1.1.1.11"), 32, (rt1 == NULL));
    ValidateBridge("vrf2", "l3evpn_1",
                   Ip4Address::from_string("2.2.2.20"), 32, false);
#endif
    DelBridgeVrf("vn1", 1);
    DelRoutingVrf(1);
    DelRoutingVrf(2);
    DeleteEnvironment(true);
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    GETUSERARGS();
    strcpy(init_file, DEFAULT_VNSW_CONFIG_FILE);
    client = TestInit(init_file, ksync_init, true, false);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    return ret;
}
