/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <algorithm>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/ecmp_load_balance.h"
#include "oper/tunnel_nh.h"
#include "pkt/flow_table.h"

void RouterIdDepInit(Agent *agent) {
}

int hash_id;
VmInterface *flow0;
VmInterface *flow1;
VmInterface *flow2;
VmInterface *flow3;
std::string eth_itf;

#define vm1_ip "11.1.1.1"
#define ip6_vm1_ip "::11.1.1.1"
#define vm2_ip "11.1.1.2"
#define ip6_vm2_ip "::11.1.1.2"
#define vm3_ip "12.1.1.1"
#define ip6_vm3_ip "::12.1.1.1"
#define vm4_ip "13.1.1.1"
#define ip6_vm4_ip "::13.1.1.1"
#define ip6_remote_vm1_ip "::11.1.1.3"
#define ip6_remote_vm3_ip "::12.1.1.3"
#define ip6_remote_vm4_ip "::13.1.1.2"
#define remote_router_ip "10.1.1.2"

struct PortInfo input[] = {
        {"flow0", 6, vm1_ip, "00:00:00:01:01:01", 5, 1, ip6_vm1_ip},
        {"flow1", 7, vm2_ip, "00:00:00:01:01:02", 5, 2, ip6_vm2_ip},
        {"flow2", 8, vm3_ip, "00:00:00:01:01:03", 5, 3, ip6_vm3_ip},
};

struct PortInfo input2[] = {
        {"flow3", 9, vm4_ip, "00:00:00:01:01:04", 3, 4, ip6_vm4_ip},
};

IpamInfo ipam_info[] = {
    {"11.1.1.0", 24, "11.1.1.10"},
    {"12.1.1.0", 24, "12.1.1.10"},
    {"::11.1.1.0", 120, "::11.1.1.10"},
    {"::12.1.1.0", 120, "::12.1.1.10"},
};

IpamInfo ipam_info2[] = {
    {"13.1.1.0", 24, "13.1.1.10"},
    {"::13.1.1.0", 120, "::13.1.1.10"},
};

class FlowTestV6 : public ::testing::Test {
public:
    FlowTestV6() : peer_(NULL), agent_(Agent::GetInstance()) {
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, agent()->pkt()->get_flow_proto()->FlowCount());
    }

    void CreateLocalRoute(const char *vrf, const char *ip,
                          VmInterface *intf, int label) {
        Ip6Address addr = Ip6Address::from_string(ip);
        InetUnicastAgentRouteTable *rt_table =
            agent()->vrf_table()->GetInet6UnicastRouteTable(vrf);
        VnListType vn_list;
        vn_list.insert(intf->vn()->GetName());
        rt_table->AddLocalVmRouteReq(agent()->local_peer(), vrf, addr,
                                     128, intf->GetUuid(),
                                     vn_list, label,
                                     SecurityGroupList(), CommunityList(), false,
                                     PathPreference(),
                                     Ip6Address(),
                                     EcmpLoadBalance(), false);
        client->WaitForIdle();
        EXPECT_TRUE(RouteFindV6(vrf, addr, 128));
    }

    void CreateRemoteRoute(const char *vrf, const char *remote_vm,
                           const char *serv, int label, const char *vn) {
        Ip6Address addr = Ip6Address::from_string(remote_vm);
        Ip4Address gw = Ip4Address::from_string(serv);
        Inet6TunnelRouteAdd(peer_, vrf, addr, 128, gw, TunnelType::MplsType(), label, vn,
                            SecurityGroupList(), PathPreference());
        client->WaitForIdle(2);
        WAIT_FOR(1000, 500, (RouteFindV6(vrf, addr, 128) == true));
    }

    void DeleteRoute(const char *vrf, const char *ip) {
        Ip6Address addr = Ip6Address::from_string(ip);
        InetUnicastAgentRouteTable *rt_table =
            agent()->vrf_table()->GetInet6UnicastRouteTable(vrf);
        rt_table->DeleteReq(agent()->local_peer(), vrf, addr, 128, NULL);
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFindV6(vrf, addr, 128) == false));
    }

    void DeleteRemoteRoute(const char *vrf, const char *ip) {
        Ip6Address addr = Ip6Address::from_string(ip);
        InetUnicastAgentRouteTable *rt_table =
            agent()->vrf_table()->GetInet6UnicastRouteTable(vrf);
        rt_table->DeleteReq(peer_, vrf, addr, 128,
                  new ControllerVmRoute(static_cast<BgpPeer *>(peer_)));
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFindV6(vrf, addr, 128) == false));
    }
    Agent *agent() {return agent_;}

    virtual void SetUp() {
        EXPECT_EQ(0U, agent()->pkt()->get_flow_proto()->FlowCount());
        hash_id = 1;
        client->Reset();
        CreateV6VmportEnv(input, 3, 1);
        client->WaitForIdle(5);
        AddIPAM("vn5", ipam_info, 4);
        client->WaitForIdle();

        WAIT_FOR(100, 1000, (VmPortActive(input, 0)) == true);
        WAIT_FOR(100, 1000, (VmPortV6Active(input, 0)) == true);
        WAIT_FOR(100, 1000, (VmPortActive(input, 1)) == true);
        WAIT_FOR(100, 1000, (VmPortV6Active(input, 1)) == true);
        WAIT_FOR(100, 1000, (VmPortActive(input, 2)) == true);
        WAIT_FOR(100, 1000, (VmPortV6Active(input, 2)) == true);
        WAIT_FOR(100, 1000, (VmPortPolicyEnable(input, 0)) == true);
        WAIT_FOR(100, 1000, (VmPortPolicyEnable(input, 1)) == true);
        WAIT_FOR(100, 1000, (VmPortPolicyEnable(input, 2)) == true);

        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmInterfaceGet(input[1].intf_id);
        assert(flow1);
        flow2 = VmInterfaceGet(input[2].intf_id);
        assert(flow2);

        /* Create interface flow3 in vn3 , vm4. Associate vn3 with acl2 */
        client->Reset();
        CreateV6VmportEnv(input2, 1, 2);
        client->WaitForIdle(5);
        AddIPAM("vn3", ipam_info2, 2);
        client->WaitForIdle();
        WAIT_FOR(100, 1000, (VmPortActive(input2, 0)) == true);
        WAIT_FOR(100, 1000, (VmPortV6Active(input2, 0)) == true);
        WAIT_FOR(100, 1000, (VmPortPolicyEnable(input2, 0)) == true);

        flow3 = VmInterfaceGet(input2[0].intf_id);
        assert(flow3);
        boost::system::error_code ec;
        peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                              "xmpp channel");
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();
        DeleteVmportEnv(input, 3, true, 1, NULL, NULL, true, true);
        client->WaitForIdle(3);
        DelIPAM("vn5");
        client->WaitForIdle();
        client->PortDelNotifyWait(3);
        WAIT_FOR(100, 1000, (VmPortFind(input, 0)) == false);
        WAIT_FOR(100, 1000, (VmPortFind(input, 1)) == false);
        WAIT_FOR(100, 1000, (VmPortFind(input, 2)) == false);

        client->Reset();
        DeleteVmportEnv(input2, 1, true, 2, NULL, NULL, true, true);
        client->WaitForIdle(3);
        DelIPAM("vn3");
        client->WaitForIdle();
        client->PortDelNotifyWait(1);
        WAIT_FOR(100, 1000, (VmPortFind(input2, 0)) == false);

        DeleteBgpPeer(peer_);
    }
private:
    BgpPeer *peer_;
    Agent *agent_;
};

TEST_F(FlowTestV6, FlowAdd_1) {
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow

        {  TestFlowPkt(Address::INET6, ip6_vm1_ip, ip6_vm2_ip, IPPROTO_ICMPV6,
                       0, 0, "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(Address::INET6, ip6_vm2_ip, ip6_vm1_ip, IPPROTO_ICMPV6,
                       0, 0, "vrf5", flow1->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(Address::INET6, ip6_vm1_ip, ip6_vm2_ip, IPPROTO_TCP,
                       1000, 200, "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(Address::INET6, ip6_vm2_ip, ip6_vm1_ip, IPPROTO_TCP,
                       200, 1000, "vrf5", flow1->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 4);
    EXPECT_EQ(4U, agent()->pkt()->get_flow_proto()->FlowCount());

    //Verify the ingress and egress flow counts
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    agent()->pkt()->get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(4U, in_count);
    EXPECT_EQ(4U, out_count);
}

//Egress flow test (IP fabric to VMPort - Same VN)
//Flow creation using GRE packets
TEST_F(FlowTestV6, FlowAdd_2) {
    EXPECT_EQ(0U, agent()->pkt()->get_flow_proto()->FlowCount());

    //Create PHYSICAL interface to receive GRE packets on it.
    PhysicalInterfaceKey key(eth_itf);
    Interface *intf = static_cast<Interface *>
        (agent()->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);

    //Create remote VM route. This will be used to figure out destination VN for
    //flow
    CreateRemoteRoute("vrf5", ip6_remote_vm1_ip, remote_router_ip, 30, "vn5");
    client->WaitForIdle();

    CreateRemoteRoute("vrf5", ip6_remote_vm3_ip, remote_router_ip, 32, "vn5");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM to local VM
        {
            TestFlowPkt(Address::INET6, ip6_remote_vm1_ip, ip6_vm1_ip,
                        IPPROTO_ICMPV6, 0, 0, "vrf5", remote_router_ip,
                        flow0->label()),
            {
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        },
        //Send a ICMP reply from local to remote VM
        {
            TestFlowPkt(Address::INET6, ip6_vm1_ip, ip6_remote_vm1_ip,
                        IPPROTO_ICMPV6, 0, 0, "vrf5", flow0->id()),
            {
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        },
        //Send a TCP flow from remote VM to local VM
        {
            TestFlowPkt(Address::INET6, ip6_remote_vm3_ip, ip6_vm3_ip,
                        IPPROTO_TCP, 1001, 1002, "vrf5", remote_router_ip,
                        flow2->label()),
            {
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        },
        //Send a TCP reply from local VM to remote VM
        {
            TestFlowPkt(Address::INET6, ip6_vm3_ip, ip6_remote_vm3_ip,
                        IPPROTO_TCP, 1002, 1001, "vrf5", flow2->id()),
            {
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        }
    };

    CreateFlow(flow, 4);
    EXPECT_EQ(4U, agent()->pkt()->get_flow_proto()->FlowCount());

    //Verify ingress and egress flow count
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    agent()->pkt()->get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //1. Remove remote VM routes
    DeleteRemoteRoute("vrf5", ip6_remote_vm1_ip);
    DeleteRemoteRoute("vrf5", ip6_remote_vm3_ip);
    client->WaitForIdle();
}

//Ingress flow test (VMport to VMport - Different VNs)
//Flow creation using IP and TCP packets
TEST_F(FlowTestV6, FlowAdd_3) {
    /* Add Local VM route of vrf3 to vrf5 */
    CreateLocalRoute("vrf5", ip6_vm4_ip, flow3, 19);
    /* Add Local VM route of vrf5 to vrf3 */
    CreateLocalRoute("vrf3", ip6_vm1_ip, flow0, 16);

    TestFlow flow[] = {
        //Send a ICMP request from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET6, ip6_vm1_ip, ip6_vm4_ip, IPPROTO_ICMPV6,
                        0, 0, "vrf5", flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an ICMP reply from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET6, ip6_vm4_ip, ip6_vm1_ip, IPPROTO_ICMPV6,
                        0, 0, "vrf3", flow3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET6, ip6_vm1_ip, ip6_vm4_ip, IPPROTO_TCP,
                        200, 300, "vrf5", flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET6, ip6_vm4_ip, ip6_vm1_ip, IPPROTO_TCP,
                        300, 200, "vrf3", flow3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };

    CreateFlow(flow, 4);
    client->WaitForIdle();
    //Verify ingress and egress flow count of VN "vn5"
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    agent()->pkt()->get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //Verify ingress and egress flow count of VN "vn3"
    fe = flow[1].pkt_.FlowFetch();
    vn = fe->data().vn_entry.get();
    agent()->pkt()->get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //1. Remove remote VM routes
    DeleteRoute("vrf5", ip6_vm4_ip);
    DeleteRoute("vrf3", ip6_vm1_ip);
    client->WaitForIdle();
}

//Egress flow test (IP fabric to VMport - Different VNs)
//Flow creation using GRE packets
TEST_F(FlowTestV6, FlowAdd_4) {
    /* Add remote VN route to vrf5 */
    CreateRemoteRoute("vrf5", ip6_remote_vm4_ip, remote_router_ip, 8, "vn3");
    Ip4Address rid1 = agent()->router_id();
    std::string router_ip_str = rid1.to_string();

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET6, ip6_remote_vm4_ip, ip6_vm1_ip,
                        IPPROTO_ICMPV6, 0, 0, "vrf5", remote_router_ip,
                        flow0->label()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a ICMP reply from local VM in vn5 to remote VM in vn3
        {
            TestFlowPkt(Address::INET6, ip6_vm1_ip, ip6_remote_vm4_ip,
                        IPPROTO_ICMPV6, 0, 0, "vrf5", flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send a TCP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET6, ip6_remote_vm4_ip, ip6_vm1_ip,
                        IPPROTO_TCP, 1006, 1007, "vrf5", remote_router_ip,
                        flow0->label()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP reply from local VM in vn5 to remote VM in vn3
        {
            TestFlowPkt(Address::INET6, ip6_vm1_ip, ip6_remote_vm4_ip,
                        IPPROTO_TCP, 1007, 1006, "vrf5", flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        }
    };

    CreateFlow(flow, 4);
    client->WaitForIdle();
    //Verify ingress and egress flow count of VN "vn5"
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    agent()->pkt()->get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //Verify ingress and egress flow count of VN "vn3"
    fe = flow[1].pkt_.FlowFetch();
    vn = fe->data().vn_entry.get();
    agent()->pkt()->get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //1. Remove remote VM routes
    DeleteRemoteRoute("vrf5", ip6_remote_vm4_ip);
    client->WaitForIdle();
}

//Duplicate Ingress flow test (VMport to VMport - Same VN)
//Flow creation using IP
TEST_F(FlowTestV6, FlowAdd_5) {
    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET6, ip6_vm1_ip, ip6_vm2_ip, IPPROTO_ICMPV6,
                        0, 0, "vrf5", flow0->id()),
            {
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->get_flow_proto()->FlowCount());

    //Verify ingress and egress flow count of VN "vn5"
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    agent()->pkt()->get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //Send duplicate flow creation request
    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->get_flow_proto()->FlowCount());

    //Verify ingress and egress flow count for VN "vn5" does not change
    fe = flow[0].pkt_.FlowFetch();
    vn = fe->data().vn_entry.get();
    agent()->pkt()->get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client =
        TestInit(init_file, ksync_init, true, true, true, (1000000 * 60 * 10),
                 (3 * 60 * 1000));
    if (vm.count("config")) {
        eth_itf = Agent::GetInstance()->fabric_interface_name();
    } else {
        eth_itf = "eth0";
        PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                                     eth_itf,
                                     Agent::GetInstance()->fabric_vrf_name(),
                                     PhysicalInterface::FABRIC,
                                     PhysicalInterface::ETHERNET, false,
                                     nil_uuid(), Ip4Address(0),
                                     Interface::TRANSPORT_ETHERNET);
        client->WaitForIdle();
    }

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
