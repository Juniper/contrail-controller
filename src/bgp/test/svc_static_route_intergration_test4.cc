/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "svc_static_route_intergration_test.h"

//
// Verify when externally connected route is available as static route.
//
TYPED_TEST(ServiceChainIntegrationTest, StaticRoute) {
    auto_ptr<autogen::StaticRouteEntriesType> params;

    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_8.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(this->cn1_->config_db(),
            "routing-instance", "blue-i1", "static-route-entries",
            params.release(), 0);

    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_8.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(this->cn2_->config_db(),
        "routing-instance", "blue-i1", "static-route-entries",
        params.release(), 0);
    task_util::WaitForIdle();

    // Add Connected route
    this->AddConnectedRoute(true);
    task_util::WaitForIdle();

    vector<PathVerify> path_list;
    if (ServiceChainIntegrationTestGlobals::mx_push_connected_) {
        PathVerify verify_1(
            "1.2.2.1", "StaticRoute", "1.2.2.1", set<string>(), "blue");
        PathVerify verify_2(
            "7.8.9.1", "StaticRoute", "7.8.9.1", set<string>(), "blue");
        PathVerify verify_3(
            "1.2.2.1", "BGP_XMPP", "1.2.2.1", set<string>(), "blue");
        PathVerify verify_4(
            "7.8.9.1", "BGP_XMPP", "7.8.9.1", set<string>(), "blue");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    } else {
        PathVerify verify_1("88.88.88.88", "StaticRoute", "88.88.88.88",
                            {"gre"}, "blue");
        PathVerify verify_2("99.99.99.99", "StaticRoute", "99.99.99.99",
                            {"gre"}, "blue");
        PathVerify verify_3("88.88.88.88", "BGP_XMPP", "88.88.88.88", {"gre"},
                            "blue");
        PathVerify verify_4("99.99.99.99", "BGP_XMPP", "99.99.99.99", {"gre"},
                            "blue");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    }

    // Check for ServiceChain route
    this->VerifyServiceChainRoute(this->cn1_.get(), "blue-i1",
        this->BuildPrefix("10.1.1.0", 24), path_list);
    this->VerifyServiceChainRoute(this->cn2_.get(), "blue-i1",
        this->BuildPrefix("10.1.1.0", 24), path_list);

    // Add Connected route
    this->DeleteConnectedRoute(true);
    task_util::WaitForIdle();
}

//
// Verify static route in VN's default routing instance.
//
TYPED_TEST(ServiceChainIntegrationTest, StaticRouteDefaultRoutingInstance) {
    auto_ptr<autogen::StaticRouteEntriesType> params;

    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_13.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(this->cn1_->config_db(),
            "routing-instance", "blue", "static-route-entries",
            params.release(), 0);

    params = this->GetStaticRouteConfig(
            "controller/src/bgp/testdata/static_route_13.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(this->cn2_->config_db(),
        "routing-instance", "blue", "static-route-entries",
        params.release(), 0);
    task_util::WaitForIdle();

    // Add Connected route
    this->AddConnectedRoute(true);
    task_util::WaitForIdle();

    vector<PathVerify> path_list;
    if (ServiceChainIntegrationTestGlobals::mx_push_connected_) {
        PathVerify verify_1(
            "1.2.2.1", "StaticRoute", "1.2.2.1", set<string>(), "blue");
        PathVerify verify_2(
            "7.8.9.1", "StaticRoute", "7.8.9.1", set<string>(), "blue");
        PathVerify verify_3(
            "1.2.2.1", "BGP_XMPP", "1.2.2.1", set<string>(), "blue");
        PathVerify verify_4(
            "7.8.9.1", "BGP_XMPP", "7.8.9.1", set<string>(), "blue");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    } else {
        PathVerify verify_1("88.88.88.88", "StaticRoute", "88.88.88.88",
                            {"gre"}, "blue");
        PathVerify verify_2("99.99.99.99", "StaticRoute", "99.99.99.99",
                            {"gre"}, "blue");
        PathVerify verify_3("88.88.88.88", "BGP_XMPP", "88.88.88.88", {"gre"},
                            "blue");
        PathVerify verify_4("99.99.99.99", "BGP_XMPP", "99.99.99.99", {"gre"},
                            "blue");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    }

    // Check for ServiceChain route
    this->VerifyServiceChainRoute(this->cn1_.get(), "blue",
        this->BuildPrefix("10.1.1.0", 24), path_list);
    this->VerifyServiceChainRoute(this->cn2_.get(), "blue",
        this->BuildPrefix("10.1.1.0", 24), path_list);

    // Add Connected route
    this->DeleteConnectedRoute(true);
    task_util::WaitForIdle();
}

//
// Verify when externally connected route is available as both static route
// and service chain route
//
TYPED_TEST(ServiceChainIntegrationTest, DISABLED_SvcStaticRoute) {
    auto_ptr<autogen::StaticRouteEntriesType> params;

    params = this->GetStaticRouteConfig(
        "controller/src/bgp/testdata/static_route_8.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(this->cn1_->config_db(),
        "routing-instance", "blue-i1", "static-route-entries",
        params.release(), 0);

    params = this->GetStaticRouteConfig(
        "controller/src/bgp/testdata/static_route_8.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(this->cn2_->config_db(),
        "routing-instance", "blue-i1", "static-route-entries",
        params.release(), 0);
    task_util::WaitForIdle();


    // Add external route from MX to a VN which is connected to dest routing
    // instance
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
        PathVerify verify_1("1.2.2.1", "StaticRoute", "1.2.2.1", set<string>(),
                            "blue");
        PathVerify verify_2("7.8.9.1", "StaticRoute", "7.8.9.1", set<string>(),
                            "blue");
        PathVerify verify_3("1.2.2.1", "ServiceChain", "1.2.2.1",
                            set<string>(), "red");
        PathVerify verify_4("7.8.9.1", "ServiceChain", "7.8.9.1",
                            set<string>(), "red");
        PathVerify verify_5("1.2.2.1", "BGP_XMPP", "1.2.2.1", set<string>(),
                            "blue");
        PathVerify verify_6("7.8.9.1", "BGP_XMPP", "7.8.9.1", set<string>(),
                            "blue");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
        path_list.push_back(verify_5);
        path_list.push_back(verify_6);
    } else {
        PathVerify verify_1("88.88.88.88", "StaticRoute", "88.88.88.88",
                            {"gre"}, "blue");
        PathVerify verify_2("99.99.99.99", "StaticRoute", "99.99.99.99",
                            {"gre"}, "blue");
        PathVerify verify_3("88.88.88.88", "ServiceChain", "88.88.88.88",
                            {"gre"}, "red");
        PathVerify verify_4("99.99.99.99", "ServiceChain", "99.99.99.99",
                            {"gre"}, "red");
        PathVerify verify_5("88.88.88.88", "BGP_XMPP", "88.88.88.88", {"gre"},
                            "blue");
        PathVerify verify_6("99.99.99.99", "BGP_XMPP", "99.99.99.99", {"gre"},
                            "blue");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
        path_list.push_back(verify_5);
        path_list.push_back(verify_6);
    }

    // Check for ServiceChain route
    this->VerifyServiceChainRoute(this->cn1_.get(),
        this->BuildPrefix("10.1.1.0", 24), path_list);
    this->VerifyServiceChainRoute(this->cn2_.get(),
        this->BuildPrefix("10.1.1.0", 24), path_list);

    this->DeleteRoute(this->mx_.get(), NULL, "public",
        this->BuildPrefix("10.1.1.0", 24));

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
        ("enable-mx-push-connected", bool_switch(
             &ServiceChainIntegrationTestGlobals::mx_push_connected_),
             "Enable mx push connected");
    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    ServiceChainIntegrationTestGlobals::connected_table_ = "blue";
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
