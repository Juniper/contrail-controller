/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "svc_static_route_intergration_test.h"

TYPED_TEST(ServiceChainIntegrationTest, Basic) {
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

    // Add Connected route
    this->AddConnectedRoute();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn1_.get()));
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn2_.get()));

    vector<PathVerify> path_list;
    if (ServiceChainIntegrationTestGlobals::mx_push_connected_) {
        PathVerify verify_1("7.8.9.1", "ServiceChain",  "7.8.9.1",
                            set<string>(), "red");
        PathVerify verify_2("7.8.9.1", "BGP_XMPP",  "7.8.9.1", set<string>(),
                            "red");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
    } else {
        PathVerify verify_1("88.88.88.88", "ServiceChain", "88.88.88.88",
                            {"gre"}, "red");
        PathVerify verify_2("88.88.88.88", "BGP_XMPP", "88.88.88.88", {"gre"},
                            "red");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
    }

    vector<string> origin_vn_path = {"red"};
    if (ServiceChainIntegrationTestGlobals::aggregate_enable_) {
        // Check for aggregated route
        this->VerifyServiceChainRoute(this->cn1_.get(),
            this->BuildPrefix("192.168.1.0", 24), path_list);
        this->VerifyServiceChainRoute(this->cn2_.get(),
            this->BuildPrefix("192.168.1.0", 24), path_list);
        this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "blue",
            this->BuildPrefix("192.168.1.0", 24), origin_vn_path);
        this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "blue",
            this->BuildPrefix("192.168.1.0", 24), origin_vn_path);
    } else {
        // Check for aggregated route
        this->VerifyServiceChainRoute(this->cn1_.get(),
            this->BuildPrefix("192.168.1.1", 32), path_list);
        this->VerifyServiceChainRoute(this->cn2_.get(),
            this->BuildPrefix("192.168.1.1", 32), path_list);
        this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "blue",
            this->BuildPrefix("192.168.1.1", 32), origin_vn_path);
        this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "blue",
            this->BuildPrefix("192.168.1.1", 32), origin_vn_path);
    }

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

    this->DeleteConnectedRoute();
}

//
// Test verify the ECMP for service instance. Connected routes can be pushed by
// MX or from Agent
//
TYPED_TEST(ServiceChainIntegrationTest, ECMP) {
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

    // Add Connected route
    this->AddConnectedRoute(true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn1_.get()));
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn2_.get()));

    vector<PathVerify> path_list;
    if (ServiceChainIntegrationTestGlobals::mx_push_connected_) {
        PathVerify verify_1("1.2.2.1", "ServiceChain", "1.2.2.1",
                            set<string>(), "red");
        PathVerify verify_2("7.8.9.1", "ServiceChain", "7.8.9.1",
                            set<string>(), "red");
        PathVerify verify_3("1.2.2.1", "BGP_XMPP", "1.2.2.1", set<string>(),
                            "red");
        PathVerify verify_4("7.8.9.1", "BGP_XMPP", "7.8.9.1", set<string>(),
                            "red");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    } else {
        PathVerify verify_1("88.88.88.88", "ServiceChain", "88.88.88.88",
                            {"gre"}, "red");
        PathVerify verify_2("99.99.99.99", "ServiceChain", "99.99.99.99",
                            {"gre"}, "red");
        PathVerify verify_3("88.88.88.88", "BGP_XMPP", "88.88.88.88", {"gre"},
                            "red");
        PathVerify verify_4("99.99.99.99", "BGP_XMPP", "99.99.99.99", {"gre"},
                            "red");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    }

    vector<string> origin_vn_path = {"red"};
    if (ServiceChainIntegrationTestGlobals::aggregate_enable_) {
        // Check for aggregated route
        this->VerifyServiceChainRoute(this->cn1_.get(),
            this->BuildPrefix("192.168.1.0", 24), path_list);
        this->VerifyServiceChainRoute(this->cn2_.get(),
            this->BuildPrefix("192.168.1.0", 24), path_list);
        this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "blue",
            this->BuildPrefix("192.168.1.0", 24), origin_vn_path);
        this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "blue",
            this->BuildPrefix("192.168.1.0", 24), origin_vn_path);
    } else {
        // Check for aggregated route
        this->VerifyServiceChainRoute(this->cn1_.get(),
            this->BuildPrefix("192.168.1.1", 32), path_list);
        this->VerifyServiceChainRoute(this->cn2_.get(),
            this->BuildPrefix("192.168.1.1", 32), path_list);
        this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "blue",
            this->BuildPrefix("192.168.1.1", 32), origin_vn_path);
        this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "blue",
            this->BuildPrefix("192.168.1.1", 32), origin_vn_path);
    }

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

    this->DeleteConnectedRoute(true);
}

//
// Verify ecmp for ext connected route
//
TYPED_TEST(ServiceChainIntegrationTest, ExtRoute) {
    // Not applicable
    ServiceChainIntegrationTestGlobals::aggregate_enable_ = false;

    // Add external route from MX to dest routing instance
    this->AddRoute(this->mx_.get(), NULL, "public",
        this->BuildPrefix("10.1.1.0", 24), 100);
    task_util::WaitForIdle();

    // Add Connected route
    this->AddConnectedRoute(true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn1_.get()));
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn2_.get()));

    vector<PathVerify> path_list;

    if (ServiceChainIntegrationTestGlobals::mx_push_connected_) {
        PathVerify verify_1("1.2.2.1", "ServiceChain", "1.2.2.1",
                            set<string>(), "red");
        PathVerify verify_2("7.8.9.1", "ServiceChain", "7.8.9.1",
                            set<string>(), "red");
        PathVerify verify_3("1.2.2.1", "BGP_XMPP", "1.2.2.1", set<string>(),
                            "red");
        PathVerify verify_4("7.8.9.1", "BGP_XMPP", "7.8.9.1", set<string>(),
                            "red");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    } else {
        PathVerify verify_1("88.88.88.88", "ServiceChain", "88.88.88.88",
                            {"gre"}, "red");
        PathVerify verify_2("99.99.99.99", "ServiceChain", "99.99.99.99",
                            {"gre"}, "red");
        PathVerify verify_3("88.88.88.88", "BGP_XMPP", "88.88.88.88", {"gre"},
                            "red");
        PathVerify verify_4("99.99.99.99", "BGP_XMPP", "99.99.99.99", {"gre"},
                            "red");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    }

    // Check for ServiceChain route
    this->VerifyServiceChainRoute(this->cn1_.get(),
        this->BuildPrefix("10.1.1.0", 24), path_list);
    this->VerifyServiceChainRoute(this->cn2_.get(),
        this->BuildPrefix("10.1.1.0", 24), path_list);
    vector<string> origin_vn_path = {"red"};
    this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "blue",
        this->BuildPrefix("10.1.1.0", 24), origin_vn_path);
    this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "blue",
        this->BuildPrefix("10.1.1.0", 24), origin_vn_path);

    this->DeleteRoute(this->mx_.get(), NULL, "public",
        this->BuildPrefix("10.1.1.0", 24));

    this->DeleteConnectedRoute(true);
}

TYPED_TEST(ServiceChainIntegrationTest, SiteOfOrigin) {
    // Not applicable
    ServiceChainIntegrationTestGlobals::aggregate_enable_ = false;

    // Add external route from MX to dest routing instance
    SiteOfOrigin soo = SiteOfOrigin::FromString("soo:65001:100");
    this->AddRoute(this->mx_.get(), NULL, "public",
        this->BuildPrefix("10.1.1.0", 24), 100, soo);
    task_util::WaitForIdle();

    // Add Connected route
    this->AddConnectedRoute(true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn1_.get()));
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn2_.get()));

    vector<PathVerify> path_list;

    if (ServiceChainIntegrationTestGlobals::mx_push_connected_) {
        PathVerify verify_1(
            "1.2.2.1", "ServiceChain", "1.2.2.1", set<string>(), "red", soo);
        PathVerify verify_2(
            "7.8.9.1", "ServiceChain", "7.8.9.1", set<string>(), "red", soo);
        PathVerify verify_3(
            "1.2.2.1", "BGP_XMPP", "1.2.2.1", set<string>(), "red", soo);
        PathVerify verify_4(
            "7.8.9.1", "BGP_XMPP", "7.8.9.1", set<string>(), "red", soo);
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    } else {
        PathVerify verify_1("88.88.88.88", "ServiceChain", "88.88.88.88",
                            {"gre"}, "red", soo);
        PathVerify verify_2("99.99.99.99", "ServiceChain", "99.99.99.99",
                            {"gre"}, "red", soo);
        PathVerify verify_3("88.88.88.88", "BGP_XMPP", "88.88.88.88", {"gre"},
                            "red", soo);
        PathVerify verify_4("99.99.99.99", "BGP_XMPP", "99.99.99.99", {"gre"},
                            "red", soo);
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    }

    // Check for ServiceChain route
    this->VerifyServiceChainRoute(this->cn1_.get(),
        this->BuildPrefix("10.1.1.0", 24), path_list);
    this->VerifyServiceChainRoute(this->cn2_.get(),
        this->BuildPrefix("10.1.1.0", 24), path_list);
    vector<string> origin_vn_path = {"red"};
    this->VerifyServiceChainRouteOriginVnPath(this->cn1_.get(), "blue",
        this->BuildPrefix("10.1.1.0", 24), origin_vn_path);
    this->VerifyServiceChainRouteOriginVnPath(this->cn2_.get(), "blue",
        this->BuildPrefix("10.1.1.0", 24), origin_vn_path);

    this->DeleteRoute(this->mx_.get(), NULL, "public",
        this->BuildPrefix("10.1.1.0", 24));

    this->DeleteConnectedRoute(true);
}

//
// For ext connected route pushed by MX contains route target which in export
//             list of dest routing instance
// Service chain route will not be generated as origin VN is not correct
//
TYPED_TEST(ServiceChainIntegrationTest, RouteTarget) {
    // Not applicable
    ServiceChainIntegrationTestGlobals::aggregate_enable_ = false;

    // Add external route from MX to a VN which is connected to dest
    // routing instance
    this->AddRoute(this->mx_.get(), NULL, "public-i1",
        this->BuildPrefix("10.1.1.0", 24), 100);
    task_util::WaitForIdle();

    // Add Connected route
    this->AddConnectedRoute(true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn1_.get()));
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn2_.get()));

    // Check for Replicated
    TASK_UTIL_WAIT_NE_NO_MSG(this->RouteLookup(this->cn1_.get(), "red",
                             this->BuildPrefix("10.1.1.0", 24)), NULL, 1000,
                             10000, "Wait for Replicated route in red..");
    // Check for Replicated
    TASK_UTIL_WAIT_NE_NO_MSG(this->RouteLookup(this->cn2_.get(), "red",
                             this->BuildPrefix("10.1.1.0", 24)), NULL, 1000,
                             10000, "Wait for Replicated route in red..");

    // Check for ServiceChainRoute
    TASK_UTIL_WAIT_EQ_NO_MSG(this->RouteLookup(this->cn1_.get(), "blue",
                             this->BuildPrefix("10.1.1.0", 24)), NULL, 1000,
                             10000, "Wait for ServiceChain route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(this->RouteLookup(this->cn2_.get(), "blue",
                             this->BuildPrefix("10.1.1.0", 24)), NULL, 1000,
                             10000, "Wait for ServiceChain route in blue..");

    this->DeleteRoute(this->mx_.get(), NULL, "public-i1",
                          this->BuildPrefix("10.1.1.0", 24));

    this->DeleteConnectedRoute(true);
}

// Verify that service chain route is not created when origin VN of the source
// route doesn't contain correct origin vn
TYPED_TEST(ServiceChainIntegrationTest, OriginVn) {

    // Not applicable
    ServiceChainIntegrationTestGlobals::aggregate_enable_ = false;

    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->AddRoute("purple",
                                   this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->AddRoute("purple",
                                   this->BuildPrefix("192.168.1.1", 32));
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->AddInet6Route("purple",
                                        this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->AddInet6Route("purple",
                                        this->BuildPrefix("192.168.1.1", 32));
    } else {
        assert(false);
    }

    // Add Connected route
    this->AddConnectedRoute(true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn1_.get()));
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty(this->cn2_.get()));

    // Check for Replicated
    TASK_UTIL_WAIT_NE_NO_MSG(this->RouteLookup(this->cn1_.get(),
        "red", this->BuildPrefix("192.168.1.1", 32)), NULL, 1000, 10000,
        "Wait for Replicated route in red..");

    // Check for Replicated
    TASK_UTIL_WAIT_NE_NO_MSG(this->RouteLookup(this->cn2_.get(), "red",
        this->BuildPrefix("192.168.1.1", 32)), NULL, 1000, 10000,
        "Wait for Replicated route in red..");

    // Check for aggregated route
    TASK_UTIL_WAIT_EQ_NO_MSG(this->RouteLookup(this->cn1_.get(),
        "blue", this->BuildPrefix("192.168.1.1", 32)), NULL, 1000, 10000,
        "Wait for Aggregate route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(this->RouteLookup(this->cn2_.get(), "blue",
        this->BuildPrefix("192.168.1.1", 32)), NULL, 1000, 10000,
        "Wait for Aggregate route in blue..");


    if (this->GetFamily() == Address::INET) {
        this->agent_a_1_->DeleteRoute("purple",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->DeleteRoute("purple",
            this->BuildPrefix("192.168.1.1", 32));
    } else if (this->GetFamily() == Address::INET6) {
        this->agent_a_1_->DeleteInet6Route("purple",
            this->BuildPrefix("192.168.1.1", 32));
        this->agent_a_2_->DeleteInet6Route("purple",
            this->BuildPrefix("192.168.1.1", 32));
    } else {
        assert(false);
    }

    this->DeleteConnectedRoute(true);
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
        ("connected-table", value<string>()->default_value("blue"),
             "connected table name (blue/blue-i1)")
        ("enable-aggregate",
         bool_switch(&ServiceChainIntegrationTestGlobals::aggregate_enable_),
             "Enable aggregates")
        ("enable-mx-push-connected",
         bool_switch(&ServiceChainIntegrationTestGlobals::mx_push_connected_),
             "Enable connected routes push from MX");
    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);

    if (vm.count("connected-table")) {
        ServiceChainIntegrationTestGlobals::connected_table_ =
            vm["connected-table"].as<string>();
    } else {
        ServiceChainIntegrationTestGlobals::connected_table_ = "blue";
    }
    ServiceChainIntegrationTestGlobals::single_si_ = true;
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
