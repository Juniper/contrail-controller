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

class EcmpTest : public ::testing::Test {
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
                            PathPreference());

        Inet4TunnelRouteAdd(bgp_peer, "default-project:vn3:vn3",
                            remote_vm_ip2_, 32,
                            remote_server_ip_, TunnelType::GREType(),
                            30, "default-project:vn3", SecurityGroupList(),
                            PathPreference());

        Inet4TunnelRouteAdd(bgp_peer, "default-project:vn4:vn4",
                            remote_vm_ip3_, 32,
                            remote_server_ip_, TunnelType::GREType(),
                            30, "default-project:vn4", SecurityGroupList(),
                            PathPreference());
        client->WaitForIdle();
        FlowStatsTimerStartStop(agent_, true);
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
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

        WAIT_FOR(1000, 1000, (agent_->vrf_table()->Size() == 1));
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
                           comp_nh_list, -1, vn, sg_id_list,
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
                             CommunityList(), PathPreference(), Ip4Address(0),
                             EcmpLoadBalance(), false, false,
                             bgp_peer->sequence_number(), false);
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
                               vn_list, SecurityGroupList(),
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

TEST_F(EcmpTest, ServiceVlanTest_1) {
    struct PortInfo input1[] = {
        {"vnet10", 10, "10.1.1.1", "00:00:00:01:01:01", 10, 10},
        {"vnet11", 11, "11.1.1.1", "00:00:00:01:01:01", 10, 11},
        {"vnet12", 12, "12.1.1.1", "00:00:00:01:01:01", 10, 12},
    };

    CreateVmportWithEcmp(input1, 3);
    IpamInfo ipam_info_10[] = {
        {"10.1.1.0", 24, "10.1.1.254"},
        {"11.1.1.0", 24, "11.1.1.254"},
        {"12.1.1.0", 24, "12.1.1.254"},
    };
    AddIPAM("vn10", ipam_info_10, 3);

    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(10));
    EXPECT_TRUE(VmPortActive(11));
    EXPECT_TRUE(VmPortActive(11));

    //Add service VRF and VN
    struct PortInfo input2[] = {
        {"vnet13", 13, "11.1.1.252", "00:00:00:01:01:01", 11, 13},
    };
    CreateVmportWithEcmp(input2, 1);
    IpamInfo ipam_info_11[] = {
        {"11.1.1.0", 24, "11.1.1.254"},
    };
    AddIPAM("vn11", ipam_info_11, 1);
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
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
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
    EXPECT_FALSE(VrfFind("vrf11"));
    EXPECT_FALSE(VrfFind("vrf10"));

    DelIPAM("vn10");
    DelIPAM("vn11");
}

//Service VM with ECMP instantiated in two 
//different remote server
//Packet sent from a VM instance to service VM instance
TEST_F(EcmpTest, ServiceVlanTest_2) {
    struct PortInfo input1[] = {
        {"vnet10", 10, "10.1.1.1", "00:00:00:01:01:01", 10, 10},
    };
    CreateVmportWithEcmp(input1, 1);
    IpamInfo ipam_info_10[] = {
        {"10.1.1.0", 24, "10.1.1.254"},
    };
    AddIPAM("vn10", ipam_info_10, 1);
    client->WaitForIdle();

    ComponentNHKeyList local_comp_nh;
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
    std::string vn_name_10("vn10");
    std::string vn_name_11("vn11");
    EXPECT_TRUE(VnMatch(entry->data().source_vn_list, vn_name_10));
    EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, vn_name_11));

    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().vrf == vrf_id);
    EXPECT_TRUE(rev_entry->data().dest_vrf == vrf_id);
    EXPECT_TRUE(VnMatch(rev_entry->data().source_vn_list, vn_name_11));
    EXPECT_TRUE(VnMatch(rev_entry->data().dest_vn_list, vn_name_10));

    DeleteVmportEnv(input1, 1, true);
    DeleteRemoteRoute("vrf10", "11.1.1.0", 24);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf10"));
    DelIPAM("vn10");
}

//Service VM with ECMP instantiated in local and remote server
//Packet sent from a VM instance to service VM instance
//Packet sent from a remote VM to service instance
TEST_F(EcmpTest, ServiceVlanTest_3) {
    struct PortInfo input1[] = {
        {"vnet10", 10, "10.1.1.1", "00:00:00:01:01:01", 10, 10},
    };
    CreateVmportWithEcmp(input1, 1);
    IpamInfo ipam_info_10[] = {
        {"10.1.1.0", 24, "10.1.1.254"},
    };
    AddIPAM("vn10", ipam_info_10, 1);
    client->WaitForIdle();

    //Add service VM in vrf11 as mgmt VRF
    struct PortInfo input2[] = {
        {"vnet11", 11, "1.1.1.252", "00:00:00:01:01:01", 11, 11},
    };
    CreateVmportWithEcmp(input2, 1);
    IpamInfo ipam_info_11[] = {
        {"11.1.1.0", 24, "11.1.1.254"},
    };
    AddIPAM("vn11", ipam_info_11, 1);
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
    InetUnicastRouteEntry *rt = RouteGet("service-vrf1", service_vm_ip, 32);
    ComponentNHKeyList comp_nh_list;
    EXPECT_TRUE(rt != NULL);
    const VlanNH *vlan_nh = static_cast<const VlanNH *>(rt->GetActiveNextHop());
    ComponentNHKeyPtr comp_nh(new ComponentNHKey(rt->GetActiveLabel(),
        vlan_nh->GetVlanTag(), vlan_nh->GetIfUuid()));
    uint32_t vlan_label = rt->GetActiveLabel();
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
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t hash_id = i + 100;
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

        std::string vn_name_10("vn10");
        std::string vn_name_11("vn11");
        EXPECT_TRUE(VnMatch(entry->data().source_vn_list, vn_name_10));
        EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, vn_name_11));

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
        EXPECT_TRUE(VnMatch(rev_entry->data().source_vn_list, vn_name_11));
        EXPECT_TRUE(VnMatch(rev_entry->data().dest_vn_list, vn_name_10));
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
    for (uint32_t i = 0; i < 32; i++) {
        char router_id[80];
        strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());

        uint32_t hash_id = i + 200;
        TxTcpMplsPacket(eth_intf_id_, "10.11.11.1", router_id, 
                        vlan_label, "10.1.1.3", "11.1.1.252", 
                        sport, dport, false, hash_id);
        client->WaitForIdle();

        int nh_id =
            GetActiveLabel(vlan_label)->nexthop()->id();
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

    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
    EXPECT_FALSE(VrfFind("vrf11"));
    EXPECT_FALSE(VrfFind("vrf10"));
    EXPECT_FALSE(VrfFind("service-vrf1"));
    DelIPAM("vn10");
    DelIPAM("vn11");
}


//Service VM with ECMP instantaited in local server
//Test with packet from virtual-machine to service VM instance
//Test with packet from external-world to service VM instance
TEST_F(EcmpTest, ServiceVlanTest_4) {
    struct PortInfo input1[] = {
        {"vnet10", 10, "10.1.1.1", "00:00:00:01:01:01", 10, 10},
    };
    CreateVmportWithEcmp(input1, 1);
    IpamInfo ipam_info_10[] = {
        {"10.1.1.0", 24, "10.1.1.254"},
    };
    AddIPAM("vn10", ipam_info_10, 1);
    client->WaitForIdle();

    //Add service VM in vrf11 as mgmt VRF
    struct PortInfo input2[] = {
        {"vnet11", 11, "1.1.1.252", "00:00:00:01:01:01", 11, 11},
        {"vnet12", 12, "1.1.1.253", "00:00:00:01:01:01", 11, 12},
    };
    CreateVmportWithEcmp(input2, 2);
    IpamInfo ipam_info_11[] = {
        {"1.1.1.0", 24, "1.1.1.254"},
    };
    AddIPAM("vn11", ipam_info_11, 1);
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

    uint32_t vrf_id = agent_->vrf_table()->FindVrfFromName("vrf10")->vrf_id();
    uint32_t service_vrf_id =
        agent_->vrf_table()->FindVrfFromName("service-vrf1")->vrf_id();

    //Leak aggregarete route to vrf10
    Ip4Address service_vm_ip = Ip4Address::from_string("10.1.1.2"); 
    InetUnicastRouteEntry *rt = RouteGet("service-vrf1", service_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);

    DBEntryBase::KeyPtr key_ref = rt->GetActiveNextHop()->GetDBRequestKey();
    CompositeNHKey *composite_nh_key = static_cast<CompositeNHKey *>
        (key_ref.get());
    ComponentNHKeyPtr comp_nh_data(new ComponentNHKey(rt->GetActiveLabel(),
        Composite::LOCAL_ECMP, false, composite_nh_key->component_nh_key_list(),
        "service-vrf1"));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(comp_nh_data);
    AddRemoteEcmpRoute("vrf10", "11.1.1.0", 24, "vn11", 0, comp_nh_list);
    AddRemoteEcmpRoute("service-vrf1", "11.1.1.0", 24, "vn11", 0, comp_nh_list);
    client->WaitForIdle();

    Ip4Address dest_ip = Ip4Address::from_string("11.1.1.0");
    InetUnicastRouteEntry *rt_vrf10 = RouteGet("vrf10", dest_ip, 24);
    const NextHop *nh_vrf10 = rt_vrf10->GetActiveNextHop();
    EXPECT_TRUE(dynamic_cast<const CompositeNH *>(nh_vrf10) != NULL);

    InetUnicastRouteEntry *rt_svrf = RouteGet("service-vrf1", dest_ip, 24);
    const NextHop *nh_svrf = rt_svrf->GetActiveNextHop();
    EXPECT_TRUE(dynamic_cast<const CompositeNH *>(nh_svrf) != NULL);

    //Choose some random source and destination port
    uint32_t sport = rand() % 65535;
    uint32_t dport = rand() % 65535;
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t hash_id = i + 100;
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

        std::string vn_name_10("vn10");
        std::string vn_name_11("vn11");
        EXPECT_TRUE(VnMatch(entry->data().source_vn_list, vn_name_10));
        EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, vn_name_11));

        //Reverse flow is no ECMP
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx == 
                CompositeNH::kInvalidComponentNHIdx);
        //Packet from service interface, vrf has to be 
        //service vlan VRF
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == vrf_id);
        EXPECT_TRUE(VnMatch(rev_entry->data().source_vn_list, vn_name_11));
        EXPECT_TRUE(VnMatch(rev_entry->data().dest_vn_list, vn_name_10));
        sport++;
        dport++;
    }

    //Leak a remote VM route in service-vrf
    AddRemoteVmRoute("service-vrf1", "10.1.1.3", 32, "vn10");
    client->WaitForIdle();
    uint32_t label = rt->GetActiveLabel();
    //Send a packet from a remote VM to service VM
    sport = rand() % 65535;
    dport = rand() % 65535;
    for (uint32_t i = 0; i < 32; i++) {
        char router_id[80];
        strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());

        uint32_t hash_id = i + 200;
        TxTcpMplsPacket(eth_intf_id_, "10.11.11.1", router_id, 
                        label, "10.1.1.3", "11.1.1.252", 
                        sport, dport, false, hash_id);
        client->WaitForIdle();

        int nh_id = GetActiveLabel(label)->nexthop()->id();
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

        std::string vn_name_10("vn10");
        std::string vn_name_11("vn11");
        EXPECT_TRUE(VnMatch(entry->data().source_vn_list, vn_name_10));
        EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, vn_name_11));

        //Reverse flow is no ECMP
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx == 
                CompositeNH::kInvalidComponentNHIdx);
        //Packet from service interface, vrf has to be 
        //service vlan VRF
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(VnMatch(rev_entry->data().source_vn_list, vn_name_11));
        EXPECT_TRUE(VnMatch(rev_entry->data().dest_vn_list, vn_name_10));
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
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf11"));
    EXPECT_FALSE(VrfFind("vrf10"));
    EXPECT_FALSE(VrfFind("service-vrf1"));
    DelIPAM("vn10");
    DelIPAM("vn11");
}

//Packet from a right service VM interface(both service vm instance launched on same server)
//reaching end host on same machine
TEST_F(EcmpTest, ServiceVlanTest_5) {
    struct PortInfo input1[] = {
        {"vnet11", 11, "11.1.1.1", "00:00:00:01:01:01", 11, 11},
    };
    CreateVmportWithEcmp(input1, 1);
    IpamInfo ipam_info_11[] = {
        {"11.1.1.0", 24, "11.1.1.254"},
    };
    AddIPAM("vn11", ipam_info_11, 1);
    client->WaitForIdle();

    //Add service VM in vrf13 as mgmt VRF
    struct PortInfo input2[] = {
        {"vnet13", 13, "1.1.1.252", "00:00:00:01:01:01", 13, 13},
        {"vnet14", 14, "1.1.1.253", "00:00:00:01:01:01", 13, 14},
    };
    CreateVmportWithEcmp(input2, 2);
    IpamInfo ipam_info_13[] = {
        {"1.1.1.0", 24, "1.1.1.254"},
    };
    AddIPAM("vn13", ipam_info_13, 1);
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

    const MacAddress mac("02:00:00:00:00:02");
    EXPECT_TRUE(L2RouteFind("service-vrf1",mac));

    uint32_t vrf_id = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf11")->vrf_id();
    uint32_t service_vrf_id = 
        Agent::GetInstance()->vrf_table()->FindVrfFromName("service-vrf1")->vrf_id();

    Ip4Address service_vm_ip = Ip4Address::from_string("11.1.1.2"); 
    InetUnicastRouteEntry *rt = RouteGet("service-vrf1", service_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);

    DBEntryBase::KeyPtr key_ref = rt->GetActiveNextHop()->GetDBRequestKey();
    CompositeNHKey *composite_nh_key = static_cast<CompositeNHKey *>
        (key_ref.get());
    ComponentNHKeyPtr comp_nh_data(new ComponentNHKey(rt->GetActiveLabel(),
        Composite::LOCAL_ECMP, false, composite_nh_key->component_nh_key_list(),
        "service-vrf1"));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(comp_nh_data);
    //Leak a aggregarate route to service VRF
    AddRemoteEcmpRoute("service-vrf1", "10.1.1.0", 24, "vn10", 0, comp_nh_list);
    //Leak a aggresgarate route vrf 11
    AddRemoteEcmpRoute("vrf11", "10.1.1.0", 24, "vn10", 0, comp_nh_list);
    //Leak route for vm11 to service vrf
    AddLocalVmRoute("service-vrf1", "11.1.1.1", 32, "vn11", 11);
    client->WaitForIdle();

    uint32_t vnet13_vlan_nh = GetServiceVlanNH(13, "service-vrf1");
    uint32_t vnet14_vlan_nh = GetServiceVlanNH(14, "service-vrf1");

    //Choose some random source and destination port
    uint32_t sport = rand() % 65535;
    uint32_t dport = rand() % 65535;
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t hash_id = i + 100;
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

        std::string vn_name_10("vn10");
        std::string vn_name_11("vn11");
        EXPECT_TRUE(VnMatch(entry->data().source_vn_list, vn_name_10));
        EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, vn_name_11));

        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
        //Packet to service interface, vrf has to be 
        //service vlan VRF
        EXPECT_TRUE(rev_entry->data().vrf == vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(VnMatch(rev_entry->data().source_vn_list, vn_name_11));
        EXPECT_TRUE(VnMatch(rev_entry->data().dest_vn_list, vn_name_10));
        sport++;
        dport++;
    }

    //Send traffice from second service interface and expect things to be fine
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t hash_id = i + 200;
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

        std::string vn_name_10("vn10");
        std::string vn_name_11("vn11");
        EXPECT_TRUE(VnMatch(entry->data().source_vn_list, vn_name_10));
        EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, vn_name_11));

        //make sure reverse flow points to right index
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);

        //Packet from vm11 to service vrf 
        EXPECT_TRUE(rev_entry->data().vrf == vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(VnMatch(rev_entry->data().source_vn_list, vn_name_11));
        EXPECT_TRUE(VnMatch(rev_entry->data().dest_vn_list, vn_name_10));
        sport++;
        dport++;
    }

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet13");
    client->WaitForIdle();
    EXPECT_TRUE(L2RouteFind("service-vrf1",mac));

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet14");
    client->WaitForIdle();
    EXPECT_FALSE(L2RouteFind("service-vrf1",mac));

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    DeleteVmportEnv(input2, 2, true);
    DelVrf("service-vrf1");
    DelVmPortVrf("ser1");
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf11"));
    EXPECT_FALSE(VrfFind("vrf13"));
    EXPECT_FALSE(VrfFind("service-vrf1"));
    DelIPAM("vn11");
    DelIPAM("vn13");
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
    IpamInfo ipam_info_13[] = {
        {"1.1.1.0", 24, "1.1.1.254"},
    };
    AddIPAM("vn13", ipam_info_13, 1);
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

    std::string vn_name_10("vn10");
    std::string vn_name_11("vn11");
    uint32_t service_vrf_id = 
        Agent::GetInstance()->vrf_table()->FindVrfFromName("service-vrf1")->vrf_id();

    Ip4Address service_vm_ip = Ip4Address::from_string("11.1.1.2"); 
    InetUnicastRouteEntry *rt = RouteGet("service-vrf1", service_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);
    uint32_t mpls_label = rt->GetActiveLabel();

    DBEntryBase::KeyPtr key_ref = rt->GetActiveNextHop()->GetDBRequestKey();
    CompositeNHKey *composite_nh_key = static_cast<CompositeNHKey *>
        (key_ref.get());
    ComponentNHKeyPtr comp_nh_data(new ComponentNHKey(rt->GetActiveLabel(),
        Composite::LOCAL_ECMP, false, composite_nh_key->component_nh_key_list(),
        "service-vrf1"));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(comp_nh_data);

    //Leak a aggregarate route to service VRF
    AddRemoteEcmpRoute("service-vrf1", "10.1.1.0", 24, "vn10", 0, comp_nh_list);
    //Leak routes from right vrf to service vrf
    AddRemoteVmRoute("service-vrf1", "11.1.1.1", 32, "vn11");
    comp_nh_list.clear();
    AddRemoteEcmpRoute("service-vrf1", "11.1.1.3", 32, "vn11", 2, comp_nh_list);
    client->WaitForIdle();

    uint32_t vnet13_vlan_nh = GetServiceVlanNH(13, "service-vrf1");
    uint32_t vnet14_vlan_nh = GetServiceVlanNH(14, "service-vrf1");
    //Choose some random source and destination port
    uint32_t sport = rand() % 65535;
    uint32_t dport = rand() % 65535;
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t hash_id = i + 100;
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

        EXPECT_TRUE(VnMatch(entry->data().source_vn_list, vn_name_10));
        EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, vn_name_11));

        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
        //Packet to service interface, vrf has to be 
        //service vlan VRF
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(VnMatch(rev_entry->data().source_vn_list, vn_name_11));
        EXPECT_TRUE(VnMatch(rev_entry->data().dest_vn_list, vn_name_10));
        sport++;
        dport++;
    }

    //Send traffic from second service interface
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t hash_id = i + 200;
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

        EXPECT_TRUE(VnMatch(entry->data().source_vn_list, vn_name_10));
        EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, vn_name_11));

        //make sure reverse flow points to right index
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);

        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(VnMatch(rev_entry->data().source_vn_list, vn_name_11));
        EXPECT_TRUE(VnMatch(rev_entry->data().dest_vn_list, vn_name_10));
        sport++;
        dport++;
    }

    char router_id[80];
    strcpy(router_id, Agent::GetInstance()->router_id().to_string().c_str());
    //Send traffic from remote VM service
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t hash_id = i + 100;
        TxTcpMplsPacket(eth_intf_id_, "10.10.10.10", router_id, 
                        mpls_label, "11.1.1.1", "10.1.1.1", 
                        sport, dport, false, hash_id);
        client->WaitForIdle();

        int nh_id =
            GetActiveLabel(mpls_label)->nexthop()->id();
        FlowEntry *entry = FlowGet(service_vrf_id,
                "11.1.1.1", "10.1.1.1", IPPROTO_TCP, sport, dport, nh_id);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(VnMatch(entry->data().source_vn_list, vn_name_11));
        EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, vn_name_10));

        //make sure reverse flow is no ecmp
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(VnMatch(rev_entry->data().source_vn_list, vn_name_10));
        EXPECT_TRUE(VnMatch(rev_entry->data().dest_vn_list, vn_name_11));
        sport++;
        dport++;
    }
 
    //Send traffic from remote VM which is also ecmp
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t hash_id = i + 200;
        TxTcpMplsPacket(eth_intf_id_, "10.10.10.10", router_id, 
                        mpls_label, "11.1.1.3", "10.1.1.1", 
                        sport, dport, false, hash_id);
        client->WaitForIdle();

        int nh_id =
            GetActiveLabel(mpls_label)->nexthop()->id();
        FlowEntry *entry = FlowGet(service_vrf_id,
                "11.1.1.3", "10.1.1.1", IPPROTO_TCP, sport, dport, nh_id);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(VnMatch(entry->data().source_vn_list, vn_name_11));
        EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, vn_name_10));

        //make sure reverse flow is no ecmp
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(VnMatch(rev_entry->data().source_vn_list, vn_name_10));
        EXPECT_TRUE(VnMatch(rev_entry->data().dest_vn_list, vn_name_11));
        sport++;
        dport++;
    }

    //Send traffic from remote VM which is also ecmp
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t hash_id = i + 100;
        TxTcpMplsPacket(eth_intf_id_, "10.10.10.11", router_id, 
                        mpls_label, "11.1.1.3", "10.1.1.1", 
                        sport, dport, false, hash_id);
        client->WaitForIdle();
        int nh_id =
            GetActiveLabel(mpls_label)->nexthop()->id();
        FlowEntry *entry = FlowGet(service_vrf_id,
                "11.1.1.3", "10.1.1.1", IPPROTO_TCP, sport, dport, nh_id);
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);

        EXPECT_TRUE(entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(VnMatch(entry->data().source_vn_list, vn_name_11));
        EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, vn_name_10));

        //make sure reverse flow is no ecmp
        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(VnMatch(rev_entry->data().source_vn_list, vn_name_10));
        EXPECT_TRUE(VnMatch(rev_entry->data().dest_vn_list, vn_name_11));
        sport++;
        dport++;
    }

    DeleteRemoteRoute("service-vrf1", "10.1.1.0", 24);
    DeleteRemoteRoute("service-vrf1", "11.1.1.3", 32);
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet13");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet14");
    client->WaitForIdle();
    DeleteVmportEnv(input2, 2, true);
    DelVrf("service-vrf1");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf13"));
    EXPECT_FALSE(VrfFind("service-vrf1"));
    DelIPAM("vn13");
}

// Packet from a right service VM interface(one service instance on
// local server, one on remote server) reaching end host on a local machine
TEST_F(EcmpTest, ServiceVlanTest_7) {
    struct PortInfo input1[] = {
        {"vnet11", 11, "11.1.1.1", "00:00:00:01:01:01", 11, 11},
    };
    CreateVmportWithEcmp(input1, 1);
    IpamInfo ipam_info_11[] = {
        {"11.1.1.0", 24, "11.1.1.254"},
    };
    AddIPAM("vn11", ipam_info_11, 1);
    client->WaitForIdle();

    //Add service VM in vrf13 as mgmt VRF
    struct PortInfo input2[] = {
        {"vnet13", 13, "1.1.1.252", "00:00:00:01:01:01", 13, 13},
    };
    CreateVmportWithEcmp(input2, 1);
    IpamInfo ipam_info_12[] = {
        {"12.1.1.0", 24, "12.1.1.254"},
    };
    AddIPAM("vn12", ipam_info_12, 1);
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
    InetUnicastRouteEntry *rt = RouteGet("service-vrf1", service_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);

    ComponentNHKeyList comp_nh_list;
    const VlanNH *vlan_nh = static_cast<const VlanNH *>(rt->GetActiveNextHop());
    ComponentNHKeyPtr comp_nh_data(new ComponentNHKey(
        rt->GetActiveLabel(), vlan_nh->GetVlanTag(), vlan_nh->GetIfUuid()));
    comp_nh_list.push_back(comp_nh_data);

    //Leak a aggregarate route to service VRF
    AddRemoteEcmpRoute("service-vrf1", "10.1.1.0", 24, "vn10", 1, comp_nh_list);
    //Leak a aggregarate route tp vrf11
    AddRemoteEcmpRoute("vrf11", "10.1.1.0", 24, "vn10", 1, comp_nh_list);
    //Leak route for vm11 to service vrf
    AddLocalVmRoute("service-vrf1", "11.1.1.1", 32, "vn11", 11);
    client->WaitForIdle();
    uint32_t vnet13_vlan_nh = GetServiceVlanNH(13, "service-vrf1");

    //Choose some random source and destination port
    uint32_t sport = rand() % 65535;
    uint32_t dport = rand() % 65535;
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t hash_id = i + 100;
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

        std::string vn_name_10("vn10");
        std::string vn_name_11("vn11");
        EXPECT_TRUE(VnMatch(entry->data().source_vn_list, vn_name_10));
        EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, vn_name_11));

        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
        //Packet to service interface, vrf has to be 
        //service vlan VRF
        EXPECT_TRUE(rev_entry->data().vrf == vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(VnMatch(rev_entry->data().source_vn_list, vn_name_11));
        EXPECT_TRUE(VnMatch(rev_entry->data().dest_vn_list, vn_name_10));
        sport++;
        dport++;
    }

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "service-vrf1");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet13");
    DeleteVmportEnv(input2, 1, true);

    DeleteVmportEnv(input1, 1, true);
    DelVrf("service-vrf1");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf11"));
    EXPECT_FALSE(VrfFind("vrf13"));
    EXPECT_FALSE(VrfFind("service-vrf1"));
    DelIPAM("vn11");
    DelIPAM("vn12");
}

// Packet from a right service VM interface (one service instance on local
// server, one on remote server), reaching end host on a remote machine
TEST_F(EcmpTest,ServiceVlanTest_8) {
    //Add service VM in vrf13 as mgmt VRF
    struct PortInfo input2[] = {
        {"vnet13", 13, "1.1.1.252", "00:00:00:01:01:01", 13, 13},
    };
    CreateVmportWithEcmp(input2, 1);
    IpamInfo ipam_info_13[] = {
        {"1.1.1.0", 24, "1.1.1.254"},
    };
    AddIPAM("vn13", ipam_info_13, 1);
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
    InetUnicastRouteEntry *rt = RouteGet("service-vrf1", service_vm_ip, 32);
    EXPECT_TRUE(rt != NULL);

    ComponentNHKeyList comp_nh_list;
    const VlanNH *vlan_nh = static_cast<const VlanNH *>(rt->GetActiveNextHop());
    ComponentNHKeyPtr comp_nh_data(new ComponentNHKey(
        rt->GetActiveLabel(), vlan_nh->GetVlanTag(),
        vlan_nh->GetIfUuid()));
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
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t hash_id = i + 100;
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
        std::string vn_name_10("vn10");
        std::string vn_name_11("vn11");
        EXPECT_TRUE(VnMatch(entry->data().source_vn_list, vn_name_10));
        EXPECT_TRUE(VnMatch(entry->data().dest_vn_list, vn_name_11));
        EXPECT_TRUE(entry->rpf_nh() == VmPortGet(13)->flow_key_nh());

        FlowEntry *rev_entry = entry->reverse_flow_entry();
        EXPECT_TRUE(entry->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
        //Packet to service interface, vrf has to be 
        //service vlan VRF
        EXPECT_TRUE(rev_entry->data().vrf == service_vrf_id);
        EXPECT_TRUE(rev_entry->data().dest_vrf == service_vrf_id);
        EXPECT_TRUE(VnMatch(rev_entry->data().source_vn_list, vn_name_11));
        EXPECT_TRUE(VnMatch(rev_entry->data().dest_vn_list, vn_name_10));
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
    EXPECT_FALSE(VrfFind("vrf13"));
    EXPECT_FALSE(VrfFind("service-vrf1"));
    DelIPAM("vn13");
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
