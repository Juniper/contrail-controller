/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <cfg/cfg_init.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/mirror_table.h>
#include <oper/physical_device.h>
#include <uve/agent_uve.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "pkt/test/test_flow_util.h"
#include "vr_types.h"
#include <uve/vrouter_stats_collector.h>
#include <uve/test/vrouter_uve_entry_test.h>
#include "uve/test/test_uve_util.h"
#include "ksync/ksync_sock_user.h"
#include "uve/test/prouter_uve_table_test.h"

using namespace std;

#define vm1_ip "11.1.1.1"
#define vm2_ip "11.1.1.2"

struct PortInfo input[] = {
        {"flow0", 6, vm1_ip, "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, vm2_ip, "00:00:00:01:01:02", 5, 2},
};
IpamInfo ipam_info[] = {
    {"11.1.1.0", 24, "11.1.1.10"},
};

void RouterIdDepInit(Agent *agent) {
}

class VrouterUveSendTask : public Task {
public:
    VrouterUveSendTask() :
        Task((TaskScheduler::GetInstance()->GetTaskId(kTaskDBExclude)), 0) {
    }
    virtual bool Run() {
        Agent::GetInstance()->uve()->vrouter_uve_entry()->TimerExpiry();
        return true;
    }
    std::string Description() const { return "VrouterUveSendTask"; }
};

int hash_id;
VmInterface *flow0, *flow1;

class UveVrouterUveTest : public ::testing::Test {
public:
    UveVrouterUveTest() : util_(), agent_(Agent::GetInstance()) {
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
    }
    static void TestSetup() {
    }
    void EnqueueSendVrouterUveTask() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        VrouterUveSendTask *task = new VrouterUveSendTask();
        scheduler->Enqueue(task);
    }
    void FlowSetUp() {
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        client->Reset();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(5);
        AddIPAM("vn5", ipam_info, 1);
        client->WaitForIdle();

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));

        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmInterfaceGet(input[1].intf_id);
        assert(flow1);
    }

    void FlowTearDown() {
        client->EnqueueFlowFlush();
        client->WaitForIdle(10);
        client->Reset();
        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle(3);
        DelIPAM("vn5");
        client->WaitForIdle();
        client->PortDelNotifyWait(2);
        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));
        EXPECT_EQ(0U, flow_proto_->FlowCount());
    }

    bool ValidateBmap(const std::vector<uint32_t> &smap, const std::vector<uint32_t> &dmap,
                      uint16_t sport, uint16_t dport) {
        bool ret = true;

        int idx = sport / L4PortBitmap::kBucketCount;
        int sport_idx = idx / L4PortBitmap::kBitsPerEntry;
        int sport_bit = (1 << (idx % L4PortBitmap::kBitsPerEntry));

        idx = dport / L4PortBitmap::kBucketCount;
        int dport_idx = idx / L4PortBitmap::kBitsPerEntry;
        int dport_bit = (1 << (idx % L4PortBitmap::kBitsPerEntry));

        uint32_t val = smap[sport_idx];
        if ((val & sport_bit) == 0) {
            EXPECT_STREQ("TCP Sport bit not set", "");
            ret = false;
        }

        val = dmap[dport_idx];
        if ((val & dport_bit) == 0) {
            EXPECT_STREQ("TCP Dport bit not set", "");
            ret = false;
        }
        return ret;
    }

    bool BandwidthMatch(const map<string,uint64_t> &imp,
                        const map<string,uint64_t> &omp,
                        uint64_t in,
                        uint64_t out) {
        if (0 == imp.size()) {
            if (in == 0) {
                return true;
            }
            return false;
        }
        if (0 == omp.size()) {
            if (out == 0) {
                return true;
            }
            return false;
        }

        map<string,uint64_t>::const_iterator it;
        EXPECT_EQ(1U, imp.size());
        it = imp.begin();
        EXPECT_EQ(in, it->second);
        if (in != it->second) {
            return false;
        }

        EXPECT_EQ(1U, omp.size());
        it = omp.begin();
        EXPECT_EQ(out, it->second);
        if (out != it->second) {
            return false;
        }
        return true;
    }

    bool BandwidthMatch(const vector<AgentIfBandwidth> &list, uint64_t in,
                        uint64_t out) {
        if (0 == list.size()) {
            if ((in == 0) && (out == 0)) {
                return true;
            }
            return false;
        }
        EXPECT_EQ(1U, list.size());
        vector<AgentIfBandwidth>::const_iterator it = list.begin();
        AgentIfBandwidth band = *it;
        EXPECT_EQ(in, band.get_in_bandwidth_usage());
        EXPECT_EQ(out, band.get_out_bandwidth_usage());
        if ((in == band.get_in_bandwidth_usage()) &&
            (out == band.get_out_bandwidth_usage())) {
            return true;
        }
        return false;
    }

    bool DropStatsEqual(const AgentDropStats &d1,
                        const vr_drop_stats_req &d2) const {
        if (d1.get_ds_discard() != (uint64_t)d2.get_vds_discard()) {
            return false;
        }
        if (d1.get_ds_pull()  != (uint64_t)d2.get_vds_pull()) {
            return false;
        }
        if (d1.get_ds_invalid_if()  != (uint64_t)d2.get_vds_invalid_if()) {
            return false;
        }
        if (d1.get_ds_invalid_arp()  != (uint64_t)d2.get_vds_invalid_arp()) {
            return false;
        }
        if (d1.get_ds_trap_no_if()  != (uint64_t)d2.get_vds_trap_no_if()) {
            return false;
        }
        if (d1.get_ds_nowhere_to_go()  != (uint64_t)d2.get_vds_nowhere_to_go()) {
            return false;
        }
        if (d1.get_ds_flow_queue_limit_exceeded()  != (uint64_t)d2.get_vds_flow_queue_limit_exceeded()) {
            return false;
        }
        if (d1.get_ds_flow_no_memory()  != (uint64_t)d2.get_vds_flow_no_memory()) {
            return false;
        }
        if (d1.get_ds_flow_invalid_protocol()  != (uint64_t)d2.get_vds_flow_invalid_protocol()) {
            return false;
        }
        if (d1.get_ds_flow_nat_no_rflow()  != (uint64_t)d2.get_vds_flow_nat_no_rflow()) {
            return false;
        }
        if (d1.get_ds_flow_action_drop()  != (uint64_t)d2.get_vds_flow_action_drop()) {
            return false;
        }
        if (d1.get_ds_flow_action_invalid()  != (uint64_t)d2.get_vds_flow_action_invalid()) {
            return false;
        }
        if (d1.get_ds_flow_unusable()  != (uint64_t)d2.get_vds_flow_unusable()) {
            return false;
        }
        if (d1.get_ds_flow_table_full()  != (uint64_t)d2.get_vds_flow_table_full()) {
            return false;
        }
        if (d1.get_ds_interface_tx_discard()  != (uint64_t)d2.get_vds_interface_tx_discard()) {
            return false;
        }
        if (d1.get_ds_interface_drop()  != (uint64_t)d2.get_vds_interface_drop()) {
            return false;
        }
        if (d1.get_ds_duplicated()  != (uint64_t)d2.get_vds_duplicated()) {
            return false;
        }
        if (d1.get_ds_push()  != (uint64_t)d2.get_vds_push()) {
            return false;
        }
        if (d1.get_ds_ttl_exceeded () != (uint64_t)d2.get_vds_ttl_exceeded()) {
            return false;
        }
        if (d1.get_ds_invalid_nh()  != (uint64_t)d2.get_vds_invalid_nh()) {
            return false;
        }
        if (d1.get_ds_invalid_label()  != (uint64_t)d2.get_vds_invalid_label()) {
            return false;
        }
        if (d1.get_ds_invalid_protocol()  != (uint64_t)d2.get_vds_invalid_protocol()) {
            return false;
        }
        if (d1.get_ds_interface_rx_discard()  != (uint64_t)d2.get_vds_interface_rx_discard()) {
            return false;
        }
        if (d1.get_ds_invalid_mcast_source()  != (uint64_t)d2.get_vds_invalid_mcast_source()) {
            return false;
        }
        if (d1.get_ds_head_alloc_fail()  != (uint64_t)d2.get_vds_head_alloc_fail()) {
            return false;
        }
        if (d1.get_ds_pcow_fail()  != (uint64_t)d2.get_vds_pcow_fail()) {
            return false;
        }
        if (d1.get_ds_mcast_clone_fail()  != (uint64_t)d2.get_vds_mcast_clone_fail()) {
            return false;
        }
        if (d1.get_ds_rewrite_fail()  != (uint64_t)d2.get_vds_rewrite_fail()) {
            return false;
        }
        if (d1.get_ds_misc()  != (uint64_t)d2.get_vds_misc()) {
            return false;
        }
        if (d1.get_ds_invalid_packet()  != (uint64_t)d2.get_vds_invalid_packet()) {
            return false;
        }
        if (d1.get_ds_cksum_err()  != (uint64_t)d2.get_vds_cksum_err()) {
            return false;
        }
        if (d1.get_ds_no_fmd()  != (uint64_t)d2.get_vds_no_fmd()) {
            return false;
        }
        if (d1.get_ds_invalid_vnid()  != (uint64_t)d2.get_vds_invalid_vnid()) {
            return false;
        }
        if (d1.get_ds_frag_err()  != (uint64_t)d2.get_vds_frag_err()) {
            return false;
        }
        if (d1.get_ds_invalid_source()  != (uint64_t)d2.get_vds_invalid_source()) {
            return false;
        }
        if (d1.get_ds_mcast_df_bit()  != (uint64_t)d2.get_vds_mcast_df_bit()) {
            return false;
        }
        if (d1.get_ds_l2_no_route()  != (uint64_t)d2.get_vds_l2_no_route()) {
            return false;
        }
        if (d1.get_ds_vlan_fwd_tx() != (uint64_t)d2.get_vds_vlan_fwd_tx()) {
            return false;
        }
        if (d1.get_ds_vlan_fwd_enq() != (uint64_t)d2.get_vds_vlan_fwd_enq()) {
            return false;
        }
        return true;
    }
    TestUveUtil util_;
    Agent *agent_;
    FlowProto *flow_proto_;
};

TEST_F(UveVrouterUveTest, VmAddDel) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (agent_->uve()->vrouter_uve_entry());
    vr->clear_count();

    const VrouterAgent &uve = vr->last_sent_vrouter();
    EXPECT_EQ(0U, vr->vrouter_msg_count());
    EXPECT_EQ(0U, uve.get_virtual_machine_list().size());

    util_.VmAdd(1);
    client->WaitForIdle();

    EnqueueSendVrouterUveTask();
    client->WaitForIdle();
    vr->WaitForWalkCompletion();

    WAIT_FOR(100, 100, (2U == vr->vrouter_msg_count()));
    if (uve.__isset.virtual_machine_list) {
        WAIT_FOR(100, 100, (1U == uve.get_virtual_machine_list().size()));
    }

    util_.VmAdd(2);
    client->WaitForIdle();

    EnqueueSendVrouterUveTask();
    vr->WaitForWalkCompletion();

    WAIT_FOR(100, 100, (3U == vr->vrouter_msg_count()));
    WAIT_FOR(100, 100, (2U == uve.get_virtual_machine_list().size()));

    util_.VmDelete(2);
    client->WaitForIdle();

    EnqueueSendVrouterUveTask();
    vr->WaitForWalkCompletion();

    WAIT_FOR(100, 100, (4U == vr->vrouter_msg_count()));
    WAIT_FOR(100, 100, (1U == uve.get_virtual_machine_list().size()));

    util_.VmDelete(1);
    client->WaitForIdle();

    EnqueueSendVrouterUveTask();
    vr->WaitForWalkCompletion();

    WAIT_FOR(100, 100, (5U == vr->vrouter_msg_count()));
    WAIT_FOR(100, 100, (0U == uve.get_virtual_machine_list().size()));
    vr->clear_count();
}

TEST_F(UveVrouterUveTest, VnAddDel) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (agent_->uve()->vrouter_uve_entry());
    vr->clear_count();

    const VrouterAgent &uve = vr->last_sent_vrouter();
    EXPECT_EQ(0U, vr->vrouter_msg_count());
    EXPECT_EQ(0U, uve.get_connected_networks().size());

    util_.VnAdd(1);
    client->WaitForIdle();

    EnqueueSendVrouterUveTask();
    vr->WaitForWalkCompletion();

    WAIT_FOR(100, 100, (1U == vr->vrouter_msg_count()));
    WAIT_FOR(100, 100, (1U == uve.get_connected_networks().size()));
    WAIT_FOR(100, 100, (1U == uve.get_vn_count()));

    util_.VnAdd(2);
    client->WaitForIdle();

    EnqueueSendVrouterUveTask();
    vr->WaitForWalkCompletion();

    WAIT_FOR(100, 100, (2U == vr->vrouter_msg_count()));
    WAIT_FOR(100, 100, (2U == uve.get_connected_networks().size()));
    WAIT_FOR(100, 100, (2U == uve.get_vn_count()));

    util_.VnDelete(2);
    client->WaitForIdle();

    EnqueueSendVrouterUveTask();
    vr->WaitForWalkCompletion();

    WAIT_FOR(100, 100, (3U == vr->vrouter_msg_count()));
    WAIT_FOR(100, 100, (1U == uve.get_connected_networks().size()));
    WAIT_FOR(100, 100, (1U == uve.get_vn_count()));

    util_.VnDelete(1);
    client->WaitForIdle();

    EnqueueSendVrouterUveTask();
    vr->WaitForWalkCompletion();

    WAIT_FOR(100, 100, (4U == vr->vrouter_msg_count()));
    WAIT_FOR(100, 100, (0U == uve.get_connected_networks().size()));
    WAIT_FOR(100, 100, (0U == uve.get_vn_count()));
    vr->clear_count();
}

TEST_F(UveVrouterUveTest, IntfAddDel) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (agent_->uve()->vrouter_uve_entry());
    vr->clear_count();

    const VrouterAgent &uve = vr->last_sent_vrouter();

    //Add VN
    util_.VnAdd(input[0].vn_id);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (VnGet(1) != NULL));

    // Nova Port add message
    util_.NovaPortAdd(input);

    // Config Port add
    util_.ConfigPortAdd(input);

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Trigger UVE send
    EnqueueSendVrouterUveTask();
    vr->WaitForWalkCompletion();

    //Verify interface lists in UVE
    WAIT_FOR(100, 100, (1U == uve.get_interface_list().size()));
    WAIT_FOR(100, 100, (1U == uve.get_error_intf_list().size()));

    //Add necessary objects and links to make vm-intf active
    util_.VmAdd(input[0].vm_id);
    util_.VrfAdd(input[0].vn_id);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    AddVmPortVrf("vnet1", "", 0);
    client->WaitForIdle();
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    //Trigger UVE send
    EnqueueSendVrouterUveTask();
    vr->WaitForWalkCompletion();

    //Verify interface lists in UVE
    WAIT_FOR(100, 100, (1U == uve.get_interface_list().size()));
    WAIT_FOR(100, 100, (0U == uve.get_error_intf_list().size()));

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Trigger UVE send
    EnqueueSendVrouterUveTask();
    vr->WaitForWalkCompletion();

    //Verify interface lists in UVE
    WAIT_FOR(100, 100, (1U == uve.get_interface_list().size()));
    WAIT_FOR(100, 100, (1U == uve.get_error_intf_list().size()));

    //other cleanup
    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    DelNode("virtual-machine-interface-routing-instance", "vnet1");
    DelNode("virtual-machine", "vm1");
    DelNode("routing-instance", "vrf1");
    //DelNode("virtual-network", "vn1");
    DelNode("virtual-machine-interface", "vnet1");
    DelInstanceIp("instance0");
    util_.VnDelete(input[0].vn_id);
    IntfCfgDel(input, 0);
    client->WaitForIdle();
    client->WaitForIdle();

    //cleanup
    EnqueueSendVrouterUveTask();
    vr->WaitForWalkCompletion();

    EXPECT_EQ(0U, uve.get_connected_networks().size());
    EXPECT_EQ(0U, uve.get_vn_count());
    EXPECT_EQ(0U, uve.get_interface_list().size());
    EXPECT_EQ(0U, uve.get_error_intf_list().size());
    vr->clear_count();
}

TEST_F(UveVrouterUveTest, ComputeCpuState_1) {

    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (agent_->uve()->vrouter_uve_entry());
    vr->clear_count();
    vr->ResetCpuStatsCount();
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    EXPECT_EQ(0U, vr->compute_state_send_count());

    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    EXPECT_EQ(1U, vr->compute_state_send_count());

    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    EXPECT_EQ(1U, vr->compute_state_send_count());

    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    EXPECT_EQ(2U, vr->compute_state_send_count());
    vr->clear_count();
}

TEST_F(UveVrouterUveTest, DropStatsAddChange) {
    Agent *agent = agent_;
    AgentUveStats *u = static_cast<AgentUveStats *>(agent_->uve());
    StatsManager *sm = u->stats_manager();
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (agent_->uve()->vrouter_uve_entry());
    vr->clear_count();

    u->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (u->vrouter_stats_collector()->run_counter_ >= 1));

    const VrouterStatsAgent &uve = vr->last_sent_stats();
    const vr_drop_stats_req ds_null;
    const AgentDropStats &ds = uve.get_raw_drop_stats();
    EXPECT_TRUE(DropStatsEqual(ds, ds_null));
    vr->clear_count();

    vr_drop_stats_req ds2;
    ds2.set_vds_discard(10);
    //Update drop-stats in mock kernel
    KSyncSockTypeMap::SetDropStats(ds2);

    //Fetch drop-stats
    agent->stats_collector()->Run();
    client->WaitForIdle();

    //Verify drop stats in agent_stats_collector
    vr_drop_stats_req fetched_ds = sm->drop_stats();
    EXPECT_TRUE((fetched_ds == ds2));

    //Trigger vrouter stats collection
    u->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (u->vrouter_stats_collector()->run_counter_ >= 1));

    //Verify UVE and send count
    EXPECT_TRUE(vr->vrouter_stats_msg_count() >= 1U);
    const VrouterStatsAgent &uve2 = vr->last_sent_stats();
    const AgentDropStats &ds3 = uve2.get_raw_drop_stats();
    EXPECT_TRUE(DropStatsEqual(ds3, ds2));

    //Update drop-stats in mock kernel
    ds2.set_vds_pull(10);
    KSyncSockTypeMap::SetDropStats(ds2);

    //Fetch drop-stats
    agent->stats_collector()->Run();
    client->WaitForIdle();

    //Trigger vrouter stats collection
    u->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (u->vrouter_stats_collector()->run_counter_ >= 1));

    //Verify UVE and send count
    EXPECT_TRUE(vr->vrouter_stats_msg_count() >= 2U);
    const VrouterStatsAgent &uve3 = vr->last_sent_stats();
    const AgentDropStats &ds4 = uve3.get_raw_drop_stats();
    EXPECT_TRUE(DropStatsEqual(ds4, ds2));

    //cleanup
    vr->clear_count();
    //Reset drop stats in mock kernel
    KSyncSockTypeMap::SetDropStats(ds_null);
    //Fetch drop-stats in agent_stats_collector by fetching null stats from
    //mock kernel
    agent->stats_collector()->Run();
    client->WaitForIdle();
}

TEST_F(UveVrouterUveTest, BandwidthTest_1) {
    Agent *agent = agent_;
    AgentUveStats *u = static_cast<AgentUveStats *>(agent_->uve());
    StatsManager *sm = u->stats_manager();
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (agent_->uve()->vrouter_uve_entry());
    vr->clear_count();
    //Reset bandwidth counter which controls when bandwidth is updated
    VrouterStatsAgent &uve = vr->prev_stats();
    vr->set_bandwidth_count(0);
    vector<AgentIfBandwidth> empty_list;
    map<string,uint64_t> empty_map;
    uve.set_phy_band_in_bps(empty_map);
    uve.set_phy_band_out_bps(empty_map);

    PhysicalInterfaceKey key(agent_->params()->eth_port());
    Interface *intf = static_cast<Interface *>
        (agent_->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE((intf != NULL));

    //Fetch interface stats
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (agent->stats_collector());
    collector->interface_stats_responses_ = 0;
    agent->stats_collector()->Run();
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));

    //Fetch the stats object from agent_stats_collector
    StatsManager::InterfaceStats* stats = sm->GetInterfaceStats(intf);
    EXPECT_TRUE((stats != NULL));

    //Run Vrouter stats collector to update bandwidth
    u->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));

    EXPECT_EQ(0U, uve.get_phy_band_in_bps().size());
    EXPECT_EQ(0U, uve.get_phy_band_out_bps().size());
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_band_in_bps(),
            uve.get_phy_band_out_bps(),
            0, 0));

    //Update the stats object
    stats->speed = 1;
    stats->in_bytes = 1 * 1024 * 1024;
    stats->out_bytes = (60 * 1024 * 1024)/8; //60 Mbps = 60 MBps/8
    stats->prev_in_bytes = 0;
    stats->prev_out_bytes = 0;

    //Run Vrouter stats collector to update bandwidth
    u->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (u->vrouter_stats_collector()->run_counter_ >= 1));

    if (vr->bandwidth_count() == 1) {
        u->vrouter_stats_collector()->run_counter_ = 0;
        util_.EnqueueVRouterStatsCollectorTask(1);
        client->WaitForIdle();
        WAIT_FOR(10000, 500, (u->vrouter_stats_collector()->run_counter_ >= 1));
    }

    EXPECT_EQ(2, vr->bandwidth_count());
    EXPECT_EQ(1U, uve.get_phy_band_in_bps().size());
    EXPECT_EQ(1U, uve.get_phy_band_out_bps().size());
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_band_in_bps(),
            uve.get_phy_band_out_bps(),
            139810, 1048576));
    vr->clear_count();

    //cleanup
    stats->speed = stats->in_bytes = stats->out_bytes = stats->prev_in_bytes
        = stats->prev_out_bytes = 0;
}

TEST_F(UveVrouterUveTest, BandwidthTest_2) {
    Agent *agent = agent_;
    AgentUveStats *u = static_cast<AgentUveStats *>(agent_->uve());
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (agent_->uve()->vrouter_uve_entry());
    vr->clear_count();
    VrouterStatsAgent &uve = vr->prev_stats();

    PhysicalInterfaceKey key(agent_->params()->eth_port());
    Interface *intf = static_cast<Interface *>
        (agent_->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE((intf != NULL));

    //Fetch interface stats
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (agent->stats_collector());
    collector->interface_stats_responses_ = 0;
    collector->Run();
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));

    //Fetch the stats object from agent_stats_collector
    StatsManager::InterfaceStats* stats =
        u->stats_manager()->GetInterfaceStats(intf);
    EXPECT_TRUE((stats != NULL));

    //Reset bandwidth counter which controls when bandwidth is updated
    vr->set_bandwidth_count(0);
    vector<AgentIfBandwidth> empty_list;
    uve.set_phy_if_5min_usage(empty_list);

    u->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (u->vrouter_stats_collector()->run_counter_ >= 1));

    //Update the stats object
    stats->speed = 1;
    stats->in_bytes = 1 * 1024 * 1024;
    stats->out_bytes = (5 * 60 * 1024 * 1024)/8; //60 Mbps = 60 MBps/8
    stats->prev_in_bytes = 0;
    stats->prev_out_bytes = 0;
    stats->prev_5min_in_bytes = 0;
    stats->prev_5min_out_bytes = 0;

    //Run Vrouter stats collector to update bandwidth
    u->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(8);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (u->vrouter_stats_collector()->run_counter_ >= 8));

    if (vr->bandwidth_count() == 8) {
        u->vrouter_stats_collector()->run_counter_ = 0;
        util_.EnqueueVRouterStatsCollectorTask(1);
        client->WaitForIdle();
        WAIT_FOR(10000, 500, (u->vrouter_stats_collector()->run_counter_ >= 1));
    }

    EXPECT_EQ(9, vr->bandwidth_count());
    //Verify the 5-min bandwidth usage
    EXPECT_EQ(0U, uve.get_phy_if_5min_usage().size());
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_5min_usage(), 0, 0));

    //Run Vrouter stats collector again
    vr->clear_count();
    EXPECT_TRUE(stats->in_bytes != stats->prev_5min_in_bytes);
    u->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (u->vrouter_stats_collector()->run_counter_ >= 1));

    //Verify the 5-min bandwidth usage
    EXPECT_EQ(1U, uve.get_phy_if_5min_usage().size());
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_5min_usage(), 27962, 1048576));

    //Run Vrouter stats collector
    u->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(9);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (u->vrouter_stats_collector()->run_counter_ >= 9));

    //Verify the 5-min bandwidth usage has not changed
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_5min_usage(), 27962, 1048576));

    //Run Vrouter stats collector again
    u->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (u->vrouter_stats_collector()->run_counter_ >= 1));

    //Verify the 5-min bandwidth usage
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_5min_usage(), 0, 0));

    //cleanup
    vr->clear_count();
    stats->speed = stats->in_bytes = stats->out_bytes = stats->prev_in_bytes
        = stats->prev_out_bytes = stats->prev_5min_in_bytes =
        stats->prev_5min_out_bytes = 0;
}

TEST_F(UveVrouterUveTest, ExceptionPktsChange) {
    AgentUveStats *u = static_cast<AgentUveStats *>(agent_->uve());
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (agent_->uve()->vrouter_uve_entry());
    vr->clear_count();
    agent_->stats()->Reset();

    u->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (u->vrouter_stats_collector()->run_counter_ >= 1));

    const VrouterStatsAgent &uve = vr->prev_stats();
    //Verify exception stats in UVE
    EXPECT_EQ(0U, uve.get_exception_packets());
    EXPECT_EQ(0U, uve.get_exception_packets_dropped());
    EXPECT_EQ(0U, uve.get_exception_packets_allowed());

    //Update exception stats
    agent_->stats()->incr_pkt_exceptions();
    agent_->stats()->incr_pkt_exceptions();
    agent_->stats()->incr_pkt_exceptions();

    agent_->stats()->incr_pkt_dropped();

    //Run vrouter_stats_collector to update the UVE with updated exception stats
    u->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (u->vrouter_stats_collector()->run_counter_ >= 1));

    //Verify exception stats in UVE
    EXPECT_EQ(3U, uve.get_exception_packets());
    EXPECT_EQ(1U, uve.get_exception_packets_dropped());
    EXPECT_EQ(2U, uve.get_exception_packets_allowed());
    vr->clear_count();
    agent_->stats()->Reset();
}

//Flow creation using IP and TCP packets
TEST_F(UveVrouterUveTest, Bitmap_1) {
    AgentUveStats *u = static_cast<AgentUveStats *>(agent_->uve());
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (agent_->uve()->vrouter_uve_entry());
    vr->clear_count();
    vr->ResetPortBitmap();

    FlowSetUp();
    TestFlow flow[] = {
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_TCP, 100, 200,
                       "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        //Add a UDP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_UDP, 100, 200,
                       "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    EXPECT_EQ(0U, flow_proto_->FlowCount());
    CreateFlow(flow, 2);
    EXPECT_EQ(4U, flow_proto_->FlowCount());

    u->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (u->vrouter_stats_collector()->run_counter_ >= 1));

    const VrouterStatsAgent stats = vr->last_sent_stats();
    const std::vector<uint32_t> &tcp_smap = stats.get_tcp_sport_bitmap();
    const std::vector<uint32_t> &tcp_dmap = stats.get_tcp_dport_bitmap();
    const std::vector<uint32_t> &udp_smap = stats.get_udp_sport_bitmap();
    const std::vector<uint32_t> &udp_dmap = stats.get_udp_dport_bitmap();

    EXPECT_TRUE(ValidateBmap(tcp_smap, tcp_dmap, 100, 200));
    EXPECT_TRUE(ValidateBmap(udp_smap, udp_dmap, 100, 200));

    TestFlow flow2[] = {
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_TCP, 1000, 2000,
                       "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        //Add a UDP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_UDP, 1000, 2000,
                       "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    u->vrouter_stats_collector()->run_counter_ = 0;
    vr->clear_count();
    CreateFlow(flow2, 2);
    EXPECT_EQ(8U, flow_proto_->FlowCount());

    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (u->vrouter_stats_collector()->run_counter_ >= 1));
    WAIT_FOR(1000, 500, (vr->vrouter_stats_msg_count() == 1));

    const VrouterStatsAgent stats2 = vr->last_sent_stats();
    const std::vector<uint32_t> &tcp_smap2 = stats2.get_tcp_sport_bitmap();
    const std::vector<uint32_t> &tcp_dmap2 = stats2.get_tcp_dport_bitmap();
    const std::vector<uint32_t> &udp_smap2 = stats2.get_udp_sport_bitmap();
    const std::vector<uint32_t> &udp_dmap2 = stats2.get_udp_dport_bitmap();

    EXPECT_TRUE(ValidateBmap(tcp_smap2, tcp_dmap2, 100, 200));
    EXPECT_TRUE(ValidateBmap(udp_smap2, udp_dmap2, 100, 200));
    FlowTearDown();
    vr->clear_count();
    vr->ResetPortBitmap();
}

/* Verifies agent configuration sent via vrouter UVE. */
TEST_F(UveVrouterUveTest, config_1) {
    AgentUveStats *u = static_cast<AgentUveStats *>(agent_->uve());
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (agent_->uve()->vrouter_uve_entry());
    vr->clear_count();
    u->vrouter_stats_collector()->run_counter_ = 0;
    if (!vr->first_uve_dispatched()) {
        util_.EnqueueVRouterStatsCollectorTask(1);
        client->WaitForIdle();
        WAIT_FOR(1000, 500, (u->
                 vrouter_stats_collector()->run_counter_ == 1));
        WAIT_FOR(1000, 500, (vr->vrouter_msg_count() == 1));
        EXPECT_TRUE(vr->first_uve_dispatched());
        const VrouterAgent &uve = vr->first_vrouter_uve();

        EXPECT_STREQ(uve.get_eth_name().c_str(), "vnet0");
        EXPECT_STREQ(uve.get_vhost_cfg().get_name().c_str(), "vhost0");
        EXPECT_STREQ(uve.get_vhost_cfg().get_ip().c_str(), "10.1.1.1");
        EXPECT_STREQ(uve.get_vhost_cfg().get_gateway().c_str(), "10.1.1.254");
        EXPECT_EQ(uve.get_vhost_cfg().get_ip_prefix_len(), 24);

        EXPECT_STREQ(uve.get_tunnel_type().c_str(), "MPLSoGRE");
        EXPECT_EQ(uve.get_flow_cache_timeout_cfg(), 0);

        EXPECT_STREQ(uve.get_hypervisor().c_str(), "kvm");
        EXPECT_STREQ(uve.get_ds_addr().c_str(), "0.0.0.0");
        EXPECT_EQ(uve.get_ds_xs_instances(), 0);
        EXPECT_STREQ(uve.get_control_ip().c_str(), "0.0.0.0");
        EXPECT_EQ(uve.get_ll_max_system_flows_cfg(), 3U);
        EXPECT_EQ(uve.get_ll_max_vm_flows_cfg(), 2U);
        EXPECT_EQ(uve.get_max_vm_flows_cfg(), 100U);
        EXPECT_EQ(uve.get_dns_server_list_cfg().size(), 2U);
        EXPECT_EQ(uve.get_control_node_list_cfg().size(), 2U);
        EXPECT_EQ(uve.get_gateway_cfg_list().size(), 2U);
        vr->clear_count();
    }
}

TEST_F(UveVrouterUveTest, TSN_intf_list1) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (agent_->uve()->vrouter_uve_entry());
    vr->clear_count();
    AgentUveStats *u = static_cast<AgentUveStats *>(agent_->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    PhysicalDeviceTable *table = agent_->physical_device_table();
    pr->ClearCount();

    agent_->set_tsn_enabled(true);
    const VrouterAgent &uve = vr->last_sent_vrouter();
    EXPECT_EQ(0U, vr->vrouter_msg_count());
    EXPECT_EQ(0U, uve.get_interface_list().size());
    EXPECT_EQ(0U, uve.get_unmanaged_if_list().size());

    struct PortInfo input[] = {
        {"vmi1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    IntfCfgAdd(input, 0);
    AddPhysicalDevice("prouter1", 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLogicalInterface("li1", 1, "lid1");
    AddPort("vmi1", 1);
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("physical-interface", "pi1", "logical-interface", "li1");
    AddLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));
    WAIT_FOR(1000, 500, (VmInterfaceGet(1) != NULL));

    //Update tsn_managed flag for PhysicalDevice as 'true'
    PhysicalDevice *pd = PhysicalDeviceGet(1);
    table->EnqueueDeviceChange(pd->uuid(), true);
    client->WaitForIdle();

    EnqueueSendVrouterUveTask();
    client->WaitForIdle();
    vr->WaitForWalkCompletion();

    //Verify the UVEs
    const VrouterAgent &uve2 = vr->prev_vrouter();
    EXPECT_EQ(1U, uve2.get_interface_list().size());
    EXPECT_EQ(0U, uve2.get_unmanaged_if_list().size());

    //Update tsn_managed flag for PhysicalDevice as 'false'
    table->EnqueueDeviceChange(pd->uuid(), false);
    client->WaitForIdle();

    EnqueueSendVrouterUveTask();
    client->WaitForIdle();
    vr->WaitForWalkCompletion();

    //Verify the UVEs
    EXPECT_EQ(0U, uve2.get_interface_list().size());
    EXPECT_EQ(1U, uve2.get_unmanaged_if_list().size());

    //cleanup
    DelLink("physical-interface", "pi1", "logical-interface", "li1");
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    DelLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    DeleteLogicalInterface("li1");
    DeletePhysicalInterface("pi1");
    DeletePhysicalDevice("prouter1");
    DelPort("vmi1");
    IntfCfgDel(input, 0);
    client->WaitForIdle();

    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->PhysicalIntfListCount() == 0U));

    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 0U));
    WAIT_FOR(1000, 500, (1U == pr->li_delete_count()));
    agent_->set_tsn_enabled(false);
}

TEST_F(UveVrouterUveTest, FlowSetupRate) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (agent_->uve()->vrouter_uve_entry());
    vr->clear_count();
    vr->set_prev_flow_created(agent_->stats()->flow_created());
    vr->set_prev_flow_aged(agent_->stats()->flow_aged());
    agent_->stats()->set_prev_flow_created(agent_->stats()->flow_created());
    agent_->stats()->set_prev_flow_aged(agent_->stats()->flow_aged());
    agent_->stats()->set_max_flow_adds_per_second(AgentStats::kInvalidFlowCount);
    agent_->stats()->set_min_flow_adds_per_second(AgentStats::kInvalidFlowCount);
    agent_->stats()->set_max_flow_deletes_per_second(AgentStats::kInvalidFlowCount);
    agent_->stats()->set_min_flow_deletes_per_second(AgentStats::kInvalidFlowCount);

    FlowSetUp();
    TestFlow flow[] = {
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_TCP, 100, 200,
                       "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        //Add a UDP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_UDP, 100, 200,
                       "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    //Create Flows
    EXPECT_EQ(0U, flow_proto_->FlowCount());
    CreateFlow(flow, 2);
    EXPECT_EQ(4U, flow_proto_->FlowCount());

    //Update prev_time to current_time - 1 sec
    uint64_t t = UTCTimestampUsec() - 1000000;
    vr->set_prev_flow_setup_rate_export_time(t);
    agent_->stats()->UpdateFlowMinMaxStats(agent_->stats()->flow_created(),
                                           agent_->stats()->added());

    //Trigger framing and send of UVE message
    vr->SendVrouterMsg();

    //Verify flow add rate
    const VrouterStatsAgent stats = vr->last_sent_stats();
    EXPECT_EQ(4U, stats.get_flow_rate().get_added_flows());
    EXPECT_EQ(4U, stats.get_flow_rate().get_max_flow_adds_per_second());
    EXPECT_EQ(4U, stats.get_flow_rate().get_min_flow_adds_per_second());
    EXPECT_EQ(0U, stats.get_flow_rate().get_deleted_flows());

    //Create two more flows
    TestFlow flow2[] = {
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_TCP, 1000, 2000,
                       "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
    };

    CreateFlow(flow2, 1);
    EXPECT_EQ(6U, flow_proto_->FlowCount());

    //Update prev_time to current_time - 1 sec
    t = UTCTimestampUsec() - 1000000;
    vr->set_prev_flow_setup_rate_export_time(t);
    agent_->stats()->UpdateFlowMinMaxStats(agent_->stats()->flow_created(),
                                           agent_->stats()->added());

    //Trigger framing and send of UVE message
    vr->SendVrouterMsg();

    //Verify flow add rate
    const VrouterStatsAgent stats2 = vr->last_sent_stats();
    EXPECT_EQ(2U, stats2.get_flow_rate().get_added_flows());
    EXPECT_EQ(2U, stats2.get_flow_rate().get_max_flow_adds_per_second());
    EXPECT_EQ(2U, stats2.get_flow_rate().get_min_flow_adds_per_second());
    EXPECT_EQ(0U, stats2.get_flow_rate().get_deleted_flows());

    //Delete flows and verify delete rate
    DeleteFlow(flow2, 1);
    WAIT_FOR(1000, 1000, ((flow_proto_->FlowCount() == 4U)));

    //Update prev_time to current_time - 1 sec
    t = UTCTimestampUsec() - 1000000;
    vr->set_prev_flow_setup_rate_export_time(t);
    agent_->stats()->UpdateFlowMinMaxStats(agent_->stats()->flow_aged(),
                                           agent_->stats()->deleted());

    //Trigger framing and send of UVE message
    vr->SendVrouterMsg();

    //Verify flow add and delete rate
    const VrouterStatsAgent stats3 = vr->last_sent_stats();
    EXPECT_EQ(0U, stats3.get_flow_rate().get_added_flows());
    EXPECT_EQ(2U, stats3.get_flow_rate().get_deleted_flows());
    EXPECT_EQ(2U, stats3.get_flow_rate().get_max_flow_deletes_per_second());
    EXPECT_EQ(2U, stats3.get_flow_rate().get_min_flow_deletes_per_second());

    FlowTearDown();
    vr->clear_count();
}

TEST_F(UveVrouterUveTest, VrLimits) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (agent_->uve()->vrouter_uve_entry());
    // Send once
    vr->SendVrouterMsg();

    // Save it
    VrouterObjectLimits prev_vr_limits = vr->prev_vrouter().get_vr_limits();

    // change max labels
    Agent::GetInstance()->set_vrouter_max_labels(100);

    // Send again
    vr->SendVrouterMsg();

    // Ensure the value is updated in UVE entry
    VrouterObjectLimits new_vr_limits = vr->prev_vrouter().get_vr_limits();
    EXPECT_EQ(new_vr_limits.get_max_labels(), 100);
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit("controller/src/vnsw/agent/uve/test/vnswa_cfg.ini",
                      ksync_init,
                      true, false, true, (10 * 60 * 1000), (10 * 60 * 1000),
                      true, true, (10 * 60 * 1000));

    usleep(10002);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
