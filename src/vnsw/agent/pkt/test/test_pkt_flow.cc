/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "test/test_cmn_util.h"
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

int hash_id;
VmPortInterface *flow0;
VmPortInterface *flow1;
VmPortInterface *flow2;
VmPortInterface *flow3;
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
        Agent::GetDefaultInet4UcRouteTable()->AddLocalVmRoute
            (NULL, vrf, addr, 32, intf->GetUuid(),
             intf->GetVnEntry()->GetName(), label); 
        client->WaitForIdle();
        EXPECT_TRUE(RouteFind(vrf, addr, 32));
    }

    void CreateRemoteRoute(const char *vrf, const char *remote_vm, 
                           const char *serv, int label, const char *vn) {
        Ip4Address addr = Ip4Address::from_string(remote_vm);
        Ip4Address gw = Ip4Address::from_string(serv);
        Agent::GetDefaultInet4UcRouteTable()->AddRemoteVmRoute
            (NULL, vrf, addr, 32, gw, TunnelType::AllType(), label, vn);
        client->WaitForIdle();
        EXPECT_TRUE(RouteFind(vrf, addr, 32));
    }

    void DeleteRoute(const char *vrf, const char *ip) {
        Ip4Address addr = Ip4Address::from_string(ip);
        Agent::GetDefaultInet4UcRouteTable()->DeleteReq(NULL, vrf, addr, 32);
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFind(vrf, addr, 32) == false));
    }

    static void FlowDel(int vrf, const char *sip, const char *dip,
                        int proto, int sport, int dport, bool del_reverse_flow) {
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
        FlowTableKSyncObject::AuditProcess(FlowTableKSyncObject::GetKSyncObject());
        FlowTableKSyncObject::AuditProcess(FlowTableKSyncObject::GetKSyncObject());
    }

    static bool KFlowHoldAdd(int hash_id, int vrf, const char *sip, const char *dip,
                             int proto, int sport, int dport) {
        if (hash_id >= FlowTableKSyncObject::GetFlowTableSize()) {
            return false;
        }
        if (ksync_init_) {
            return false;
        }

        vr_flow_entry *vr_flow = KSyncSockTypeMap::GetFlowEntry(hash_id);

        vr_flow->fe_key.key_vrf_id = ntohs(vrf);
        vr_flow->fe_key.key_src_ip = ntohl(inet_addr(sip));
        vr_flow->fe_key.key_dest_ip = ntohl(inet_addr(dip));
        vr_flow->fe_key.key_proto = proto;
        vr_flow->fe_key.key_src_port = sport;
        vr_flow->fe_key.key_dst_port = dport;
        vr_flow->fe_action = VR_FLOW_ACTION_HOLD;
        KSyncSockTypeMap::SetFlowEntry(hash_id, true);

        return true;
    }

    static void KFlowPurgeHold() {
        if (ksync_init_) {
            return;
        }

        for (int count = 0; count < FlowTableKSyncObject::GetFlowTableSize();
             count++) {
            vr_flow_entry *vr_flow = KSyncSockTypeMap::GetFlowEntry(count);
            vr_flow->fe_action = VR_FLOW_ACTION_DROP;;
            KSyncSockTypeMap::SetFlowEntry(hash_id, false);
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
        DeleteVmportEnv(input, 3, true, 1);
        client->PortDelNotifyWait(3);
        client->WaitForIdle();
        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));
        EXPECT_FALSE(VmPortFind(input, 2));
        EXPECT_EQ(5U, Agent::GetInterfaceTable()->Size());
        EXPECT_EQ(1U, Agent::GetIntfCfgTable()->Size());

        client->Reset();
        DeleteVmportEnv(input2, 1, true, 2);
        client->PortDelNotifyWait(1);
        client->WaitForIdle();
        EXPECT_EQ(4U, Agent::GetInterfaceTable()->Size());
        EXPECT_EQ(0U, Agent::GetIntfCfgTable()->Size());
        EXPECT_FALSE(VmPortFind(input2, 0));
        EXPECT_EQ(0U, Agent::GetVmTable()->Size());
        EXPECT_EQ(0U, Agent::GetVnTable()->Size());
        EXPECT_EQ(0U, Agent::GetAclTable()->Size());

        if (ksync_init_) {
            DeleteTapIntf(fd_table, MAX_VNET);
        }
        client->WaitForIdle();
    }

    static void TestSetup(bool ksync_init) {
        unsigned int vn_count = 0;

        ksync_init_ = ksync_init;
        if (ksync_init_) {
            CreateTapInterfaces("flow", MAX_VNET, fd_table);
            client->WaitForIdle();
        }

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
        EXPECT_EQ(7U, Agent::GetInterfaceTable()->Size());
        EXPECT_EQ(3U, Agent::GetVmTable()->Size());
        EXPECT_EQ(vn_count, Agent::GetVnTable()->Size());
        EXPECT_EQ(3U, Agent::GetIntfCfgTable()->Size());

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
        EXPECT_EQ(8U, Agent::GetInterfaceTable()->Size());
        EXPECT_EQ(4U, Agent::GetVmTable()->Size());
        EXPECT_EQ(vn_count, Agent::GetVnTable()->Size());
        EXPECT_EQ(4U, Agent::GetIntfCfgTable()->Size());
        EXPECT_EQ(2U, Agent::GetAclTable()->Size());

        flow3 = VmPortInterfaceGet(input2[0].intf_id);
        assert(flow3);

        client->SetFlowAgeExclusionPolicy();
    }

protected:
    virtual void SetUp() {
        EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());
        //Reset flow age
        client->EnqueueFlowAge();
        client->WaitForIdle();
    }

    virtual void TearDown() {
        FlushFlowTable();
    }

private:
    static bool ksync_init_;
};

bool FlowTest::ksync_init_;
//Ingress flow test (VMport to VMport - Same VN)
//Flow creation using IP and TCP packets
TEST_F(FlowTest, FlowAdd_1) {

    //Flow creation using IP packet
    TxIpPacketUtil(flow0->GetInterfaceId(), vm1_ip, vm2_ip, 1, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm1_ip, vm2_ip, 1, 0, 0, false, 
                        "vn5", "vn5", hash_id++));

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxIpPacketUtil(flow1->GetInterfaceId(), vm2_ip, vm1_ip, 1, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm2_ip, vm1_ip, 1, 0, 0, true, 
                          "vn5", "vn5", hash_id++));

    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->GetInterfaceId(), vm1_ip, vm2_ip, 1000, 200, 
                    hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm1_ip, vm2_ip, 6, 1000, 200, false,
                        "vn5", "vn5", hash_id++));

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(flow0->GetInterfaceId(), vm2_ip, vm1_ip, 200, 1000, 
                    hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm2_ip, vm1_ip, 6, 200, 1000, true, 
                        "vn5", "vn5", hash_id++));

    EXPECT_EQ(4U, FlowTable::GetFlowTableObject()->Size());
}

//Egress flow test (IP fabric to VMPort - Same VN)
//Flow creation using GRE packets
TEST_F(FlowTest, FlowAdd_2) {
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());

    //Create ETH interface to receive GRE packets on it.
    EthInterfaceKey key(nil_uuid(), eth_itf);
    Interface *intf = static_cast<Interface *>
                                 (Agent::GetInterfaceTable()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);

    //Create remote VM route. This will be used to figure out destination VN for
    //flow
    Ip4Address addr = Ip4Address::from_string(remote_vm1_ip);
    Ip4Address gw = Ip4Address::from_string(remote_router_ip);
    Agent::GetDefaultInet4UcRouteTable()->AddRemoteVmRoute
        (NULL, "vrf5", addr, 32, gw, TunnelType::AllType(), 30, "vn5");
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("vrf5", addr, 32));
    
    Ip4Address rid1 = Agent::GetRouterId();
    std::string router_ip_str = rid1.to_string();

    //Send GRE-MPLS packet containing IP datagram
    TxMplsPacketUtil(intf->GetInterfaceId(), remote_router_ip, router_ip_str.c_str(), 16, 
                     remote_vm1_ip, vm1_ip, 1, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", remote_vm1_ip, vm1_ip, 1, 0, 0, false, 
                        "vn5", "vn5", hash_id++));

    TxIpPacketUtil(flow0->GetInterfaceId(), vm1_ip, remote_vm1_ip, 1, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm1_ip, remote_vm1_ip, 1, 0, 0, true, 
                        "vn5", "vn5", hash_id++));
    
    //Create remote VM route. This will be used to figure out destination VN for
    //flow
    addr = Ip4Address::from_string(remote_vm3_ip);
    Agent::GetDefaultInet4UcRouteTable()->AddRemoteVmRoute
        (NULL, "vrf5", addr, 32, gw, TunnelType::AllType(), 30, "vn5");
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("vrf5", addr, 32));

    //Send GRE-MPLS packet containing TCP packet
    TxMplsTcpPacketUtil(intf->GetInterfaceId(), remote_router_ip, router_ip_str.c_str(),
                        18, remote_vm3_ip, vm3_ip, 1001, 1002, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", remote_vm3_ip, vm3_ip, 6, 1001, 1002, false, 
                        "vn5", "vn5", hash_id++));

    TxTcpPacketUtil(flow2->GetInterfaceId(), vm3_ip, remote_vm3_ip,
                    1002, 1001, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm3_ip, remote_vm3_ip, 6, 1002, 1001, true, 
                        "vn5", "vn5", hash_id++));

    //cleanup

    //1. Remove remote VM routes
    DeleteRoute("vrf5", remote_vm1_ip);
    DeleteRoute("vrf5", remote_vm3_ip);
}

//Ingress flow test (VMport to VMport - Different VNs)
//Flow creation using IP and TCP packets
TEST_F(FlowTest, FlowAdd_3) {

    /* Add Local VM route of vrf3 to vrf5 */
    CreateLocalRoute("vrf5", vm4_ip, flow3, 19);
    /* Add Local VM route of vrf5 to vrf3 */
    CreateLocalRoute("vrf3", vm1_ip, flow0, 16);

    //Flow creation using IP packet
    TxIpPacketUtil(flow0->GetInterfaceId(), vm1_ip, vm4_ip, 1, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm1_ip, vm4_ip, 1, 0, 0, false, 
                        "vn5", "vn3", hash_id++));

    //Create flow in reverse direction and make sure it is NOT linked to previous flow
    TxIpPacketUtil(flow3->GetInterfaceId(), vm4_ip, vm1_ip, 1, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf3", vm4_ip, vm1_ip, 1, 0, 0, false, 
                        "vn3", "vn5", hash_id++));

    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->GetInterfaceId(), vm1_ip, vm4_ip,
                    1004, 1005, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm1_ip, vm4_ip, 6, 1004, 1005, false,
                        "vn5", "vn3", hash_id++));

    //Create flow in reverse direction and make sure it is NOT linked to previous flow
    TxTcpPacketUtil(flow3->GetInterfaceId(), vm4_ip, vm1_ip,
                    1005, 1004, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf3", vm4_ip, vm1_ip, 6, 1005, 1004, false, 
                        "vn3", "vn5", hash_id++));
    //cleanup

    //1. Remove remote VM routes
    DeleteRoute("vrf5", vm4_ip);
    DeleteRoute("vrf3", vm1_ip);
}

//Egress flow test (IP fabric to VMport - Different VNs)
//Flow creation using GRE packets
TEST_F(FlowTest, FlowAdd_4) {
    /* Add remote VN route to vrf5 */
    CreateRemoteRoute("vrf5", remote_vm4_ip, remote_router_ip, 8, "vn3");

    EthInterfaceKey key(nil_uuid(), eth_itf);
    Interface *intf = static_cast<Interface *>
                                 (Agent::GetInterfaceTable()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);

    Ip4Address rid1 = Agent::GetRouterId();
    std::string router_ip_str = rid1.to_string();

    //Send GRE-MPLS packet containing IP datagram
    TxMplsPacketUtil(intf->GetInterfaceId(), remote_router_ip, 
                     router_ip_str.c_str(), 16, remote_vm4_ip, 
                     vm1_ip, 1, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", remote_vm4_ip, vm1_ip, 1, 0, 0, false, 
                        "vn3", "vn5", hash_id++));

    TxIpPacketUtil(flow0->GetInterfaceId(), vm1_ip, remote_vm4_ip, 1, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm1_ip, remote_vm4_ip, 1, 0, 0, true, 
                        "vn5", "vn3", hash_id++));

    //Send GRE-MPLS packet containing TCP packet
    TxMplsTcpPacketUtil(intf->GetInterfaceId(), remote_router_ip, 
                        router_ip_str.c_str(), 16, remote_vm4_ip, 
                        vm1_ip, 1006, 1007, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", remote_vm4_ip, vm1_ip, 6, 1006, 1007, false, 
                        "vn3", "vn5", hash_id++));

    TxTcpPacketUtil(flow0->GetInterfaceId(), vm1_ip, remote_vm4_ip, 
                    1007, 1006, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm1_ip, remote_vm4_ip, 6, 1007, 1006, true, 
                        "vn5", "vn3", hash_id++));
    //cleanup

    //1. Remove remote VM routes
    DeleteRoute("vrf5", remote_vm4_ip);
}

//Duplicate Ingress flow test (VMport to VMport - Same VN)
//Flow creation using IP
TEST_F(FlowTest, FlowAdd_5) {
    //Flow creation using IP packet
    TxIpPacketUtil(flow0->GetInterfaceId(), vm1_ip, vm2_ip, 1, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm1_ip, vm2_ip, 1, 0, 0, false, 
                        "vn5", "vn5", hash_id));

    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());

    //Create the same flow again
    TxIpPacketUtil(flow0->GetInterfaceId(), vm1_ip, vm2_ip, 1, hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm1_ip, vm2_ip, 1, 0, 0, false, 
                        "vn5", "vn5", hash_id++));

    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());
}

//Duplicate Ingress flow test for flow having reverse flow (VMport to VMport - Same VN)
//Flow creation using TCP packets
TEST_F(FlowTest, FlowAdd_6) {
    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->GetInterfaceId(), vm1_ip, vm2_ip, 1000, 200, 
                    hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm1_ip, vm2_ip, 6, 1000, 200, false,
                        "vn5", "vn5", hash_id++));

    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(flow0->GetInterfaceId(), vm2_ip, vm1_ip, 200, 1000, 
                    hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm2_ip, vm1_ip, 6, 200, 1000, true, 
                        "vn5", "vn5", hash_id));

    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());

    //Send request for reverse flow again
    TxTcpPacketUtil(flow0->GetInterfaceId(), vm2_ip, vm1_ip, 200, 1000, 
                    hash_id);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm2_ip, vm1_ip, 6, 200, 1000, true, 
                        "vn5", "vn5", hash_id));

    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());

    //Send request for forward flow again 
    TxTcpPacketUtil(flow0->GetInterfaceId(), vm1_ip, vm2_ip, 1000, 200, 
                    (hash_id - 1));
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm1_ip, vm2_ip, 6, 1000, 200, true,
                        "vn5", "vn5", (hash_id - 1)));

    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());
}

TEST_F(FlowTest, FlowAge_1) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = AgentUve::GetFlowStatsCollector()->GetFlowAgeTime();
    //Set the flow age time to 100 microsecond
    AgentUve::GetFlowStatsCollector()->SetFlowAgeTime(tmp_age_time);

    //Create bidirectional flow
    TxIpPacketUtil(flow0->GetInterfaceId(), vm1_ip, vm2_ip, 1, 1);
    TxIpPacketUtil(flow1->GetInterfaceId(), vm2_ip, vm1_ip, 1, 2);
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet("vrf5", vm1_ip, vm2_ip, 1, 0, 0, true,
                        "vn5", "vn5", 1));
    EXPECT_TRUE(FlowGet("vrf5", vm2_ip, vm1_ip, 1, 0, 0, true, 
                        "vn5", "vn5", 2));
    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());

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
    AgentUve::GetFlowStatsCollector()->SetFlowAgeTime(bkp_age_time);
}

// Flow with index -1 ages out on age-time irrespective of stats
TEST_F(FlowTest, FlowAge_2) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = AgentUve::GetFlowStatsCollector()->GetFlowAgeTime();
    //Set the flow age time to 100 microsecond
    AgentUve::GetFlowStatsCollector()->SetFlowAgeTime(tmp_age_time);

    // Create flow with flow-index -1
    FlowAdd(-1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, NULL, NULL, 0);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));

    //Trigger flow-aging
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    //Verify that both flows get removed after reverse-flow ages
    WAIT_FOR(100, 1, (0U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());

    //Restore flow aging time
    AgentUve::GetFlowStatsCollector()->SetFlowAgeTime(bkp_age_time);
}

// Aging with more than 2 entries
TEST_F(FlowTest, FlowAge_3) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = AgentUve::GetFlowStatsCollector()->GetFlowAgeTime();
    //Set the flow age time to 100 microsecond
    AgentUve::GetFlowStatsCollector()->SetFlowAgeTime(tmp_age_time);

    //Create bidirectional flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.1", 1, 0, 0, NULL, NULL, 0);
    FlowAdd(2, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, NULL, NULL, 0);
    FlowAdd(3, 1, "1.1.1.1", "2.2.2.3", 1, 0, 0, NULL, NULL, 0);
    FlowAdd(4, 1, "1.1.1.1", "2.2.2.4", 1, 0, 0, NULL, NULL, 0);
    client->WaitForIdle();
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
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.1", 1, 0, 0, NULL, NULL, 0);
    FlowAdd(2, 1, "2.2.2.1", "1.1.1.1", 1, 0, 0, NULL, NULL, 0);
    FlowAdd(3, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, NULL, NULL, 0);
    FlowAdd(4, 1, "2.2.2.2", "1.1.1.1", 1, 0, 0, NULL, NULL, 0);
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (0U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());

    // Delete 2 out of 4 linked entries
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.1", 1, 0, 0, NULL, NULL, 0);
    FlowAdd(2, 1, "2.2.2.1", "1.1.1.1", 1, 0, 0, NULL, NULL, 0);
    FlowAdd(3, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, NULL, NULL, 0);
    FlowAdd(4, 1, "2.2.2.2", "1.1.1.1", 1, 0, 0, NULL, NULL, 0);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (2U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.1", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.1", "1.1.1.1", 1, 0, 0, false, -1, -1));

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (0U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());

    //Restore flow aging time
    AgentUve::GetFlowStatsCollector()->SetFlowAgeTime(bkp_age_time);
}

TEST_F(FlowTest, ScaleFlowAge_1) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = AgentUve::GetFlowStatsCollector()->GetFlowAgeTime();
    //Set the flow age time to 100 microsecond
    AgentUve::GetFlowStatsCollector()->SetFlowAgeTime(tmp_age_time);
    int count_per_pass = FlowStatsCollector::FlowCountPerPass;

    for (int i = 0; i < count_per_pass + 10; i++) {
        FlowAdd(i, i, "1.1.1.1", "2.2.2.1", 1, 0, 0, NULL, NULL, 0);
        FlowAdd(i + 100, i, "2.2.2.1", "1.1.1.1", 1, 0, 0, NULL, NULL, 0);
    }
    EXPECT_EQ(((count_per_pass + 10U)*2), FlowTable::GetFlowTableObject()->Size());

    // Flow entries are created with #pkts = 1. 
    // Do first sleep for aging to work correctly below
    client->EnqueueFlowAge();
    client->WaitForIdle();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_EQ(((count_per_pass + 10U)*2), FlowTable::GetFlowTableObject()->Size());

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
    AgentUve::GetFlowStatsCollector()->SetFlowAgeTime(bkp_age_time);
}

TEST_F(FlowTest, ScaleFlowAge_2) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = AgentUve::GetFlowStatsCollector()->GetFlowAgeTime();
    //Set the flow age time to 100 microsecond
    AgentUve::GetFlowStatsCollector()->SetFlowAgeTime(tmp_age_time);
    int count_per_pass = FlowStatsCollector::FlowCountPerPass;

    for (int i = 0; i < count_per_pass + 10; i++) {
        FlowAdd(i, i, "1.1.1.1", "2.2.2.1", 1, 0, 0, NULL, NULL, 0);
        FlowAdd(i + 100, i, "2.2.2.1", "1.1.1.1", 1, 0, 0, NULL, NULL, 0);
    }
    EXPECT_EQ(((count_per_pass + 10U)*2), FlowTable::GetFlowTableObject()->Size());

    // Flow entries are created with #pkts = 1. 
    // Do first sleep for aging to work correctly below
    client->EnqueueFlowAge();
    client->WaitForIdle();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_EQ(((count_per_pass + 10U)*2), FlowTable::GetFlowTableObject()->Size());

    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (22U == FlowTable::GetFlowTableObject()->Size()));
    EXPECT_EQ(22U, FlowTable::GetFlowTableObject()->Size());

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
    AgentUve::GetFlowStatsCollector()->SetFlowAgeTime(bkp_age_time);
}

TEST_F(FlowTest, Nat_FlowAge_1) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = AgentUve::GetFlowStatsCollector()->GetFlowAgeTime();
    //Set the flow age time to 100 microsecond
    AgentUve::GetFlowStatsCollector()->SetFlowAgeTime(tmp_age_time);

    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Update stats so that flow is not aged
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);

    // Sleep for age-time
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Sleep for age-time
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    WAIT_FOR(100, 1, (0U == FlowTable::GetFlowTableObject()->Size()));

    //Restore flow aging time
    AgentUve::GetFlowStatsCollector()->SetFlowAgeTime(bkp_age_time);
}

TEST_F(FlowTest, teardown) {
    FlowTest::TestTearDown();
}

TEST_F(FlowTest, NonNatFlowAdd_1) {
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, NULL, NULL, 0);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, 1, -1));

    // Add duplicate flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, NULL, NULL, 0);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, 1, -1));

    // Add reverse flow
    FlowAdd(2, 1, "2.2.2.2", "1.1.1.1", 1, 0, 0, NULL, NULL, 0);
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "1.1.1.1", 1, 0, 0, false, 2, 1));

    // Delete forward flow. It should delete both flows
    FlowDel(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0));

    // Add forward and reverse flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, NULL, NULL, 0);
    FlowAdd(2, 1, "2.2.2.2", "1.1.1.1", 1, 0, 0, NULL, NULL, 0);
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "1.1.1.1", 1, 0, 0, false, 2, 1));

    // Delete reverse flow. Should delete both flows
    FlowDel(1, "2.2.2.2", "1.1.1.1", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NonNatDupFlowAdd_1) {
    // Add forward and reverse flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, NULL, NULL, 0);
    FlowAdd(2, 1, "2.2.2.2", "1.1.1.1", 1, 0, 0, NULL, NULL, 0);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, 1, 2));

    // Add duplicate forward and reverse flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, NULL, NULL, 0);
    FlowAdd(2, 1, "2.2.2.2", "1.1.1.1", 1, 0, 0, NULL, NULL, 0);
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "1.1.1.1", 1, 0, 0, false, 2, 1));

    // Delete forward flow. It should delete both flows
    FlowDel(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NonNatAddOldNat_1) {
    // Add NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Add Non-NAT forward flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, NULL, NULL, 0);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, true, -1, -1));

    // Delete forward flow. It should delete both flows
    FlowDel(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}


TEST_F(FlowTest, NonNatAddOldNat_2) {
    // Add NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Add Non-NAT Reverse flow
    FlowAdd(1001, 1, "2.2.2.2", "3.3.3.3", 1, 0, 0, NULL, NULL, 0);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, true, -1, -1));

    // Delete forward flow. It should delete both flows
    FlowDel(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));

    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, true, -1, -1));
    EXPECT_TRUE(FlowGet(1, "3.3.3.3", "2.2.2.2", 1, 0, 0, true, -1, -1));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NonNatAddOldNat_3) {
    // Add NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Add Non-NAT flow. Reverse flow matching a NAT flow
    FlowAdd(3, 1, "3.3.3.3", "2.2.2.2", 1, 0, 0, NULL, NULL, 0);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, true, -1, -1));
    EXPECT_TRUE(FlowGet(1, "3.3.3.3", "2.2.2.2", 1, 0, 0, true, -1, -1));

    // Delete new Non-NAT flow. The NAT flow should still be present
    FlowDel(1, "3.3.3.3", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "3.3.3.3", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "3.3.3.3", 1, 0, 0));
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true, -1, -1));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatFlowAdd_1) {
    // Add a NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Add duplicate flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Add duplicate flow
    FlowAdd(1, 1, "2.2.2.2", "3.3.3.3", 1, 0, 0, "2.2.2.2", "1.1.1.1", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Delete forward flow. It should delete both flows
    FlowDel(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "3.3.3.3", 1, 0, 0));

    // Add a NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Delete reverse flow. Should delete both flows
    FlowDel(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "3.3.3.3", 1, 0, 0));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatFlowAdd_2) {
    // Add a NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Add reverse flow as NAT
    FlowAdd(1, 1, "2.2.2.2", "3.3.3.3", 1, 0, 0, "2.2.2.2", "1.1.1.1", 1);
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));

    // Delete forward flow. It should delete both flows
    FlowDel(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "3.3.3.3", 1, 0, 0));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatAddOldNonNat_1) {
    // Add Non-NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, NULL, NULL, 1);
    FlowAdd(2, 1, "2.2.2.2", "1.1.1.1", 1, 0, 0, NULL, NULL, 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "1.1.1.1", 1, 0, 0, false, -2, -1));

    // Add NAT forward flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);

    // Forward flow must be converted to NAT flow. Reverse flow must be unlinked
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, true, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "1.1.1.1", 1, 0, 0, true, -2, -1));

    // Delete forward flow. It should delete both flows
    FlowDel(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "3.3.3.3", 1, 0, 0));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "1.1.1.1", 1, 0, 0, true, -1, -1));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatAddOldNonNat_2) {
    // Add Non-NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, NULL, NULL, 1);
    FlowAdd(2, 1, "2.2.2.2", "1.1.1.1", 1, 0, 0, NULL, NULL, 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, 1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "1.1.1.1", 1, 0, 0, false, 2, -1));

    // Add NAT forward flow
    FlowAdd(1, 1, "3.3.3.3", "2.2.2.2", 1, 0, 0, "1.1.1.1", "2.2.2.2", 1);

    // Forward flow must be converted to NAT flow. Reverse flow must be unlinked
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "1.1.1.1", 1, 0, 0, true, -2, -1));
    EXPECT_TRUE(FlowGet(1, "3.3.3.3", "2.2.2.2", 1, 0, 0, true, -1, -1));

    // Delete forward flow. It should delete both flows
    FlowDel(1, "2.2.2.2", "1.1.1.1", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "3.3.3.3", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true, -1, -1));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatAddOldNonNat_3) {
    // Add Non-NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, NULL, NULL, 1);
    FlowAdd(2, 1, "2.2.2.2", "1.1.1.1", 1, 0, 0, NULL, NULL, 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, 1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "1.1.1.1", 1, 0, 0, false, 2, -1));

    // Add NAT forward flow
    FlowAdd(1, 1, "2.2.2.2", "3.3.3.3", 1, 0, 0, "2.2.2.2", "1.1.1.1", 1);

    // Forward flow must be converted to NAT flow. Reverse flow must be unlinked
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, true, -1, -1));
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "1.1.1.1", 1, 0, 0, true, -1, -1));

    // Delete forward flow. It should delete both flows
    FlowDel(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "3.3.3.3", 1, 0, 0));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "1.1.1.1", 1, 0, 0, true, -1, -1));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatAddOldNat_1) {
    // Add NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Add new NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "4.4.4.4", "2.2.2.2", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, true, -1, -1));

    // Delete forward flow. It should delete both flows
    FlowDel(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "4.4.4.4", 1, 0, 0));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatAddOldNat_2) {
    // Add NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Add new NAT flow
    FlowAdd(3, 1, "1.1.1.2", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "1.1.1.2", "2.2.2.2", 1, 0, 0, false, -3, -1));

    // Delete forward flow. It should delete both flows
    FlowDel(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowGet(1, "1.1.1.2", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    FlowDel(1, "1.1.1.2", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.2", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "3.3.3.3", 1, 0, 0));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatAddOldNat_3) {
    // Add NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Add new NAT flow
    FlowAdd(3, 1, "2.2.2.2", "3.3.3.3", 1, 0, 0, "2.2.2.2", "1.1.1.2", 1);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true, -1, -1));
    EXPECT_TRUE(FlowGet(1, "1.1.1.2", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    // Delete forward flow. It should delete both flows
    FlowDel(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowGet(1, "1.1.1.2", "2.2.2.2", 1, 0, 0, false, -1, -1));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1, -1));

    FlowDel(1, "1.1.1.2", "2.2.2.2", 1, 0, 0, true);
    EXPECT_TRUE(FlowFail(1, "1.1.1.2", "2.2.2.2", 1, 0, 0));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "3.3.3.3", 1, 0, 0));
    EXPECT_TRUE(FlowTableWait(0));
}

//Create same Nat flow with different flow handles
TEST_F(FlowTest, TwoNatFlow) {
    // Add NAT flow
    FlowAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, "3.3.3.3", "2.2.2.2", 1);
    EXPECT_TRUE(FlowTableWait(2));
    usleep(1000);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1001));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1001, -1));

    FlowAdd(2, 1, "2.2.2.2", "3.3.3.3", 1, 0, 0, "2.2.2.2", "1.1.1.1", 1);
    usleep(1000);
    EXPECT_TRUE(FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, false, -1, -1001));
    EXPECT_TRUE(FlowGet(1, "2.2.2.2", "3.3.3.3", 1, 0, 0, false, -1001, -1));

    FlowDel(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, true);
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

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, true, true, (1000000 * 60 * 10));
    if (vm.count("config")) {
		eth_itf = Agent::GetIpFabricItfName();
    } else {
        eth_itf = "eth0";
        EthInterface::CreateReq(eth_itf, Agent::GetDefaultVrf());
        client->WaitForIdle();
    }

    FlowTest::TestSetup(ksync_init);
    int ret = RUN_ALL_TESTS();
    usleep(1000);
    Agent::GetEventManager()->Shutdown();
    AsioStop();
    return ret;
}
