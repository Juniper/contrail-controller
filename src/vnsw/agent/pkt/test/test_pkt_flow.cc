/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"

#define MAX_VNET 4

void RouterIdDepInit() {
}

struct TestFlowKey {
    uint16_t        vrfid_;
    const char      *sip_;
    const char      *dip_;
    uint8_t         proto_;
    uint16_t        sport_;
    uint16_t        dport_;
};

#define vm1_ip "11.1.1.1"
#define vm2_ip "11.1.1.2"
#define vm3_ip "12.1.1.1"
#define vm4_ip "13.1.1.1"
#define vm5_ip "14.1.1.1"
#define vm1_fip "14.1.1.100"
#define vm1_fip2 "14.1.1.101"
#define vm2_fip "14.1.1.100"
#define remote_vm1_ip "11.1.1.3"
#define remote_vm3_ip "12.1.1.3"
#define remote_vm4_ip "13.1.1.2"
#define remote_router_ip "10.1.1.2"

int fd_table[MAX_VNET];

struct PortInfo input[] = {
        {"flow0", 6, vm1_ip, "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, vm2_ip, "00:00:00:01:01:02", 5, 2},
        {"flow2", 8, vm3_ip, "00:00:00:01:01:03", 5, 3},
};

struct PortInfo input2[] = {
        {"flow3", 9, vm4_ip, "00:00:00:01:01:04", 3, 4},
};

struct PortInfo input3[] = {
        {"flow4", 10, vm5_ip, "00:00:00:01:01:06", 4, 5},
};

int hash_id;
VmPortInterface *flow0;
VmPortInterface *flow1;
VmPortInterface *flow2;
VmPortInterface *flow3;
VmPortInterface *flow4;
std::string eth_itf;

class FlowTest : public ::testing::Test {
public:
    bool FlowTableWait(int count) {
        int i = 1000;
        while (i > 0) {
            i--;
            if (FlowTable::GetFlowTableObject()->Size() == count) {
                break;
            }
            client->WaitForIdle();
            usleep(1);
        }
        return (FlowTable::GetFlowTableObject()->Size() == count);
    }

    void FlushFlowTable() {
        FlowTable::GetFlowTableObject()->DeleteAll();
        client->WaitForIdle();
        EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());
    }

    void CreateLocalRoute(const char *vrf, const char *ip,
                          VmPortInterface *intf, int label) {
        Ip4Address addr = Ip4Address::from_string(ip);
        Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->AddLocalVmRoute
            (NULL, vrf, addr, 32, intf->GetUuid(),
             intf->GetVnEntry()->GetName(), label); 
        client->WaitForIdle();
        EXPECT_TRUE(RouteFind(vrf, addr, 32));
    }

    void CreateRemoteRoute(const char *vrf, const char *remote_vm, 
                           const char *serv, int label, const char *vn) {
        Ip4Address addr = Ip4Address::from_string(remote_vm);
        Ip4Address gw = Ip4Address::from_string(serv);
        Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->AddRemoteVmRoute
            (peer_, vrf, addr, 32, gw, TunnelType::AllType(), label, vn);
        client->WaitForIdle();
        EXPECT_TRUE(RouteFind(vrf, addr, 32));
    }

    void DeleteRoute(const char *vrf, const char *ip) {
        Ip4Address addr = Ip4Address::from_string(ip);
        Agent::Agent::GetInstance()->
            GetDefaultInet4UnicastRouteTable()->DeleteReq(NULL, 
                                                vrf, addr, 32);
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFind(vrf, addr, 32) == false));
    }

    void DeleteRemoteRoute(const char *vrf, const char *ip) {
        Ip4Address addr = Ip4Address::from_string(ip);
        Agent::Agent::GetInstance()->
            GetDefaultInet4UnicastRouteTable()->DeleteReq(peer_, 
                vrf, addr, 32);
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFind(vrf, addr, 32) == false));
    }

    static void FlowDel(int vrf, const char *sip, const char *dip,
                        int proto, int sport, int dport, 
                        bool del_reverse_flow) {
        FlowKey key;

        key.vrf = vrf;
        key.src.ipv4 = ntohl(inet_addr(sip));
        key.dst.ipv4 = ntohl(inet_addr(dip));
        key.protocol = proto;
        key.src_port = sport;
        key.dst_port = dport;
        FlowTable::GetFlowTableObject()->DeleteRevFlow(key, del_reverse_flow);
        client->WaitForIdle();
    }

    static void RunFlowAudit() {
        FlowTableKSyncObject::GetKSyncObject()->AuditProcess(
                FlowTableKSyncObject::GetKSyncObject()->GetKSyncObject());
        FlowTableKSyncObject::GetKSyncObject()->AuditProcess(
                FlowTableKSyncObject::GetKSyncObject()->GetKSyncObject());
    }

    static bool KFlowHoldAdd(int hash_id, int vrf, const char *sip, 
                             const char *dip, int proto, int sport, int dport) {
        if (hash_id >= 
                FlowTableKSyncObject::GetKSyncObject()->GetFlowTableSize()) {
            return false;
        }
        if (ksync_init_) {
            return false;
        }

        vr_flow_entry *vr_flow = KSyncSockTypeMap::GetFlowEntry(hash_id);

        vr_flow_req req;
        req.set_fr_index(hash_id);
        req.set_fr_flow_sip(inet_addr(sip));
        req.set_fr_flow_dip(inet_addr(dip));
        req.set_fr_flow_proto(proto);
        req.set_fr_flow_sport(htons(sport));
        req.set_fr_flow_dport(htons(dport));
        req.set_fr_flow_vrf(vrf);

        vr_flow->fe_action = VR_FLOW_ACTION_HOLD;
        KSyncSockTypeMap::SetFlowEntry(&req, false, true);

        return true;
    }

    static void KFlowPurgeHold() {
        if (ksync_init_) {
            return;
        }

        for (int count = 0; 
             count < FlowTableKSyncObject::GetKSyncObject()->GetFlowTableSize();
             count++) {
            vr_flow_entry *vr_flow = KSyncSockTypeMap::GetFlowEntry(count);
            vr_flow->fe_action = VR_FLOW_ACTION_DROP;
            vr_flow_req req;
            req.set_fr_index(hash_id);
            KSyncSockTypeMap::SetFlowEntry(&req, false, false);
        }

        return;
    }

    static void FlowAdd(int hash_id, int vrf, const char *sip, const char *dip,
                        int proto, int sport, int dport, const char *nat_sip,
                        const char *nat_dip, int nat_vrf) {
        PktInfo pkt_1;
        PktInfo *pkt = &pkt_1;
        PktFlowInfo flow_info_1(pkt);
        PktFlowInfo *flow_info = &flow_info_1;
        MatchPolicy policy;
        string svn = "svn";
        string dvn = "dvn";

        memset(pkt, 0, sizeof(*pkt));
        pkt->vrf = vrf;
        pkt->ip_saddr = ntohl(inet_addr(sip));
        pkt->ip_daddr = ntohl(inet_addr(dip));
        pkt->ip_proto = proto;
        pkt->sport = sport;
        pkt->dport = dport;
        policy.action_info.action = (1 << TrafficAction::PASS);

        flow_info->nat_vrf = nat_vrf;
        if (nat_sip) {
            flow_info->nat_ip_saddr = ntohl(inet_addr(nat_sip));
        } else {
            flow_info->nat_ip_saddr = pkt->ip_saddr;
        }

        if (nat_dip) {
            flow_info->nat_ip_daddr = ntohl(inet_addr(nat_dip));
        } else {
            flow_info->nat_ip_daddr = pkt->ip_daddr;
        }
        flow_info->nat_sport = sport;
        flow_info->nat_dport = dport;

        if (pkt->ip_saddr != flow_info->nat_ip_saddr ||
            pkt->ip_daddr != flow_info->nat_ip_daddr) {
            flow_info->nat_done = true;
        } else {
            flow_info->nat_done = false;
        }
        pkt->agent_hdr.cmd_param = hash_id;
        flow_info->source_vn = &svn;
        flow_info->dest_vn = &dvn;

        SecurityGroupList empty_sg_id_l;
        flow_info->source_sg_id_l = &empty_sg_id_l;
        flow_info->dest_sg_id_l = &empty_sg_id_l;

        PktControlInfo in;
        PktControlInfo out;

        flow_info->Add(pkt, &in, &out);
        //FlowTable::GetFlowTableObject()->Add(pkt, flow_info);
        client->WaitForIdle();
    }

    static void TestTearDown() {
        client->Reset();
        if (ksync_init_) {
            DeleteTapIntf(fd_table, MAX_VNET);
        }
        client->SetFlowAgeExclusionPolicy();
        client->WaitForIdle();
    }

    static void TestSetup(bool ksync_init) {
        ksync_init_ = ksync_init;
        if (ksync_init_) {
            CreateTapInterfaces("flow", MAX_VNET, fd_table);
            client->WaitForIdle();
        }
    }

protected:
    virtual void SetUp() {
        unsigned int vn_count = 0;
        EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());
        //Reset flow age
        client->EnqueueFlowAge();
        client->WaitForIdle();
        hash_id = 1;
        client->Reset();
        CreateVmportEnv(input, 3, 1);
        client->WaitForIdle();
        vn_count++;

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortActive(input, 2));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 2));
        EXPECT_EQ(7U, Agent::GetInstance()->GetInterfaceTable()->Size());
        EXPECT_EQ(3U, Agent::GetInstance()->GetVmTable()->Size());
        EXPECT_EQ(vn_count, Agent::GetInstance()->GetVnTable()->Size());
        EXPECT_EQ(3U, Agent::GetInstance()->GetIntfCfgTable()->Size());

        flow0 = VmPortInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmPortInterfaceGet(input[1].intf_id);
        assert(flow1);
        flow2 = VmPortInterfaceGet(input[2].intf_id);
        assert(flow2);

        /* Create interface flow3 in vn3 , vm4. Associate vn3 with acl2 */
        client->Reset();
        CreateVmportEnv(input2, 1, 2);
        client->WaitForIdle();
        vn_count++;
        EXPECT_TRUE(VmPortActive(input2, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input2, 0));
        EXPECT_EQ(8U, Agent::GetInstance()->GetInterfaceTable()->Size());
        EXPECT_EQ(4U, Agent::GetInstance()->GetVmTable()->Size());
        EXPECT_EQ(vn_count, Agent::GetInstance()->GetVnTable()->Size());
        EXPECT_EQ(4U, Agent::GetInstance()->GetIntfCfgTable()->Size());
        EXPECT_EQ(2U, Agent::GetInstance()->GetAclTable()->Size());

        flow3 = VmPortInterfaceGet(input2[0].intf_id);
        assert(flow3);

        /* Create interface flow4 in vn4 */
        client->Reset();
        CreateVmportEnv(input3, 1);
        client->WaitForIdle();
        vn_count++;
        EXPECT_TRUE(VmPortActive(input3, 0));
        EXPECT_EQ(9U, Agent::GetInstance()->GetInterfaceTable()->Size());
        EXPECT_EQ(5U, Agent::GetInstance()->GetVmTable()->Size());
        EXPECT_EQ(vn_count, Agent::GetInstance()->GetVnTable()->Size());
        EXPECT_EQ(5U, Agent::GetInstance()->GetIntfCfgTable()->Size());
        flow4 = VmPortInterfaceGet(input3[0].intf_id);
        assert(flow4);
        // Configure Floating-IP
        AddFloatingIpPool("fip-pool1", 1);
        AddFloatingIp("fip1", 1, "14.1.1.100");
        AddFloatingIp("fip2", 1, "14.1.1.101");
        AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        AddLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool1");
        AddLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn4");
        AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
        client->WaitForIdle();
        peer_ = new BgpPeer("BGP Peer 1", NULL, -1);
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();
        DeleteVmportEnv(input, 3, true, 1);
        client->PortDelNotifyWait(3);
        client->WaitForIdle();
        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));
        EXPECT_FALSE(VmPortFind(input, 2));
        EXPECT_EQ(6U, Agent::GetInstance()->GetInterfaceTable()->Size());
        EXPECT_EQ(2U, Agent::GetInstance()->GetIntfCfgTable()->Size());

        client->Reset();
        DeleteVmportEnv(input2, 1, true, 2);
        client->PortDelNotifyWait(1);
        client->WaitForIdle();
        EXPECT_EQ(5U, Agent::GetInstance()->GetInterfaceTable()->Size());
        EXPECT_EQ(1U, Agent::GetInstance()->GetIntfCfgTable()->Size());
        EXPECT_FALSE(VmPortFind(input2, 0));

        client->Reset();
        DeleteVmportEnv(input3, 1, true);
        client->PortDelNotifyWait(1);
        client->WaitForIdle();
        EXPECT_EQ(4U, Agent::GetInstance()->GetInterfaceTable()->Size());
        EXPECT_EQ(0U, Agent::GetInstance()->GetIntfCfgTable()->Size());
        EXPECT_FALSE(VmPortFind(input3, 0));

        EXPECT_EQ(0U, Agent::GetInstance()->GetVmTable()->Size());
        EXPECT_EQ(0U, Agent::GetInstance()->GetVnTable()->Size());
        EXPECT_EQ(0U, Agent::GetInstance()->GetAclTable()->Size());
        delete static_cast<Peer *>(peer_);
    }

private:
    static bool ksync_init_;
    BgpPeer *peer_;
};

bool FlowTest::ksync_init_;
//Ingress flow test (VMport to VMport - Same VN)
//Flow creation using IP and TCP packets
TEST_F(FlowTest, FlowAdd_1) {
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(vm1_ip, vm2_ip, 1, 0, 0, "vrf5", 
                flow0->GetInterfaceId()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(vm2_ip, vm1_ip, 1, 0, 0, "vrf5", 
                flow1->GetInterfaceId()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(vm1_ip, vm2_ip, IPPROTO_TCP, 1000, 200, 
                "vrf5", flow0->GetInterfaceId()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(vm2_ip, vm1_ip, IPPROTO_TCP, 200, 1000, 
                "vrf5", flow1->GetInterfaceId()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 4);
    EXPECT_EQ(4U, FlowTable::GetFlowTableObject()->Size());
}

//Egress flow test (IP fabric to VMPort - Same VN)
//Flow creation using GRE packets
TEST_F(FlowTest, FlowAdd_2) {
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());

    //Create ETH interface to receive GRE packets on it.
    EthInterfaceKey key(nil_uuid(), eth_itf);
    Interface *intf = static_cast<Interface *>
        (Agent::GetInstance()->GetInterfaceTable()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);

    //Create remote VM route. This will be used to figure out destination VN for
    //flow
    CreateRemoteRoute("vrf5", remote_vm1_ip, remote_router_ip, 30, "vn5");
    client->WaitForIdle();

    CreateRemoteRoute("vrf5", remote_vm3_ip, remote_router_ip, 32, "vn5");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM to local VM
        {
            TestFlowPkt(remote_vm1_ip, vm1_ip, 1, 0, 0, "vrf5", 
                    remote_router_ip, 16),
            { 
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        },
        //Send a ICMP reply from local to remote VM
        {
            TestFlowPkt(vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId()),
            {
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        },
        //Send a TCP flow from remote VM to local VM
        {
            TestFlowPkt(remote_vm3_ip, vm3_ip, IPPROTO_TCP, 1001, 1002,
                    "vrf5", remote_router_ip, 18),
            {
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        },
        //Send a TCP reply from local VM to remote VM
        {
            TestFlowPkt(vm3_ip, remote_vm3_ip, IPPROTO_TCP, 1002, 1001,
                    "vrf5", flow2->GetInterfaceId()),
            {
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        }
    };

    CreateFlow(flow, 4);
    //1. Remove remote VM routes
    DeleteRemoteRoute("vrf5", remote_vm1_ip);
    DeleteRemoteRoute("vrf5", remote_vm3_ip);
    client->WaitForIdle();
}

//Ingress flow test (VMport to VMport - Different VNs)
//Flow creation using IP and TCP packets
TEST_F(FlowTest, FlowAdd_3) {

    /* Add Local VM route of vrf3 to vrf5 */
    CreateLocalRoute("vrf5", vm4_ip, flow3, 19);
    /* Add Local VM route of vrf5 to vrf3 */
    CreateLocalRoute("vrf3", vm1_ip, flow0, 16);

    TestFlow flow[] = {
        //Send a ICMP request from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(vm1_ip, vm4_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an ICMP reply from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(vm4_ip, vm1_ip, 1, 0, 0, "vrf3", 
                    flow3->GetInterfaceId()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(vm1_ip, vm4_ip, IPPROTO_TCP, 200, 300, "vrf5", 
                    flow0->GetInterfaceId()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(vm4_ip, vm1_ip, IPPROTO_TCP, 300, 200, "vrf3", 
                    flow3->GetInterfaceId()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };

    CreateFlow(flow, 4);
    client->WaitForIdle();

    //1. Remove remote VM routes
    DeleteRoute("vrf5", vm4_ip);
    DeleteRoute("vrf3", vm1_ip);
    client->WaitForIdle();
}

//Egress flow test (IP fabric to VMport - Different VNs)
//Flow creation using GRE packets
TEST_F(FlowTest, FlowAdd_4) {
    /* Add remote VN route to vrf5 */
    CreateRemoteRoute("vrf5", remote_vm4_ip, remote_router_ip, 8, "vn3");
    Ip4Address rid1 = Agent::GetInstance()->GetRouterId();
    std::string router_ip_str = rid1.to_string();

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(remote_vm4_ip, vm1_ip, 1, 0, 0, "vrf5", 
                    remote_router_ip, 16),
            { 
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a ICMP reply from local VM in vn5 to remote VM in vn3
        {
            TestFlowPkt(vm1_ip, remote_vm4_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send a TCP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(remote_vm4_ip, vm1_ip, IPPROTO_TCP, 1006, 1007,
                    "vrf5", remote_router_ip, 16),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP reply from local VM in vn5 to remote VM in vn3
        {
            TestFlowPkt(vm1_ip, remote_vm4_ip, IPPROTO_TCP, 1007, 1006,
                    "vrf5", flow0->GetInterfaceId()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        }
    };

    CreateFlow(flow, 4); 
    client->WaitForIdle();
    //1. Remove remote VM routes
    DeleteRemoteRoute("vrf5", remote_vm4_ip);
    client->WaitForIdle();
}

//Duplicate Ingress flow test (VMport to VMport - Same VN)
//Flow creation using IP
TEST_F(FlowTest, FlowAdd_5) {
    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(vm1_ip, vm2_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId()),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());
    CreateFlow(flow, 1);
    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());
}

//Duplicate Ingress flow test for flow having reverse flow (VMport to VMport - Same VN)
//Flow creation using TCP packets
TEST_F(FlowTest, FlowAdd_6) {
    TestFlow fwd_flow[] = {
        {
            TestFlowPkt(vm1_ip, vm2_ip, IPPROTO_TCP, 1000, 200, "vrf5", 
                    flow0->GetInterfaceId()),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    TestFlow rev_flow[] = {
        {
            TestFlowPkt(vm2_ip, vm1_ip, IPPROTO_TCP, 200, 1000, "vrf5",
                    flow1->GetInterfaceId()),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        }
    };


    CreateFlow(fwd_flow, 1);
    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());

    CreateFlow(rev_flow, 1);
    //Send request for reverse flow again
    CreateFlow(rev_flow, 1);
    //Send request for forward flow again 
    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());
}

TEST_F(FlowTest, FlowAge_1) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = 
        AgentUve::GetInstance()->GetFlowStatsCollector()->GetFlowAgeTime();
    //Set the flow age time to 100 microsecond
    AgentUve::GetInstance()->GetFlowStatsCollector()->SetFlowAgeTime(
            tmp_age_time);

    //Create bidirectional flow
    TestFlow flow[] = {
        {
            TestFlowPkt(vm1_ip, vm2_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 1),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        },
        {
            TestFlowPkt(vm2_ip, vm1_ip, 1, 0, 0, "vrf5", 
                    flow1->GetInterfaceId(), 2),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    client->WaitForIdle();

    // Sleep for age-time
    usleep(tmp_age_time + 10);

    //Trigger flow-aging and make sure they are not removed because 
    //of difference in stats between oper flow and Kernel flow
    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());

    //Update reverse-flow stats to postpone aging of forward-flow
    KSyncSockTypeMap::IncrFlowStats(2, 1, 30);

    //Trigger flow-aging
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    //Verify that forward-flow is not removed even though it is eligible to be removed
    //because reverse-flow is not aged
    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());

    // Sleep for age-time
    usleep(tmp_age_time + 10);

    //Trigger to Age the flow
    client->EnqueueFlowAge();
    client->WaitForIdle();

    //Verify that both flows get removed after reverse-flow ages
    WAIT_FOR(100, 1, (0U == FlowTable::GetFlowTableObject()->Size()));

    //Restore flow aging time
    AgentUve::GetInstance()->
        GetFlowStatsCollector()->SetFlowAgeTime(bkp_age_time);
}

// Flow with index -1 ages out on age-time irrespective of stats
TEST_F(FlowTest, FlowAge_2) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = 
        AgentUve::GetInstance()->GetFlowStatsCollector()->GetFlowAgeTime();
    //Set the flow age time to 100 microsecond
    AgentUve::GetInstance()->
        GetFlowStatsCollector()->SetFlowAgeTime(tmp_age_time);

    //Create a flow with index -1
    TestFlow flow[] = {
        {
            TestFlowPkt(vm1_ip, vm2_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), -1),
            { }
        }
    };

    CreateFlow(flow, 1);
    //Trigger flow-aging
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    //Verify that both flows get removed after reverse-flow ages
    WAIT_FOR(100, 1, (0U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());

    //Restore flow aging time
    AgentUve::GetInstance()->
        GetFlowStatsCollector()->SetFlowAgeTime(bkp_age_time);
}

// Aging with more than 2 entries
TEST_F(FlowTest, FlowAge_3) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = 
        AgentUve::GetInstance()->GetFlowStatsCollector()->GetFlowAgeTime();
    //Set the flow age time to 100 microsecond
    AgentUve::GetInstance()->
        GetFlowStatsCollector()->SetFlowAgeTime(tmp_age_time);

    //Create bidirectional flow
    TestFlow flow[] = {
        {
            TestFlowPkt(vm1_ip, vm2_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 1),
            { }
        },
        {
            TestFlowPkt(vm1_ip, vm3_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 2),
            { }
        },
        {
            TestFlowPkt(vm1_ip, vm4_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 3),
            { }
        },
        {
            TestFlowPkt(vm2_ip, vm3_ip, 1, 0, 0, "vrf5", 
                    flow1->GetInterfaceId(), 4),
            { }
        },
    };

    CreateFlow(flow, 4);
    EXPECT_EQ(8U, FlowTable::GetFlowTableObject()->Size());

    // Flow entries are created with #pkts = 1. 
    // Do first sleep for aging to work correctly below
    client->EnqueueFlowAge();
    client->WaitForIdle();

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (0U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());

    // Delete of 2 linked flows
    CreateFlow(flow, 2);
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (0U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());

    // Delete 2 out of 4 linked entries
    CreateFlow(flow, 2);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (2U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());
    EXPECT_TRUE(FlowGet(1, vm1_ip, vm2_ip, 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, vm2_ip, vm1_ip, 1, 0, 0, false, -1, -1));

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (0U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());

    //Restore flow aging time
    AgentUve::GetInstance()->
        GetFlowStatsCollector()->SetFlowAgeTime(bkp_age_time);
}

TEST_F(FlowTest, ScaleFlowAge_1) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = 
        AgentUve::GetInstance()->GetFlowStatsCollector()->GetFlowAgeTime();
    //Set the flow age time to 100 microsecond
    AgentUve::GetInstance()->
        GetFlowStatsCollector()->SetFlowAgeTime(tmp_age_time);
    int count_per_pass = FlowStatsCollector::FlowCountPerPass;

    for (int i = 0; i < count_per_pass + 10; i++) {
        Ip4Address dip(0x1010101 + i);
        //Add route for all of them
        CreateRemoteRoute("vrf5", dip.to_string().c_str(), remote_router_ip, 
                10, "vn5");
        TestFlow flow[]=  {
            {
                TestFlowPkt(vm1_ip, dip.to_string(), 1, 0, 0, "vrf5", 
                        flow0->GetInterfaceId(), i),
                { }
            },
            {
                TestFlowPkt(dip.to_string(), vm1_ip, 1, 0, 0, "vrf5",
                        flow1->GetInterfaceId(), i + 100),
                { }
            }
        };
        CreateFlow(flow, 2);
    }
    EXPECT_EQ(((count_per_pass + 10U)*2), 
            FlowTable::GetFlowTableObject()->Size());

    // Flow entries are created with #pkts = 1. 
    // Do first sleep for aging to work correctly below
    client->EnqueueFlowAge();
    client->WaitForIdle();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_EQ(((count_per_pass + 10U)*2), 
            FlowTable::GetFlowTableObject()->Size());

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (20U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(20U, FlowTable::GetFlowTableObject()->Size());

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (0U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());

    //Restore flow aging time
    AgentUve::GetInstance()->
        GetFlowStatsCollector()->SetFlowAgeTime(bkp_age_time);
}

TEST_F(FlowTest, ScaleFlowAge_2) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = 
        AgentUve::GetInstance()->GetFlowStatsCollector()->GetFlowAgeTime();
    //Set the flow age time to 100 microsecond
    AgentUve::GetInstance()->
        GetFlowStatsCollector()->SetFlowAgeTime(tmp_age_time);
    int count_per_pass = FlowStatsCollector::FlowCountPerPass;

    for (int i = 0; i < count_per_pass + 10; i++) {
        Ip4Address dip(0x1010101 + i);
        //Add route for all of them
        CreateRemoteRoute("vrf5", dip.to_string().c_str(), remote_router_ip, 
                          10, "vn5");
        TestFlow flow[]=  {
            {
                TestFlowPkt(vm1_ip, dip.to_string(), 1, 0, 0, "vrf5", 
                        flow0->GetInterfaceId(), i),
                { }
            },
            {
                TestFlowPkt(dip.to_string(), vm1_ip, 1, 0, 0, "vrf5",
                        flow1->GetInterfaceId(), i + count_per_pass + 10),
                { }
            }
        };
        CreateFlow(flow, 2);
    }
    EXPECT_EQ(((count_per_pass + 10U)*2), 
            FlowTable::GetFlowTableObject()->Size());

    // Flow entries are created with #pkts = 1. 
    // Do first sleep for aging to work correctly below
    client->EnqueueFlowAge();
    client->WaitForIdle();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_EQ(((count_per_pass + 10U)*2), 
            FlowTable::GetFlowTableObject()->Size());

    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (20U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(20U, FlowTable::GetFlowTableObject()->Size());

    KSyncSockTypeMap::IncrFlowStats(201, 1, 30);
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (2U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (0U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());

    //Restore flow aging time
    AgentUve::GetInstance()->
        GetFlowStatsCollector()->SetFlowAgeTime(bkp_age_time);
}

TEST_F(FlowTest, Nat_FlowAge_1) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = 
        AgentUve::GetInstance()->GetFlowStatsCollector()->GetFlowAgeTime();
    //Set the flow age time to 100 microsecond
    AgentUve::GetInstance()->
        GetFlowStatsCollector()->SetFlowAgeTime(tmp_age_time);

    TestFlow flow[] = {
        {
            TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 1),
            { 
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0)
            }
        }
    };

    CreateFlow(flow, 1);
    // Update stats so that flow is not aged
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);

    // Sleep for age-time
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->GetVrfId(), vm1_ip, vm5_ip, 1, 0, 0, 
                        false, -1, -1));
    EXPECT_TRUE(FlowGet(VrfGet("vrf4")->GetVrfId(), vm5_ip, vm1_fip, 1, 0, 0, 
                        false, -1, -1));

    // Sleep for age-time
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    WAIT_FOR(100, 1, (0U == FlowTable::GetFlowTableObject()->Size()));

    //Restore flow aging time
    AgentUve::GetInstance()->
        GetFlowStatsCollector()->SetFlowAgeTime(bkp_age_time);
}

#if 0
TEST_F(FlowTest, teardown) {
    FlowTest::TestTearDown();
}
#endif

TEST_F(FlowTest, NonNatFlowAdd_1) {
    TestFlow flow[] = {
        {
            TestFlowPkt(vm1_ip, vm2_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId()),
            {}
        },
        {
            TestFlowPkt(vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                    flow1->GetInterfaceId()),
            {}
        }   
    };

    CreateFlow(flow, 1);

    // Add duplicate flow
    CreateFlow(flow, 1);

    // Add reverse flow
    CreateFlow(flow + 1, 1);

    // Delete forward flow. It should delete both flows
    DeleteFlow(flow, 1);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0));

    // Add forward and reverse flow
    CreateFlow(flow, 2);
    // Delete reverse flow. Should delete both flows
    DeleteFlow(flow + 1, 1);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NonNatDupFlowAdd_1) {
    TestFlow flow[] = {
        {
             TestFlowPkt(vm1_ip, vm2_ip, 1, 0, 0, "vrf5", 
                                flow0->GetInterfaceId()),
            {}
        },
        {
             TestFlowPkt(vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                                flow1->GetInterfaceId()),
            {}
        }   
    };

    // Add forward and reverse flow
    CreateFlow(flow, 2); 

    // Add duplicate forward and reverse flow
    CreateFlow(flow, 2);

    // Delete forward flow. It should delete both flows
    DeleteFlow(flow, 1);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NonNatAddOldNat_1) {
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId()),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    TestFlow non_nat_flow[] = {
        {
            TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId()),
            { }
        }
    };

    // Add NAT flow
    CreateFlow(nat_flow, 1);

    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1"); 
    client->WaitForIdle();
        
    // Add Non-NAT forward flow
    CreateFlow(non_nat_flow, 1);
    //Make sure NAT reverse flow is also deleted
    EXPECT_TRUE(FlowFail(VrfGet("vrf4")->GetVrfId(), vm5_ip, vm1_fip, 1, 
                          0, 0));

    DeleteFlow(non_nat_flow, 1); 
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->GetVrfId(), vm5_ip, vm1_ip, 1, 
                         0, 0));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}


TEST_F(FlowTest, NonNatAddOldNat_2) {
#if 0
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(vm5_ip, vm1_fip, 1, 0, 0, "vrf4", 
                    flow4->GetInterfaceId()),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0) 
            }
        }
    };

    TestFlow non_nat_flow[] = {
        {
             TestFlowPkt(vm5_ip, vm1_fip, 1, 0, 0, "vrf4", 
                    flow4->GetInterfaceId()),
            {
                new VerifyVn(unknown_vn_, unknown_vn_)
            }
        }
    };

    // Add NAT flow
    CreateFlow(nat_flow, 1);

    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1"); 
    client->WaitForIdle();
    //Make sure NAT reverse flow is deleted
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->GetVrfId(), vm1_ip, vm5_ip, 1, 
                         0, 0));
    EXPECT_TRUE(FlowFail(VrfGet("vrf4")->GetVrfId(), vm5_ip, vm1_fip, 1, 
                         0, 0));

    // Add Non-NAT reverse flow
    CreateFlow(non_nat_flow, 1);
    DeleteFlow(non_nat_flow, 1); 
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->GetVrfId(), vm1_ip, vm5_ip, 1, 
                         0, 0));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
#endif
}

TEST_F(FlowTest, NonNatAddOldNat_3) {
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId()),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    TestFlow non_nat_flow[] = {
        {
            TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId()),
            {
                new VerifyVn(unknown_vn_, unknown_vn_)
            }
        }
    };

    // Add NAT flow
    CreateFlow(nat_flow, 1);

    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1"); 
    client->WaitForIdle();
    //Make sure NAT reverse flow is also deleted
    EXPECT_TRUE(FlowFail(VrfGet("vrf4")->GetVrfId(), vm5_ip, vm1_fip, 1, 
                0, 0));

    // Add Non-NAT forward flow
    CreateFlow(non_nat_flow, 1);
    DeleteFlow(non_nat_flow, 1); 
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->GetVrfId(), vm5_ip, vm1_ip, 1, 
                0, 0));
    //Make sure NAT reverse flow is not present
    EXPECT_TRUE(FlowFail(VrfGet("vrf4")->GetVrfId(), vm5_ip, vm1_fip, 1, 
                0, 0));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatFlowAdd_1) {
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);
    //Add duplicate flow
    CreateFlow(nat_flow, 1);
    
    //Send a reverse nat flow packet
    TestFlow nat_rev_flow[] = {
        {
            TestFlowPkt(vm5_ip, vm1_fip, 1, 0, 0, "vrf4", 
                    flow4->GetInterfaceId(), 2),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0)
            }
        }
    };
    CreateFlow(nat_rev_flow, 1);

    //Delete a forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    EXPECT_TRUE(FlowFail(VrfGet("vrf4")->GetVrfId(), vm5_ip, vm1_fip, 1, 0, 0));

    CreateFlow(nat_flow, 1); 
    DeleteFlow(nat_rev_flow, 1);
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->GetVrfId(), vm1_ip, vm5_ip, 1, 0, 0));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatFlowAdd_2) {
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);
    //Add duplicate flow
    CreateFlow(nat_flow, 1);
    //Send a reverse nat flow packet
    TestFlow nat_rev_flow[] = {
        {
             TestFlowPkt(vm5_ip, vm1_fip, 1, 0, 0, "vrf4", 
                    flow4->GetInterfaceId(), 2),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0)
            }
        }
    };
    CreateFlow(nat_rev_flow, 1);

    //Delete a forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    EXPECT_TRUE(FlowFail(VrfGet("vrf3")->GetVrfId(), vm5_ip, vm1_fip, 1, 0, 0));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatAddOldNonNat_1) {
    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1"); 
    client->WaitForIdle();

    TestFlow non_nat_flow[] = {
        {
             TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 1),
            {
                new VerifyVn(unknown_vn_, unknown_vn_) 
            }
        }
    };
    CreateFlow(non_nat_flow, 1);

    //Associate a floating IP with flow0
    AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
    client->WaitForIdle();
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };
    CreateFlow(nat_flow, 1);
    DeleteFlow(nat_flow, 1);

    EXPECT_TRUE(FlowGet(VrfGet("vrf4")->GetVrfId(), vm5_ip, 
                       vm1_fip, 1, 0, 0) == NULL);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatAddOldNonNat_2) {
    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1"); 
    client->WaitForIdle();

    TestFlow non_nat_flow[] = {
        {
             TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 1),
            {
                new VerifyVn(unknown_vn_, unknown_vn_) 
            }
        }
    };
    CreateFlow(non_nat_flow, 1);

    //Associate a floating IP with flow0
    AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
    client->WaitForIdle();
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(vm5_ip, vm1_fip, 1, 0, 0, "vrf4", 
                    flow4->GetInterfaceId(), 1),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0) 
            }
        }
    };
    CreateFlow(nat_flow, 1);
    DeleteFlow(nat_flow, 1);

    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->GetVrfId(), vm1_ip, 
                vm5_ip, 1, 0, 0) == NULL);

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatAddOldNat_1) {
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);

    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");  
    AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip2"); 
    client->WaitForIdle();

    TestFlow new_nat_flow[] = {
        {
             TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip2, 1, 0, 0) 
            }
        }
    };
    CreateFlow(new_nat_flow, 1);
    DeleteFlow(new_nat_flow, 1);
 
    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatAddOldNat_2) {
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);

    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");  
    AddLink("virtual-machine-interface", "flow1", "floating-ip", "fip1"); 
    client->WaitForIdle();

    TestFlow new_nat_flow[] = {
        {
             TestFlowPkt(vm2_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow1->GetInterfaceId(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0)
            }
        }
    };
    CreateFlow(new_nat_flow, 1);
    DeleteFlow(new_nat_flow, 1);

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatAddOldNat_3) {
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);

    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");  
    AddLink("virtual-machine-interface", "flow1", "floating-ip", "fip1"); 
    client->WaitForIdle();

    TestFlow new_nat_flow[] = {
        {
             TestFlowPkt(vm5_ip, vm1_fip, 1, 0, 0, "vrf4", 
                    flow4->GetInterfaceId(), 1),
            {
                new VerifyNat(vm2_ip, vm5_ip, 1, 0, 0)
            }
        }
    };
    CreateFlow(new_nat_flow, 1);
    DeleteFlow(new_nat_flow, 1);

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

//Create same Nat flow with different flow handles
TEST_F(FlowTest, TwoNatFlow) {
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->GetInterfaceId(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);
    TestFlow nat_rev_flow[] = {
        {
             TestFlowPkt(vm5_ip, vm1_fip, 1, 0, 0, "vrf4", 
                    flow4->GetInterfaceId(), 1),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0)
            }
        }
    };
    CreateFlow(nat_rev_flow, 1);

    DeleteFlow(nat_flow, 1);    
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, FlowAudit) {
    KFlowPurgeHold();
    EXPECT_TRUE(KFlowHoldAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(KFlowHoldAdd(2, 1, "2.2.2.2", "3.3.3.3", 1, 0, 0));
    RunFlowAudit();
    EXPECT_TRUE(FlowTableWait(2));
    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
    KFlowPurgeHold();

    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    EXPECT_TRUE(FlowTableWait(2));
    EXPECT_TRUE(KFlowHoldAdd(10, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    RunFlowAudit();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    usleep(500);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
    KFlowPurgeHold();
}

//Test flow deletion on ACL deletion
TEST_F(FlowTest, AclDelete) {
    AddAcl("acl1", 1, "vn5" , "vn5");
    client->WaitForIdle();
    uint32_t sport = 30;
    for (uint32_t i = 0; i < 1; i++) {
        sport++;
        TestFlow flow[] = {
            {
                TestFlowPkt(vm2_ip, vm1_ip, IPPROTO_TCP, sport, 40, "vrf5",
                            flow1->GetInterfaceId(), 1),
                {
                    new VerifyVn("vn5", "vn5")
                }
            }
        };
        CreateFlow(flow, 1);
    }

    //Delete the acl
    DelOperDBAcl(1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}


int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = 
        TestInit(init_file, ksync_init, true, true, true, (1000000 * 60 * 10));
    if (vm.count("config")) {
		eth_itf = Agent::GetInstance()->GetIpFabricItfName();
    } else {
        eth_itf = "eth0";
        EthInterface::CreateReq(eth_itf, Agent::GetInstance()->GetDefaultVrf());
        client->WaitForIdle();
    }

    FlowTest::TestSetup(ksync_init);
    int ret = RUN_ALL_TESTS();
    usleep(1000);
    Agent::GetInstance()->GetEventManager()->Shutdown();
    AsioStop();
    return ret;
}
