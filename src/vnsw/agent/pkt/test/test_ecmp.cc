/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"

#define AGE_TIME 10*1000

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input1[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 2, 1},
};
//virtual IP VM
struct PortInfo input2[] = {
    {"vnet2", 2, "2.1.1.1", "00:00:00:01:01:01", 2, 2},
    {"vnet3", 3, "2.1.1.1", "00:00:00:02:02:01", 2, 3},
    {"vnet4", 4, "2.1.1.1", "00:00:00:02:02:01", 2, 4},
};

struct PortInfo input3[] = {
    {"vnet5", 5, "3.1.1.1", "00:00:00:01:01:01", 3, 5},
    {"vnet6", 6, "3.1.1.2", "00:00:00:02:02:01", 3, 6},
    {"vnet7", 7, "3.1.1.3", "00:00:00:02:02:01", 3, 7},
};

struct PortInfo input4[] = {
    {"vnet8", 8, "4.1.1.1", "00:00:00:01:01:01", 4, 8},
};

class EcmpTest : public ::testing::Test {
    virtual void SetUp() {
        //                 -<vnet2, vrf2>----<vnet5,vrf3>
        // <vnet1, vrf1>----<vnet3, vrf2>----<vnet6,vrf3>----<vnet8, vrf4>
        //                 -<vnet4, vrf2>----<vnet7,vrf3>
        CreateVmportWithEcmp(input1, 1);
        CreateVmportWithEcmp(input2, 3);
        CreateVmportFIpEnv(input3, 3);
        CreateVmportFIpEnv(input4, 1);
        client->WaitForIdle();
        for (uint32_t i = 1; i < 9; i++) {
            EXPECT_TRUE(VmPortActive(i));
        }

        boost::system::error_code ec;
        bgp_peer = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                 "xmpp channel");

        //Add floating IP for vrf2 interface to talk to
        //vrf3
        AddFloatingIpPool("fip-pool1", 1);
        AddFloatingIp("fip1", 1, "3.1.1.100");
        AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
                "default-project:vn3");
        AddLink("virtual-machine-interface", "vnet2", "floating-ip", "fip1");
        AddLink("virtual-machine-interface", "vnet3", "floating-ip", "fip1");
        AddLink("virtual-machine-interface", "vnet4", "floating-ip", "fip1");
        client->WaitForIdle();
        Ip4Address ip = Ip4Address::from_string("3.1.1.100");
        Inet4UnicastRouteEntry *rt = RouteGet("default-project:vn3:vn3", ip, 32);
        EXPECT_TRUE(rt != NULL);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
        mpls_label_1 = rt->GetMplsLabel();

        ip = Ip4Address::from_string("2.1.1.1");
        rt = RouteGet("vrf2", ip, 32);
        EXPECT_TRUE(rt != NULL);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
        mpls_label_2 = rt->GetMplsLabel();

        //Add floating IP for vrf3 interfaces to talk vrf4
        AddFloatingIpPool("fip-pool2", 2);
        AddFloatingIp("fip2", 2, "4.1.1.100");
        AddLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool2");
        AddLink("floating-ip-pool", "fip-pool2", "virtual-network",
                "default-project:vn4");
        AddLink("virtual-machine-interface", "vnet5", "floating-ip", "fip2");
        AddLink("virtual-machine-interface", "vnet6", "floating-ip", "fip2");
        AddLink("virtual-machine-interface", "vnet7", "floating-ip", "fip2");
        client->WaitForIdle();
        ip = Ip4Address::from_string("4.1.1.100");
        rt = RouteGet("default-project:vn4:vn4", ip, 32);
        EXPECT_TRUE(rt != NULL);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
        mpls_label_3 = rt->GetMplsLabel();

        //Populate ethernet interface id
        eth_intf_id_ = EthInterfaceGet("vnet0")->id();

        remote_vm_ip1_ = Ip4Address::from_string("2.2.2.2");
        remote_vm_ip2_ = Ip4Address::from_string("3.3.3.3");
        remote_vm_ip3_ = Ip4Address::from_string("4.4.4.4");
        remote_server_ip_ = Ip4Address::from_string("10.10.1.1");

        //Add couple of remote VM routes for generating packet
        Inet4TunnelRouteAdd(bgp_peer, "vrf2", remote_vm_ip1_, 32, 
                            remote_server_ip_, TunnelType::AllType(),
                            30, "vn2", SecurityGroupList(),
                            PathPreference());

        Inet4TunnelRouteAdd(bgp_peer, "default-project:vn3:vn3",
                            remote_vm_ip2_, 32,
                            remote_server_ip_, TunnelType::AllType(),
                            30, "default-project:vn3", SecurityGroupList(),
                            PathPreference());

        Inet4TunnelRouteAdd(bgp_peer, "default-project:vn4:vn4",
                            remote_vm_ip3_, 32,
                            remote_server_ip_, TunnelType::AllType(),
                            30, "default-project:vn4", SecurityGroupList(),
                            PathPreference());
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DelLink("virtual-machine-interface", "vnet2", "floating-ip", "fip1");
        DelLink("virtual-machine-interface", "vnet3", "floating-ip", "fip1");
        DelLink("virtual-machine-interface", "vnet4", "floating-ip", "fip1");
        DelLink("floating-ip-pool", "fip-pool1", "virtual-network",
                "default-project:vn3");
        client->WaitForIdle();

        DelLink("virtual-machine-interface", "vnet5", "floating-ip", "fip2");
        DelLink("virtual-machine-interface", "vnet6", "floating-ip", "fip2");
        DelLink("virtual-machine-interface", "vnet7", "floating-ip", "fip2");
        DelLink("floating-ip-pool", "fip-pool2", "virtual-network",
                "default-project:vn4");
        client->WaitForIdle();
        DeleteVmportEnv(input1, 1, false);
        DeleteVmportEnv(input2, 3, true);
        DeleteVmportFIpEnv(input3, 3, true);
        DeleteVmportFIpEnv(input4, 1, true);
        Agent::GetInstance()->fabric_inet4_unicast_table()->DeleteReq(NULL, "vrf2", 
                remote_vm_ip1_, 32, NULL);
        Agent::GetInstance()->fabric_inet4_unicast_table()->DeleteReq(NULL,
                "default-project:vn3:vn3", remote_vm_ip2_, 32, NULL);
        Agent::GetInstance()->fabric_inet4_unicast_table()->DeleteReq(NULL,
                "default-project:vn4:vn4", remote_vm_ip3_, 32, NULL);

        client->WaitForIdle();
        DeleteBgpPeer(bgp_peer);
        EXPECT_FALSE(VrfFind("vrf1", true));
        EXPECT_FALSE(VrfFind("vrf2", true));
        EXPECT_FALSE(VrfFind("default-project:vn3:vn3", true));
        EXPECT_FALSE(VrfFind("default-project:vn4:vn4", true));
    }
public:
    uint32_t GetServiceVlanNH(uint32_t intf_id, std::string vrf_name) const {
        const VmInterface *vm_intf = VmInterfaceGet(intf_id);
        const VrfEntry *vrf = VrfGet(vrf_name.c_str());
        uint32_t label = vm_intf->GetServiceVlanLabel(vrf);
        int nh_id = Agent::GetInstance()->mpls_table()->
                        FindMplsLabel(label)->nexthop()->id();
        return nh_id;
    }

    void AddRemoteEcmpRoute(const string vrf_name, const string ip,
            uint32_t plen, const string vn, int count,
            std::vector<ComponentNHData> local_list,
            bool same_label = false) {
        //If there is a local route, include that always
        Ip4Address vm_ip = Ip4Address::from_string(ip);

        std::vector<ComponentNHData> comp_nh_list = local_list;

        int remote_server_ip = 0x0A0A0A0A;
        int label = 16;
        SecurityGroupList sg_id_list;

        for(int i = 0; i < count; i++) {
            ComponentNHData comp_nh(label,Agent::GetInstance()->fabric_vrf_name(),
                    Agent::GetInstance()->router_id(), 
                    Ip4Address(remote_server_ip++),
                    false, TunnelType::AllType());
            comp_nh_list.push_back(comp_nh);
            if (!same_label) {
                label++;
            }
        }
        EcmpTunnelRouteAdd(bgp_peer, vrf_name, vm_ip, plen, 
                           comp_nh_list, -1, vn, sg_id_list,
                           PathPreference());
    }

    void AddLocalVmRoute(const string vrf_name, const string ip, uint32_t plen,
                         const string vn, uint32_t intf_uuid) {
        Ip4Address vm_ip = Ip4Address::from_string(ip);
        const VmInterface *vm_intf = static_cast<const VmInterface *>
            (VmPortGet(intf_uuid));
        Agent::GetInstance()->fabric_inet4_unicast_table()->
            AddLocalVmRouteReq(bgp_peer, vrf_name, vm_ip, plen,
                               vm_intf->GetUuid(), vn, vm_intf->label(),
                               SecurityGroupList(), false, PathPreference());
    }

    void AddRemoteVmRoute(const string vrf_name, const string ip, uint32_t plen,
                          const string vn) {
        Ip4Address vm_ip = Ip4Address::from_string(ip);
        Ip4Address server_ip = Ip4Address::from_string("10.11.1.1");
        Inet4TunnelRouteAdd(bgp_peer, vrf_name, vm_ip, plen, server_ip,
                            TunnelType::AllType(), 16,
                            vn, SecurityGroupList(), PathPreference());
    }

    void DeleteRemoteRoute(const string vrf_name, const string ip,
                               uint32_t plen) {
        Ip4Address server_ip = Ip4Address::from_string(ip);
        Agent::GetInstance()->fabric_inet4_unicast_table()->DeleteReq(bgp_peer, vrf_name,
                                                        server_ip, plen, NULL);
    } 

    uint32_t eth_intf_id_;
    Ip4Address remote_vm_ip1_;
    Ip4Address remote_vm_ip2_;
    Ip4Address remote_vm_ip3_;
    Ip4Address remote_server_ip_;
    uint32_t mpls_label_1;
    uint32_t mpls_label_2;
    uint32_t mpls_label_3;
    BgpPeer *bgp_peer;
    AgentXmppChannel *channel;
};

//Ping from vrf1 to vrf2(which has ECMP vip)
TEST_F(EcmpTest, EcmpTest_1) {
    TxIpPacket(VmPortGetId(1), "1.1.1.1", "2.1.1.1", 1);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("vrf2")->vrf_id(),
                               "1.1.1.1", "2.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx != 
            CompositeNH::kInvalidComponentNHIdx);

    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
}

//Ping from vrf2(vip and floating IP ECMP) to vrf3
TEST_F(EcmpTest, EcmpTest_2) {
    TxIpPacket(VmPortGetId(4), "2.1.1.1", "3.1.1.1", 1);
    client->WaitForIdle();
    FlowEntry *entry = FlowGet(VrfGet("vrf2")->vrf_id(),
                               "2.1.1.1", "3.1.1.1", 1, 0, 0, GetFlowKeyNH(4));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->is_flags_set(FlowEntry::NatFlow) == true);

    //Reverse flow should be set and should also be ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx != 
            CompositeNH::kInvalidComponentNHIdx);
    
    //Make sure reverse component index point to right interface
    Ip4Address ip = Ip4Address::from_string("3.1.1.100");
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>
        (RouteGet("default-project:vn3:vn3", ip, 32)->GetActiveNextHop());
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
        (comp_nh->GetComponentNHList()->Get(rev_entry->data().component_nh_idx)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet4");
}

//Ping from vrf3(floating IP is ECMP) to vrf4
TEST_F(EcmpTest, EcmpTest_3) {
    TxIpPacket(VmPortGetId(5), "3.1.1.1", "4.1.1.1", 1);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("default-project:vn3:vn3")->vrf_id(),
                               "3.1.1.1", "4.1.1.1", 1, 0, 0, GetFlowKeyNH(5));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->is_flags_set(FlowEntry::NatFlow) == true);

    //Reverse flow should be set and should also be ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx != 
            CompositeNH::kInvalidComponentNHIdx);
    //Make sure reverse component index point to right interface
    Ip4Address ip = Ip4Address::from_string("4.1.1.100");
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>
        (RouteGet("default-project:vn4:vn4", ip, 32)->GetActiveNextHop());
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
        (comp_nh->GetComponentNHList()->Get(rev_entry->data().component_nh_idx)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet5");
}

//Ping from vrf3(floating IP is ECMP) to vrf4
TEST_F(EcmpTest, EcmpTest_7) {
    TxIpPacket(VmPortGetId(6), "3.1.1.2", "4.1.1.1", 1);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("default-project:vn3:vn3")->vrf_id(),
                               "3.1.1.2", "4.1.1.1", 1, 0, 0, GetFlowKeyNH(6));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->is_flags_set(FlowEntry::NatFlow) == true);

    //Reverse flow should be set and should also be ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx != 
            CompositeNH::kInvalidComponentNHIdx);
    //Make sure reverse component index point to right interface
    Ip4Address ip = Ip4Address::from_string("4.1.1.100");
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>
        (RouteGet("default-project:vn4:vn4", ip, 32)->GetActiveNextHop());
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
        (comp_nh->GetComponentNHList()->Get(rev_entry->data().component_nh_idx)->GetNH());
    EXPECT_TRUE(intf_nh->GetInterface()->name() == "vnet6");
}

//Ping from external world to ECMP vip
TEST_F(EcmpTest, EcmpTest_4) {
    //VIP of vrf2 interfaces
    char vm_ip[80] = "2.1.1.1";
    char router_id[80];
    char remote_server_ip[80]; 
    char remote_vm_ip[80];

    strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());
    strcpy(remote_server_ip, remote_server_ip_.to_string().c_str());
    strcpy(remote_vm_ip, remote_vm_ip1_.to_string().c_str());

    TxIpMplsPacket(eth_intf_id_, remote_server_ip, router_id, mpls_label_2,
                   remote_vm_ip, vm_ip, 1, 10);

    client->WaitForIdle();
    int nh_id = GetMplsLabel(MplsLabel::VPORT_NH, mpls_label_2)->nexthop()->id();
    FlowEntry *entry = FlowGet(VrfGet("vrf2")->vrf_id(),
                               remote_vm_ip, vm_ip, 1, 0, 0,  nh_id);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx != 
            CompositeNH::kInvalidComponentNHIdx);

    //Reverse flow should be set and should also be ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
}

//Ping from external world to ECMP fip
TEST_F(EcmpTest, EcmpTest_5) {
    //FIP of vrf3 interfaces
    char vm_ip[80] = "4.1.1.100";
    char router_id[80];
    char remote_server_ip[80]; 
    char remote_vm_ip[80];

    strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());
    strcpy(remote_server_ip, remote_server_ip_.to_string().c_str());
    strcpy(remote_vm_ip, remote_vm_ip3_.to_string().c_str());

    TxIpMplsPacket(eth_intf_id_, remote_server_ip, router_id, mpls_label_3,
                   remote_vm_ip, vm_ip, 1, 10);
    client->WaitForIdle();

    int nh_id = GetMplsLabel(MplsLabel::VPORT_NH, mpls_label_3)->nexthop()->id();
    FlowEntry *entry = FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(),
                               remote_vm_ip, vm_ip, 1, 0, 0, nh_id);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx != 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->is_flags_set(FlowEntry::NatFlow) == true);

    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
}

//Ping from vrf3(floating IP is ECMP) to vrf4
TEST_F(EcmpTest, EcmpTest_6) {
    TxIpPacket(VmPortGetId(8), "4.1.1.1", "4.1.1.100", 1);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(),
                               "4.1.1.1", "4.1.1.100", 1, 0, 0,
                               GetFlowKeyNH(8));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx != 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->is_flags_set(FlowEntry::NatFlow) == true);

    //Reverse flow should be set and should also be ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
}

TEST_F(EcmpTest, EcmpTest_8) {
    Ip4Address ip = Ip4Address::from_string("30.30.30.0");
    std::vector<ComponentNHData> comp_nh;
    Ip4Address server_ip1 = Ip4Address::from_string("15.15.15.15");
    Ip4Address server_ip2 = Ip4Address::from_string("15.15.15.16");
    Ip4Address server_ip3 = Ip4Address::from_string("15.15.15.17");

    ComponentNHData comp_nh_data1(16, Agent::GetInstance()->fabric_vrf_name(), 
                                  Agent::GetInstance()->router_id(), server_ip1, false,
                                  TunnelType::AllType());
    comp_nh.push_back(comp_nh_data1);

    ComponentNHData comp_nh_data2(17, Agent::GetInstance()->fabric_vrf_name(), 
                                  Agent::GetInstance()->router_id(),
                                  server_ip2, false, TunnelType::AllType());
    comp_nh.push_back(comp_nh_data2);

    ComponentNHData comp_nh_data3(18, Agent::GetInstance()->fabric_vrf_name(), 
                                  Agent::GetInstance()->router_id(),
                                  server_ip3, false, TunnelType::AllType());
    comp_nh.push_back(comp_nh_data3);

    SecurityGroupList sg_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf2", ip, 24, comp_nh, -1, "vn2", sg_list,
                       PathPreference());
    client->WaitForIdle();

    //VIP of vrf2 interfaces
    char vm_ip[80] = "1.1.1.1";
    char router_id[80];
    char remote_server_ip[80]; 
    char remote_vm_ip[80];

    strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());
    strcpy(remote_server_ip, "15.15.15.16");
    strcpy(remote_vm_ip, "30.30.30.1");

    TxIpMplsPacket(eth_intf_id_, remote_server_ip, router_id, 16,
                   remote_vm_ip, vm_ip, 1, 10);

    client->WaitForIdle();
    int nh_id = GetMplsLabel(MplsLabel::VPORT_NH, 16)->nexthop()->id();
    FlowEntry *entry = FlowGet(VrfGet("vrf2")->vrf_id(),
                               remote_vm_ip, vm_ip, 1, 0, 0, nh_id);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);

    //Reverse flow should be set and should also be ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx != 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().component_nh_idx == 1);
    Agent::GetInstance()->fabric_inet4_unicast_table()->DeleteReq(NULL,
                                         "vrf2", ip, 24, NULL);
    client->WaitForIdle();
}

//Ping from a interface with ECMP FIP to remote machine
TEST_F(EcmpTest, EcmpTest_9) {
    char remote_vm_ip[80];

    strcpy(remote_vm_ip, remote_vm_ip3_.to_string().c_str());
    TxIpPacket(VmPortGetId(5), "3.1.1.1", remote_vm_ip, 1);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("default-project:vn3:vn3")->vrf_id(),
                               "3.1.1.1", remote_vm_ip, 1, 0, 0,
                               GetFlowKeyNH(5));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->is_flags_set(FlowEntry::NatFlow) == true);
    EXPECT_TRUE(entry->data().vrf ==
            VrfGet("default-project:vn3:vn3")->vrf_id());
    EXPECT_TRUE(entry->data().dest_vrf ==
            VrfGet("default-project:vn4:vn4")->vrf_id());
    EXPECT_TRUE(entry->data().source_vn == "default-project:vn4");
    EXPECT_TRUE(entry->data().dest_vn == "default-project:vn4");

    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx != 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().vrf == VrfGet("default-project:vn4:vn4")->vrf_id());
    EXPECT_TRUE(rev_entry->data().dest_vrf == VrfGet("default-project:vn3:vn3")->vrf_id());
    EXPECT_TRUE(rev_entry->data().source_vn == "default-project:vn4");
    EXPECT_TRUE(rev_entry->data().dest_vn == "default-project:vn4");
}

TEST_F(EcmpTest, EcmpReEval_1) {
    TxIpPacket(VmPortGetId(1), "1.1.1.1", "2.1.1.1", 1);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("vrf2")->vrf_id(),
            "1.1.1.1", "2.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
            CompositeNH::kInvalidComponentNHIdx);

    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
            CompositeNH::kInvalidComponentNHIdx);

    uint32_t ecmp_index = entry->data().component_nh_idx;
    //Delete VM corresponding to index component_nh_idx
    IntfCfgDel(input2, ecmp_index); 
    client->WaitForIdle();
    //Enqueue a re-evaluate request
    TxIpPacketEcmp(VmPortGetId(1), "1.1.1.1", "2.1.1.1", 1);
    client->WaitForIdle();
    //Upon interface deletion flow would have been deleted, get flow again
    FlowEntry *entry2 = FlowGet(VrfGet("vrf2")->vrf_id(),
            "1.1.1.1", "2.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
 
    //Verify compoennt NH index is different
    EXPECT_TRUE(entry->data().component_nh_idx !=
            CompositeNH::kInvalidComponentNHIdx);
    //make sure new ecmp index is different from that of old ecmp index
    EXPECT_TRUE(ecmp_index != entry2->data().component_nh_idx);
}

//Send a flow for VM to non ECMP dip
//Change the nexthop pointed by non ECMP dip to ECMP dip
//Check if re-evaluation happens
TEST_F(EcmpTest, EcmpReEval_2) {
    //Add a remote VM route for 3.1.1.10
    Ip4Address remote_vm_ip = Ip4Address::from_string("3.1.1.10");
    Ip4Address remote_server_ip = Ip4Address::from_string("10.10.10.10");
    Inet4TunnelRouteAdd(bgp_peer, "vrf2",remote_vm_ip, 32, 
                        remote_server_ip, TunnelType::AllType(), 16, "vn2",
                        SecurityGroupList(), PathPreference());

    TxIpPacket(VmPortGetId(1), "1.1.1.1", "3.1.1.10", 1);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("vrf2")->vrf_id(),
            "1.1.1.1", "3.1.1.10", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx ==
            CompositeNH::kInvalidComponentNHIdx);

    //Convert dip to ECMP nh
    std::vector<ComponentNHData> local_comp_nh;
    AddRemoteEcmpRoute("vrf2", "3.1.1.10", 32, "vn2", 2, local_comp_nh);
    client->WaitForIdle();

    //Enqueue a re-evaluate request
    TxIpPacketEcmp(VmPortGetId(1), "1.1.1.1", "3.1.1.10", 1);
    client->WaitForIdle();
    EXPECT_TRUE(entry != NULL);
    //Since flow already existed, use same old NH which would be at index 0
    EXPECT_TRUE(entry->data().component_nh_idx == 0);

    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
            CompositeNH::kInvalidComponentNHIdx);
    DeleteRemoteRoute("vrf2", "3.1.1.10", 32);
}

//Send a flow for VM to non ECMP dip
//Change the nexthop pointed by non ECMP dip to ECMP dip
//Check if re-evaluation happens
TEST_F(EcmpTest, EcmpReEval_3) {
    struct PortInfo input1[] = {
        {"vnet10", 10, "10.1.1.1", "00:00:00:01:01:01", 10, 10},
        {"vnet11", 11, "11.1.1.1", "00:00:00:01:01:01", 10, 11},
    };
    CreateVmportFIpEnv(input1, 2);
    client->WaitForIdle();

    //Associate floating IP with one interface
    AddFloatingIpPool("fip-pool3", 3);
    AddFloatingIp("fip3", 3, "3.1.1.10");
    AddLink("floating-ip", "fip3", "floating-ip-pool", "fip-pool3");
    AddLink("floating-ip-pool", "fip-pool3", "virtual-network", "vn2");
    AddLink("virtual-machine-interface", "vnet10", "floating-ip", "fip3");
    client->WaitForIdle();

    TxIpPacket(VmPortGetId(1), "1.1.1.1", "3.1.1.10", 1);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("vrf2")->vrf_id(),
            "1.1.1.1", "3.1.1.10", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx ==
            CompositeNH::kInvalidComponentNHIdx);

    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
            CompositeNH::kInvalidComponentNHIdx);

    AddLink("virtual-machine-interface", "vnet11", "floating-ip", "fip3");
    client->WaitForIdle();

    //Enqueue a re-evaluate request
    TxIpPacketEcmp(VmPortGetId(1), "1.1.1.1", "3.1.1.10", 1);
    client->WaitForIdle();
    entry = FlowGet(VrfGet("vrf2")->vrf_id(), "1.1.1.1", "3.1.1.10", 1, 0, 0,
                    GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    //Since flow already existed, use same old NH which would be at index 0
    EXPECT_TRUE(entry->data().component_nh_idx == 0);

    FlowEntry *rev_entry1 = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry == rev_entry1);
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
            CompositeNH::kInvalidComponentNHIdx);

    DelLink("virtual-machine-interface", "vnet10", "floating-ip", "fip3");
    DelLink("virtual-machine-interface", "vnet11", "floating-ip", "fip3");
    DelLink("floating-ip-pool", "fip-pool3", "virtual-network", "vn2");
    client->WaitForIdle();
    DeleteVmportFIpEnv(input1, 2, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vn10:vn10"));
}

TEST_F(EcmpTest, ServiceVlanTest_1) {
    struct PortInfo input1[] = {
        {"vnet10", 10, "10.1.1.1", "00:00:00:01:01:01", 10, 10},
        {"vnet11", 11, "11.1.1.1", "00:00:00:01:01:01", 10, 11},
        {"vnet12", 12, "12.1.1.1", "00:00:00:01:01:01", 10, 12},
    };

    CreateVmportWithEcmp(input1, 3);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(10));
    EXPECT_TRUE(VmPortActive(11));
    EXPECT_TRUE(VmPortActive(11));

    //Add service VRF and VN
    struct PortInfo input2[] = {
        {"vnet13", 13, "11.1.1.252", "00:00:00:01:01:01", 11, 13},
    };
    CreateVmportWithEcmp(input2, 1);
    client->WaitForIdle();

    AddVmPortVrf("ser1", "11.1.1.253", 1);
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "vrf11");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet10");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet11");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet12");
    client->WaitForIdle();

    const VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf11");
    TxIpPacket(VmPortGetId(11), "11.1.1.253", "11.1.1.252", 1, 10, vrf->vrf_id());
    client->WaitForIdle();

    int nh_id = GetServiceVlanNH(11, "vrf11");
    FlowEntry *entry = FlowGet(VrfGet("vrf11")->vrf_id(),
                               "11.1.1.253", "11.1.1.252", 1, 0, 0, nh_id);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx ==
            CompositeNH::kInvalidComponentNHIdx);

    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx != 
            CompositeNH::kInvalidComponentNHIdx);

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "vrf11");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet10");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet11");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet12");
    DeleteVmportEnv(input1, 3, true);
    DeleteVmportEnv(input2, 1, true);
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->pkt()->flow_table()->Size() == 0);
    EXPECT_FALSE(VrfFind("vrf11"));
    EXPECT_FALSE(VrfFind("vrf10"));
}

//Service VM with ECMP instantiated in two 
//different remote server
//Packet sent from a VM instance to service VM instance
TEST_F(EcmpTest, ServiceVlanTest_2) {
    struct PortInfo input1[] = {
        {"vnet10", 10, "10.1.1.1", "00:00:00:01:01:01", 10, 10},
    };
    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();

    std::vector<ComponentNHData> local_comp_nh;
    AddRemoteEcmpRoute("vrf10", "11.1.1.0", 24, "vn11", 2, local_comp_nh);

    uint32_t vrf_id = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf10")->vrf_id();
    TxIpPacket(VmPortGetId(10), "10.1.1.1", "11.1.1.252", 1, 10, vrf_id);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("vrf10")->vrf_id(),
            "10.1.1.1", "11.1.1.252", 1, 0, 0, GetFlowKeyNH(10));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().vrf == vrf_id);
    EXPECT_TRUE(entry->data().dest_vrf == vrf_id);
    EXPECT_TRUE(entry->data().source_vn == "vn10");
    EXPECT_TRUE(entry->data().dest_vn == "vn11");

    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().vrf == vrf_id);
    EXPECT_TRUE(rev_entry->data().dest_vrf == vrf_id);
    EXPECT_TRUE(rev_entry->data().source_vn == "vn11");
    EXPECT_TRUE(rev_entry->data().dest_vn == "vn10");

    DeleteVmportEnv(input1, 1, true);
    DeleteRemoteRoute("vrf10", "11.1.1.0", 24);
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->pkt()->flow_table()->Size() == 0);
    EXPECT_FALSE(VrfFind("vrf10"));
}

//Service VM with ECMP instantiated in local and remote server
//Packet sent from a VM instance to service VM instance
//Packet sent from a remote VM to service instance
TEST_F(EcmpTest, ServiceVlanTest_3) {
    struct PortInfo input1[] = {
        {"vnet10", 10, "10.1.1.1", "00:00:00:01:01:01", 10, 10},
    };
    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();

    //Add service VM in vrf11 as mgmt VRF
    struct PortInfo input2[] = {
        {"vnet11", 11, "1.1.1.252", "00:00:00:01:01:01", 11, 11},
    };
    CreateVmportWithEcmp(input2, 1);
    client->WaitForIdle();

    AddVrf("service-vrf1", 12);
    AddVmPortVrf("ser1", "10.1.1.2", 1);
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet11");
    client->WaitForIdle();

    //Leak aggregarete route to vrf10
    Ip4Address service_vm_ip = Ip4Address::from_string("10.1.1.2"); 
    Inet4UnicastRouteEntry *rt = RouteGet("service-vrf1", service_vm_ip, 32);
    std::vector<ComponentNHData> comp_nh_list;
    EXPECT_TRUE(rt != NULL);
    const VlanNH *vlan_nh = static_cast<const VlanNH *>(rt->GetActiveNextHop());
    ComponentNHData comp_nh(rt->GetMplsLabel(), vlan_nh->GetIfUuid(), 
                            vlan_nh->GetVlanTag(), false);
    uint32_t vlan_label = rt->GetMplsLabel();
    comp_nh_list.push_back(comp_nh);
    AddRemoteEcmpRoute("vrf10", "11.1.1.0", 24, "vn11", 1, comp_nh_list);
    AddRemoteEcmpRoute("service-vrf1", "11.1.1.0", 24, "vn11", 1, comp_nh_list);

    //Leak a remote VM route in service-vrf
    AddRemoteVmRoute("service-vrf1", "10.1.1.3", 32, "vn10");
    client->WaitForIdle();

    uint32_t vrf_id = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf10")->vrf_id();
    uint32_t service_vrf_id = 
        Agent::GetInstance()->vrf_table()->FindVrfFromName("service-vrf1")->vrf_id();

    //Choose some random source and destination port
    uint32_t sport = rand() % 65535;
    uint32_t dport = rand() % 65535;
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t hash_id = rand() % 65535;
        TxTcpPacket(VmPortGetId(10), "10.1.1.1", "11.1.1.252", sport, dport, 
                    false, hash_id);
        client->WaitForIdle();

        FlowEntry *entry = FlowGet(VrfGet("vrf10")->vrf_id(),
                "10.1.1.1", "11.1.1.252", IPPROTO_TCP, sport, dport,
                GetFlowKeyNH(10));
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == vrf_id);
        if (entry->data().component_nh_idx == 1) {
            //Remote component index will be installed at index 1
            //Destination on remote server, hence destination
            //VRF will be same as that of interface
            EXPECT_TRUE(entry->data().dest_vrf == vrf_id);
        } else {
            //Destination on local server, destination VRF
            //will be that of service VRF
            LOG(DEBUG, "Dest vrf " << entry->data().dest_vrf << "Service VRF:" << service_vrf_id);
            EXPECT_TRUE(entry->data().dest_vrf == service_vrf_id);
        }

        EXPECT_TRUE(entry->data().source_vn == "vn10");
        EXPECT_TRUE(entry->data().dest_vn == "vn11");

        //Reverse flow is no ECMP
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx == 
                CompositeNH::kInvalidComponentNHIdx);
        if (entry->data().component_nh_idx == 1) {
            //Reverse flow originates remote server, hence
            //source VRF is same as destination interface VRF
            EXPECT_TRUE(rev_entry->data().vrf == vrf_id);
        } else {
            //Reverse flow on same server, source VRF will be that
            //of service VM vlan interface
            EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        }
        EXPECT_TRUE(rev_entry->data().dest_vrf == vrf_id);
        EXPECT_TRUE(rev_entry->data().source_vn == "vn11");
        EXPECT_TRUE(rev_entry->data().dest_vn == "vn10");
        sport++;
        dport++;
    }

    //Leak a remote VM route in service-vrf
    AddRemoteVmRoute("service-vrf1", "10.1.1.3", 32, "vn10");
    client->WaitForIdle();
    //Send a packet from a remote VM to service VM
    //Below scenario wouldnt happen with vrouter, as destination
    //packet came with explicit unicast mpls tag
    sport = rand() % 65535;
    dport = rand() % 65535;
    for (uint32_t i = 0; i < 100; i++) {
        char router_id[80];
        strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());

        uint32_t hash_id = rand() % 65535;
        TxTcpMplsPacket(eth_intf_id_, "10.11.11.1", router_id, 
                        vlan_label, "10.1.1.3", "11.1.1.252", 
                        sport, dport, false, hash_id);
        client->WaitForIdle();

        int nh_id =
            GetMplsLabel(MplsLabel::VPORT_NH, vlan_label)->nexthop()->id();
        FlowEntry *entry = FlowGet(VrfGet("service-vrf1")->vrf_id(),
                "10.1.1.3", "11.1.1.252", IPPROTO_TCP, sport, dport, nh_id);
        EXPECT_TRUE(entry != NULL);
        //No ECMP as packet came with explicit mpls label 
        //pointing to vlan NH
        EXPECT_TRUE(entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
        sport++;
        dport++;
    }

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet11");
    DeleteVmportEnv(input2, 1, true);
    DeleteRemoteRoute("service-vrf1", "11.1.1.0", 24);
    DelVrf("service-vrf1");
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();

    EXPECT_TRUE(Agent::GetInstance()->pkt()->flow_table()->Size() == 0);
    EXPECT_FALSE(VrfFind("vrf11"));
    EXPECT_FALSE(VrfFind("vrf10"));
    EXPECT_FALSE(VrfFind("service-vrf1"));
}


//Service VM with ECMP instantaited in local server
//Test with packet from virtual-machine to service VM instance
//Test with packet from external-world to service VM instance
TEST_F(EcmpTest, ServiceVlanTest_4) {
    struct PortInfo input1[] = {
        {"vnet10", 10, "10.1.1.1", "00:00:00:01:01:01", 10, 10},
    };
    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();

    //Add service VM in vrf11 as mgmt VRF
    struct PortInfo input2[] = {
        {"vnet11", 11, "1.1.1.252", "00:00:00:01:01:01", 11, 11},
        {"vnet12", 12, "1.1.1.253", "00:00:00:01:01:01", 11, 12},
    };
    CreateVmportWithEcmp(input2, 2);
    client->WaitForIdle();

    AddVrf("service-vrf1", 12);
    AddVmPortVrf("ser1", "10.1.1.2", 1);
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet11");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet12");
    client->WaitForIdle();

    uint32_t vrf_id = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf10")->vrf_id();
    uint32_t service_vrf_id = 
        Agent::GetInstance()->vrf_table()->FindVrfFromName("service-vrf1")->vrf_id();

    //Leak aggregarete route to vrf10
    Ip4Address service_vm_ip = Ip4Address::from_string("10.1.1.2"); 
    Inet4UnicastRouteEntry *rt = RouteGet("service-vrf1", service_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);

    std::vector<ComponentNHData> comp_nh_list;
    CompositeNHKey nh_key("service-vrf1", service_vm_ip, 32, true);
    ComponentNHData comp_nh(-1, &nh_key);
    comp_nh_list.push_back(comp_nh);
    AddRemoteEcmpRoute("vrf10", "11.1.1.0", 24, "vn11", 0, comp_nh_list);
    AddRemoteEcmpRoute("service-vrf1", "11.1.1.0", 24, "vn11", 0, comp_nh_list);

    //Choose some random source and destination port
    uint32_t sport = rand() % 65535;
    uint32_t dport = rand() % 65535;
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t hash_id = rand() % 65535;
        TxTcpPacket(VmPortGetId(10), "10.1.1.1", "11.1.1.252", sport, dport, 
                    false, hash_id);
        client->WaitForIdle();

        FlowEntry *entry = FlowGet(VrfGet("vrf10")->vrf_id(),
                "10.1.1.1", "11.1.1.252", IPPROTO_TCP, sport, dport,
                GetFlowKeyNH(10));
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == vrf_id);
        //Packet destined to service interface, vrf has to be 
        //service vlan VRF
        EXPECT_TRUE(entry->data().dest_vrf == service_vrf_id);

        EXPECT_TRUE(entry->data().source_vn == "vn10");
        EXPECT_TRUE(entry->data().dest_vn == "vn11");

        //Reverse flow is no ECMP
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx == 
                CompositeNH::kInvalidComponentNHIdx);
        //Packet from service interface, vrf has to be 
        //service vlan VRF
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == vrf_id);
        EXPECT_TRUE(rev_entry->data().source_vn == "vn11");
        EXPECT_TRUE(rev_entry->data().dest_vn == "vn10");
        sport++;
        dport++;
    }

    //Leak a remote VM route in service-vrf
    AddRemoteVmRoute("service-vrf1", "10.1.1.3", 32, "vn10");
    client->WaitForIdle();
    uint32_t label = rt->GetMplsLabel();
    //Send a packet from a remote VM to service VM
    sport = rand() % 65535;
    dport = rand() % 65535;
    for (uint32_t i = 0; i < 100; i++) {
        char router_id[80];
        strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());

        uint32_t hash_id = rand() % 65535;
        TxTcpMplsPacket(eth_intf_id_, "10.11.11.1", router_id, 
                        label, "10.1.1.3", "11.1.1.252", 
                        sport, dport, false, hash_id);
        client->WaitForIdle();

        int nh_id = GetMplsLabel(MplsLabel::VPORT_NH, label)->nexthop()->id();
        FlowEntry *entry = FlowGet(VrfGet("service-vrf1")->vrf_id(),
                "10.1.1.3", "11.1.1.252", IPPROTO_TCP, sport, dport, nh_id);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == service_vrf_id);
        //Packet destined to service interface, vrf has to be 
        //service vlan VRF
        LOG(DEBUG, "Vrf" << entry->data().dest_vrf << ":" << service_vrf_id);
        EXPECT_TRUE(entry->data().dest_vrf == service_vrf_id);

        EXPECT_TRUE(entry->data().source_vn == "vn10");
        EXPECT_TRUE(entry->data().dest_vn == "vn11");

        //Reverse flow is no ECMP
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx == 
                CompositeNH::kInvalidComponentNHIdx);
        //Packet from service interface, vrf has to be 
        //service vlan VRF
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().source_vn == "vn11");
        EXPECT_TRUE(rev_entry->data().dest_vn == "vn10");
        sport++;
        dport++;
    }

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet11");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet12");
    DeleteVmportEnv(input2, 2, true);
    DelVrf("service-vrf1");
    client->WaitForIdle(2); 
    EXPECT_TRUE(Agent::GetInstance()->pkt()->flow_table()->Size() == 0);

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf11"));
    EXPECT_FALSE(VrfFind("vrf10"));
    EXPECT_FALSE(VrfFind("service-vrf1"));
}


//Packet from a right service VM interface(both service vm instance launched on same server)
//reaching end host on same machine
TEST_F(EcmpTest, ServiceVlanTest_5) {
    struct PortInfo input1[] = {
        {"vnet11", 11, "11.1.1.1", "00:00:00:01:01:01", 11, 11},
    };
    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();

    //Add service VM in vrf13 as mgmt VRF
    struct PortInfo input2[] = {
        {"vnet13", 13, "1.1.1.252", "00:00:00:01:01:01", 13, 13},
        {"vnet14", 14, "1.1.1.253", "00:00:00:01:01:01", 13, 14},
    };
    CreateVmportWithEcmp(input2, 2);
    client->WaitForIdle();

    AddVrf("service-vrf1", 12);
    AddVmPortVrf("ser1", "11.1.1.2", 1);
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet13");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet14");
    client->WaitForIdle();

    uint32_t vrf_id = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf11")->vrf_id();
    uint32_t service_vrf_id = 
        Agent::GetInstance()->vrf_table()->FindVrfFromName("service-vrf1")->vrf_id();

    Ip4Address service_vm_ip = Ip4Address::from_string("11.1.1.2"); 
    Inet4UnicastRouteEntry *rt = RouteGet("service-vrf1", service_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);

    std::vector<ComponentNHData> comp_nh_list;
    CompositeNHKey nh_key("service-vrf1", service_vm_ip, 32, true);
    ComponentNHData comp_nh_data(-1, &nh_key);
    comp_nh_list.push_back(comp_nh_data);
    //Leak a aggregarate route to service VRF
    AddRemoteEcmpRoute("service-vrf1", "10.1.1.0", 24, "vn10", 0, comp_nh_list);
    //Leak route for vm11 to service vrf
    AddLocalVmRoute("service-vrf1", "11.1.1.1", 32, "vn11", 11);
    client->WaitForIdle();
    Ip4Address vn10_agg_ip = Ip4Address::from_string("10.1.1.0");
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>
        (RouteGet("service-vrf1", vn10_agg_ip, 24)->GetActiveNextHop());

    uint32_t vnet13_vlan_nh = GetServiceVlanNH(13, "service-vrf1");
    uint32_t vnet14_vlan_nh = GetServiceVlanNH(14, "service-vrf1");

    //Choose some random source and destination port
    uint32_t sport = rand() % 65535;
    uint32_t dport = rand() % 65535;
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t hash_id = rand() % 65535;
        TxTcpPacket(VmPortGetId(13), "10.1.1.1", "11.1.1.1", sport, dport, 
                    false, hash_id, service_vrf_id);
        client->WaitForIdle();

        FlowEntry *entry = FlowGet(service_vrf_id,
                "10.1.1.1", "11.1.1.1", IPPROTO_TCP, sport, dport,
                vnet13_vlan_nh);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == service_vrf_id);
        //Packet destined to vm11, vrf has to be vrf 11
        EXPECT_TRUE(entry->data().dest_vrf == vrf_id);

        EXPECT_TRUE(entry->data().source_vn == "vn10");
        EXPECT_TRUE(entry->data().dest_vn == "vn11");

        FlowEntry *rev_entry = entry->reverse_flow_entry();
        const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
            (comp_nh->GetNH(rev_entry->data().component_nh_idx));
        EXPECT_TRUE(intf_nh->GetIfUuid() == MakeUuid(13));
        //Packet to service interface, vrf has to be 
        //service vlan VRF
        EXPECT_TRUE(rev_entry->data().vrf == vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().source_vn == "vn11");
        EXPECT_TRUE(rev_entry->data().dest_vn == "vn10");
        sport++;
        dport++;
    }

    //Send traffice from second service interface and expect things to be fine
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t hash_id = rand() % 65535;
        TxTcpPacket(VmPortGetId(14), "10.1.1.1", "11.1.1.1", sport, dport, 
                    false, hash_id, service_vrf_id);
        client->WaitForIdle();

        FlowEntry *entry = FlowGet(service_vrf_id,
                "10.1.1.1", "11.1.1.1", IPPROTO_TCP, sport, dport,
                vnet14_vlan_nh);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == service_vrf_id);
        //Packet destined to vm11, vrf has to be vrf11
        EXPECT_TRUE(entry->data().dest_vrf == vrf_id);

        EXPECT_TRUE(entry->data().source_vn == "vn10");
        EXPECT_TRUE(entry->data().dest_vn == "vn11");

        //make sure reverse flow points to right index
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
            (comp_nh->GetNH(rev_entry->data().component_nh_idx));
        EXPECT_TRUE(intf_nh->GetIfUuid() == MakeUuid(14));

        //Packet from vm11 to service vrf 
        EXPECT_TRUE(rev_entry->data().vrf == vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().source_vn == "vn11");
        EXPECT_TRUE(rev_entry->data().dest_vn == "vn10");
        sport++;
        dport++;
    }

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet13");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet14");
    DeleteVmportEnv(input2, 2, true);
    DelVrf("service-vrf1");
    client->WaitForIdle();
    //Make sure all flows are also deleted
    EXPECT_TRUE(Agent::GetInstance()->pkt()->flow_table()->Size() == 0);

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf11"));
    EXPECT_FALSE(VrfFind("vrf13"));
    EXPECT_FALSE(VrfFind("service-vrf1"));
}

//Packet from a right service VM interface(both service vm instance launched on same server)
//reaching end host on a remote machine
//Packet from remote host reaching service VM
//Packet from remote ecmp host reaching service VM
TEST_F(EcmpTest, ServiceVlanTest_6) {
    //Add service VM in vrf13 as mgmt VRF
    struct PortInfo input2[] = {
        {"vnet13", 13, "1.1.1.252", "00:00:00:01:01:01", 13, 13},
        {"vnet14", 14, "1.1.1.253", "00:00:00:01:01:01", 13, 14},
    };
    CreateVmportWithEcmp(input2, 2);
    client->WaitForIdle();

    AddVrf("service-vrf1", 12);
    AddVmPortVrf("ser1", "11.1.1.2", 1);
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet13");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet14");
    client->WaitForIdle();

    uint32_t service_vrf_id = 
        Agent::GetInstance()->vrf_table()->FindVrfFromName("service-vrf1")->vrf_id();

    Ip4Address service_vm_ip = Ip4Address::from_string("11.1.1.2"); 
    Inet4UnicastRouteEntry *rt = RouteGet("service-vrf1", service_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);
    uint32_t mpls_label = rt->GetMplsLabel();

    std::vector<ComponentNHData> comp_nh_list;
    CompositeNHKey nh_key("service-vrf1", service_vm_ip, 32, true);
    ComponentNHData comp_nh_data(-1, &nh_key);
    comp_nh_list.push_back(comp_nh_data);
    //Leak a aggregarate route to service VRF
    AddRemoteEcmpRoute("service-vrf1", "10.1.1.0", 24, "vn10", 0, comp_nh_list);
    //Leak routes from right vrf to service vrf
    AddRemoteVmRoute("service-vrf1", "11.1.1.1", 32, "vn11");
    comp_nh_list.clear();
    AddRemoteEcmpRoute("service-vrf1", "11.1.1.3", 32, "vn11", 2, comp_nh_list);
    client->WaitForIdle();
    Ip4Address vn10_agg_ip = Ip4Address::from_string("10.1.1.0");
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>
        (RouteGet("service-vrf1", vn10_agg_ip, 24)->GetActiveNextHop());

    uint32_t vnet13_vlan_nh = GetServiceVlanNH(13, "service-vrf1");
    uint32_t vnet14_vlan_nh = GetServiceVlanNH(14, "service-vrf1");
    //Choose some random source and destination port
    uint32_t sport = rand() % 65535;
    uint32_t dport = rand() % 65535;
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t hash_id = rand() % 65535;
        TxTcpPacket(VmPortGetId(13), "10.1.1.1", "11.1.1.1", sport, dport, 
                    false, hash_id, service_vrf_id);
        client->WaitForIdle();

        FlowEntry *entry = FlowGet(service_vrf_id,
                "10.1.1.1", "11.1.1.1", IPPROTO_TCP, sport, dport,
                vnet13_vlan_nh);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == service_vrf_id);
        //Packet destined to remote server, vrf would be same as service vrf
        EXPECT_TRUE(entry->data().dest_vrf == service_vrf_id);

        EXPECT_TRUE(entry->data().source_vn == "vn10");
        EXPECT_TRUE(entry->data().dest_vn == "vn11");

        FlowEntry *rev_entry = entry->reverse_flow_entry();
        const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
            (comp_nh->GetNH(rev_entry->data().component_nh_idx));
        EXPECT_TRUE(intf_nh->GetIfUuid() == MakeUuid(13));
        //Packet to service interface, vrf has to be 
        //service vlan VRF
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().source_vn == "vn11");
        EXPECT_TRUE(rev_entry->data().dest_vn == "vn10");
        sport++;
        dport++;
    }

    //Send traffic from second service interface
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t hash_id = rand() % 65535;
        TxTcpPacket(VmPortGetId(14), "10.1.1.1", "11.1.1.1", sport, dport, 
                    false, hash_id, service_vrf_id);
        client->WaitForIdle();

        FlowEntry *entry = FlowGet(service_vrf_id,
                "10.1.1.1", "11.1.1.1", IPPROTO_TCP, sport, dport,
                vnet14_vlan_nh);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == service_vrf_id);
        //Packet destined to remote server, vrf would be same as service vrf
        EXPECT_TRUE(entry->data().dest_vrf == service_vrf_id);

        EXPECT_TRUE(entry->data().source_vn == "vn10");
        EXPECT_TRUE(entry->data().dest_vn == "vn11");

        //make sure reverse flow points to right index
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
            (comp_nh->GetNH(rev_entry->data().component_nh_idx));
        EXPECT_TRUE(intf_nh->GetIfUuid() == MakeUuid(14));

        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().source_vn == "vn11");
        EXPECT_TRUE(rev_entry->data().dest_vn == "vn10");
        sport++;
        dport++;
    }

    char router_id[80];
    strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());
    //Send traffic from remote VM service
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t hash_id = rand() % 65535;
        TxTcpMplsPacket(eth_intf_id_, "10.10.10.10", router_id, 
                        mpls_label, "11.1.1.1", "10.1.1.1", 
                        sport, dport, false, hash_id);
        client->WaitForIdle();

        int nh_id =
            GetMplsLabel(MplsLabel::VPORT_NH, mpls_label)->nexthop()->id();
        FlowEntry *entry = FlowGet(service_vrf_id,
                "11.1.1.1", "10.1.1.1", IPPROTO_TCP, sport, dport, nh_id);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(entry->data().source_vn == "vn11");
        EXPECT_TRUE(entry->data().dest_vn == "vn10");

        //make sure reverse flow is no ecmp
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().source_vn == "vn10");
        EXPECT_TRUE(rev_entry->data().dest_vn == "vn11");
        sport++;
        dport++;
    }
 
    //Send traffic from remote VM which is also ecmp
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t hash_id = rand() % 65535;
        TxTcpMplsPacket(eth_intf_id_, "10.10.10.10", router_id, 
                        mpls_label, "11.1.1.3", "10.1.1.1", 
                        sport, dport, false, hash_id);
        client->WaitForIdle();

        int nh_id =
            GetMplsLabel(MplsLabel::VPORT_NH, mpls_label)->nexthop()->id();
        FlowEntry *entry = FlowGet(service_vrf_id,
                "11.1.1.3", "10.1.1.1", IPPROTO_TCP, sport, dport, nh_id);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(entry->data().source_vn == "vn11");
        EXPECT_TRUE(entry->data().dest_vn == "vn10");

        //make sure reverse flow is no ecmp
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx == 0);
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().source_vn == "vn10");
        EXPECT_TRUE(rev_entry->data().dest_vn == "vn11");
        sport++;
        dport++;
    }

    //Send traffic from remote VM which is also ecmp
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t hash_id = rand() % 65535;
        TxTcpMplsPacket(eth_intf_id_, "10.10.10.11", router_id, 
                        mpls_label, "11.1.1.3", "10.1.1.1", 
                        sport, dport, false, hash_id);
        client->WaitForIdle();
        int nh_id =
            GetMplsLabel(MplsLabel::VPORT_NH, mpls_label)->nexthop()->id();
        FlowEntry *entry = FlowGet(service_vrf_id,
                "11.1.1.3", "10.1.1.1", IPPROTO_TCP, sport, dport, nh_id);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(entry->data().source_vn == "vn11");
        EXPECT_TRUE(entry->data().dest_vn == "vn10");

        //make sure reverse flow is no ecmp
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx == 1);
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().source_vn == "vn10");
        EXPECT_TRUE(rev_entry->data().dest_vn == "vn11");
        sport++;
        dport++;
    }

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet13");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet14");
    DeleteVmportEnv(input2, 2, true);
    DelVrf("service-vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->pkt()->flow_table()->Size() == 0);

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf13"));
    EXPECT_FALSE(VrfFind("service-vrf1"));
}

//Packet from a right service VM interface(one service instance on local server,
// one on remote server) reaching end host on a local machine
TEST_F(EcmpTest, ServiceVlanTest_7) {
    struct PortInfo input1[] = {
        {"vnet11", 11, "11.1.1.1", "00:00:00:01:01:01", 11, 11},
    };
    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();

    //Add service VM in vrf13 as mgmt VRF
    struct PortInfo input2[] = {
        {"vnet13", 13, "1.1.1.252", "00:00:00:01:01:01", 13, 13},
    };
    CreateVmportWithEcmp(input2, 1);
    client->WaitForIdle();

    AddVrf("service-vrf1", 12);
    AddVmPortVrf("ser1", "11.1.1.2", 1);
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet13");
    client->WaitForIdle();

    uint32_t vrf_id = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf11")->vrf_id();
    uint32_t service_vrf_id = 
        Agent::GetInstance()->vrf_table()->FindVrfFromName("service-vrf1")->vrf_id();

    Ip4Address service_vm_ip = Ip4Address::from_string("11.1.1.2"); 
    Inet4UnicastRouteEntry *rt = RouteGet("service-vrf1", service_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);

    std::vector<ComponentNHData> comp_nh_list;
    const VlanNH *vlan_nh = static_cast<const VlanNH *>(rt->GetActiveNextHop());
    ComponentNHData comp_nh_data(rt->GetMplsLabel(), vlan_nh->GetIfUuid(),
                                 vlan_nh->GetVlanTag(), false);
    comp_nh_list.push_back(comp_nh_data);

    //Leak a aggregarate route to service VRF
    AddRemoteEcmpRoute("service-vrf1", "10.1.1.0", 24, "vn10", 1, comp_nh_list);
    //Leak route for vm11 to service vrf
    AddLocalVmRoute("service-vrf1", "11.1.1.1", 32, "vn11", 11);
    client->WaitForIdle();
    Ip4Address vn10_agg_ip = Ip4Address::from_string("10.1.1.0");
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>
        (RouteGet("service-vrf1", vn10_agg_ip, 24)->GetActiveNextHop());
    uint32_t vnet13_vlan_nh = GetServiceVlanNH(13, "service-vrf1");

    //Choose some random source and destination port
    uint32_t sport = rand() % 65535;
    uint32_t dport = rand() % 65535;
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t hash_id = rand() % 65535;
        TxTcpPacket(VmPortGetId(13), "10.1.1.1", "11.1.1.1", sport, dport, 
                    false, hash_id, service_vrf_id);
        client->WaitForIdle();

        FlowEntry *entry = FlowGet(service_vrf_id,
                "10.1.1.1", "11.1.1.1", IPPROTO_TCP, sport, dport,
                vnet13_vlan_nh);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == service_vrf_id);
        //Packet destined to vm11, vrf has to be vrf 11
        EXPECT_TRUE(entry->data().dest_vrf == vrf_id);

        EXPECT_TRUE(entry->data().source_vn == "vn10");
        EXPECT_TRUE(entry->data().dest_vn == "vn11");

        FlowEntry *rev_entry = entry->reverse_flow_entry();
        const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
            (comp_nh->GetNH(rev_entry->data().component_nh_idx));
        EXPECT_TRUE(intf_nh->GetIfUuid() == MakeUuid(13));
        //Packet to service interface, vrf has to be 
        //service vlan VRF
        EXPECT_TRUE(rev_entry->data().vrf == vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().source_vn == "vn11");
        EXPECT_TRUE(rev_entry->data().dest_vn == "vn10");
        sport++;
        dport++;
    }

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet13");
    DeleteVmportEnv(input2, 1, true);
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->pkt()->flow_table()->Size() == 0);

    DeleteVmportEnv(input1, 1, true);
    DelVrf("service-vrf1");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf11"));
    EXPECT_FALSE(VrfFind("vrf13"));
    EXPECT_FALSE(VrfFind("service-vrf1"));
}

//Packet from a right service VM interface (one service instance on local server,
// one on remote server), reaching end host on a remote machine
TEST_F(EcmpTest,ServiceVlanTest_8) {
    //Add service VM in vrf13 as mgmt VRF
    struct PortInfo input2[] = {
        {"vnet13", 13, "1.1.1.252", "00:00:00:01:01:01", 13, 13},
    };
    CreateVmportWithEcmp(input2, 1);
    client->WaitForIdle();

    AddVrf("service-vrf1", 12);
    AddVmPortVrf("ser1", "11.1.1.2", 1);
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet13");
    client->WaitForIdle();

    uint32_t service_vrf_id = 
        Agent::GetInstance()->vrf_table()->FindVrfFromName("service-vrf1")->vrf_id();

    Ip4Address service_vm_ip = Ip4Address::from_string("11.1.1.2"); 
    Inet4UnicastRouteEntry *rt = RouteGet("service-vrf1", service_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);

    std::vector<ComponentNHData> comp_nh_list;
    const VlanNH *vlan_nh = static_cast<const VlanNH *>(rt->GetActiveNextHop());
    ComponentNHData comp_nh_data(rt->GetMplsLabel(), vlan_nh->GetIfUuid(),
                                 vlan_nh->GetVlanTag(), false);
    comp_nh_list.push_back(comp_nh_data);

    //Leak a aggregarate route to service VRF
    AddRemoteEcmpRoute("service-vrf1", "10.1.1.0", 24, "vn10", 1, comp_nh_list);
    //Leak route a for remote server to service vrf
    AddRemoteVmRoute("service-vrf1", "11.1.1.1", 32, "vn11");
    client->WaitForIdle();

    uint32_t vnet13_vlan_nh = GetServiceVlanNH(13, "service-vrf1");
    //Choose some random source and destination port
    uint32_t sport = rand() % 65535;
    uint32_t dport = rand() % 65535;
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t hash_id = rand() % 65535;
        TxTcpPacket(VmPortGetId(13), "10.1.1.1", "11.1.1.1", sport, dport, 
                    false, hash_id, service_vrf_id);
        client->WaitForIdle();

        FlowEntry *entry = FlowGet(service_vrf_id,
                "10.1.1.1", "11.1.1.1", IPPROTO_TCP, sport, dport,
                vnet13_vlan_nh);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == service_vrf_id);
        //Packet destined to remote server, vrf has to be service vrf
        EXPECT_TRUE(entry->data().dest_vrf == service_vrf_id);

        EXPECT_TRUE(entry->data().source_vn == "vn10");
        EXPECT_TRUE(entry->data().dest_vn == "vn11");

        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
        //Packet to service interface, vrf has to be 
        //service vlan VRF
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().source_vn == "vn11");
        EXPECT_TRUE(rev_entry->data().dest_vn == "vn10");
        sport++;
        dport++;
    }

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet13");
    DeleteVmportEnv(input2, 1, true);
    DelVrf("service-vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->pkt()->flow_table()->Size() == 0);
    EXPECT_FALSE(VrfFind("vrf13"));
    EXPECT_FALSE(VrfFind("service-vrf1"));
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
