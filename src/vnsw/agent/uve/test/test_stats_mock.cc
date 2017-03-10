/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "cmn/agent_cmn.h"
#include "pkt/pkt_init.h"
#include "pkt/flow_table.h"
#include "test_cmn_util.h"
#include <uve/agent_uve.h>
#include <uve/test/vn_uve_table_test.h>
#include "ksync/ksync_sock_user.h"
#include <uve/test/agent_stats_collector_test.h>
#include <vrouter/flow_stats/test/flow_stats_collector_test.h>
#include "uve/test/test_uve_util.h"
#include "pkt/test/test_flow_util.h"

using namespace std;
#define remote_vm4_ip "13.1.1.2"
#define remote_router_ip "10.1.1.2"

BgpPeer *peer_;
void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
        {"flow0", 6, "1.1.1.1", "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, "1.1.1.2", "00:00:00:01:01:02", 5, 2},
};

struct PortInfo stats_if[] = {
        {"test0", 8, "3.1.1.1", "00:00:00:01:01:01", 6, 3},
        {"test1", 9, "4.1.1.2", "00:00:00:01:01:02", 6, 4},
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

IpamInfo ipam_stats[] = {
    {"3.1.1.0", 24, "3.1.1.10"},
    {"4.1.1.0", 24, "4.1.1.10"},
};

int hash_id;
VmInterface *flow0, *flow1;
VmInterface *test0, *test1;

class StatsTestMock : public ::testing::Test {
public:
    StatsTestMock() : util_(), agent_(Agent::GetInstance()) {
        flow_proto_ = agent_->pkt()->get_flow_proto();
    }
    bool InterVnStatsMatch(const string &svn, const string &dvn, uint32_t pkts,
                           uint32_t bytes, bool out) {
        VnUveTableTest *vut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
        const VnUveEntry::VnStatsSet *stats_set = vut->FindInterVnStats(svn);

        if (!stats_set) {
            return false;
        }
        VnUveEntry::VnStatsPtr key(new VnUveEntry::VnStats(dvn, 0, 0, false));
        VnUveEntry::VnStatsSet::iterator it = stats_set->find(key);
        if (it == stats_set->end()) {
            return false;
        }
        VnUveEntry::VnStatsPtr stats_ptr(*it);
        const VnUveEntry::VnStats *stats = stats_ptr.get();
        if (out && stats->out_bytes_ == bytes && stats->out_pkts_ == pkts) {
            return true;
        }
        if (!out && stats->in_bytes_ == bytes && stats->in_pkts_ == pkts) {
            return true;
        }
        return false;
    }

    static void TestSetup() {
        unsigned int vn_count = 0;
        hash_id = 1;
        client->Reset();
        AddIPAM("vn5", ipam_info, 1);
        client->WaitForIdle();
        AddIPAM("vn6", ipam_stats, 2);
        client->WaitForIdle();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(10);
        vn_count++;

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));
        EXPECT_EQ(5U, Agent::GetInstance()->interface_table()->Size());
        EXPECT_EQ(2U, Agent::GetInstance()->vm_table()->Size());
        EXPECT_EQ(vn_count, Agent::GetInstance()->vn_table()->Size());
        EXPECT_EQ(2U, PortSubscribeSize(Agent::GetInstance()));

        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmInterfaceGet(input[1].intf_id);
        assert(flow1);

        /* verify that there are no existing Flows */
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->get_flow_proto()->FlowCount());

        //Prerequisites for interface stats test
        client->Reset();
        CreateVmportEnv(stats_if, 2);
        client->WaitForIdle(10);
        vn_count++;

        EXPECT_TRUE(VmPortActive(stats_if, 0));
        EXPECT_TRUE(VmPortActive(stats_if, 1));
        EXPECT_EQ(4U, Agent::GetInstance()->vm_table()->Size());
        EXPECT_EQ(vn_count, Agent::GetInstance()->vn_table()->Size());
        EXPECT_EQ(4U, PortSubscribeSize(Agent::GetInstance()));

        test0 = VmInterfaceGet(stats_if[0].intf_id);
        assert(test0);
        test1 = VmInterfaceGet(stats_if[1].intf_id);
        assert(test1);

        //To disable flow aging set the flow age time to high value
        Agent::GetInstance()->flow_stats_manager()->
            default_flow_stats_collector_obj()->SetFlowAgeTime(1000000 * 60 * 10);

    }
    static void TestTeardown() {
        client->Reset();
        DeleteVmportEnv(input, 2, 1, 1);
        client->WaitForIdle(10);
        client->VnDelNotifyWait(1);
        client->PortDelNotifyWait(2);
        EXPECT_TRUE(client->AclNotifyWait(1));

        client->Reset();
        DeleteVmportEnv(stats_if, 2, 1);
        client->WaitForIdle(10);
        DelIPAM("vn5");
        client->WaitForIdle();
        DelIPAM("vn6");
        client->WaitForIdle();
        client->VnDelNotifyWait(1);
        client->PortDelNotifyWait(2);
    }

    vr_vrf_stats_req GetVrfStatsObj(uint64_t discards, uint64_t resolves,
        uint64_t receives, uint64_t udp_tunnels, uint64_t udp_mpls_tunnels,
        uint64_t gre_mpls_tunnels, uint64_t ecmp_composites,
        uint64_t l2_mcast_composites, uint64_t fabric_composites,
        uint64_t l2_encaps, uint64_t encaps, uint64_t gros, uint64_t diags,
        uint64_t encap_composites, uint64_t evpn_composites,
        uint64_t vrf_translates, uint64_t vxlan_tunnels,
        uint64_t arp_virtual_proxy, uint64_t arp_virtual_stitch,
        uint64_t arp_virtual_flood, uint64_t arp_physical_stitch,
        uint64_t arp_tor_proxy, uint64_t arp_physical_flood,
        uint64_t l2_receives, uint64_t uuc_floods) {

        vr_vrf_stats_req req;

        req.set_vsr_discards(discards);
        req.set_vsr_resolves(resolves);
        req.set_vsr_receives(receives);
        req.set_vsr_udp_tunnels(udp_tunnels);
        req.set_vsr_udp_mpls_tunnels(udp_mpls_tunnels);
        req.set_vsr_gre_mpls_tunnels(gre_mpls_tunnels);
        req.set_vsr_ecmp_composites(ecmp_composites);
        req.set_vsr_l2_mcast_composites(l2_mcast_composites);
        req.set_vsr_fabric_composites(fabric_composites);
        req.set_vsr_l2_encaps(l2_encaps);
        req.set_vsr_encaps(encaps);
        req.set_vsr_gros(gros);
        req.set_vsr_diags(diags);
        req.set_vsr_encap_composites(encap_composites);
        req.set_vsr_evpn_composites(evpn_composites);
        req.set_vsr_vrf_translates(vrf_translates);
        req.set_vsr_vxlan_tunnels(vxlan_tunnels);
        req.set_vsr_arp_virtual_proxy(arp_virtual_proxy);
        req.set_vsr_arp_virtual_stitch(arp_virtual_stitch);
        req.set_vsr_arp_virtual_flood(arp_virtual_flood);
        req.set_vsr_arp_physical_stitch(arp_physical_stitch);
        req.set_vsr_arp_tor_proxy(arp_tor_proxy);
        req.set_vsr_arp_physical_flood(arp_physical_flood);
        req.set_vsr_l2_receives(l2_receives);
        req.set_vsr_uuc_floods(uuc_floods);
        return req;
    }
    TestUveUtil util_;
    Agent* agent_;
    FlowProto *flow_proto_;
};

TEST_F(StatsTestMock, FlowStatsTest) {
    hash_id = 1;
    //Flow creation using IP packet
    TxIpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2", 0, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 0, 0, 0, false,
                        "vn5", "vn5", hash_id++, flow0->flow_key_nh()->id()));

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);
    FlowEntry *f1 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 0, 0, 0,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f1 != NULL);
    FlowEntry *f1_rev = f1->reverse_flow_entry();
    EXPECT_TRUE(f1_rev != NULL);

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxIpPacketUtil(flow1->id(), "1.1.1.2", "1.1.1.1", 0, f1_rev->flow_handle());
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 0, 0, 0, true,
                        "vn5", "vn5", f1_rev->flow_handle(),
                        flow1->flow_key_nh()->id(),
                        flow0->flow_key_nh()->id()));

    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id++, flow0->flow_key_nh()->id()));
    FlowEntry *f2 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f2 != NULL);
    FlowEntry *f2_rev = f2->reverse_flow_entry();
    EXPECT_TRUE(f2_rev != NULL);

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(flow1->id(), "1.1.1.2", "1.1.1.1", 200, 1000,
                    f2_rev->flow_handle());
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, true, 
                        "vn5", "vn5", f2_rev->flow_handle(),
                        flow1->flow_key_nh()->id(),
                        flow0->flow_key_nh()->id()));

    //Verify flow count
    EXPECT_EQ(4U, flow_proto_->FlowCount());

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 0, 0, 0, 1, 30,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 0, 0, 0, 1, 30,
                               flow1->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 1, 30,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, 1, 30,
                               flow1->flow_key_nh()->id()));

    //Change the stats
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), 1, 30);
    KSyncSockTypeMap::IncrFlowStats(2, 1, 30);
    KSyncSockTypeMap::IncrFlowStats(f2_rev->flow_handle(), 1, 30);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats. Flow was deleted and re-added when
    //reverse packet was setn
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 0, 0, 0, 1, 30,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 0, 0, 0, 2, 60,
                               flow1->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 1, 30,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, 2, 60,
                               flow1->flow_key_nh()->id()));

    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    WAIT_FOR(100, 10000, (flow_proto_->FlowCount() == 0U));
}

TEST_F(StatsTestMock, FlowStatsOverflowTest) {
    hash_id = 1;
    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id++, flow0->flow_key_nh()->id()));
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);
    FlowEntry *f1 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f1 != NULL);
    FlowEntry *f1_rev = f1->reverse_flow_entry();
    EXPECT_TRUE(f1_rev != NULL);


    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(flow1->id(), "1.1.1.2", "1.1.1.1", 200, 1000,
                    f1_rev->flow_handle());
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, true, 
                        "vn5", "vn5", f1_rev->flow_handle(),
                        flow1->flow_key_nh()->id(),
                        flow0->flow_key_nh()->id()));

    //Verify flow count
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 1, 30,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, 1, 30,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of vrouter-Test1 */
    //Decrement the stats so that they become 0
    f1 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                 flow0->flow_key_nh()->id());
    KSyncSockTypeMap::IncrFlowStats(f1->flow_handle(), -1, -30);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), -1, -30);

    KSyncSockTypeMap::SetOFlowStats(f1->flow_handle(), 1, 1);
    KSyncSockTypeMap::SetOFlowStats(f1_rev->flow_handle(), 1, 1);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x100000000ULL, 0x100000000ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x100000000ULL, 0x100000000ULL,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of vrouter-Test2 */
    KSyncSockTypeMap::IncrFlowStats(f1->flow_handle(), 1, 0x10);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), 1, 0x10);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x100000001ULL, 0x100000010ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x100000001ULL, 0x100000010ULL,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of vrouter-Test3 */
    KSyncSockTypeMap::SetOFlowStats(f1->flow_handle(), 2, 3);
    KSyncSockTypeMap::SetOFlowStats(f1_rev->flow_handle(), 4, 5);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x200000001ULL, 0x300000010ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x400000001ULL, 0x500000010ULL,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of vrouter-Test4 */
    KSyncSockTypeMap::IncrFlowStats(f1->flow_handle(), 1, 0x10);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), 1, 0x10);

    KSyncSockTypeMap::SetOFlowStats(f1->flow_handle(), 0xA, 0xB);
    KSyncSockTypeMap::SetOFlowStats(f1_rev->flow_handle(), 0xC, 0xD);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0xA00000002ULL, 0xB00000020ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0xC00000002ULL, 0xD00000020ULL,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of agent-Test1 */
    //Decrement flow stats
    KSyncSockTypeMap::IncrFlowStats(f1->flow_handle(), -1, -0x10);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), -1, -0x10);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x10A00000001ULL, 0x1000B00000010ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x10C00000001ULL, 0x1000D00000010ULL,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of agent-Test2 */
    KSyncSockTypeMap::IncrFlowStats(f1->flow_handle(), 1, 0x10);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), 1, 0x10);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x10A00000002ULL, 0x1000B00000020ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x10C00000002ULL, 0x1000D00000020ULL,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of agent-Test3 */
    KSyncSockTypeMap::SetOFlowStats(f1->flow_handle(), 0xA1, 0xB1);
    KSyncSockTypeMap::SetOFlowStats(f1_rev->flow_handle(), 0xC1, 0xD1);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x1A100000002ULL, 0x100B100000020ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x1C100000002ULL, 0x100D100000020ULL,
                               flow1->flow_key_nh()->id()));

    //cleanup
    KSyncSockTypeMap::SetOFlowStats(f1->flow_handle(), 0, 0);
    KSyncSockTypeMap::SetOFlowStats(f1_rev->flow_handle(), 0, 0);
    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    WAIT_FOR(100, 10000, (flow_proto_->FlowCount() == 0U));
}

TEST_F(StatsTestMock, FlowStatsOverflow_AgeTest) {
    hash_id = 1;
    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id++, flow0->flow_key_nh()->id()));

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);
    FlowEntry *f1 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f1 != NULL);
    FlowEntry *f1_rev = f1->reverse_flow_entry();
    EXPECT_TRUE(f1_rev != NULL);

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(flow1->id(), "1.1.1.2", "1.1.1.1", 200, 1000,
                    f1_rev->flow_handle());
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, true,
                        "vn5", "vn5", f1_rev->flow_handle(),
                        flow1->flow_key_nh()->id(),
                        flow0->flow_key_nh()->id()));

    //Verify flow count
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 1, 30,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, 1, 30,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of agent */
    //Decrement the stats so that they become 0
    KSyncSockTypeMap::IncrFlowStats(f1->flow_handle(), -1, -30);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), -1, -30);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x10000000000ULL, 0x1000000000000ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x10000000000ULL, 0x1000000000000ULL,
                               flow1->flow_key_nh()->id()));

    int tmp_age_time = 1000 * 1000;
    Agent* agent = Agent::GetInstance();
    int bkp_age_time = agent->flow_stats_manager()->
                           default_flow_stats_collector_obj()->GetFlowAgeTime();

    //Set the flow age time to 1000 microsecond
    agent->flow_stats_manager()->default_flow_stats_collector_obj()->
        SetFlowAgeTime(tmp_age_time);

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (flow_proto_->FlowCount() == 0U));

    //Restore flow aging time
    agent->flow_stats_manager()->default_flow_stats_collector_obj()->
        SetFlowAgeTime(bkp_age_time);
}

TEST_F(StatsTestMock, FlowStatsTest_tcp_flags) {
    hash_id = 1;

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id++, flow0->flow_key_nh()->id()));
    FlowEntry *f2 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f2 != NULL);
    FlowEntry *f2_rev = f2->reverse_flow_entry();
    EXPECT_TRUE(f2_rev != NULL);
    FlowStatsCollector *fsc = f2->fsc();
    EXPECT_TRUE(fsc != NULL);
    FlowExportInfo *info = fsc->FindFlowExportInfo(f2);
    FlowExportInfo *rinfo = fsc->FindFlowExportInfo(f2_rev);
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);

    //Create flow in reverse direction and make sure it is linked to previous
    //flow
    TxTcpPacketUtil(flow1->id(), "1.1.1.2", "1.1.1.1", 200, 1000,
                    f2_rev->flow_handle());
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, true,
                        "vn5", "vn5", f2_rev->flow_handle(),
                        flow1->flow_key_nh()->id(),
                        flow0->flow_key_nh()->id()));

    //Verify flow count
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    //Invoke FlowStatsCollector to update the TCP flags
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    info = fsc->FindFlowExportInfo(f2);
    rinfo = fsc->FindFlowExportInfo(f2_rev);
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);
    //Verify flow TCP flags
    EXPECT_TRUE((info->tcp_flags() == 0));
    EXPECT_TRUE((rinfo->tcp_flags() == 0));

    //Change the stats
    KSyncSockTypeMap::SetFlowTcpFlags(f2->flow_handle(), VR_FLOW_TCP_SYN);
    KSyncSockTypeMap::SetFlowTcpFlags(f2_rev->flow_handle(), VR_FLOW_TCP_SYN);

    //Invoke FlowStatsCollector to update the TCP flags
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    info = fsc->FindFlowExportInfo(f2);
    rinfo = fsc->FindFlowExportInfo(f2_rev);
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);
    //Verify the updated flow TCP flags
    EXPECT_TRUE((info->tcp_flags() == VR_FLOW_TCP_SYN));
    EXPECT_TRUE((rinfo->tcp_flags() == VR_FLOW_TCP_SYN));

    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    WAIT_FOR(100, 10000, (flow_proto_->FlowCount() == 0U));
}

TEST_F(StatsTestMock, IntfStatsTest) {
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (Agent::GetInstance()->stats_collector());
    collector->interface_stats_responses_ = 0;

    util_.EnqueueAgentStatsCollectorTask(1);
    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));
    client->WaitForIdle(3);

    EXPECT_TRUE(VmPortStatsMatch(test0, 0,0,0,0)); 
    EXPECT_TRUE(VmPortStatsMatch(test1, 0,0,0,0)); 

    //Change the stats
    KSyncSockTypeMap::IfStatsUpdate(test0->id(), 1, 50, 0, 1, 20, 0);
    KSyncSockTypeMap::IfStatsUpdate(test1->id(), 1, 50, 0, 1, 20, 0);

    //Wait for stats to be updated
    collector->interface_stats_responses_ = 0;

    util_.EnqueueAgentStatsCollectorTask(1);
    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));
    client->WaitForIdle(3);

    //Verify the updated flow stats
    EXPECT_TRUE(VmPortStatsMatch(test0, 1, 50, 1, 20)); 
    EXPECT_TRUE(VmPortStatsMatch(test1, 1, 50, 1, 20)); 

    //Reset the stats so that repeat of this test case works
    KSyncSockTypeMap::IfStatsSet(test0->id(), 0, 0, 0, 0, 0, 0);
    KSyncSockTypeMap::IfStatsSet(test1->id(), 0, 0, 0, 0, 0, 0);

    collector->interface_stats_responses_ = 0;
    util_.EnqueueAgentStatsCollectorTask(1);
    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));
    client->WaitForIdle(3);
}

TEST_F(StatsTestMock, InterVnStatsTest) {
    hash_id = 1;
    EXPECT_EQ(0U, flow_proto_->FlowCount());
    //(1) Inter-VN stats between for traffic within same VN
    //Flow creation using IP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    30, 40, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 30, 40, false, 
                        "vn5", "vn5", hash_id++, flow0->flow_key_nh()->id()));

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 30, 40, 1, 30,
                               flow0->flow_key_nh()->id()));

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", "vn5", 1, 30, true); //outgoing stats
    InterVnStatsMatch("vn5", "vn5", 1, 30, false); //Incoming stats

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);
    FlowEntry *f1 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 30, 40,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f1 != NULL);
    FlowEntry *f1_rev = f1->reverse_flow_entry();
    EXPECT_TRUE(f1_rev != NULL);

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(flow1->id(), "1.1.1.2", "1.1.1.1", 40, 30,
                    f1_rev->flow_handle());
    client->WaitForIdle(10);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 40, 30, true, 
                        "vn5", "vn5", f1_rev->flow_handle(),
                        flow1->flow_key_nh()->id(),
                        flow0->flow_key_nh()->id()));

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 40, 30, 1, 30,
                               flow1->flow_key_nh()->id()));

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", "vn5", 2, 60, true); //outgoing stats
    InterVnStatsMatch("vn5", "vn5", 2, 60, false); //Incoming stats

    //(2) Inter-VN stats updation when flow stats are updated
    //Change the stats
    KSyncSockTypeMap::IncrFlowStats(f1->flow_handle(), 1, 30);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), 1, 30);
    client->WaitForIdle(10);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", "vn5", 4, 120, true); //outgoing stats
    InterVnStatsMatch("vn5", "vn5", 4, 120, false); //Incoming stats

    //(3) Inter-VN stats between known and unknown VNs
    //Flow creation using IP packet
    TxIpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.3", 1, hash_id);
    client->WaitForIdle(10);

    // A short flow would be created with Source VN as "vn5" and dest-vn as "Unknown".
    // Not checking for creation of short flow because it will get removed during 
    // the next flow-age evaluation cycle

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    /* Make sure that the short flow is removed */
    WAIT_FOR(100, 10000, (flow_proto_->FlowCount() == 2U));

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", (FlowHandler::UnknownVn()).c_str(), 1, 30, true); //outgoing stats
    InterVnStatsMatch((FlowHandler::UnknownVn()).c_str(), "vn5", 1, 30, false); //Incoming stats

    //clean-up. Flush flow table
    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    WAIT_FOR(100, 10000, (flow_proto_->FlowCount() == 0U));
}

TEST_F(StatsTestMock, VrfStatsTest) {
    //Create 2 vrfs in agent
    VrfAddReq("vrf41");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf41")== true));
    EXPECT_TRUE(DBTableFind("vrf41.uc.route.0"));

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf41");
    EXPECT_TRUE(vrf != NULL);
    int vrf41_id = vrf->vrf_id();

    VrfAddReq("vrf42");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf42")== true));
    EXPECT_TRUE(DBTableFind("vrf42.uc.route.0"));

    vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf42");
    EXPECT_TRUE(vrf != NULL);
    int vrf42_id = vrf->vrf_id();

    //Create 2 vrfs in mock Kernel and update its stats
    KSyncSockTypeMap::VrfStatsAdd(vrf41_id);
    KSyncSockTypeMap::VrfStatsAdd(vrf42_id);
    vr_vrf_stats_req v41_req = GetVrfStatsObj(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
        12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25);
    vr_vrf_stats_req v42_req = GetVrfStatsObj(26, 27, 28, 29, 30, 31, 32, 33,
        34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50);
    KSyncSockTypeMap::VrfStatsUpdate(vrf41_id, v41_req);
    KSyncSockTypeMap::VrfStatsUpdate(vrf42_id, v42_req);

    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (Agent::GetInstance()->stats_collector());
    //Wait for stats to be updated in agent
    collector->vrf_stats_responses_ = 0;

    util_.EnqueueAgentStatsCollectorTask(1);
    client->WaitForIdle(3);
    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->vrf_stats_responses_ >= 1));
    client->WaitForIdle(3);

    //Verfify the stats read from kernel
    EXPECT_TRUE(VrfStatsMatch(vrf41_id, string("vrf41"), true, v41_req));
    EXPECT_TRUE(VrfStatsMatch(vrf42_id, string("vrf42"), true, v42_req));

    vr_vrf_stats_req zero_req;
    //Verfify the prev_* fields of vrf_stats are 0
    EXPECT_TRUE(VrfStatsMatchPrev(vrf41_id, zero_req));
    EXPECT_TRUE(VrfStatsMatchPrev(vrf42_id, zero_req));

    //Delete both the VRFs from agent
    VrfDelReq("vrf41");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf41")== false));
    EXPECT_FALSE(DBTableFind("vrf41.uc.route.0"));

    VrfDelReq("vrf42");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf42")== false));
    EXPECT_FALSE(DBTableFind("vrf42.uc.route.0"));

    //Verify that vrf_stats's entry is still present in agent_stats_collector.
    EXPECT_TRUE(VrfStatsMatch(vrf41_id, string("vrf41"), false, zero_req));
    EXPECT_TRUE(VrfStatsMatch(vrf42_id, string("vrf42"), false, zero_req));

    //Verify that prev_* fields of vrf_stats are updated
    EXPECT_TRUE(VrfStatsMatchPrev(vrf41_id, v41_req));
    EXPECT_TRUE(VrfStatsMatchPrev(vrf42_id, v42_req));

    //Update stats in mock kernel when vrfs are absent in agent
    vr_vrf_stats_req v41_req2 = GetVrfStatsObj(51, 52, 53, 54, 55, 56, 57, 58,
        59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75);
    vr_vrf_stats_req v42_req2 = GetVrfStatsObj(76, 77, 78, 79, 80, 81, 82, 83,
        84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100);
    KSyncSockTypeMap::VrfStatsUpdate(vrf41_id, v41_req2);
    KSyncSockTypeMap::VrfStatsUpdate(vrf42_id, v42_req2);

    //Wait for stats to be updated in agent
    collector->vrf_stats_responses_ = 0;

    util_.EnqueueAgentStatsCollectorTask(1);
    client->WaitForIdle(3);
    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->vrf_stats_responses_ >= 1));
    client->WaitForIdle(3);

    //Verify that prev_* fields of vrf_stats are updated when vrf is absent from agent
    EXPECT_TRUE(VrfStatsMatchPrev(vrf41_id, v41_req2));
    EXPECT_TRUE(VrfStatsMatchPrev(vrf42_id, v42_req2));

    //Add 2 VRFs in agent. They will re-use the id's allocated earlier
    VrfAddReq("vrf41");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf41")== true));
    EXPECT_TRUE(DBTableFind("vrf41.uc.route.0"));

    vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf41");
    EXPECT_TRUE(vrf != NULL);

    VrfAddReq("vrf42");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf42")== true));
    EXPECT_TRUE(DBTableFind("vrf42.uc.route.0"));

    vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf42");
    EXPECT_TRUE(vrf != NULL);

    //Wait for stats to be updated in agent
    collector->vrf_stats_responses_ = 0;

    util_.EnqueueAgentStatsCollectorTask(1);
    client->WaitForIdle(3);
    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->vrf_stats_responses_ >= 1));
    client->WaitForIdle(3);

    //Verify that vrf_stats's entry stats are set to 0
    EXPECT_TRUE(VrfStatsMatch(vrf41_id, string("vrf41"), true, zero_req));
    EXPECT_TRUE(VrfStatsMatch(vrf42_id, string("vrf42"), true, zero_req));

    //Update stats in mock kernel when vrfs are absent in agent
    vr_vrf_stats_req v41_req3 = GetVrfStatsObj(101, 102, 103, 104, 105, 106,
        107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120,
        121, 122, 123, 124, 125);
    vr_vrf_stats_req v42_req3 = GetVrfStatsObj(126, 127, 128, 129, 130, 131,
        132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145,
        146, 147, 148, 149, 150);
    KSyncSockTypeMap::VrfStatsUpdate(vrf41_id, v41_req3);
    KSyncSockTypeMap::VrfStatsUpdate(vrf42_id, v42_req3);

    //Wait for stats to be updated in agent
    collector->vrf_stats_responses_ = 0;

    util_.EnqueueAgentStatsCollectorTask(1);
    client->WaitForIdle(3);
    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->vrf_stats_responses_ >= 1));
    client->WaitForIdle(3);

    //Verify that vrf_stats_entry's stats are set to values after the vrf was added
    vr_vrf_stats_req match_req = GetVrfStatsObj(50, 50, 50, 50, 50, 50, 50,
        50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50);
    EXPECT_TRUE(VrfStatsMatch(vrf41_id, string("vrf41"), true, match_req));
    EXPECT_TRUE(VrfStatsMatch(vrf42_id, string("vrf42"), true, match_req));

    //Cleanup-Remove the VRFs added 
    VrfDelReq("vrf41");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf41")== false));
    EXPECT_FALSE(DBTableFind("vrf41.uc.route.0"));

    VrfDelReq("vrf42");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf42")== false));
    EXPECT_FALSE(DBTableFind("vrf42.uc.route.0"));

    //Remove vrf stats object from agent
    collector->Test_DeleteVrfStatsEntry(vrf41_id);
    collector->Test_DeleteVrfStatsEntry(vrf42_id);

    //Remove vrf stats object from mock Kernel
    KSyncSockTypeMap::VrfStatsDelete(vrf41_id);
    KSyncSockTypeMap::VrfStatsDelete(vrf42_id);
}

//Flow parameters (vrouter, peer_vrouter and tunnel_type) verification for
//local flow
TEST_F(StatsTestMock, Underlay_1) {
    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf5",
                        flow0->id()),
            {
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    CreateFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(fe != NULL);
    FlowStatsCollector *fsc = fe->fsc();
    EXPECT_TRUE(fsc != NULL);
    FlowExportInfo *info = fsc->FindFlowExportInfo(fe);
    FlowExportInfo *rinfo = fsc->FindFlowExportInfo(rfe);
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);

    EXPECT_STREQ(fe->peer_vrouter().c_str(),
                 Agent::GetInstance()->router_id().to_string().c_str());
    EXPECT_EQ(fe->tunnel_type().GetType(), TunnelType::INVALID);
    EXPECT_EQ(info->underlay_source_port(), 0);

    EXPECT_STREQ(rfe->peer_vrouter().c_str(),
                 Agent::GetInstance()->router_id().to_string().c_str());
    EXPECT_EQ(rfe->tunnel_type().GetType(), TunnelType::INVALID);
    EXPECT_EQ(rinfo->underlay_source_port(), 0);

    DeleteFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(0U, flow_proto_->FlowCount());
}

//Flow parameters (vrouter, peer_vrouter and tunnel_type) verification for
//non-local flow
TEST_F(StatsTestMock, Underlay_2) {
    /* Add remote VN route to vrf5 */
    util_.CreateRemoteRoute("vrf5", remote_vm4_ip, remote_router_ip, 8, "vn3",
                            peer_);

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, remote_vm4_ip, "1.1.1.1", 1, 0, 0, "vrf5",
                        remote_router_ip, flow0->label()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };
    CreateFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(fe != NULL);
    FlowStatsCollector *fsc = fe->fsc();
    EXPECT_TRUE(fsc != NULL);
    FlowExportInfo *info = fsc->FindFlowExportInfo(fe);
    FlowExportInfo *rinfo = fsc->FindFlowExportInfo(rfe);
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);

    EXPECT_STREQ(fe->peer_vrouter().c_str(), remote_router_ip);
    EXPECT_EQ(fe->tunnel_type().GetType(), TunnelType::MPLS_GRE);
    EXPECT_EQ(info->underlay_source_port(), 0);

    EXPECT_STREQ(rfe->peer_vrouter().c_str(), remote_router_ip);
    EXPECT_EQ(rfe->tunnel_type().GetType(), TunnelType::MPLS_GRE);
    EXPECT_EQ(rinfo->underlay_source_port(), 0);

    DeleteFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(0U, flow_proto_->FlowCount());

    //Remove remote VM routes
    util_.DeleteRemoteRoute("vrf5", remote_vm4_ip, peer_);
    client->WaitForIdle();
}

//Flow underlay source port verification for non-local flow
TEST_F(StatsTestMock, Underlay_3) {
    /* Add remote VN route to vrf5 */
    util_.CreateRemoteRoute("vrf5", remote_vm4_ip, remote_router_ip, 8, "vn3",
                            peer_);

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, remote_vm4_ip, "1.1.1.1", 1, 0, 0, "vrf5",
                        remote_router_ip, flow0->label()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };
    CreateFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(fe != NULL);
    FlowStatsCollector *fsc = fe->fsc();
    EXPECT_TRUE(fsc != NULL);
    FlowExportInfo *info = fsc->FindFlowExportInfo(fe);
    FlowExportInfo *rinfo = fsc->FindFlowExportInfo(rfe);
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);
    client->WaitForIdle();

    WAIT_FOR(1000, 10000, (rfe->flow_handle() != FlowEntry::kInvalidFlowHandle));
    //Change the underlay source port
    KSyncSockTypeMap::SetUnderlaySourcePort(fe->flow_handle(), 1234);
    KSyncSockTypeMap::SetUnderlaySourcePort(rfe->flow_handle(), 5678);

    //Increment flow stats to ensure that when flow_stats_collector is
    //invoked, the dispatch of flow log message happens
    KSyncSockTypeMap::IncrFlowStats(fe->flow_handle(), 1, 30);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify that exported flows have exported_alteast_once_ flag set
    EXPECT_TRUE(info->exported_atleast_once());
    EXPECT_TRUE(rinfo->exported_atleast_once());

    //Verify underlay source port for forward flow
    EXPECT_EQ(fe->tunnel_type().GetType(), TunnelType::MPLS_GRE);
    EXPECT_EQ(info->underlay_source_port(), 1234);

    //Verify underlay source port for reverse flow
    EXPECT_EQ(rfe->tunnel_type().GetType(), TunnelType::MPLS_GRE);
    EXPECT_EQ(rinfo->underlay_source_port(), 5678);

    //Since encap type is MPLS_GRE verify that exported flow has
    //0 as underlay source port
    FlowStatsCollectorTest *f = static_cast<FlowStatsCollectorTest *>(fsc);
    FlowLogData flow_log = f->last_sent_flow_log();
    EXPECT_EQ(flow_log.get_underlay_source_port(), 0);

    //Verify that forward_flow field is set
    EXPECT_TRUE((flow_log.__isset.forward_flow));

    //Verify that teardown_time is not set
    EXPECT_FALSE((flow_log.__isset.teardown_time));

    //cleanup
    DeleteFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(0U, flow_proto_->FlowCount());

    //Verify that FlowLog sent for deleted flow has teadown time set.
    flow_log = f->last_sent_flow_log();
    EXPECT_TRUE((flow_log.__isset.teardown_time));
    EXPECT_TRUE((flow_log.get_teardown_time() != 0));

    //Verify that forward_flow field is set
    EXPECT_TRUE((flow_log.__isset.forward_flow));

    //Remove remote VM routes
    util_.DeleteRemoteRoute("vrf5", remote_vm4_ip, peer_);
    client->WaitForIdle();
}

TEST_F(StatsTestMock, FlowReuse_1) {
    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf5",
                        flow0->id()),
            {
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    FlowStatsCollector *col1 = agent_->flow_stats_manager()->
        default_flow_stats_collector_obj()->GetCollector(0);
    EXPECT_TRUE(col1 != NULL);
    FlowStatsCollectorTest *f1 = static_cast<FlowStatsCollectorTest *>(col1);
    f1->ClearCount();
    FlowStatsCollector *col2 = agent_->flow_stats_manager()->
        default_flow_stats_collector_obj()->GetCollector(1);
    EXPECT_TRUE(col2 != NULL);
    FlowStatsCollectorTest *f2 = static_cast<FlowStatsCollectorTest *>(col2);
    f2->ClearCount();

    CreateFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(fe != NULL);

    FlowStatsCollector *fsc = fe->fsc();
    EXPECT_TRUE(fsc != NULL);
    FlowStatsCollectorTest *f = static_cast<FlowStatsCollectorTest *>(fsc);
    WAIT_FOR(1000, 10000, (f->dispatch_count() == 4U));
    boost::uuids::uuid u = fe->uuid();
    boost::uuids::uuid u_r = rfe->uuid();
    boost::uuids::uuid u_e = fe->egress_uuid();
    boost::uuids::uuid u_r_e = rfe->egress_uuid();
    f->ResetLastSentLog();
    f->ClearCount();
    client->WaitForIdle();
    CreateFlow(flow, 1);
    client->WaitForIdle();
    fe = flow[0].pkt_.FlowFetch();
    EXPECT_TRUE((u != fe->uuid()));

    WAIT_FOR(1000, 10000, (f->dispatch_count() == 4U));
    WAIT_FOR(1000, 10000, (f->flow_del_log_valid() == true));
    FlowLogData flow_log = f->last_sent_flow_del_log();
    EXPECT_TRUE((flow_log.get_flowuuid() == to_string(u)) ||
                (flow_log.get_flowuuid() == to_string(u_r)) ||
                (flow_log.get_flowuuid() == to_string(u_e)) ||
                (flow_log.get_flowuuid() == to_string(u_r_e)));
    EXPECT_TRUE((!flow_log.get_sourcevn().empty()));
    EXPECT_TRUE((!flow_log.get_destvn().empty()));

    DeleteFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(0U, flow_proto_->FlowCount());
    f->ResetLastSentLog();
    f->ClearCount();
}

#if 0
TEST_F(StatsTestMock, FlowTcpClosedFlow) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);

    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id, flow0->flow_key_nh()->id()));
    FlowEntry *f2 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f2 != NULL);
    FlowEntry *f2_rev = f2->reverse_flow_entry();
    EXPECT_TRUE(f2_rev != NULL);

    //Verify flow count
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 1, 30,
                               flow0->flow_key_nh()->id()));


    //Mark the flow as TCP closed and verify flow gets aged
    KSyncSockTypeMap::SetTcpFlag(hash_id, VR_FLOW_TCP_HALF_CLOSE);
    KSyncSockTypeMap::SetTcpFlag(f2_rev->flow_handle(), VR_FLOW_TCP_HALF_CLOSE);
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    EXPECT_FALSE(FlowGet(flow0->flow_key_nh()->id(),
                "1.1.1.1", "1.1.1.2", 6, 1000, 200));

    EXPECT_FALSE(FlowGet(flow1->flow_key_nh()->id(),
                "1.1.1.2", "1.1.1.1", 6, 200, 1000));

    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    WAIT_FOR(100, 10000, (flow_proto_->FlowCount() == 0U));
}

TEST_F(StatsTestMock, FlowTcpHalfClosedFlow) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);

    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id, flow0->flow_key_nh()->id()));
    FlowEntry *f2 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f2 != NULL);
    FlowEntry *f2_rev = f2->reverse_flow_entry();
    EXPECT_TRUE(f2_rev != NULL);

    //Verify flow count
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    KSyncSockTypeMap::IncrFlowStats(hash_id, 1, 30);
    KSyncSockTypeMap::IncrFlowStats(f2_rev->flow_handle(), 1, 30);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                         "vn5", "vn5", hash_id, flow0->flow_key_nh()->id()));

    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, true,
                         "vn5", "vn5", f2_rev->flow_handle(),
                         flow1->flow_key_nh()->id(),
                         flow0->flow_key_nh()->id()));

    //Mark the flow as TCP closed and verify flow gets aged
    KSyncSockTypeMap::SetTcpFlag(hash_id, VR_FLOW_TCP_HALF_CLOSE);
    KSyncSockTypeMap::IncrFlowStats(f2_rev->flow_handle(), 5, 30);
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                         "vn5", "vn5", hash_id, flow0->flow_key_nh()->id()));

    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, true,
                         "vn5", "vn5", f2_rev->flow_handle(),
                         flow1->flow_key_nh()->id(),
                         flow0->flow_key_nh()->id()));

    KSyncSockTypeMap::SetTcpFlag(f2_rev->flow_handle(), VR_FLOW_TCP_HALF_CLOSE);
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    EXPECT_FALSE(FlowGet(flow0->flow_key_nh()->id(),
                "1.1.1.1", "1.1.1.2", 6, 1000, 200));

    EXPECT_FALSE(FlowGet(flow1->flow_key_nh()->id(),
                "1.1.1.2", "1.1.1.1", 6, 200, 1000));

    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    WAIT_FOR(100, 10000, (flow_proto_->FlowCount() == 0U));
}

TEST_F(StatsTestMock, FlowSyncFlow) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);

    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id, flow0->flow_key_nh()->id()));
    FlowEntry *f2 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f2 != NULL);
    FlowEntry *f2_rev = f2->reverse_flow_entry();
    EXPECT_TRUE(f2_rev != NULL);

    //Verify flow count
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    KSyncSockTypeMap::IncrFlowStats(hash_id, 1, 30);
    KSyncSockTypeMap::SetTcpFlag(hash_id, VR_FLOW_TCP_SYN);
    KSyncSockTypeMap::SetTcpFlag(f2_rev->flow_handle(), VR_FLOW_TCP_SYN_R);
    Agent::GetInstance()->flow_stats_collector()->
         set_flow_tcp_syn_age_time(1000000 * 1);
    sleep(1);
    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    EXPECT_FALSE(FlowGet(flow0->flow_key_nh()->id(),
                "1.1.1.1", "1.1.1.2", 6, 1000, 200));

    EXPECT_FALSE(FlowGet(flow1->flow_key_nh()->id(),
                "1.1.1.2", "1.1.1.1", 6, 200, 1000));

    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    WAIT_FOR(100, 10000, (flow_proto_->FlowCount() == 0U));
}

TEST_F(StatsTestMock, FlowTcpResetFlow) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);

    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id, flow0->flow_key_nh()->id()));
    FlowEntry *f2 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f2 != NULL);
    FlowEntry *f2_rev = f2->reverse_flow_entry();
    EXPECT_TRUE(f2_rev != NULL);

    //Verify flow count
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 1, 30,
                               flow0->flow_key_nh()->id()));


    //Mark the flow as TCP closed and verify flow gets aged
    KSyncSockTypeMap::SetTcpFlag(hash_id, VR_FLOW_TCP_RST);
    KSyncSockTypeMap::SetTcpFlag(f2_rev->flow_handle(), VR_FLOW_TCP_RST);
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    EXPECT_FALSE(FlowGet(flow0->flow_key_nh()->id(),
                "1.1.1.1", "1.1.1.2", 6, 1000, 200));

    EXPECT_FALSE(FlowGet(flow1->flow_key_nh()->id(),
                "1.1.1.2", "1.1.1.1", 6, 200, 1000));

    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    WAIT_FOR(100, 10000, (flow_proto_->FlowCount() == 0U));
}

TEST_F(StatsTestMock, FlowSyncFlow) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);

    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id, flow0->flow_key_nh()->id()));
    FlowEntry *f2 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f2 != NULL);
    FlowEntry *f2_rev = f2->reverse_flow_entry();
    EXPECT_TRUE(f2_rev != NULL);

    //Verify flow count
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowStatsCollector *fsc = f2->fsc();
    EXPECT_TRUE(fsc != NULL);
    KSyncSockTypeMap::IncrFlowStats(hash_id, 1, 30);
    KSyncSockTypeMap::SetTcpFlag(hash_id, VR_FLOW_TCP_SYN);
    KSyncSockTypeMap::SetTcpFlag(f2_rev->flow_handle(), VR_FLOW_TCP_SYN_R);
    fsc->set_flow_tcp_syn_age_time(1000000 * 1);
    sleep(1);
    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    EXPECT_FALSE(FlowGet(flow0->flow_key_nh()->id(),
                "1.1.1.1", "1.1.1.2", 6, 1000, 200));

    EXPECT_FALSE(FlowGet(flow1->flow_key_nh()->id(),
                "1.1.1.2", "1.1.1.1", 6, 200, 1000));

    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    WAIT_FOR(100, 10000, (flow_proto_->FlowCount() == 0U));
}

TEST_F(StatsTestMock, FlowTcpResetFlow) {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);

    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id, flow0->flow_key_nh()->id()));
    FlowEntry *f2 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f2 != NULL);
    FlowEntry *f2_rev = f2->reverse_flow_entry();
    EXPECT_TRUE(f2_rev != NULL);

    //Verify flow count
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 1, 30,
                               flow0->flow_key_nh()->id()));


    //Mark the flow as TCP closed and verify flow gets aged
    KSyncSockTypeMap::SetTcpFlag(hash_id, VR_FLOW_TCP_RST);
    KSyncSockTypeMap::SetTcpFlag(f2_rev->flow_handle(), VR_FLOW_TCP_RST);
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    EXPECT_FALSE(FlowGet(flow0->flow_key_nh()->id(),
                "1.1.1.1", "1.1.1.2", 6, 1000, 200));

    EXPECT_FALSE(FlowGet(flow1->flow_key_nh()->id(),
                "1.1.1.2", "1.1.1.1", 6, 200, 1000));

    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    WAIT_FOR(100, 10000, (flow_proto_->FlowCount() == 0U));
}
#endif

int main(int argc, char *argv[]) {
    int ret = 0;

    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false, true);
    usleep(10000);
    StatsTestMock::TestSetup();
    peer_ = CreateBgpPeer("127.0.0.1", "Bgp Peer");

    ret = RUN_ALL_TESTS();
    client->WaitForIdle(3);
    StatsTestMock::TestTeardown();
    DeleteBgpPeer(peer_);
    client->WaitForIdle(3);
    TestShutdown();
    delete client;
    return ret;
}
