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
// Default scheduler policy for control-node daemon and test processes.
//
void ControlNode::SetDefaultSchedulingPolicy() {
    static bool policy_set;

    if (policy_set)
        return;
    policy_set = true;

    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    // Policy for bgp::Config Task.
    TaskPolicy config_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("bgp::Config")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ConfigHelper")))
        (TaskExclusion(scheduler->GetTaskId("bgp::RTFilter")))
        (TaskExclusion(scheduler->GetTaskId("bgp::SendTask")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ServiceChain")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StateMachine")))
        (TaskExclusion(scheduler->GetTaskId("bgp::PeerMembership")))
        (TaskExclusion(scheduler->GetTaskId("db::DBTable")))
        (TaskExclusion(scheduler->GetTaskId("db::IFMapTable")))
        (TaskExclusion(scheduler->GetTaskId("db::Walker")))
        (TaskExclusion(scheduler->GetTaskId("io::ReaderTask")))
        (TaskExclusion(scheduler->GetTaskId("ifmap::StateMachine")))
        (TaskExclusion(scheduler->GetTaskId("xmpp::StateMachine")))
        (TaskExclusion(scheduler->GetTaskId("timer::TimerTask")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ShowCommand")))
        (TaskExclusion(scheduler->GetTaskId("bgp::SendReadyTask")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StaticRoute")))
        (TaskExclusion(scheduler->GetTaskId("bgp::Uve")))
        (TaskExclusion(scheduler->GetTaskId("bgp::RouteAggregation")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ResolverPath")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ResolverNexthop")));
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::Config"), config_policy);

    // Policy for bgp::ConfigHelper Task.
    // It's the same as that for bgp:Config Task except that bgp:ConfigHelper
    // is not exclusive with db::IFMapTable.
    TaskPolicy config_helper_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("bgp::Config")))
        (TaskExclusion(scheduler->GetTaskId("bgp::RTFilter")))
        (TaskExclusion(scheduler->GetTaskId("bgp::SendTask")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ServiceChain")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StateMachine")))
        (TaskExclusion(scheduler->GetTaskId("bgp::PeerMembership")))
        (TaskExclusion(scheduler->GetTaskId("db::DBTable")))
        (TaskExclusion(scheduler->GetTaskId("db::Walker")))
        (TaskExclusion(scheduler->GetTaskId("io::ReaderTask")))
        (TaskExclusion(scheduler->GetTaskId("ifmap::StateMachine")))
        (TaskExclusion(scheduler->GetTaskId("xmpp::StateMachine")))
        (TaskExclusion(scheduler->GetTaskId("timer::TimerTask")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ShowCommand")))
        (TaskExclusion(scheduler->GetTaskId("bgp::SendReadyTask")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StaticRoute")))
        (TaskExclusion(scheduler->GetTaskId("bgp::Uve")))
        (TaskExclusion(scheduler->GetTaskId("bgp::RouteAggregation")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ResolverPath")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ResolverNexthop")));
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::ConfigHelper"),
        config_helper_policy);

    // Policy for bgp::ServiceChain and bgp::StaticRoute Tasks.
    TaskPolicy static_service_chain_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("bgp::Config")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ConfigHelper")))
        (TaskExclusion(scheduler->GetTaskId("bgp::PeerMembership")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ResolverPath")))
        (TaskExclusion(scheduler->GetTaskId("bgp::RouteAggregation")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ServiceChain")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StaticRoute")))
        (TaskExclusion(scheduler->GetTaskId("db::Walker")))
        (TaskExclusion(scheduler->GetTaskId("db::DBTable")));
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::ServiceChain"),
        static_service_chain_policy);
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::StaticRoute"),
        static_service_chain_policy);


    // Policy for bgp::StateMachine and xmpp::StateMachine Tasks.
    // Add policy to provision exclusion between io::Reader and
    // bgp/xmpp StateMachine tasks with the same task instance.
    TaskPolicy sm_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("bgp::Config")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ConfigHelper")))
        (TaskExclusion(scheduler->GetTaskId("bgp::PeerMembership")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ShowCommand")))
        (TaskExclusion(scheduler->GetTaskId("bgp::RTFilter")));
    for (int idx = 0; idx < scheduler->HardwareThreadCount(); ++idx) {
        sm_policy.push_back(
            (TaskExclusion(scheduler->GetTaskId("io::ReaderTask"), idx)));
    }
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::StateMachine"),
        sm_policy);
    scheduler->SetPolicy(scheduler->GetTaskId("xmpp::StateMachine"),
        sm_policy);

    // Policy for bgp::PeerMembership Task.
    TaskPolicy peer_membership_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("bgp::Config")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ConfigHelper")))
        (TaskExclusion(scheduler->GetTaskId("db::DBTable")))
        (TaskExclusion(scheduler->GetTaskId("db::Walker")))
        (TaskExclusion(scheduler->GetTaskId("bgp::SendTask")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ServiceChain")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ShowCommand")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StateMachine")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StaticRoute")))
        (TaskExclusion(scheduler->GetTaskId("xmpp::StateMachine")));
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::PeerMembership"),
        peer_membership_policy);

    // Policy for bgp::SendReadyTask Task.
    TaskPolicy send_ready_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("bgp::Config")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ConfigHelper")))
        (TaskExclusion(scheduler->GetTaskId("bgp::SendTask")))
        (TaskExclusion(scheduler->GetTaskId("bgp::PeerMembership")));
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::SendReadyTask"),
        send_ready_policy);

    // Policy for bgp::RTFilter Task.
    TaskPolicy rtfilter_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("db::DBTable")))
        (TaskExclusion(scheduler->GetTaskId("db::Walker")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StateMachine")))
        (TaskExclusion(scheduler->GetTaskId("bgp::Config")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ConfigHelper")));
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::RTFilter"),
        rtfilter_policy);

    // Policy for bgp::ResolverPath Task.
    TaskPolicy resolver_path_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("db::DBTable")))
        (TaskExclusion(scheduler->GetTaskId("db::Walker")))
        (TaskExclusion(scheduler->GetTaskId("bgp::Config")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ConfigHelper")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ResolverNexthop")))
        (TaskExclusion(scheduler->GetTaskId("bgp::RouteAggregation")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ServiceChain")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StaticRoute")));
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::ResolverPath"),
        resolver_path_policy);

    // Policy for bgp::ResolverNexthop Task.
    TaskPolicy resolver_nexthop_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("db::DBTable")))
        (TaskExclusion(scheduler->GetTaskId("bgp::Config")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ConfigHelper")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ResolverPath")))
        (TaskExclusion(scheduler->GetTaskId("bgp::RouteAggregation")));
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::ResolverNexthop"),
        resolver_nexthop_policy);

    // Policy for bgp::RouteAggregation Task.
    TaskPolicy route_aggregation_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("db::DBTable")))
        (TaskExclusion(scheduler->GetTaskId("db::Walker")))
        (TaskExclusion(scheduler->GetTaskId("bgp::Config")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ConfigHelper")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ResolverNexthop")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ResolverPath")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ServiceChain")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StaticRoute")));
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::RouteAggregation"),
        route_aggregation_policy);

    // Policy for db::IFMapTable Task.
    TaskPolicy db_ifmap_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("bgp::Config")))
        (TaskExclusion(scheduler->GetTaskId("db::Walker")));
    scheduler->SetPolicy(scheduler->GetTaskId("db::IFMapTable"),
        db_ifmap_policy);

    // Policy for db::Walker Task.
    // Rules:
    // 1. All tasks that trigger WalkTable should be mutually exclusive to
    // db::Walker task
    // 2. All tasks that updates the db table partition should be mutually
    // mutually exclusive
    TaskPolicy walker_policy = boost::assign::list_of
        // Following tasks trigger WalkTable
        (TaskExclusion(scheduler->GetTaskId("bgp::Config")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ConfigHelper")))
        (TaskExclusion(scheduler->GetTaskId("bgp::PeerMembership")))
        (TaskExclusion(scheduler->GetTaskId("bgp::RTFilter")))
        // Following tasks updates db table partition
        (TaskExclusion(scheduler->GetTaskId("db::DBTable")))
        (TaskExclusion(scheduler->GetTaskId("db::IFMapTable")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ResolverPath")))
        (TaskExclusion(scheduler->GetTaskId("bgp::RouteAggregation")))
        (TaskExclusion(scheduler->GetTaskId("bgp::ServiceChain")))
        (TaskExclusion(scheduler->GetTaskId("bgp::StaticRoute")));
    scheduler->SetPolicy(scheduler->GetTaskId("db::Walker"), walker_policy);
}
