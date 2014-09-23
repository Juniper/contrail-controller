/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __test_uve_util__
#define __test_uve_util__

class AgentStatsCollectorTask : public Task {
public:
    AgentStatsCollectorTask(int count) :
        Task((TaskScheduler::GetInstance()->GetTaskId
            ("Agent::StatsCollector")), StatsCollector::AgentStatsCollector),
        count_(count) {
    }
    virtual bool Run() {
        for (int i = 0; i < count_; i++)
            Agent::GetInstance()->uve()->agent_stats_collector()->Run();
        return true;
    }
private:
    int count_;
};

class FlowStatsCollectorTask : public Task {
public:
    FlowStatsCollectorTask() :
        Task((TaskScheduler::GetInstance()->GetTaskId("Agent::StatsCollector")),
                StatsCollector::FlowStatsCollector) {
    }
    virtual bool Run() {
        Agent::GetInstance()->uve()->flow_stats_collector()->Run();
    }
};

class VRouterStatsCollectorTask : public Task {
public:
    VRouterStatsCollectorTask(int count) :
        Task((TaskScheduler::GetInstance()->GetTaskId("Agent::Uve")), 0),
        count_(count) {
    }
    virtual bool Run() {
        for (int i = 0; i < count_; i++)
            Agent::GetInstance()->uve()->vrouter_stats_collector()->Run();
        return true;
    }
private:
    int count_;
};

class TestUveUtil {
public:
    void EnqueueAgentStatsCollectorTask(int count) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        AgentStatsCollectorTask *task = new AgentStatsCollectorTask(count);
        scheduler->Enqueue(task);
    }

    void EnqueueFlowStatsCollectorTask() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        FlowStatsCollectorTask *task = new FlowStatsCollectorTask();
        scheduler->Enqueue(task);
    }

    void EnqueueVRouterStatsCollectorTask(int count) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        VRouterStatsCollectorTask *task = new VRouterStatsCollectorTask(count);
        scheduler->Enqueue(task);
    }
    void VnAdd(int id) {
        char vn_name[80];

        sprintf(vn_name, "vn%d", id);
        uint32_t vn_count = Agent::GetInstance()->vn_table()->Size();
        client->Reset();
        AddVn(vn_name, id);
        WAIT_FOR(1000, 5000, (VnFind(id) == true));
        EXPECT_EQ((vn_count + 1), Agent::GetInstance()->vn_table()->Size());
    }

    void L2VnAdd(int id) {
        char vn_name[80];

        sprintf(vn_name, "vn%d", id);
        uint32_t vn_count = Agent::GetInstance()->vn_table()->Size();
        client->Reset();
        AddL2Vn(vn_name, id);
        WAIT_FOR(1000, 5000, (VnFind(id) == true));
        EXPECT_EQ((vn_count + 1), Agent::GetInstance()->vn_table()->Size());
    }

    void VnDelete(int id) {
        char vn_name[80];

        sprintf(vn_name, "vn%d", id);
        uint32_t vn_count = Agent::GetInstance()->vn_table()->Size();
        client->WaitForIdle(10);
        client->Reset();
        DelNode("virtual-network", vn_name);
        client->WaitForIdle(10);
        WAIT_FOR(1000, 5000, (VnFind(id) == false));
        EXPECT_EQ((vn_count - 1), Agent::GetInstance()->vn_table()->Size());
    }

    void VnAddByName(const char *vn_name, int id) {
        uint32_t vn_count = Agent::GetInstance()->vn_table()->Size();
        client->Reset();
        AddVn(vn_name, id);
        client->WaitForIdle(10);
        WAIT_FOR(1000, 5000, (VnFind(id) == true));
        EXPECT_EQ((vn_count + 1), Agent::GetInstance()->vn_table()->Size());
    }

    void VnDeleteByName(const char *vn_name, int id) {
        uint32_t vn_count = Agent::GetInstance()->vn_table()->Size();
        client->Reset();
        DelNode("virtual-network", vn_name);
        client->WaitForIdle(10);
        WAIT_FOR(1000, 5000, (VnFind(id) == false));
        EXPECT_EQ((vn_count - 1), Agent::GetInstance()->vn_table()->Size());
    }

    void VmAdd(int id) {
        char vm_name[80];

        sprintf(vm_name, "vm%d", id);
        uint32_t vm_count = Agent::GetInstance()->vm_table()->Size();
        client->Reset();
        AddVm(vm_name, id);
        WAIT_FOR(1000, 5000, (VmFind(id) == true));
        EXPECT_EQ((vm_count + 1), Agent::GetInstance()->vm_table()->Size());
    }

    void VmDelete(int id) {
        char vm_name[80];

        sprintf(vm_name, "vm%d", id);
        uint32_t vm_count = Agent::GetInstance()->vm_table()->Size();
        client->Reset();
        DelNode("virtual-machine", vm_name);
        client->WaitForIdle();
        WAIT_FOR(1000, 5000, (VmFind(id) == false));
        EXPECT_EQ((vm_count - 1), Agent::GetInstance()->vm_table()->Size());
    }

    void VrfAdd(int id) {
        char vrf_name[80];

        sprintf(vrf_name, "vrf%d", id);
        client->Reset();
        EXPECT_FALSE(VrfFind(vrf_name));
        AddVrf(vrf_name);
        client->WaitForIdle(3);
        WAIT_FOR(1000, 5000, (VrfFind(vrf_name) == true));
    }

    void NovaPortAdd(struct PortInfo *input) {
        client->Reset();
        IntfCfgAdd(input, 0);
        EXPECT_TRUE(client->PortNotifyWait(1));
    }

    void ConfigPortAdd(struct PortInfo *input) {
        client->Reset();
        AddPort(input[0].name, input[0].intf_id);
        client->WaitForIdle();
    }

    void CreateRemoteRoute(const char *vrf, const char *remote_vm,
                           const char *serv, int label, const char *vn,
                           BgpPeer *peer) {
        boost::system::error_code ec;
        Ip4Address addr = Ip4Address::from_string(remote_vm, ec);
        Ip4Address gw = Ip4Address::from_string(serv, ec);
        Inet4TunnelRouteAdd(peer, vrf, addr, 32, gw, TunnelType::AllType(), label, vn,
                            SecurityGroupList(), PathPreference());
        client->WaitForIdle(2);
        WAIT_FOR(1000, 500, (RouteFind(vrf, addr, 32) == true));
    }

    void DeleteRemoteRoute(const char *vrf, const char *ip, BgpPeer *peer) {
        boost::system::error_code ec;
        Ip4Address addr = Ip4Address::from_string(ip, ec);
        Agent::Agent::GetInstance()->
            fabric_inet4_unicast_table()->DeleteReq(peer,
                vrf, addr, 32, NULL);
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFind(vrf, addr, 32) == false));
    }

};

#endif
