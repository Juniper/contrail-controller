/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __test_uve_util__
#define __test_uve_util__

#include "uve/test/vn_uve_table_test.h"
#include "uve/agent_uve_stats.h"
#include <vrouter/flow_stats/test/flow_stats_collector_test.h>

class FlowActionLogTask : public Task {
public:
    FlowActionLogTask(FlowEntry *fe) :
        Task((TaskScheduler::GetInstance()->GetTaskId("Agent::StatsCollector")),
              StatsCollector::FlowStatsCollector), fe_(fe) {
    }
    virtual bool Run() {
        if (!fe_) {
            return true;
        }
        FlowStatsCollectorTest *f = static_cast<FlowStatsCollectorTest *>
                                    (Agent::GetInstance()->flow_stats_manager()
                                     ->default_flow_stats_collector());
        FlowExportInfo *info = f->FindFlowExportInfo(fe_);
        if (info) {
            info->SetActionLog();
        }
        return true;
    }
    std::string Description() const { return "FlowActionLogTask"; }
private:
    FlowEntry *fe_;
};

class ProuterUveSendTask : public Task {
public:
    ProuterUveSendTask() :
        Task((TaskScheduler::GetInstance()->GetTaskId(kTaskDBExclude)), 0) {
    }
    virtual bool Run() {
        Agent::GetInstance()->uve()->prouter_uve_table()->TimerExpiry();
        return true;
    }
    std::string Description() const { return "ProuterUveSendTask"; }
};

class PIUveSendTask : public Task {
public:
    PIUveSendTask() :
        Task((TaskScheduler::GetInstance()->GetTaskId(kTaskDBExclude)), 0) {
    }
    virtual bool Run() {
        Agent::GetInstance()->uve()->prouter_uve_table()->PITimerExpiry();
        return true;
    }
    std::string Description() const { return "PIUveSendTask"; }
};

class LIUveSendTask : public Task {
public:
    LIUveSendTask() :
        Task((TaskScheduler::GetInstance()->GetTaskId(kTaskDBExclude)), 0) {
    }
    virtual bool Run() {
        Agent::GetInstance()->uve()->prouter_uve_table()->LITimerExpiry();
        return true;
    }
    std::string Description() const { return "LIUveSendTask"; }
};

class AgentStatsCollectorTask : public Task {
public:
    AgentStatsCollectorTask(int count) :
        Task((TaskScheduler::GetInstance()->GetTaskId
            ("Agent::StatsCollector")), StatsCollector::AgentStatsCollector),
        count_(count) {
    }
    virtual bool Run() {
        for (int i = 0; i < count_; i++) {
            Agent::GetInstance()->stats_collector()->Run();
        }
        return true;
    }
    std::string Description() const { return "AgentStatsCollectorTask"; }
private:
    int count_;
};

class FlowStatsCollectorTask : public Task {
public:
    FlowStatsCollectorTask() :
        Task((TaskScheduler::GetInstance()->GetTaskId(kTaskFlowStatsCollector)),
              0) {
    }
    virtual bool Run() {
        Agent::GetInstance()->flow_stats_manager()->
            default_flow_stats_collector()->Run();
    }
    std::string Description() const { return "FlowStatsCollectorTask"; }
};

class VRouterStatsCollectorTask : public Task {
public:
    VRouterStatsCollectorTask(int count) :
        Task((TaskScheduler::GetInstance()->GetTaskId(kTaskDBExclude)), 0),
        count_(count) {
    }
    virtual bool Run() {
        AgentUveStats *uve = static_cast<AgentUveStats *>
            (Agent::GetInstance()->uve());
        for (int i = 0; i < count_; i++)
            uve->vrouter_stats_collector()->Run();
        return true;
    }
    std::string Description() const { return "VRouterStatsCollectorTask"; }
private:
    int count_;
};

class VnUveSendTask : public Task {
public:
    VnUveSendTask() :
        Task((TaskScheduler::GetInstance()->GetTaskId(kTaskDBExclude)), 0) {
    }
    virtual bool Run() {
        Agent::GetInstance()->uve()->vn_uve_table()->TimerExpiry();
        return true;
    }
    std::string Description() const { return "VnUveSendTask"; }
};

class VmUveSendTask : public Task {
public:
    VmUveSendTask() :
        Task((TaskScheduler::GetInstance()->GetTaskId(kTaskDBExclude)), 0) {
    }
    virtual bool Run() {
        Agent::GetInstance()->uve()->vm_uve_table()->TimerExpiry();
        return true;
    }
    std::string Description() const { return "VmUveSendTask"; }
};

class VmiUveSendTask : public Task {
public:
    VmiUveSendTask() :
        Task((TaskScheduler::GetInstance()->GetTaskId(kTaskDBExclude)), 0) {
    }
    virtual bool Run() {
        Agent::GetInstance()->uve()->interface_uve_table()->TimerExpiry();
        return true;
    }
    std::string Description() const { return "VmiUveSendTask"; }
};

static bool FlowStatsTimerStartStopTrigger(FlowStatsCollector *fsc, bool stop) {
    fsc->TestStartStopTimer(stop);
    return true;
}

class TestUveUtil {
public:
    void EnqueueSendProuterUveTask() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        ProuterUveSendTask *task = new ProuterUveSendTask();
        scheduler->Enqueue(task);
    }
    void EnqueueSendPIUveTask() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        PIUveSendTask *task = new PIUveSendTask();
        scheduler->Enqueue(task);
    }
    void EnqueueSendLIUveTask() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        LIUveSendTask *task = new LIUveSendTask();
        scheduler->Enqueue(task);
    }
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

    void EnqueueSendVnUveTask() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        VnUveSendTask *task = new VnUveSendTask();
        scheduler->Enqueue(task);
    }

    void EnqueueSendVmUveTask() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        VmUveSendTask *task = new VmUveSendTask();
        scheduler->Enqueue(task);
    }

    void EnqueueSendVmiUveTask() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        VmiUveSendTask *task = new VmiUveSendTask();
        scheduler->Enqueue(task);
    }

    void EnqueueFlowActionLogChange(FlowEntry *fe) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        FlowActionLogTask *task = new FlowActionLogTask(fe);
        scheduler->Enqueue(task);
    }

    void FlowStatsTimerStartStop(bool stop) {
        Agent *agent = Agent::GetInstance();
        FlowStatsCollector* fsc = agent->flow_stats_manager()->
            default_flow_stats_collector();
        int task_id =
            agent->task_scheduler()->GetTaskId(kTaskFlowStatsCollector);
        std::auto_ptr<TaskTrigger> trigger_
            (new TaskTrigger(boost::bind(FlowStatsTimerStartStopTrigger,
                                         fsc, stop),
                             task_id, 0));
        trigger_->Set();
        client->WaitForIdle();
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

    void ConfigSriovPortAdd(struct PortInfo *input) {
        client->Reset();
        AddSriovPort(input[0].name, input[0].intf_id);
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
        DeleteRoute(vrf, ip, 32, static_cast<Peer *>(peer));
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFind(vrf, addr, 32) == false));
    }

    bool FlowBasedVnStatsMatch(const std::string &vn, uint64_t in,
                               uint64_t out) {
        VnUveTableTest *vut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
        const VnUveEntry *entry = vut->GetVnUveEntry(vn);
        if (!entry) {
            return false;
        }
        if ((entry->in_bytes() == in) && (entry->out_bytes() == out)) {
            return true;
        }
        return false;
    }

};

#endif
