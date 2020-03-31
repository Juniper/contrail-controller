/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "svc_static_route_intergration_test.h"

TYPED_TEST(ServiceChainIntegrationTest, BidirectionalChain) {
    string blue_conn_table =
        ServiceChainIntegrationTestGlobals::transparent_ ? "blue-i1" : "blue";
    string red_conn_table =
        ServiceChainIntegrationTestGlobals::transparent_ ? "red-i2" : "red";

    // Add more specifics
    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->AddRoute("red", "192.168.1.1/32");
        this->agent_a_2_->AddRoute("red", "192.168.1.1/32");
        this->agent_a_1_->AddRoute("blue", "192.168.0.1/32");
        this->agent_a_2_->AddRoute("blue", "192.168.0.1/32");
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->AddInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->AddInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_1_->AddInet6Route("blue",
            this->BuildPrefix("192.168.0.1", 32));
        this->agent_a_2_->AddInet6Route("blue",
            this->BuildPrefix("192.168.0.1", 32));
    } else {
        assert(false);
    }
    task_util::WaitForIdle();

    // Add Connected routes
    this->AddTableConnectedRoute(blue_conn_table, false,
        this->BuildPrefix("1.1.2.3", 32),
        this->BuildNextHopAddress("88.88.88.88"));
    this->AddTableConnectedRoute(red_conn_table, false,
        this->BuildPrefix("1.1.2.1", 32),
        this->BuildNextHopAddress("66.66.66.66"));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn1_.get()));
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn2_.get()));

    vector<PathVerify> pl_blue;
    PathVerify verify_blue1("88.88.88.88", "ServiceChain", "88.88.88.88",
                            {"gre"}, "red");
    PathVerify verify_blue2("88.88.88.88", "BGP_XMPP", "88.88.88.88", {"gre"},
                            "red");
    pl_blue.push_back(verify_blue1);
    pl_blue.push_back(verify_blue2);

    vector<PathVerify> pl_red;
    PathVerify verify_red1("66.66.66.66", "ServiceChain", "66.66.66.66",
                           {"gre"}, "blue");
    PathVerify verify_red2("66.66.66.66", "BGP_XMPP", "66.66.66.66", {"gre"},
                           "blue");
    pl_red.push_back(verify_red1);
    pl_red.push_back(verify_red2);

    vector<string> origin_vn_path_blue = {"blue"};
    vector<string> origin_vn_path_red = {"red"};
    if (ServiceChainIntegrationTestGlobals::aggregate_enable_) {
        this->VerifyServiceChainRoute(this->cn1_.get(), "blue",
            this->BuildPrefix("192.168.1.0", 24), pl_blue);
        this->VerifyServiceChainRoute(this->cn2_.get(), "blue",
            this->BuildPrefix("192.168.1.0", 24), pl_blue);
        this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "blue",
            this->BuildPrefix("192.168.1.0", 24), origin_vn_path_red);
        this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "blue",
            this->BuildPrefix("192.168.1.0", 24), origin_vn_path_red);

        this->VerifyServiceChainRoute(this->cn1_.get(), "red",
            this->BuildPrefix("192.168.0.0", 24), pl_red);
        this->VerifyServiceChainRoute(this->cn2_.get(), "red",
            this->BuildPrefix("192.168.0.0", 24), pl_red);
        this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "red",
            this->BuildPrefix("192.168.0.0", 24), origin_vn_path_blue);
        this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "red",
            this->BuildPrefix("192.168.0.0", 24), origin_vn_path_blue);
    } else {
        this->VerifyServiceChainRoute(this->cn1_.get(), "blue",
            this->BuildPrefix("192.168.1.1", 32), pl_blue);
        this->VerifyServiceChainRoute(this->cn2_.get(), "blue",
            this->BuildPrefix("192.168.1.1", 32), pl_blue);
        this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "blue",
            this->BuildPrefix("192.168.1.1", 32), origin_vn_path_red);
        this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "blue",
            this->BuildPrefix("192.168.1.1", 32), origin_vn_path_red);

        this->VerifyServiceChainRoute(this->cn1_.get(), "red",
            this->BuildPrefix("192.168.0.1", 32), pl_red);
        this->VerifyServiceChainRoute(this->cn2_.get(), "red",
            this->BuildPrefix("192.168.0.1", 32), pl_red);
        this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "red",
            this->BuildPrefix("192.168.0.1", 32), origin_vn_path_blue);
        this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "red",
            this->BuildPrefix("192.168.0.1", 32), origin_vn_path_blue);
    }

    // Delete Connected routes
    this->DeleteTableConnectedRoute(blue_conn_table, false,
        this->BuildPrefix("1.1.2.3", 32));
    this->DeleteTableConnectedRoute(red_conn_table, false,
        this->BuildPrefix("1.1.2.1", 32));
    task_util::WaitForIdle();

    // Delete more specifics
    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->DeleteRoute("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->DeleteRoute("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_1_->DeleteRoute("blue",
            this->BuildPrefix("192.168.0.1", 32));
        this->agent_a_2_->DeleteRoute("blue",
            this->BuildPrefix("192.168.0.1", 32));
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->DeleteInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->DeleteInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_1_->DeleteInet6Route("blue",
            this->BuildPrefix("192.168.0.1", 32));
        this->agent_a_2_->DeleteInet6Route("blue",
            this->BuildPrefix("192.168.0.1", 32));
    } else {
        assert(false);
    }
    task_util::WaitForIdle();
}

TYPED_TEST(ServiceChainIntegrationTest, BidirectionalChainWithTransitNetwork) {
    this->ToggleAllowTransit(this->cn1_.get(), "red");
    this->ToggleAllowTransit(this->cn2_.get(), "red");
    this->VerifyInstanceIsTransit(this->cn1_.get(), "red");
    this->VerifyInstanceIsTransit(this->cn2_.get(), "red");

    string blue_conn_table =
        ServiceChainIntegrationTestGlobals::transparent_ ? "blue-i1" : "blue";
    string red_conn_table =
        ServiceChainIntegrationTestGlobals::transparent_ ? "red-i2" : "red";

    // Add more specifics
    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->AddRoute("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->AddRoute("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_1_->AddRoute("blue",
            this->BuildPrefix("192.168.0.1", 32));
        this->agent_a_2_->AddRoute("blue",
            this->BuildPrefix("192.168.0.1", 32));
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->AddInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->AddInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_1_->AddInet6Route("blue",
            this->BuildPrefix("192.168.0.1", 32));
        this->agent_a_2_->AddInet6Route("blue",
            this->BuildPrefix("192.168.0.1", 32));
    } else {
        assert(false);
    }
    task_util::WaitForIdle();

    // Add Connected routes
    this->AddTableConnectedRoute(blue_conn_table, false,
        this->BuildPrefix("1.1.2.3", 32), "88.88.88.88");
    this->AddTableConnectedRoute(red_conn_table, false,
        this->BuildPrefix("1.1.2.1", 32), "66.66.66.66");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn1_.get()));
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn2_.get()));

    vector<PathVerify> pl_blue;
    PathVerify verify_blue1("88.88.88.88", "ServiceChain", "88.88.88.88",
                            {"gre"}, "red");
    PathVerify verify_blue2("88.88.88.88", "BGP_XMPP", "88.88.88.88", {"gre"},
                            "red");
    pl_blue.push_back(verify_blue1);
    pl_blue.push_back(verify_blue2);

    vector<PathVerify> pl_red;
    PathVerify verify_red1("66.66.66.66", "ServiceChain", "66.66.66.66",
                           {"gre"}, "blue");
    PathVerify verify_red2("66.66.66.66", "BGP_XMPP", "66.66.66.66", {"gre"},
                           "blue");
    pl_red.push_back(verify_red1);
    pl_red.push_back(verify_red2);

    vector<string> origin_vn_path_blue = {"blue"};
    vector<string> origin_vn_path_red = {"red"};
    if (ServiceChainIntegrationTestGlobals::aggregate_enable_) {
        this->VerifyServiceChainRoute(this->cn1_.get(), "blue",
            this->BuildPrefix("192.168.1.0", 24), pl_blue);
        this->VerifyServiceChainRoute(this->cn2_.get(), "blue",
            this->BuildPrefix("192.168.1.0", 24), pl_blue);
        this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "blue",
            this->BuildPrefix("192.168.1.0", 24), origin_vn_path_red);
        this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "blue",
            this->BuildPrefix("192.168.1.0", 24), origin_vn_path_red);

        this->VerifyServiceChainRoute(this->cn1_.get(), "red",
            this->BuildPrefix("192.168.0.0", 24), pl_red);
        this->VerifyServiceChainRoute(this->cn2_.get(), "red",
            this->BuildPrefix("192.168.0.0", 24), pl_red);
        this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "red",
            this->BuildPrefix("192.168.0.0", 24), origin_vn_path_blue);
        this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "red",
            this->BuildPrefix("192.168.0.0", 24), origin_vn_path_blue);
    } else {
        this->VerifyServiceChainRoute(this->cn1_.get(), "blue",
            this->BuildPrefix("192.168.1.1", 32), pl_blue);
        this->VerifyServiceChainRoute(this->cn2_.get(), "blue",
            this->BuildPrefix("192.168.1.1", 32), pl_blue);
        this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "blue",
            this->BuildPrefix("192.168.1.1", 32), origin_vn_path_red);
        this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "blue",
            this->BuildPrefix("192.168.1.1", 32), origin_vn_path_red);

        this->VerifyServiceChainRoute(this->cn1_.get(), "red",
            this->BuildPrefix("192.168.0.1", 32), pl_red);
        this->VerifyServiceChainRoute(this->cn2_.get(), "red",
            this->BuildPrefix("192.168.0.1", 32), pl_red);
        this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "red",
            this->BuildPrefix("192.168.0.1", 32), origin_vn_path_blue);
        this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "red",
            this->BuildPrefix("192.168.0.1", 32), origin_vn_path_blue);
    }

    // Delete Connected routes
    this->DeleteTableConnectedRoute(blue_conn_table, false,
        this->BuildPrefix("1.1.2.3", 32));
    this->DeleteTableConnectedRoute(red_conn_table, false,
        this->BuildPrefix("1.1.2.1", 32));
    task_util::WaitForIdle();

    // Delete more specifics
    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->DeleteRoute("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->DeleteRoute("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_1_->DeleteRoute("blue",
            this->BuildPrefix("192.168.0.1", 32));
        this->agent_a_2_->DeleteRoute("blue",
            this->BuildPrefix("192.168.0.1", 32));
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->DeleteInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->DeleteInet6Route("red",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_1_->DeleteInet6Route("blue",
            this->BuildPrefix("192.168.0.1", 32));
        this->agent_a_2_->DeleteInet6Route("blue",
            this->BuildPrefix("192.168.0.1", 32));
    } else {
        assert(false);
    }
    task_util::WaitForIdle();
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
        ("enable-transparent-mode",
            bool_switch(&ServiceChainIntegrationTestGlobals::transparent_),
             "Enable transparent mode")
        ("enable-aggregate", bool_switch(
                 &ServiceChainIntegrationTestGlobals::aggregate_enable_),
             "Direction left to right");
    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    ServiceChainIntegrationTestGlobals::connected_table_ = "multiple";
    ServiceChainIntegrationTestGlobals::single_si_ = true;
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
