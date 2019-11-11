/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"

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

IpamInfo ipam_info_2[] = {
    {"1.1.1.0", 24, "1.1.1.254"},
    {"2.1.1.0", 24, "2.1.1.254"},
};

IpamInfo ipam_info_3[] = {
    {"3.1.1.0", 24, "3.1.1.254"},
};

IpamInfo ipam_info_4[] = {
    {"4.1.1.0", 24, "4.1.1.254"},
};

class RemoteEcmpTest : public ::testing::Test {
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
        //                 -<vnet2, vrf2>----<vnet5,vrf3>
        // <vnet1, vrf1>----<vnet3, vrf2>----<vnet6,vrf3>----<vnet8, vrf4>
        //                 -<vnet4, vrf2>----<vnet7,vrf3>
        CreateVmportWithEcmp(input1, 1);
        CreateVmportWithEcmp(input2, 3);
        CreateVmportFIpEnv(input3, 3);
        CreateVmportFIpEnv(input4, 1);
        AddIPAM("vn2", ipam_info_2, 2);
        AddIPAM("default-project:vn3", ipam_info_3, 1);
        AddIPAM("default-project:vn4", ipam_info_4, 1);
        client->WaitForIdle();
        for (uint32_t i = 1; i < 9; i++) {
            EXPECT_TRUE(VmPortActive(i));
        }

        boost::system::error_code ec;
        bgp_peer = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                 "xmpp channel");
        client->WaitForIdle();

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
        InetUnicastRouteEntry *rt = RouteGet("default-project:vn3:vn3", ip, 32);
        EXPECT_TRUE(rt != NULL);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
        mpls_label_1 = rt->GetActiveLabel();

        ip = Ip4Address::from_string("2.1.1.1");
        rt = RouteGet("vrf2", ip, 32);
        EXPECT_TRUE(rt != NULL);
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
        mpls_label_2 = rt->GetActiveLabel();

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
        mpls_label_3 = rt->GetActiveLabel();

        //Populate ethernet interface id
        eth_intf_id_ = EthInterfaceGet("vnet0")->id();

        remote_vm_ip1_ = Ip4Address::from_string("2.2.2.2");
        remote_vm_ip2_ = Ip4Address::from_string("3.3.3.3");
        remote_vm_ip3_ = Ip4Address::from_string("4.4.4.4");
        remote_server_ip_ = Ip4Address::from_string("10.10.1.1");

        //Add couple of remote VM routes for generating packet
        Inet4TunnelRouteAdd(bgp_peer, "vrf2", remote_vm_ip1_, 32,
                            remote_server_ip_, TunnelType::GREType(),
                            30, "vn2", SecurityGroupList(),
                            TagList(),
                            PathPreference());

        Inet4TunnelRouteAdd(bgp_peer, "default-project:vn3:vn3",
                            remote_vm_ip2_, 32,
                            remote_server_ip_, TunnelType::GREType(),
                            30, "default-project:vn3", SecurityGroupList(),
                            TagList(), PathPreference());

        Inet4TunnelRouteAdd(bgp_peer, "default-project:vn4:vn4",
                            remote_vm_ip3_, 32,
                            remote_server_ip_, TunnelType::GREType(),
                            30, "default-project:vn4", SecurityGroupList(),
                            TagList(), PathPreference());
        client->WaitForIdle();
        FlowStatsTimerStartStop(agent_, true);
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    }

    virtual void TearDown() {
        FlushFlowTable();

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
        agent_->fabric_inet4_unicast_table()->DeleteReq(bgp_peer,
                "vrf2", remote_vm_ip1_, 32, new ControllerVmRoute(bgp_peer));
        agent_->fabric_inet4_unicast_table()->DeleteReq(bgp_peer,
                "default-project:vn3:vn3", remote_vm_ip2_, 32,
                new ControllerVmRoute(bgp_peer));
        agent_->fabric_inet4_unicast_table()->DeleteReq(bgp_peer,
                "default-project:vn4:vn4", remote_vm_ip3_, 32,
                new ControllerVmRoute(bgp_peer));

        DelIPAM("vn2");
        DelIPAM("default-project:vn3");
        DelIPAM("default-project:vn4");
        client->WaitForIdle();
        DeleteBgpPeer(bgp_peer);
        EXPECT_FALSE(VrfFind("vrf1", true));
        EXPECT_FALSE(VrfFind("vrf2", true));
        EXPECT_FALSE(VrfFind("default-project:vn3:vn3", true));
        EXPECT_FALSE(VrfFind("default-project:vn4:vn4", true));
        FlowStatsTimerStartStop(agent_, false);

        WAIT_FOR(1000, 1000, (agent_->vrf_table()->Size() == 2));
    }

public:
    FlowProto *get_flow_proto() const { return flow_proto_; }
    uint32_t GetServiceVlanNH(uint32_t intf_id, std::string vrf_name) const {
        const VmInterface *vm_intf = VmInterfaceGet(intf_id);
        const VrfEntry *vrf = VrfGet(vrf_name.c_str());
        uint32_t label = vm_intf->GetServiceVlanLabel(vrf);
        int nh_id = agent_->mpls_table()->
                        FindMplsLabel(label)->nexthop()->id();
        return nh_id;
    }

    void AddRemoteEcmpRoute(const string vrf_name, const string ip,
            uint32_t plen, const string vn, int count,
            ComponentNHKeyList local_list,
            bool same_label = false) {
        //If there is a local route, include that always
        Ip4Address vm_ip = Ip4Address::from_string(ip);

        ComponentNHKeyList comp_nh_list = local_list;

        int remote_server_ip = 0x0A0A0A0A;
        int label = 16;
        SecurityGroupList sg_id_list;

        for(int i = 0; i < count; i++) {
            ComponentNHKeyPtr comp_nh(new ComponentNHKey(
                        label, agent_->fabric_vrf_name(),
                        agent_->router_id(),
                        Ip4Address(remote_server_ip++),
                        false, TunnelType::GREType()));
            comp_nh_list.push_back(comp_nh);
            if (!same_label) {
                label++;
            }
        }
        EcmpTunnelRouteAdd(bgp_peer, vrf_name, vm_ip, plen,
                           comp_nh_list, -1, vn, sg_id_list, TagList(),
                           PathPreference());
    }

    void AddLocalVmRoute(const string vrf_name, const string ip, uint32_t plen,
                         const string vn, uint32_t intf_uuid) {
        const VmInterface *vm_intf = static_cast<const VmInterface *>
            (VmPortGet(intf_uuid));
        VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, MakeUuid(intf_uuid), "");
        VnListType vn_list;
        vn_list.insert(vn);
        LocalVmRoute *local_vm_route =
            new LocalVmRoute(intf_key, vm_intf->label(),
                             VxLanTable::kInvalidvxlan_id, false, vn_list,
                             InterfaceNHFlags::INET4, SecurityGroupList(),
                             TagList(), CommunityList(),
                             PathPreference(), Ip4Address(0),
                             EcmpLoadBalance(), false, false,
                             bgp_peer->sequence_number(), false, false);
        InetUnicastAgentRouteTable *rt_table =
            agent_->vrf_table()->GetInet4UnicastRouteTable(vrf_name);

        rt_table->AddLocalVmRouteReq(bgp_peer, vrf_name,
                  Ip4Address::from_string(ip), plen,
                  static_cast<LocalVmRoute *>(local_vm_route));
    }

    void AddRemoteVmRoute(const string vrf_name, const string ip, uint32_t plen,
                          const string vn) {
        const Ip4Address addr = Ip4Address::from_string(ip);
        VnListType vn_list;
        vn_list.insert(vn);
        ControllerVmRoute *data =
            ControllerVmRoute::MakeControllerVmRoute(bgp_peer,
                               agent_->fabric_vrf_name(), agent_->router_id(),
                               vrf_name, addr, TunnelType::GREType(), 16,
                               MacAddress(), vn_list, SecurityGroupList(), TagList(),
                               PathPreference(), false, EcmpLoadBalance(),
                               false);
        InetUnicastAgentRouteTable::AddRemoteVmRouteReq(bgp_peer,
            vrf_name, addr, plen, data);
    }

    void DeleteRemoteRoute(const string vrf_name, const string ip,
                               uint32_t plen) {
        Ip4Address server_ip = Ip4Address::from_string(ip);
        agent_->fabric_inet4_unicast_table()->DeleteReq(bgp_peer,
                vrf_name, server_ip, plen, new ControllerVmRoute(bgp_peer));
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
    Agent *agent_;
    FlowProto *flow_proto_;
};

// Ping from from fabric. Non-ECMP to ECMP
TEST_F(RemoteEcmpTest, Fabric_NonEcmpToEcmp_1) {
    //VIP of vrf2 interfaces
    char vm_ip[80] = "2.1.1.1";
    char router_id[80];
    char remote_server_ip[80];
    char remote_vm_ip[80];

    strcpy(router_id, agent_->router_id().to_string().c_str());
    strcpy(remote_server_ip, remote_server_ip_.to_string().c_str());
    strcpy(remote_vm_ip, remote_vm_ip1_.to_string().c_str());

    TxIpMplsPacket(eth_intf_id_, remote_server_ip, router_id, mpls_label_2,
                   remote_vm_ip, vm_ip, 1, 10);

    client->WaitForIdle();
    int nh_id = GetActiveLabel(mpls_label_2)->nexthop()->id();
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

// Ping from from fabric. Non-ECMP to ECMP
// delete first member and verify that no crash is
// seen in processing flow trap originated from remote server to
// local ecmp destination
TEST_F(RemoteEcmpTest, Fabric_NonEcmpToEcmp_2) {
    //VIP of vrf2 interfaces
    char vm_ip[80] = "2.1.1.1";
    char router_id[80];
    char remote_server_ip[80];
    char remote_vm_ip[80];

    strcpy(router_id, agent_->router_id().to_string().c_str());
    strcpy(remote_server_ip, remote_server_ip_.to_string().c_str());
    strcpy(remote_vm_ip, remote_vm_ip1_.to_string().c_str());

    DeleteVmportEnv(input2, 1, false);
    client->WaitForIdle();
    TxIpMplsPacket(eth_intf_id_, remote_server_ip, router_id, mpls_label_2,
                   remote_vm_ip, vm_ip, 1, 10);

    client->WaitForIdle();
    int nh_id = GetActiveLabel(mpls_label_2)->nexthop()->id();
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
// Ping from fabric.
// Non-ECMP to ECMP
// FIP DNAT case with source in vrf2 and dest in vrf1
TEST_F(RemoteEcmpTest, Fabric_DstFip_NonEcmpToEcmp_1) {
    //FIP of vrf3 interfaces
    char vm_ip[80] = "4.1.1.100";
    char router_id[80];
    char remote_server_ip[80];
    char remote_vm_ip[80];

    strcpy(router_id, agent_->router_id().to_string().c_str());
    strcpy(remote_server_ip, remote_server_ip_.to_string().c_str());
    strcpy(remote_vm_ip, remote_vm_ip3_.to_string().c_str());

    TxIpMplsPacket(eth_intf_id_, remote_server_ip, router_id, mpls_label_3,
                   remote_vm_ip, vm_ip, 1, 10);
    client->WaitForIdle();

    int nh_id = GetActiveLabel(mpls_label_3)->nexthop()->id();
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

// Non-ECMP to ECMP
// FIP DNAT case with source in vrf2 and dest in vrf1
// Ensure that flows are distributed across all members
TEST_F(RemoteEcmpTest, Fabric_DstFip_NonEcmpToEcmp_2) {
    //FIP of vrf3 interfaces
    char vm_ip[80] = "4.1.1.100";
    char router_id[80];
    char remote_server_ip[80];
    char remote_vm_ip[80];

    strcpy(router_id, agent_->router_id().to_string().c_str());
    strcpy(remote_server_ip, remote_server_ip_.to_string().c_str());
    strcpy(remote_vm_ip, remote_vm_ip3_.to_string().c_str());

    uint8_t ecmp_flow_count[3] = { 0, 0, 0 };
    for (int i = 0; i < 32; i++) {
        TxUdpMplsPacket(eth_intf_id_, remote_server_ip, router_id, mpls_label_3,
                       remote_vm_ip, vm_ip, 1, 100+i);
        client->WaitForIdle();

        int nh_id = GetActiveLabel(mpls_label_3)->nexthop()->id();
        FlowEntry *entry = FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(),
                                   remote_vm_ip, vm_ip, 17, 1, 100+i, nh_id);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx !=
                    CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->is_flags_set(FlowEntry::NatFlow) == true);

        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);

        if (entry->data().component_nh_idx !=
            CompositeNH::kInvalidComponentNHIdx) {
            ecmp_flow_count[entry->data().component_nh_idx]++;
        }
    }

    for (int i = 0; i < 3; i++) {
        EXPECT_TRUE(ecmp_flow_count[i] != 0);
    }
}

// Ping from fabric.
// ECMP to Non-ECMP
// FIP DNAT case with source in vrf2 and dest in vrf1
TEST_F(RemoteEcmpTest, Fabric_DstFip_EcmpToNonEcmp_1) {
    Ip4Address ip = Ip4Address::from_string("30.30.30.0");
    ComponentNHKeyList comp_nh;
    Ip4Address server_ip1 = Ip4Address::from_string("15.15.15.15");
    Ip4Address server_ip2 = Ip4Address::from_string("15.15.15.16");
    Ip4Address server_ip3 = Ip4Address::from_string("15.15.15.17");

    ComponentNHKeyPtr comp_nh_data1(new ComponentNHKey(
        16, agent_->fabric_vrf_name(), agent_->router_id(), server_ip1, false,
        TunnelType::GREType()));
    comp_nh.push_back(comp_nh_data1);

    ComponentNHKeyPtr comp_nh_data2(new ComponentNHKey(
        17, agent_->fabric_vrf_name(), agent_->router_id(), server_ip2, false,
        TunnelType::GREType()));
    comp_nh.push_back(comp_nh_data2);

    ComponentNHKeyPtr comp_nh_data3(new ComponentNHKey(
        18, agent_->fabric_vrf_name(), agent_->router_id(), server_ip3, false,
        TunnelType::GREType()));
    comp_nh.push_back(comp_nh_data3);

    SecurityGroupList sg_list;
    EcmpTunnelRouteAdd(bgp_peer, "vrf2", ip, 24, comp_nh, -1, "vn2", sg_list,
                       TagList(), PathPreference());
    client->WaitForIdle();

    //VIP of vrf2 interfaces
    char vm_ip[80] = "1.1.1.1";
    char router_id[80];
    char remote_server_ip[80];
    char remote_vm_ip[80];

    strcpy(router_id, agent_->router_id().to_string().c_str());
    strcpy(remote_server_ip, "15.15.15.16");
    strcpy(remote_vm_ip, "30.30.30.1");

    const VmInterface *vintf =
        static_cast<const VmInterface *>(VmPortGet(1));
    TxIpMplsPacket(eth_intf_id_, remote_server_ip, router_id, vintf->label(),
                   remote_vm_ip, vm_ip, 1, 10);

    client->WaitForIdle();
    int nh_id = GetActiveLabel(vintf->label())->nexthop()->id();
    FlowEntry *entry = FlowGet(VrfGet("vrf2")->vrf_id(),
                               remote_vm_ip, vm_ip, 1, 0, 0, nh_id);
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);

    //Reverse flow should be set and should also be ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    DeleteRemoteRoute("vrf2", ip.to_string(), 24);
    client->WaitForIdle();
}

// Ping from VMI to Fabric
// Source interface vmi-21 has FIP in vrf2
// Dest on fabric and has ECMP route with 2 nexthops (10.10.10.100 and
// 10.10.10.101)
//
// After flow setup, the reverse flow moves from 10.10.10.100 to 10.10.10.101
// or vice-versa
//
// In normal scenario, VRouter should take care of this flow-movement. However
// agent can come into picture if the flow is already evicted in vrouter
TEST_F(RemoteEcmpTest, Vmi_DstFip_NonEcmpToEcmp_FlowMove_1) {
    struct PortInfo input1[] = {
        {"vnet9", 9, "9.1.1.1", "00:00:00:01:01:01", 9, 9},
    };
    CreateVmportWithEcmp(input1, 1);
    IpamInfo ipam_info_9[] = {
        {"9.1.1.0", 24, "9.1.1.254"},
    };
    AddIPAM("vn9", ipam_info_9, 1);
    client->WaitForIdle();

    VmInterface *intf = static_cast<VmInterface *>(VmPortGet(9));
    uint32_t label = intf->label();

    AddVn("default-project:vn10", 10);
    AddVrf("default-project:vn10:vn10", 10);
    AddLink("virtual-network", "default-project:vn10",
            "routing-instance", "default-project:vn10:vn10");
    AddFloatingIpPool("fip-pool9", 10);
    AddFloatingIp("fip9", 10, "10.10.10.2");
    AddLink("floating-ip", "fip9", "floating-ip-pool", "fip-pool9");
    AddLink("floating-ip-pool", "fip-pool9", "virtual-network",
            "default-project:vn10");
    AddLink("virtual-machine-interface", "vnet9", "floating-ip", "fip9");
    client->WaitForIdle();

    Ip4Address gw_rt = Ip4Address::from_string("0.0.0.0");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);
    EcmpTunnelRouteAdd(bgp_peer, "default-project:vn10:vn10", gw_rt, 0,
                       comp_nh_list, -1, "default-project:vn10",
                       SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();

    TxIpPacket(VmPortGetId(9), "9.1.1.1", "10.1.1.1", 1);
    client->WaitForIdle();
    FlowEntry *entry;
    entry = FlowGet(VrfGet("default-project:vn10:vn10")->vrf_id(),
                    "9.1.1.1", "10.1.1.1", 1, 0, 0, GetFlowKeyNH(9));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->is_flags_set(FlowEntry::ShortFlow) == false);

    uint32_t reverse_index = entry->reverse_flow_entry()->flow_handle();
    if (entry->data().component_nh_idx == 0) {
        // If forward flow setup to go on 10.10.10.100, trap reverse flow
        // from 10.10.10.101
        TxIpMplsPacket(eth_intf_id_, "10.10.10.101",
                       agent_->router_id().to_string().c_str(),
                       label, "10.1.1.1", "10.10.10.2", 1, reverse_index);
        client->WaitForIdle();
        // "entry" is new reverse flow now.
        // VRouter manages its ECMP member index
        EXPECT_TRUE(entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
    } else {
        // If forward flow setup to go on 10.10.10.101, trap reverse flow
        // from 10.10.10.100
        TxIpMplsPacket(eth_intf_id_, "10.10.10.100",
                       agent_->router_id().to_string().c_str(),
                       label, "10.1.1.1", "10.10.10.2", 1, reverse_index);
        client->WaitForIdle();
        // "entry" is new reverse flow now.
        // VRouter manages its ECMP member index
        EXPECT_TRUE(entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
    }

    DeleteVmportEnv(input1, 1, true);
    DeleteRemoteRoute("default-project:vn10:vn10", "0.0.0.0", 0);
    DelLink("floating-ip", "fip9", "floating-ip-pool", "fip-pool9");
    DelLink("floating-ip-pool", "fip-pool9",
            "virtual-network", "default-project:vn10");
    DelLink("virtual-machine-interface", "vnet9", "floating-ip", "fip9");
    DelFloatingIp("fip9");
    DelFloatingIpPool("fip-pool9");
    client->WaitForIdle();
    DelVrf("default-project:vn10:vn10");
    DelVn("default-project:vn10");
    client->WaitForIdle();
    WAIT_FOR(100, 1000, VrfFind("default-project:vn10:vn10", true) == false);
    client->WaitForIdle();
    EXPECT_TRUE(get_flow_proto()->FlowCount() == 0);
    EXPECT_FALSE(VrfFind("vrf9", true));
    DelIPAM("vn9");
}

// Add a test case to check if rpf NH of flow using floating IP
//is set properly upon nexthop change of from 2 destination to 3
//destinations
TEST_F(RemoteEcmpTest, Vmi_EcmpTest_13) {
    struct PortInfo input1[] = {
        {"vnet9", 9, "9.1.1.1", "00:00:00:01:01:01", 9, 9},
    };
    CreateVmportWithEcmp(input1, 1);
    IpamInfo ipam_info_9[] = {
        {"9.1.1.0", 24, "9.1.1.254"},
    };
    AddIPAM("vn9", ipam_info_9, 1);
    client->WaitForIdle();

    VmInterface *intf = static_cast<VmInterface *>(VmPortGet(9));
    uint32_t label = intf->label();

    AddVn("default-project:vn10", 10);
    AddVrf("default-project:vn10:vn10", 10);
    AddLink("virtual-network", "default-project:vn10",
            "routing-instance", "default-project:vn10:vn10");
    AddFloatingIpPool("fip-pool9", 10);
    AddFloatingIp("fip9", 10, "10.10.10.2");
    AddLink("floating-ip", "fip9", "floating-ip-pool", "fip-pool9");
    AddLink("floating-ip-pool", "fip-pool9", "virtual-network",
            "default-project:vn10");
    AddLink("virtual-machine-interface", "vnet9", "floating-ip", "fip9");
    client->WaitForIdle();

    Ip4Address gw_rt = Ip4Address::from_string("0.0.0.0");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    Ip4Address remote_server_ip3 = Ip4Address::from_string("10.10.10.102");
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);
    EcmpTunnelRouteAdd(bgp_peer, "default-project:vn10:vn10", gw_rt, 0,
                       comp_nh_list, false, "default-project:vn10",
                       SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();
    InetUnicastRouteEntry *rt = RouteGet("default-project:vn10:vn10", gw_rt, 0);

    TxIpPacket(VmPortGetId(9), "9.1.1.1", "10.1.1.1", 1);
    client->WaitForIdle();
    FlowEntry *entry;
    entry = FlowGet(VrfGet("default-project:vn10:vn10")->vrf_id(),
                    "9.1.1.1", "10.1.1.1", 1, 0, 0, GetFlowKeyNH(9));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->is_flags_set(FlowEntry::ShortFlow) == false);

    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->rpf_nh() == rt->GetActiveNextHop());

    uint32_t reverse_index = entry->reverse_flow_entry()->flow_handle();
    if (entry->data().component_nh_idx == 0) {
        // If forward flow setup to go on 10.10.10.100, trap reverse flow
        // from 10.10.10.101
        TxIpMplsPacket(eth_intf_id_, "10.10.10.101",
                       agent_->router_id().to_string().c_str(),
                       label, "10.1.1.1", "10.10.10.2", 1, reverse_index);
        client->WaitForIdle();
        EXPECT_TRUE(entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
    } else {
        // If forward flow setup to go on 10.10.10.101, trap reverse flow
        // from 10.10.10.100
        TxIpMplsPacket(eth_intf_id_, "10.10.10.100",
                       agent_->router_id().to_string().c_str(),
                       label, "10.1.1.1", "10.10.10.2", 1, reverse_index);
        client->WaitForIdle();
        EXPECT_TRUE(entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
    }

    //Update the route to make the composite NH point to new nexthop
    ComponentNHKeyPtr nh_data3(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip3,
                                                  false,
                                                  TunnelType::DefaultType()));
    comp_nh_list.push_back(nh_data3);
    EcmpTunnelRouteAdd(bgp_peer, "default-project:vn10:vn10", gw_rt, 0,
                       comp_nh_list, false, "default-project:vn10",
                       SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();
    EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    //Make sure flow has the right nexthop set.
    rt = RouteGet("default-project:vn10:vn10", gw_rt, 0);
    rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->rpf_nh() == rt->GetActiveNextHop());

    DeleteVmportEnv(input1, 1, true);
    DeleteRemoteRoute("default-project:vn10:vn10", "0.0.0.0", 0);
    DelLink("floating-ip", "fip9", "floating-ip-pool", "fip-pool9");
    DelLink("floating-ip-pool", "fip-pool9",
            "virtual-network", "default-project:vn10");
    DelLink("virtual-machine-interface", "vnet9", "floating-ip", "fip9");
    DelFloatingIp("fip9");
    DelFloatingIpPool("fip-pool9");
    client->WaitForIdle();
    DelVrf("default-project:vn10:vn10");
    DelVn("default-project:vn10");
    client->WaitForIdle();
    WAIT_FOR(100, 1000, VrfFind("default-project:vn10:vn10", true) == false);
    client->WaitForIdle();
    EXPECT_TRUE(get_flow_proto()->FlowCount() == 0);
    EXPECT_FALSE(VrfFind("vrf9", true));
    DelIPAM("vn9");
}

//Add a test case to check if rpf NH of flow using floating IP
//gets properly upon nexthop change from ecmp to unicast
TEST_F(RemoteEcmpTest, EcmpTest_14) {
    struct PortInfo input1[] = {
        {"vnet9", 9, "9.1.1.1", "00:00:00:01:01:01", 9, 9},
    };
    CreateVmportWithEcmp(input1, 1);
    IpamInfo ipam_info_9[] = {
        {"9.1.1.0", 24, "9.1.1.254"},
    };
    AddIPAM("vn9", ipam_info_9, 1);
    client->WaitForIdle();

    AddVn("default-project:vn10", 10);
    AddVrf("default-project:vn10:vn10", 10);
    AddLink("virtual-network", "default-project:vn10",
            "routing-instance", "default-project:vn10:vn10");
    AddFloatingIpPool("fip-pool9", 10);
    AddFloatingIp("fip9", 10, "10.10.10.2");
    AddLink("floating-ip", "fip9", "floating-ip-pool", "fip-pool9");
    AddLink("floating-ip-pool", "fip-pool9", "virtual-network",
            "default-project:vn10");
    AddLink("virtual-machine-interface", "vnet9", "floating-ip", "fip9");
    client->WaitForIdle();

    Ip4Address gw_rt = Ip4Address::from_string("0.0.0.0");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);
    EcmpTunnelRouteAdd(bgp_peer, "default-project:vn10:vn10", gw_rt, 0,
                       comp_nh_list, false, "default-project:vn10",
                       SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();

    TxIpPacket(VmPortGetId(9), "9.1.1.1", "10.1.1.1", 1);
    client->WaitForIdle();
    FlowEntry *entry;
    entry = FlowGet(VrfGet("default-project:vn10:vn10")->vrf_id(),
                    "9.1.1.1", "10.1.1.1", 1, 0, 0, GetFlowKeyNH(9));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->is_flags_set(FlowEntry::ShortFlow) == false);

    //Update the route to make the remote destination unicast
    Inet4TunnelRouteAdd(bgp_peer, "default-project:vn10:vn10", gw_rt, 0,
                        Ip4Address::from_string("8.8.8.8"),
                        TunnelType::ComputeType(TunnelType::MplsType()),
                        100, "default-project:vn10", SecurityGroupList(),
                        TagList(), PathPreference());
    client->WaitForIdle();
    //Make sure flow has the right nexthop set.
    InetUnicastRouteEntry *rt = RouteGet("default-project:vn10:vn10", gw_rt, 0);
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->rpf_nh() == rt->GetActiveNextHop());

    DeleteVmportEnv(input1, 1, true);
    DeleteRemoteRoute("default-project:vn10:vn10", "0.0.0.0", 0);
    DelLink("floating-ip", "fip9", "floating-ip-pool", "fip-pool9");
    DelLink("floating-ip-pool", "fip-pool9",
            "virtual-network", "default-project:vn10");
    DelLink("virtual-machine-interface", "vnet9", "floating-ip", "fip9");
    DelFloatingIp("fip9");
    DelFloatingIpPool("fip-pool9");
    client->WaitForIdle();
    DelVrf("default-project:vn10:vn10");
    DelVn("default-project:vn10");
    client->WaitForIdle();
    WAIT_FOR(100, 1000, VrfFind("default-project:vn10:vn10", true) == false);
    client->WaitForIdle();
    EXPECT_TRUE(get_flow_proto()->FlowCount() == 0);
    EXPECT_FALSE(VrfFind("vrf9", true));
    DelIPAM("vn9");
}

TEST_F(RemoteEcmpTest, EcmpTest_15) {
    struct PortInfo input1[] = {
        {"vnet9", 9, "9.1.1.1", "00:00:00:01:01:01", 9, 9},
    };
    CreateVmportWithEcmp(input1, 1);
    IpamInfo ipam_info_9[] = {
        {"9.1.1.0", 24, "9.1.1.254"},
    };
    AddIPAM("vn9", ipam_info_9, 1);
    client->WaitForIdle();

    Ip4Address gw_rt = Ip4Address::from_string("0.0.0.0");
    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    Ip4Address remote_server_ip2 = Ip4Address::from_string("10.10.10.101");
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip2,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);
    EcmpTunnelRouteAdd(bgp_peer, "vrf9", gw_rt, 0,
                       comp_nh_list, false, "vn9",
                       SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();

    TxIpPacket(VmPortGetId(9), "9.1.1.1", "10.1.1.1", 1);
    client->WaitForIdle();
    FlowEntry *entry;
    entry = FlowGet(VrfGet("vrf9")->vrf_id(),
                    "9.1.1.1", "10.1.1.1", 1, 0, 0, GetFlowKeyNH(9));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->is_flags_set(FlowEntry::ShortFlow) == false);

    //Update the route to make the remote destination unicast
    Inet4TunnelRouteAdd(bgp_peer, "vrf9", gw_rt, 0,
                        Ip4Address::from_string("8.8.8.8"),
                        TunnelType::ComputeType(TunnelType::MplsType()),
                        100, "vn9", SecurityGroupList(),
                        TagList(), PathPreference());
    client->WaitForIdle();
    //Make sure flow has the right nexthop set.
    InetUnicastRouteEntry *rt = RouteGet("vrf9", gw_rt, 0);
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->rpf_nh() == rt->GetActiveNextHop());

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_TRUE(get_flow_proto()->FlowCount() == 0);
    EXPECT_FALSE(VrfFind("vrf9", true));
    DelIPAM("vn9");
}

//In case of ECMP route leaked across VRF, there might
//not be local VM peer path in all the VRF, in that case
//we are picking local component NH from composite NH
//Since those component NH are policy disabled, flow
//calculation should pick policy enabled local NH for
//flow key calculation. This test case verifies the same
TEST_F(RemoteEcmpTest, EcmpTest_16) {
    AddVrf("vrf9");
    client->WaitForIdle();

    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    //Add default route in vrf 9 pointing to remote VM
    AddRemoteVmRoute("vrf9", "0.0.0.0", 0, "vn3");
    AddRemoteVmRoute("vrf2", "0.0.0.0", 0, "vn3");
    client->WaitForIdle();

    VmInterface *intf = static_cast<VmInterface *>(VmPortGet(1));
    uint32_t label = intf->label();
    //Add a ECMP route for traffic origiator
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(label, MakeUuid(1),
                                                  InterfaceNHFlags::INET4,
                                                  intf->vm_mac()));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);
    //Delete local VM peer route for 1.1.1.1 to simulate
    //error case, local component NH would be picked from
    //composite NH, than from local vm peer
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    agent_->fabric_inet4_unicast_table()->DeleteReq(intf->peer(), "vrf2",
                                                   ip, 32, NULL);
    EcmpTunnelRouteAdd(bgp_peer, "vrf9", ip, 32,
                       comp_nh_list, false, "vn3",
                       SecurityGroupList(), TagList(), PathPreference());
    EcmpTunnelRouteAdd(bgp_peer, "vrf2", ip, 32,
                       comp_nh_list, false, "vn2",
                       SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();

    //Add a vrf assign ACL to vn1, so that traffic is forwarded via
    //vrf 9
    AddVrfAssignNetworkAcl("Acl", 10, "vn2", "vn3", "pass", "vrf9");
    AddLink("virtual-network", "vn2", "access-control-list", "Acl");
    client->WaitForIdle();

    TxIpPacket(VmPortGetId(1), "1.1.1.1", "10.1.1.1", 1);
    client->WaitForIdle();

    FlowEntry *entry;
    FlowEntry *rev_entry;
    entry = FlowGet(VrfGet("vrf2")->vrf_id(),
                    "1.1.1.1", "10.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx  ==
                CompositeNH::kInvalidComponentNHIdx);

    rev_entry = FlowGet(VrfGet("vrf9")->vrf_id(),
                       "10.1.1.1", "1.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(rev_entry != NULL);
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);

    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "access-control-list", "Acl");
    DelNode("access-control-list", "Acl");

    DeleteRemoteRoute("vrf9", "1.1.1.1", 32);
    DeleteRemoteRoute("vrf9", "0.0.0.0", 0);
    DeleteRemoteRoute("vrf2", "0.0.0.0", 0);
    client->WaitForIdle();
    DelVrf("vrf9");
    client->WaitForIdle();
}

TEST_F(RemoteEcmpTest, INVALID_EcmpTest_17) {
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
    EXPECT_TRUE(rev_entry->rpf_nh() != NULL);

    AddRemoteVmRoute("vrf2", "2.1.1.1", 32, "vn10");
    client->WaitForIdle();
    entry = FlowGet(VrfGet("vrf2")->vrf_id(),
                    "1.1.1.1", "2.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry->is_flags_set(FlowEntry::ShortFlow) == false);
}

TEST_F(RemoteEcmpTest, DISABLED_EcmpReEval_1) {
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
    TxIpPacket(VmPortGetId(1), "1.1.1.1", "2.1.1.1", 1);
    client->WaitForIdle();
    //Upon interface deletion flow would have been deleted, get flow again
    FlowEntry *entry2 = FlowGet(VrfGet("vrf2")->vrf_id(),
            "1.1.1.1", "2.1.1.1", 1, 0, 0, GetFlowKeyNH(1));

    //Verify compoennt NH index is different
    EXPECT_TRUE(entry2->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
    //make sure new ecmp index is different from that of old ecmp index
    EXPECT_TRUE(ecmp_index != entry2->data().component_nh_idx);
}

//Send a flow for VM to non ECMP dip
//Change the nexthop pointed by non ECMP dip to ECMP dip
//Check if re-evaluation happens
TEST_F(RemoteEcmpTest, EcmpReEval_2) {
    //Add a remote VM route for 3.1.1.10
    Ip4Address remote_vm_ip = Ip4Address::from_string("3.1.1.10");
    Ip4Address remote_server_ip = Ip4Address::from_string("10.10.10.10");
    Inet4TunnelRouteAdd(bgp_peer, "vrf2",remote_vm_ip, 32,
                        remote_server_ip, TunnelType::GREType(), 16, "vn2",
                        SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();
    TxIpPacket(VmPortGetId(1), "1.1.1.1", "3.1.1.10", 1);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("vrf2")->vrf_id(),
            "1.1.1.1", "3.1.1.10", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);

    //Convert dip to ECMP nh
    ComponentNHKeyList local_comp_nh;
    AddRemoteEcmpRoute("vrf2", "3.1.1.10", 32, "vn2", 2, local_comp_nh);
    client->WaitForIdle();

    //Enqueue a re-evaluate request
    TxIpPacket(VmPortGetId(1), "1.1.1.1", "3.1.1.10", 1);
    client->WaitForIdle();
    entry = FlowGet(VrfGet("vrf2")->vrf_id(),
            "1.1.1.1", "3.1.1.10", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    //Since flow already existed, use same old NH which would be at index 0
    EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);

    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    DeleteRemoteRoute("vrf2", "3.1.1.10", 32);
}

//Send a flow for VM to non ECMP dip
//Change the nexthop pointed by non ECMP dip to ECMP dip
//Check if re-evaluation happens
TEST_F(RemoteEcmpTest, EcmpReEval_3) {
    struct PortInfo input1[] = {
        {"vnet10", 10, "10.1.1.1", "00:00:00:01:01:01", 10, 10},
        {"vnet11", 11, "11.1.1.1", "00:00:00:01:01:01", 10, 11},
    };
    CreateVmportFIpEnv(input1, 2);
    IpamInfo ipam_info_10[] = {
        {"10.1.1.0", 24, "10.1.1.254"},
        {"11.1.1.0", 24, "11.1.1.254"},
    };
    AddIPAM("vn10", ipam_info_10, 2);
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
    TxIpPacket(VmPortGetId(1), "1.1.1.1", "3.1.1.10", 1);
    client->WaitForIdle();
    entry = FlowGet(VrfGet("vrf2")->vrf_id(), "1.1.1.1", "3.1.1.10", 1, 0, 0,
                    GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);

    DelLink("virtual-machine-interface", "vnet10", "floating-ip", "fip3");
    DelLink("virtual-machine-interface", "vnet11", "floating-ip", "fip3");
    DelLink("floating-ip-pool", "fip-pool3", "virtual-network", "vn2");
    client->WaitForIdle();
    DeleteVmportFIpEnv(input1, 2, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vn10:vn10"));
    DelIPAM("vn10");
}

//Send a packet from vgw to ecmp destination
TEST_F(RemoteEcmpTest, DISABLE_VgwFlag) {
    InetInterface::CreateReq(agent_->interface_table(), "vgw1",
                            InetInterface::SIMPLE_GATEWAY, "vrf2",
                            Ip4Address(0), 0, Ip4Address(0), Agent::NullString(),
                            "", Interface::TRANSPORT_ETHERNET);
    client->WaitForIdle();

    InetInterfaceKey *intf_key = new InetInterfaceKey("vgw1");
    InetInterface *intf = InetInterfaceGet("vgw1");
    std::auto_ptr<const NextHopKey> nh_key(new InterfaceNHKey(intf_key, false,
                                                              InterfaceNHFlags::INET4,
                                                              intf->mac()));

    Ip4Address remote_server_ip1 = Ip4Address::from_string("10.10.10.100");
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(16, nh_key));
    ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  remote_server_ip1,
                                                  false,
                                                  TunnelType::DefaultType()));
    Ip4Address ip = Ip4Address::from_string("0.0.0.0");
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(nh_data1);
    comp_nh_list.push_back(nh_data2);
    EcmpTunnelRouteAdd(bgp_peer, "vrf2", ip, 0,
                       comp_nh_list, false, "vn2",
                       SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();

    //Send packet on vgw interface
    TxIpPacket(intf->id(), "100.1.1.1", "2.2.2.2", 1);
    client->WaitForIdle();

    FlowEntry *entry;
    FlowEntry *rev_entry;
    entry = FlowGet(VrfGet("vrf2")->vrf_id(),
                    "2.2.2.2", "100.1.1.1", 1, 0, 0, intf->flow_key_nh()->id());
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx  ==
                CompositeNH::kInvalidComponentNHIdx);

    rev_entry = FlowGet(VrfGet("vrf2")->vrf_id(),
                       "100.1.1.1", "2.2.2.2", 1, 0, 0, intf->flow_key_nh()->id());
    EXPECT_TRUE(rev_entry != NULL);
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);

    client->WaitForIdle();
    agent_->fabric_inet4_unicast_table()->DeleteReq(bgp_peer, "vrf2",
               ip, 0, new ControllerVmRoute(bgp_peer));
    InetInterface::DeleteReq(agent_->interface_table(), "vgw1");
    client->WaitForIdle();
}

// Packet from non-ECMP interface to ECMP Interface
// Send packet from ECMP VM to ECMP MX
// Verify component index is set and correspnding rpf nexthop
TEST_F(RemoteEcmpTest, DISABLED_EcmpTest_1) {
    AddRemoteEcmpRoute("vrf1", "0.0.0.0", 0, "vn1", 4, ComponentNHKeyList());
    AddRemoteEcmpRoute("vrf1", "1.1.1.1", 32, "vn1", 4, ComponentNHKeyList());

    TxIpPacket(VmPortGetId(1), "1.1.1.1", "2.1.1.1", 1);
    client->WaitForIdle();

    AgentRoute *rt = RouteGet("vrf1", Ip4Address::from_string("0.0.0.0"), 0);
    InetUnicastRouteEntry *src_rt = static_cast<InetUnicastRouteEntry *>(
        RouteGet("vrf1", Ip4Address::from_string("1.1.1.1"), 32));

    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
                               "1.1.1.1", "2.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().rpf_nh.get() == EcmpData::GetLocalNextHop(src_rt));

    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().rpf_nh.get() == rt->GetActiveNextHop());

    DeleteRoute("vrf1", "0.0.0.0", 0, bgp_peer);
    client->WaitForIdle();
    sleep(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Send packet from ECMP of AAP and verify ECMP index and
//RPF nexthop is set fine
TEST_F(RemoteEcmpTest, DISABLED_EcmpTest_2) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.10");
    std::string mac("0a:0b:0c:0d:0e:0f");

    AddEcmpAap("vnet1", 1, ip, mac);
    AddEcmpAap("vnet2", 2, ip, mac);
    AddRemoteEcmpRoute("vrf1", "0.0.0.0", 0, "vn1", 4, ComponentNHKeyList());
    client->WaitForIdle();

    TxIpPacket(VmPortGetId(1), "1.1.1.10", "2.1.1.1", 1);
    client->WaitForIdle();

    AgentRoute *rt = RouteGet("vrf1", Ip4Address::from_string("0.0.0.0"), 0);
    InetUnicastRouteEntry *src_rt = static_cast<InetUnicastRouteEntry *>(
            RouteGet("vrf1", Ip4Address::from_string("1.1.1.10"), 32));

    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
                               "1.1.1.10", "2.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().rpf_nh.get() == EcmpData::GetLocalNextHop(src_rt));

    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().rpf_nh.get() == rt->GetActiveNextHop());

    DeleteRoute("vrf1", "0.0.0.0", 0, bgp_peer);
    client->WaitForIdle();
    sleep(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
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
