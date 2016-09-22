/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "svc_static_route_intergration_test.h"

//
// Verify 2 in-network services on the same compute node.
//
// The RDs used when advertising service chain routes for the 2 service
// instances should be different. The RD is based on the compute node's
// registration id for the service routing instances, rather then the
// connected routing instance.
//
TYPED_TEST(ServiceChainIntegrationTest, MultipleInNetwork) {
    // Add VM routes in red.
    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->AddRoute("red", this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->AddRoute("red", this->BuildPrefix("192.168.1.1", 32));
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->AddInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->AddInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
    } else {
        assert(false);
    }
    task_util::WaitForIdle();

    // Add Connected routes for both service instances.
    if (ServiceChainIntegrationTestGlobals::left_to_right_) {
        this->AddConnectedRoute(false, this->BuildPrefix("1.1.2.3", 32),
            "8.8.8.8");
        this->AddConnectedRoute(false, this->BuildPrefix("1.1.2.4", 32),
            "8.8.8.8");
    } else {
        this->AddConnectedRoute(false, this->BuildPrefix("1.1.2.4", 32),
            "8.8.8.8");
        this->AddConnectedRoute(false, this->BuildPrefix("1.1.2.3", 32),
            "8.8.8.8");
    }

    // Wait for everything to be processed.
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn1_.get()));
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn2_.get()));

    // Verify service chain routes are replicated to bgp.l3vpn.0.
    // Note that agents are registered to blue-i1 with instance id 1.
    // Note that agents are registered to blue-i3 with instance id 6.
    if (ServiceChainIntegrationTestGlobals::aggregate_enable_) {
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:1:" +
            this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:6:" +
            this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:1:" +
            this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:6:" +
            this->BuildPrefix("192.168.1.0", 24));
    } else {
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:1:" +
            this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:6:" +
            this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:1:" +
            this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:6:" +
            this->BuildPrefix("192.168.1.1", 32));
    }

    // Delete Connected routes for both service instances.
    if (ServiceChainIntegrationTestGlobals::left_to_right_) {
        this->DeleteConnectedRoute(false, this->BuildPrefix("1.1.2.3", 32));
        this->DeleteConnectedRoute(false, this->BuildPrefix("1.1.2.4", 32));
    } else {
        this->DeleteConnectedRoute(false, this->BuildPrefix("1.1.2.4", 32));
        this->DeleteConnectedRoute(false, this->BuildPrefix("1.1.2.3", 32));
    }

    // Delete VM routes in red.
    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->DeleteRoute("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->DeleteRoute("red",
            this->BuildPrefix("192.168.1.1", 32));
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->DeleteInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->DeleteInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
    } else {
        assert(false);
    }
}

//
// Verify 2 in-network services scaled on 2 computes nodes each.
//
// The RDs used when advertising service chain routes for the 2 service
// instances should be different. The RD is based on the compute node's
// registration id for the service routing instances, rather then the
// connected routing instance.
//
TYPED_TEST(ServiceChainIntegrationTest, MultipleInNetworkECMP) {
    // Add VM routes in red.
    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->AddRoute("red", this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->AddRoute("red", this->BuildPrefix("192.168.1.1", 32));
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->AddInet6Route("red", this->BuildPrefix("192.168.1.1",
                                                                 32));
        this->agent_a_2_->AddInet6Route("red", this->BuildPrefix("192.168.1.1",
                                                                 32));
    } else {
        assert(false);
    }
    task_util::WaitForIdle();

    // Add Connected routes for both service instances.
    if (ServiceChainIntegrationTestGlobals::left_to_right_) {
        this->AddConnectedRoute(true, this->BuildPrefix("1.1.2.3", 32),
                                "8.8.8.8", "9.9.9.9");
        this->AddConnectedRoute(true, this->BuildPrefix("1.1.2.4", 32),
                                "8.8.8.8", "9.9.9.9");
    } else {
        this->AddConnectedRoute(true, this->BuildPrefix("1.1.2.4", 32),
                                "8.8.8.8", "9.9.9.9");
        this->AddConnectedRoute(true, this->BuildPrefix("1.1.2.3", 32),
                                "8.8.8.8", "9.9.9.9");
    }

    // Wait for everything to be processed.
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn1_.get()));
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn2_.get()));

    // Verify service chain routes are replicated to bgp.l3vpn.0.
    // Note that agents are registered to blue-i1 with instance id 1.
    // Note that agents are registered to blue-i3 with instance id 6.
    if (ServiceChainIntegrationTestGlobals::aggregate_enable_) {
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:1:" +
                                   this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:6:" +
                                   this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:1:" +
                                   this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:6:" +
                                   this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn1_.get(), "9.9.9.9:1:" +
                                   this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn1_.get(), "9.9.9.9:6:" +
                                   this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn2_.get(), "9.9.9.9:1:" +
                                   this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn2_.get(), "9.9.9.9:6:" +
                                   this->BuildPrefix("192.168.1.0", 24));
    } else {
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:1:" +
                                   this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:6:" +
                                   this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:1:" +
                                   this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:6:" +
                                   this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn1_.get(), "9.9.9.9:1:" +
                                   this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn1_.get(), "9.9.9.9:6:" +
                                   this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn2_.get(), "9.9.9.9:1:" +
                                   this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn2_.get(), "9.9.9.9:6:" +
                                   this->BuildPrefix("192.168.1.1", 32));
    }

    // Delete Connected routes for both service instances.
    if (ServiceChainIntegrationTestGlobals::left_to_right_) {
        this->DeleteConnectedRoute(true, this->BuildPrefix("1.1.2.3", 32));
        this->DeleteConnectedRoute(true, this->BuildPrefix("1.1.2.4", 32));
    } else {
        this->DeleteConnectedRoute(true, this->BuildPrefix("1.1.2.4", 32));
        this->DeleteConnectedRoute(true, this->BuildPrefix("1.1.2.3", 32));
    }

    // Delete VM routes in red.
    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->DeleteRoute("red", this->BuildPrefix("192.168.1.1",
                                      32));
        this->agent_a_2_->DeleteRoute("red", this->BuildPrefix("192.168.1.1",
                                                               32));
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->DeleteInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->DeleteInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
    } else {
        assert(false);
    }
}

//
// Verify 2 in-network services on the same compute node, in cases where
// subscribe for the service routing instances happens later i.e. after
// the connected route is processed.
//
// The RDs used when advertising service chain routes for the 2 service
// instances should be different. The RD is based on the compute node's
// registration id for the service routing instances, rather then the
// connected routing instance.
//
TYPED_TEST(ServiceChainIntegrationTest, MultipleInNetworkUnsubscribeSubscribe) {
    // Add VM routes in red.
    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->AddRoute("red", this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->AddRoute("red", this->BuildPrefix("192.168.1.1", 32));
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->AddInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->AddInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
    } else {
        assert(false);
    }
    task_util::WaitForIdle();

    // Add Connected routes for both service instances.
    if (ServiceChainIntegrationTestGlobals::left_to_right_) {
        this->AddConnectedRoute(false,
            this->BuildPrefix("1.1.2.3", 32), "8.8.8.8");
        this->AddConnectedRoute(false,
            this->BuildPrefix("1.1.2.4", 32), "8.8.8.8");
    } else {
        this->AddConnectedRoute(false, this->BuildPrefix("1.1.2.4", 32),
                                "8.8.8.8");
        this->AddConnectedRoute(false, this->BuildPrefix("1.1.2.3", 32),
                                "8.8.8.8");
    }

    // Wait for everything to be processed.
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn1_.get()));
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn2_.get()));

    // Verify service chain routes are replicated to bgp.l3vpn.0.
    // Note that agents are registered to blue-i1 with instance id 1.
    // Note that agents are registered to blue-i3 with instance id 6.
    if (ServiceChainIntegrationTestGlobals::aggregate_enable_) {
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:1:" +
                                   this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:6:" +
                                   this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:1:" +
                                   this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:6:" +
                                   this->BuildPrefix("192.168.1.0", 24));
    } else {
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:1:" +
                                   this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:6:" +
                                   this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:1:" +
                                   this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:6:" +
                                       this->BuildPrefix("192.168.1.1", 32));
    }

    // Unsubscribe agents from blue-i1 and blue-i3.
    this->UnsubscribeAgents("blue-i1");
    this->UnsubscribeAgents("blue-i3");

    // Verify service chain routes are no longer in bgp.l3vpn.0.
    // Note that agents are registered to blue-i1 with instance id 1.
    // Note that agents are registered to blue-i3 with instance id 6.
    if (ServiceChainIntegrationTestGlobals::aggregate_enable_) {
        this->VerifyVpnRouteNoExists(this->cn1_.get(), "8.8.8.8:1:" +
                                     this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteNoExists(this->cn1_.get(), "8.8.8.8:6:" +
                                     this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteNoExists(this->cn2_.get(), "8.8.8.8:1:" +
                                     this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteNoExists(this->cn2_.get(), "8.8.8.8:6:" +
                                     this->BuildPrefix("192.168.1.0", 24));
    } else {
        this->VerifyVpnRouteNoExists(this->cn1_.get(), "8.8.8.8:1:" +
                                     this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteNoExists(this->cn1_.get(), "8.8.8.8:6:" +
                                     this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteNoExists(this->cn2_.get(), "8.8.8.8:1:" +
                                     this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteNoExists(this->cn2_.get(), "8.8.8.8:6:" +
                                     this->BuildPrefix("192.168.1.1", 32));
    }

    // Subscribe agents to blue-i1 and blue-i3.
    this->SubscribeAgents("blue-i1", 1);
    this->SubscribeAgents("blue-i3", 6);

    // Verify service chain routes are replicated to bgp.l3vpn.0.
    // Note that agents are registered to blue-i1 with instance id 1.
    // Note that agents are registered to blue-i3 with instance id 6.
    if (ServiceChainIntegrationTestGlobals::aggregate_enable_) {
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:1:" +
                                   this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:6:" +
                                   this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:1:" +
                                   this->BuildPrefix("192.168.1.0", 24));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:6:" +
                                   this->BuildPrefix("192.168.1.0", 24));
    } else {
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:1:" +
                                   this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn1_.get(), "8.8.8.8:6:" +
                                   this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:1:" +
                                   this->BuildPrefix("192.168.1.1", 32));
        this->VerifyVpnRouteExists(this->cn2_.get(), "8.8.8.8:6:" +
                                       this->BuildPrefix("192.168.1.1", 32));
    }

    // Delete Connected routes for both service instances.
    if (ServiceChainIntegrationTestGlobals::left_to_right_) {
        this->DeleteConnectedRoute(false, this->BuildPrefix("1.1.2.3", 32));
        this->DeleteConnectedRoute(false, this->BuildPrefix("1.1.2.4", 32));
    } else {
        this->DeleteConnectedRoute(false, this->BuildPrefix("1.1.2.4", 32));
        this->DeleteConnectedRoute(false, this->BuildPrefix("1.1.2.3", 32));
    }

    // Delete VM routes in red.
    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->DeleteRoute("red", this->BuildPrefix("192.168.1.1",
                                                               32));
        this->agent_a_2_->DeleteRoute("red", this->BuildPrefix("192.168.1.1",
                                                               32));
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->DeleteInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->DeleteInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
    } else {
        assert(false);
    }
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());
}

static void TearDown() {
    task_util::WaitForIdle();
    BgpServer::Terminate();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

static void process_command_line_args(int argc, const char **argv) {
    options_description desc("ServiceChainTest");
    desc.add_options()
        ("help", "produce help message")
        ("enable-aggregate",
            bool_switch(&ServiceChainIntegrationTestGlobals::aggregate_enable_),
             "Enable aggregate")
        ("left-to-right",
             bool_switch(&ServiceChainIntegrationTestGlobals::left_to_right_),
             "Direction left to right");
    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    ServiceChainIntegrationTestGlobals::connected_table_ = "blue";
    notify(vm);
}

int service_chain_test_main(int argc, const char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, const_cast<char **>(argv));
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    process_command_line_args(argc, const_cast<const char **>(argv));
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}

#ifndef __SERVICE_CHAIN_STATIC_ROUTE_INTEGRATION_TEST_WRAPPER_TEST_SUITE__

int main(int argc, char **argv) {
    return service_chain_test_main(argc, const_cast<const char **>(argv));
}

#endif
