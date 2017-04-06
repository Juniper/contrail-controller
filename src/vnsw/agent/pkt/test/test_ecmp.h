#ifndef _VNSW_AGENT_PKT_TEST_TEST_ECMP_H__
#define _VNSW_AGENT_PKT_TEST_TEST_ECMP_H__
/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include <oper/tunnel_nh.h>

// Number of interfaces in base class
#define VMI_MAX_COUNT 256
#define REMOTE_COMPUTE_1 "100.100.100.1"
#define REMOTE_COMPUTE_2 "100.100.100.2"
#define REMOTE_COMPUTE_3 "100.100.100.3"

#define REMOTE_NON_ECMP_1 "1.1.1.101"
#define REMOTE_NON_ECMP_2 "1.1.1.102"

// ECMP Route with remote members only
#define REMOTE_ECMP_IP_1 "1.1.1.40"

IpamInfo ipam_info_1[] = {
    {"1.1.1.0", 24, "1.1.1.254"},
};

// A non-ecmp port
struct PortInfo input1[] = {
    {"vif1", 1, "1.1.1.1", "00:01:01:01:01:01", 1, 1},
    {"vif2", 2, "1.1.1.2", "00:01:01:01:01:02", 1, 1},
};

// ECMP Ports-1 - All members are local
struct PortInfo input10[] = {
    {"vif11", 11, "1.1.1.10", "00:01:01:01:01:11", 1, 11},
    {"vif12", 12, "1.1.1.10", "00:01:01:01:01:12", 1, 12},
    {"vif13", 13, "1.1.1.10", "00:01:01:01:01:13", 1, 13},
};

// ECMP Ports-2 - All members are local
struct PortInfo input11[] = {
    {"vif21", 21, "1.1.1.20", "00:01:01:01:01:21", 1, 21},
    {"vif22", 22, "1.1.1.20", "00:01:01:01:01:22", 1, 22},
    {"vif23", 23, "1.1.1.20", "00:01:01:01:01:23", 1, 23},
};

// ECMP Ports-3 - Has both local and remote members
struct PortInfo input13[] = {
    {"vif31", 31, "1.1.1.30", "00:01:01:01:01:31", 1, 31},
    {"vif32", 32, "1.1.1.30", "00:01:01:01:01:32", 1, 32},
    {"vif33", 33, "1.1.1.30", "00:01:01:01:01:33", 1, 33},
};

IpamInfo ipam_info_2[] = {
    {"2.1.1.0", 24, "2.1.1.254"},
};

struct PortInfo input21[] = {
    {"vif101", 101, "2.1.1.1", "00:02:01:01:01:01", 2, 101},
};

struct PortInfo input22[] = {
    {"vif111", 111, "2.1.1.10", "00:02:01:01:01:11", 2, 111},
    {"vif112", 112, "2.1.1.10", "00:02:01:01:01:12", 2, 112},
    {"vif113", 113, "2.1.1.10", "00:02:01:01:01:13", 2, 113},
};

class EcmpTest : public ::testing::Test {
public:
    EcmpTest() :
        agent_(Agent::GetInstance()),
        flow_proto_(agent_->pkt()->get_flow_proto()) {
    }
    virtual ~EcmpTest() { }

    virtual void SetUp() {
        boost::system::error_code ec;
        bgp_peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                 "xmpp channel");
        client->WaitForIdle();

        // Get ethernet interface id
        eth_intf_id_ = EthInterfaceGet("vnet0")->id();

        CreateVmportEnv(input1, 2);
        CreateVmportWithEcmp(input10, 3);
        CreateVmportWithEcmp(input11, 3);
        CreateVmportWithEcmp(input13, 3);
        AddIPAM("vn1", ipam_info_1, 1);
        client->WaitForIdle();

        // Add remote non-ECMP route
        Inet4TunnelRouteAdd(bgp_peer_, "vrf1",
                            Ip4Address::from_string(REMOTE_NON_ECMP_1), 32,
                            Ip4Address::from_string(REMOTE_COMPUTE_1),
                            TunnelType::AllType(),
                            16, "vn1", SecurityGroupList(), PathPreference());
        Inet4TunnelRouteAdd(bgp_peer_, "vrf1",
                            Ip4Address::from_string(REMOTE_NON_ECMP_2), 32,
                            Ip4Address::from_string(REMOTE_COMPUTE_1),
                            TunnelType::AllType(),
                            17, "vn1", SecurityGroupList(), PathPreference());
        client->WaitForIdle();

        // Add ECMP Route members to 1.1.1.30. It has both local and remote
        // members
        InetUnicastRouteEntry *rt =
            RouteGet("vrf1", Ip4Address::from_string("1.1.1.30"), 32);
        EXPECT_TRUE(rt != NULL);
        const CompositeNH *composite_nh = static_cast<const CompositeNH *>
            (rt->GetActiveNextHop());
        ComponentNHKeyList comp_nh_list = composite_nh->component_nh_key_list();
        AddRemoteEcmpRoute("vrf1", "1.1.1.30", 32, "vn1", 3, comp_nh_list);

        // Add ECMP Route members to 1.1.1.40. It has both only remote members
        AddRemoteEcmpRoute("vrf1", REMOTE_ECMP_IP_1, 32, "vn1", 3,
                           ComponentNHKeyList());

        FlowStatsTimerStartStop(agent_, true);
        GetInfo();
    }

    virtual void TearDown() {
        FlushFlowTable();

        DeleteRemoteRoute("vrf1", REMOTE_NON_ECMP_1, 32);
        DeleteRemoteRoute("vrf1", REMOTE_NON_ECMP_2, 32);
        DeleteRemoteRoute("vrf1", "1.1.1.30", 32);
        DeleteRemoteRoute("vrf1", REMOTE_ECMP_IP_1, 32);
        client->WaitForIdle();

        DeleteVmportEnv(input1, 2, false);
        DeleteVmportEnv(input10, 3, false);
        DeleteVmportEnv(input11, 3, false);
        DeleteVmportEnv(input13, 3, true);
        client->WaitForIdle();

        DelIPAM("vn1");
        client->WaitForIdle();

        DeleteBgpPeer(bgp_peer_);
        FlowStatsTimerStartStop(agent_, false);
        WAIT_FOR(1000, 1000, (agent_->vrf_table()->Size() == 1));
    }

    void GetInfo() {
        for (uint32_t i = 1; i <= 256; i++) {
            vmi_[i] = VmInterfaceGet(i);
            if (vmi_[i])
                EXPECT_TRUE(VmPortActive(i));
        }
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        WAIT_FOR(1000, 1000, (flow_proto_->FlowCount() == 0));
    }

    uint32_t GetServiceVlanNH(uint32_t intf_id,
                              const std::string &vrf_name) const {
        const VmInterface *vm_intf = VmInterfaceGet(intf_id);
        const VrfEntry *vrf = VrfGet(vrf_name.c_str());
        uint32_t label = vm_intf->GetServiceVlanLabel(vrf);
        return agent_->mpls_table()->FindMplsLabel(label)->nexthop()->id();
    }

    void AddRemoteEcmpRoute(const std::string &vrf_name, const std::string &ip,
                            uint32_t plen, const std::string &vn, int count,
                            const ComponentNHKeyList &local_list,
                            bool same_label = false) {
        //If there is a local route, include that always
        Ip4Address vm_ip = Ip4Address::from_string(ip);
        ComponentNHKeyList comp_nh_list = local_list;
        int remote_server_ip = 0x0A0A0A0A;
        int label = 16;
        SecurityGroupList sg_id_list;
        for(int i = 0; i < count; i++) {
            ComponentNHKeyPtr comp_nh
                (new ComponentNHKey(label, agent_->fabric_vrf_name(),
                                    agent_->router_id(),
                                    Ip4Address(remote_server_ip++), false,
                                    TunnelType::GREType()));
            comp_nh_list.push_back(comp_nh);
            if (!same_label) {
                label++;
            }
        }
        EcmpTunnelRouteAdd(bgp_peer_, vrf_name, vm_ip, plen, comp_nh_list, -1,
                           vn, sg_id_list, PathPreference());
    }

    void AddLocalVmRoute(const std::string &vrf_name, const std::string &ip,
                         uint32_t plen, const std::string &vn,
                         uint32_t intf_uuid) {
        const VmInterface *vm_intf =
            static_cast<const VmInterface *> (VmPortGet(intf_uuid));
        VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, MakeUuid(intf_uuid),
                                "");
        VnListType vn_list;
        vn_list.insert(vn);
        LocalVmRoute *local_vm_rt =
            new LocalVmRoute(intf_key, vm_intf->label(),
                             VxLanTable::kInvalidvxlan_id, false, vn_list,
                             InterfaceNHFlags::INET4, SecurityGroupList(),
                             CommunityList(), PathPreference(), Ip4Address(0),
                             EcmpLoadBalance(), false, false,
                             bgp_peer_->sequence_number(), false);
        InetUnicastAgentRouteTable *rt_table =
            agent_->vrf_table()->GetInet4UnicastRouteTable(vrf_name);

        rt_table->AddLocalVmRouteReq(bgp_peer_, vrf_name,
                                     Ip4Address::from_string(ip), plen,
                                     static_cast<LocalVmRoute *>(local_vm_rt));
    }

    void AddRemoteVmRoute(const std::string &vrf_name, const std::string &ip,
                          uint32_t plen, const std::string &vn) {
        const Ip4Address addr = Ip4Address::from_string(ip);
        VnListType vn_list;
        vn_list.insert(vn);
        ControllerVmRoute *data = ControllerVmRoute::MakeControllerVmRoute
            (bgp_peer_, agent_->fabric_vrf_name(), agent_->router_id(),
             vrf_name, addr, TunnelType::GREType(), 16, vn_list,
             SecurityGroupList(), PathPreference(), false, EcmpLoadBalance(),
             false);
        InetUnicastAgentRouteTable::AddRemoteVmRouteReq(bgp_peer_, vrf_name,
                                                        addr, plen, data);
    }

    void DeleteRemoteRoute(const std::string &vrf_name, const std::string &ip,
                           uint32_t plen) {
        Ip4Address server_ip = Ip4Address::from_string(ip);
        agent_->fabric_inet4_unicast_table()->DeleteReq
            (bgp_peer_, vrf_name, server_ip, plen,
             new ControllerVmRoute(bgp_peer_));
    }

    // Get VMI for the reverse flow
    const VmInterface *GetOutVmi(const FlowEntry *flow) {
        const VrfEntry *vrf = VrfGet(flow->data().flow_dest_vrf);
        const AgentRoute *rt =
            FlowEntry::GetUcRoute(vrf, flow->key().dst_addr);
        const NextHop *nh = rt->GetActiveNextHop();

        if (dynamic_cast<const CompositeNH *>(nh)) {
            const CompositeNH *cnh = dynamic_cast<const CompositeNH *>(nh);
            nh = cnh->GetNH(flow->GetEcmpIndex());
        }

        if (dynamic_cast<const InterfaceNH *>(nh)) {
            const InterfaceNH *intf_nh = dynamic_cast<const InterfaceNH *>(nh);
            return dynamic_cast<const VmInterface *>(intf_nh->GetInterface());
        }

        return NULL;
    }

    // Get outgoing NH for a ECMP flow
    const NextHop *GetOutMemberNh(const FlowEntry *flow) {
        const VrfEntry *vrf = VrfGet(flow->data().flow_dest_vrf);
        const AgentRoute *rt =
            FlowEntry::GetUcRoute(vrf, flow->key().dst_addr);
        const NextHop *nh = rt->GetActiveNextHop();

        if (dynamic_cast<const CompositeNH *>(nh)) {
            const CompositeNH *cnh = dynamic_cast<const CompositeNH *>(nh);
            return cnh->GetNH(flow->GetEcmpIndex());
        }

        return nh;
    }

    // Get outgoing NH for flow
    const NextHop *GetOutNh(const FlowEntry *flow) {
        const VrfEntry *vrf = VrfGet(flow->data().flow_dest_vrf);
        const AgentRoute *rt =
            FlowEntry::GetUcRoute(vrf, flow->key().dst_addr);
        return rt->GetActiveNextHop();
    }

    // Get RPF-NH when it is supposed to be local composite-NH
    const NextHop *GetLocalCompRpfNh(const FlowEntry *flow) {
        const FlowEntry *rflow = flow->reverse_flow_entry();
        const VrfEntry *vrf = VrfGet(flow->data().flow_dest_vrf);
        const InetUnicastRouteEntry *rt =
            dynamic_cast<const InetUnicastRouteEntry *>
            (FlowEntry::GetUcRoute(vrf, rflow->key().dst_addr));
        return rt->GetLocalNextHop();
    }

    // Leak route for ip in vrf1 to vrf2 by re-ordering of ECMP components
    void LeakRoute(const char *vrf1, const char *ip, const char *vrf2) {
        InetUnicastRouteEntry *rt =
            RouteGet(vrf1, Ip4Address::from_string(ip), 32);
        EXPECT_TRUE(rt != NULL);
        const CompositeNH *composite_nh =
            static_cast<const CompositeNH *>(rt->GetActiveNextHop());
        ComponentNHKeyList comp_nh_list = composite_nh->component_nh_key_list();
        std::reverse(comp_nh_list.begin(), comp_nh_list.end());
        AddRemoteEcmpRoute(vrf2, ip, 32, rt->dest_vn_name(), 0, comp_nh_list);
    }

    // Leak route for 2.1.1.10 into vrf-1. But with changed order of components
protected:
    Agent *agent_;
    FlowProto *flow_proto_;
    uint32_t eth_intf_id_;
    BgpPeer *bgp_peer_;
    VmInterface *vmi_[VMI_MAX_COUNT];
};
#endif //  _VNSW_AGENT_PKT_TEST_TEST_ECMP_H__
