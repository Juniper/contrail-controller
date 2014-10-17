/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "control_node.h"
#include <boost/assign.hpp>
#include "base/task.h"

std::string ControlNode::hostname_;
std::string ControlNode::prog_name_;
std::string ControlNode::self_ip_;
DiscoveryServiceClient* ControlNode::ds_client_;
bool ControlNode::test_mode_;

//
// Default scheduler policy for control-node daemon and test processes
//
void ControlNode::SetDefaultSchedulingPolicy() {
    static bool policy_set_;

    if (policy_set_) return;

    policy_set_ = true;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    TaskPolicy exclude_all;
    const char *task_ids[] = {
        "bgp::Config",
        "bgp::RTFilter",
        "bgp::SendTask",
        "bgp::ServiceChain",
        "bgp::StateMachine",
        "bgp::PeerMembership",
        "db::DBTable",
        "io::ReaderTask",
        "ifmap::StateMachine",
        "xmpp::StateMachine",
        "timer::TimerTask",
        "bgp::ShowCommand",
        "bgp::SendReadyTask",
        "bgp::StaticRoute",
    };
    int arraysize = sizeof(task_ids) / sizeof(char *);
    for (int i = 0; i < arraysize; ++i) {
        int task_id = scheduler->GetTaskId(task_ids[i]);
        exclude_all.push_back(TaskExclusion(task_id));
    }
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::Config"), exclude_all);

    // Both ServiceChain and StaticRoute task have same mutual exclusion 
    // task policy
    TaskPolicy mutual_exc_service_chain;
    const char *svc_chain_exclusion_task_ids[] = {
        "bgp::Config",
        "bgp::PeerMembership",
        "bgp::ServiceChain",
        "bgp::StaticRoute",
        "db::DBTable",
    };
    arraysize = sizeof(svc_chain_exclusion_task_ids) / sizeof(char *);
    for (int i = 0; i < arraysize; ++i) {
        int task_id = scheduler->GetTaskId(svc_chain_exclusion_task_ids[i]);
        mutual_exc_service_chain.push_back(TaskExclusion(task_id));
    }
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::ServiceChain"),
                            mutual_exc_service_chain);
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::StaticRoute"),
                            mutual_exc_service_chain);


    // TODO: There should be exclusion between Reader and StateMachine
    // tasks with the same index only (vs all indices).
    TaskPolicy sm_task_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("io::ReaderTask")));
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::StateMachine"),
                            sm_task_policy);
    scheduler->SetPolicy(scheduler->GetTaskId("xmpp::StateMachine"),
                            sm_task_policy);

    TaskPolicy peer_membership_policy =
        boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("db::DBTable")))
        (TaskExclusion(scheduler->GetTaskId("bgp::SendTask")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ServiceChain")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ShowCommand")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StateMachine")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StaticRoute")))
        (TaskExclusion(scheduler->GetTaskId("xmpp::StateMachine")));
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::PeerMembership"),
                            peer_membership_policy);

    TaskPolicy exclude_send_ready = 
        boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("bgp::SendTask")))
        (TaskExclusion(scheduler->GetTaskId("bgp::PeerMembership")));
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::SendReadyTask"), exclude_send_ready);

    TaskPolicy rtfilter_task_policy =
        boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("db::DBTable")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StateMachine")))
        (TaskExclusion(scheduler->GetTaskId("bgp::RTFilter")))
        (TaskExclusion(scheduler->GetTaskId("bgp::Config")));
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::RTFilter"),
                            rtfilter_task_policy);
}
