/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_graph_walker.h"

#include <fstream>

#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "io/event_manager.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_util.h"
#include "ifmap/ifmap_whitelist.h"
#include "ifmap/test/ifmap_client_mock.h"
#include "ifmap/test/ifmap_test_util.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;

class IFMapGraphWalkerTest : public ::testing::Test {
protected:
    IFMapGraphWalkerTest()
            : server_(&db_, &db_graph_, evm_.io_service()), parser_(NULL) {
    }

    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &db_graph_);
        parser_ = IFMapServerParser::GetInstance("vnc_cfg");
        vnc_cfg_ParserInit(parser_);
        vnc_cfg_Server_ModuleInit(&db_, &db_graph_);
        bgp_schema_ParserInit(parser_);
        bgp_schema_Server_ModuleInit(&db_, &db_graph_);
        server_.Initialize();
    }

    virtual void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
        IFMapLinkTable_Clear(&db_);
        IFMapTable::ClearTables(&db_);
        task_util::WaitForIdle();
        db_.Clear();
        parser_->MetadataClear("vnc_cfg");
        evm_.Shutdown();
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    DB db_;
    DBGraph db_graph_;
    EventManager evm_;
    IFMapServer server_;
    IFMapServerParser *parser_;
};

// Ensure that only a single virtual-network is received.
TEST_F(IFMapGraphWalkerTest, VNPropagation_1) {
    string content = 
        FileRead("controller/src/ifmap/testdata/vn_propagation_1.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    IFMapClientMock c1("user-X9SCL-X9SCM");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe("user-X9SCL-X9SCM",
                               "f40a487f-09b8-4918-83a0-8cc0ac148cf0", true, 1);

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, c1.object_map().count("virtual-network"));
}

TEST_F(IFMapGraphWalkerTest, ToggleIpamLink) {
    string content = 
        FileRead("controller/src/ifmap/testdata/vn_propagation_1.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    IFMapClientMock c1("user-X9SCL-X9SCM");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe("user-X9SCL-X9SCM",
                               "f40a487f-09b8-4918-83a0-8cc0ac148cf0", true, 1);

    task_util::WaitForIdle();

    string left, l_mid;
    boost::tie(left, l_mid) = c1.LinkFind("virtual-network",
                                          "virtual-network-network-ipam");
    TASK_UTIL_EXPECT_NE(0, left.size());
    TASK_UTIL_EXPECT_NE(0, l_mid.size());
    string r_mid, right;
    boost::tie(r_mid, right) = c1.LinkFind("virtual-network-network-ipam",
                                           "network-ipam");
    TASK_UTIL_EXPECT_EQ(l_mid, r_mid);
    TASK_UTIL_EXPECT_NE(0, right.size());
    
    
    ifmap_test_util::IFMapMsgUnlink(&db_, "virtual-network", left,
                                    "network-ipam", right,
                                    "virtual-network-network-ipam");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, c1.object_map().count("network-ipam"));

    int current = c1.count();
    ifmap_test_util::IFMapMsgLink(&db_, "virtual-network", left,
                                  "network-ipam", right,
                                  "virtual-network-network-ipam");
    
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, c1.object_map().count("network-ipam"));
    // virtual-network-network-ipam is a LinkAttr. There are 2 nodes and
    // 2 links that are added.
    TASK_UTIL_EXPECT_EQ(4, c1.count() - current);
}

TEST_F(IFMapGraphWalkerTest, Cli1Vn1Vm3Add) {
    string content = 
        FileRead("controller/src/ifmap/testdata/cli1_vn1_vm3_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(0, c1.count());
    TASK_UTIL_EXPECT_NE(0, c1.node_count());
    TASK_UTIL_EXPECT_NE(0, c1.link_count());

    IFMapClientMock 
        c2("default-global-system-config:a1s28.contrail.juniper.net");
    server_.AddClient(&c2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, c2.count());
    TASK_UTIL_EXPECT_EQ(0, c2.node_count());
    TASK_UTIL_EXPECT_EQ(0, c2.link_count());

    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-network",
                                        "default-domain:demo:vn27"));
    TASK_UTIL_EXPECT_FALSE(c1.NodeExists("virtual-network",
                                         "default-domain:demo:vn28"));
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-router",
        "default-global-system-config:a1s27.contrail.juniper.net"));
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine"), 3);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine-interface"), 3);
}

TEST_F(IFMapGraphWalkerTest, Cli2Vn2Vm2Add) {
    string content = 
        FileRead("controller/src/ifmap/testdata/cli2_vn2_vm2_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "0af0866c-08c9-49ae-856b-0f4a58179920", true, 1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(0, c1.count());
    TASK_UTIL_EXPECT_NE(0, c1.node_count());
    TASK_UTIL_EXPECT_NE(0, c1.link_count());

    IFMapClientMock 
        c2("default-global-system-config:a1s28.contrail.juniper.net");
    server_.AddClient(&c2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s28.contrail.juniper.net",
        "0d9dd007-b25a-4d86-bf68-dc0e85e317e3", true, 1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(0, c2.count());
    TASK_UTIL_EXPECT_NE(0, c2.node_count());
    TASK_UTIL_EXPECT_NE(0, c2.link_count());

    IFMapClientMock c3("no-name-client");
    server_.AddClient(&c3);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, c3.count());
    TASK_UTIL_EXPECT_EQ(0, c3.node_count());
    TASK_UTIL_EXPECT_EQ(0, c3.link_count());

    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-network",
                                        "default-domain:demo:vn104"));
    TASK_UTIL_EXPECT_FALSE(c1.NodeExists("virtual-network",
                                         "default-domain:demo:vn28"));
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-router",
        "default-global-system-config:a1s27.contrail.juniper.net"));
    TASK_UTIL_EXPECT_FALSE(c1.NodeExists("virtual-router",
        "default-global-system-config:a1s28.contrail.juniper.net"));
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine-interface"), 1);

    TASK_UTIL_EXPECT_TRUE(c2.NodeExists("virtual-network",
                                        "default-domain:demo:vn28"));
    TASK_UTIL_EXPECT_FALSE(c2.NodeExists("virtual-network",
                                         "default-domain:demo:vn104"));
    TASK_UTIL_EXPECT_TRUE(c2.NodeExists("virtual-router",
        "default-global-system-config:a1s28.contrail.juniper.net"));
    TASK_UTIL_EXPECT_FALSE(c2.NodeExists("virtual-router",
        "default-global-system-config:a1s27.contrail.juniper.net"));
    TASK_UTIL_EXPECT_EQ(c2.NodeKeyCount("virtual-network"), 1);
    TASK_UTIL_EXPECT_EQ(c2.NodeKeyCount("virtual-machine"), 1);
    TASK_UTIL_EXPECT_EQ(c2.NodeKeyCount("virtual-machine-interface"), 1);
}

TEST_F(IFMapGraphWalkerTest, Cli1Vn2Np2Add) {
    string content = 
        FileRead("controller/src/ifmap/testdata/cli1_vn2_np2_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "695d391b-65e6-4091-bea5-78e5eae32e66", true, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "5f25dd5e-5442-4edf-89d1-6a318c0d213b", true, 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(0, c1.count());
    TASK_UTIL_EXPECT_NE(0, c1.node_count());
    TASK_UTIL_EXPECT_NE(0, c1.link_count());

    IFMapClientMock 
        c2("default-global-system-config:a1s28.contrail.juniper.net");
    server_.AddClient(&c2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, c2.count());
    TASK_UTIL_EXPECT_EQ(0, c2.node_count());
    TASK_UTIL_EXPECT_EQ(0, c2.link_count());

    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-network",
                                        "default-domain:demo:vn27-1"));
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-network",
                                        "default-domain:demo:vn27-2"));
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-router",
        "default-global-system-config:a1s27.contrail.juniper.net"));
    TASK_UTIL_EXPECT_FALSE(c1.LinkExists("virtual-network",
        "virtual-network-network-policy", "default-domain:demo:vn27-1",
        "attr(default-domain:demo:vn27-1,default-domain:demo:vn27-1to2)"));
    TASK_UTIL_EXPECT_FALSE(c1.LinkExists("virtual-network",
        "virtual-network-network-policy", "default-domain:demo:vn27-2",
        "attr(default-domain:demo:vn27-2,default-domain:demo:vn27-2to1)"));
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network"), 2);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine"), 2);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine-interface"), 2);

    // Since network-policy and virtual-network-network-policy is not downloaded
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network-network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-network",
                                        "virtual-network-network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-network-network-policy",
                                        "network-policy"), 0);
}

TEST_F(IFMapGraphWalkerTest, Cli1Vn2Np1Add) {
    string content = 
        FileRead("controller/src/ifmap/testdata/cli1_vn2_np1_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "ae85ef17-1bff-4303-b1a0-980e0e9b0705", true, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "fd6e78d3-a4fb-400f-94a7-c367c232a56c", true, 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(0, c1.count());
    TASK_UTIL_EXPECT_NE(0, c1.node_count());
    TASK_UTIL_EXPECT_NE(0, c1.link_count());

    IFMapClientMock 
        c2("default-global-system-config:a1s28.contrail.juniper.net");
    server_.AddClient(&c2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, c2.count());
    TASK_UTIL_EXPECT_EQ(0, c2.node_count());
    TASK_UTIL_EXPECT_EQ(0, c2.link_count());

    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-network",
                                        "default-domain:demo:vn27-1"));
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-network",
                                        "default-domain:demo:vn27-2"));
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-router",
        "default-global-system-config:a1s27.contrail.juniper.net"));
    TASK_UTIL_EXPECT_FALSE(c1.LinkExists("virtual-network",
        "virtual-network-network-policy", "default-domain:demo:vn27-1",
        "attr(default-domain:demo:v27-1-2,default-domain:demo:vn27-1)"));
    TASK_UTIL_EXPECT_FALSE(c1.LinkExists("virtual-network",
        "virtual-network-network-policy", "default-domain:demo:vn27-2",
        "attr(default-domain:demo:v27-1-2,default-domain:demo:vn27-2)"));

    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network"), 2);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine"), 2);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine-interface"), 2);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network-network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-network",
                                        "virtual-network-network-policy"), 0);
    // Since network-policy and virtual-network-network-policy is not downloaded
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-network-network-policy",
                                        "network-policy"), 0);
}

TEST_F(IFMapGraphWalkerTest, Cli2Vn2Np2Add) {
    string content = 
        FileRead("controller/src/ifmap/testdata/cli2_vn2_np2_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "29fe5698-d04b-47ca-acf0-199b21c0a6ee", true, 1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(0, c1.count());
    TASK_UTIL_EXPECT_NE(0, c1.node_count());
    TASK_UTIL_EXPECT_NE(0, c1.link_count());

    IFMapClientMock 
        c2("default-global-system-config:a1s28.contrail.juniper.net");
    server_.AddClient(&c2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s28.contrail.juniper.net",
        "39ed8f81-cf9c-4789-a118-e71f53abdf85", true, 1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(0, c2.count());
    TASK_UTIL_EXPECT_NE(0, c2.node_count());
    TASK_UTIL_EXPECT_NE(0, c2.link_count());

    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-network",
                                        "default-domain:demo:vn27"));
    TASK_UTIL_EXPECT_FALSE(c1.NodeExists("virtual-network",
                                         "default-domain:demo:vn28"));
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-router",
        "default-global-system-config:a1s27.contrail.juniper.net"));
    TASK_UTIL_EXPECT_FALSE(c1.NodeExists("virtual-router",
        "default-global-system-config:a1s28.contrail.juniper.net"));
    TASK_UTIL_EXPECT_FALSE(c1.LinkExists("virtual-network",
        "virtual-network-network-policy", "default-domain:demo:vn27",
        "attr(default-domain:demo:2728biIcmp,default-domain:demo:vn27)"));
    TASK_UTIL_EXPECT_FALSE(c1.LinkExists("virtual-network",
        "virtual-network-network-policy", "default-domain:demo:vn27",
        "attr(default-domain:demo:27to28tcp,default-domain:demo:vn27)"));
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-network", 
                                        "virtual-network-network-policy"), 0);

    TASK_UTIL_EXPECT_TRUE(c2.NodeExists("virtual-network",
                                        "default-domain:demo:vn28"));
    TASK_UTIL_EXPECT_FALSE(c2.NodeExists("virtual-network",
                                         "default-domain:demo:vn27"));
    TASK_UTIL_EXPECT_TRUE(c2.NodeExists("virtual-router",
        "default-global-system-config:a1s28.contrail.juniper.net"));
    TASK_UTIL_EXPECT_FALSE(c2.NodeExists("virtual-router",
        "default-global-system-config:a1s27.contrail.juniper.net"));
    TASK_UTIL_EXPECT_FALSE(c2.LinkExists("virtual-network",
        "virtual-network-network-policy", "default-domain:demo:vn28",
        "attr(default-domain:demo:2728biIcmp,default-domain:demo:vn28)"));
    TASK_UTIL_EXPECT_FALSE(c2.LinkExists("virtual-network",
        "virtual-network-network-policy", "default-domain:demo:vn28",
        "attr(default-domain:demo:28to27udp,default-domain:demo:vn28)"));
    TASK_UTIL_EXPECT_EQ(c2.LinkKeyCount("virtual-network", 
                                        "virtual-network-network-policy"), 0);

    // Since network-policy and virtual-network-network-policy is not downloaded
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-network-network-policy",
                                        "network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c2.NodeKeyCount("network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c2.LinkKeyCount("virtual-network-network-policy",
                                        "network-policy"), 0);
}

// 2 servers
// srv1: 2 vns, 2vms/vn, 1 np between 2 vns
// srv2: 1 vn, 2vms, 1 np with vm in srv1
TEST_F(IFMapGraphWalkerTest, Cli2Vn3Vm6Np2Add) {
    string content = 
        FileRead("controller/src/ifmap/testdata/cli2_vn3_vm6_np2_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "7285b8b4-63e7-4251-8690-bbef70c2ccc1", true, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "98e60d70-460a-4618-b334-1dbd6333e599", true, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "7e87e01a-6847-4e24-b668-4a1ad24cef1c", true, 3);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "34a09a89-823a-4934-bf3d-f2cd9513e121", true, 4);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(0, c1.count());
    TASK_UTIL_EXPECT_NE(0, c1.node_count());
    TASK_UTIL_EXPECT_NE(0, c1.link_count());

    IFMapClientMock 
        c2("default-global-system-config:a1s28.contrail.juniper.net");
    server_.AddClient(&c2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s28.contrail.juniper.net",
        "2af8952f-ee66-444b-be63-67e8c6efaf74", true, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s28.contrail.juniper.net",
        "9afa046f-743c-42e0-ab63-2786a81d5731", true, 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(0, c2.count());
    TASK_UTIL_EXPECT_NE(0, c2.node_count());
    TASK_UTIL_EXPECT_NE(0, c2.link_count());

    TASK_UTIL_EXPECT_FALSE(c1.LinkExists("virtual-network",
    "virtual-network-network-policy", "default-domain:demo:vn27-1",
    "attr(default-domain:demo:vn27-1,default-domain:demo:vn27-1tovn27-2_BI)"));
    TASK_UTIL_EXPECT_FALSE(c1.LinkExists("virtual-network",
    "virtual-network-network-policy", "default-domain:demo:vn27-1",
    "attr(default-domain:demo:vn27-1,default-domain:demo:vn27-1tovn28-1_BI)"));
    TASK_UTIL_EXPECT_FALSE(c1.LinkExists("virtual-network",
    "virtual-network-network-policy", "default-domain:demo:vn27-2",
    "attr(default-domain:demo:vn27-1tovn27-2_BI,default-domain:demo:vn27-2)"));

    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network"), 2);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine"), 4);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine-interface"), 4);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network-network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-network",
                                        "virtual-network-network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-router",
                                        "virtual-machine"), 4);

    TASK_UTIL_EXPECT_FALSE(c2.LinkExists("virtual-network",
    "virtual-network-network-policy", "default-domain:demo:vn28-1",
    "attr(default-domain:demo:vn27-1tovn28-1_BI,default-domain:demo:vn28-1)"));
    TASK_UTIL_EXPECT_EQ(c2.NodeKeyCount("virtual-network"), 1);
    TASK_UTIL_EXPECT_EQ(c2.NodeKeyCount("virtual-machine"), 2);
    TASK_UTIL_EXPECT_EQ(c2.NodeKeyCount("virtual-machine-interface"), 2);
    TASK_UTIL_EXPECT_EQ(c2.NodeKeyCount("virtual-network-network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c2.NodeKeyCount("network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c2.LinkKeyCount("virtual-network",
                                        "virtual-network-network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c2.LinkKeyCount("virtual-router",
                                        "virtual-machine"), 2);

    // Since network-policy and virtual-network-network-policy is not downloaded
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-network-network-policy",
                                        "network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c2.NodeKeyCount("network-policy"), 0);
    TASK_UTIL_EXPECT_EQ(c2.LinkKeyCount("virtual-network-network-policy",
                                        "network-policy"), 0);

    c1.PrintNodes();
    c1.PrintLinks();
    c2.PrintNodes();
    c2.PrintLinks();
}

// Receive config and then VR-subscribe
TEST_F(IFMapGraphWalkerTest, ConfigVrsub) {
    // Config
    string content(FileRead("controller/src/ifmap/testdata/vr_gsc_config.xml"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    usleep(5000);

    // VR-reg 
    IFMapClientMock c1("vr1");
    server_.AddClient(&c1);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(3, c1.count());
    TASK_UTIL_EXPECT_EQ(2, c1.node_count());
    TASK_UTIL_EXPECT_EQ(1, c1.link_count());

    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-router"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("global-system-config"), 1);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-router",
                                        "global-system-config"), 1);
    TASK_UTIL_EXPECT_TRUE(c1.LinkExists("virtual-router",
        "global-system-config", "vr1", "gsc1"));
    c1.PrintLinks();
    c1.PrintNodes();
}

// Receive VR-subscribe and then config
TEST_F(IFMapGraphWalkerTest, VrsubConfig) {
    // VR-reg 
    IFMapClientMock c1("vr1");
    server_.AddClient(&c1);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, c1.count());
    TASK_UTIL_EXPECT_EQ(0, c1.node_count());
    TASK_UTIL_EXPECT_EQ(0, c1.link_count());

    // Config
    string content(FileRead("controller/src/ifmap/testdata/vr_gsc_config.xml"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    usleep(5000);

    TASK_UTIL_EXPECT_EQ(3, c1.count());
    TASK_UTIL_EXPECT_EQ(2, c1.node_count());
    TASK_UTIL_EXPECT_EQ(1, c1.link_count());

    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-router"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("global-system-config"), 1);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-router",
                                        "global-system-config"), 1);
    TASK_UTIL_EXPECT_TRUE(c1.LinkExists("virtual-router",
        "global-system-config", "vr1", "gsc1"));
    c1.PrintLinks();
    c1.PrintNodes();
}

// Receive config where nodes have no properties and then VR-subscribe
// Then receive config where the nodes have properties
TEST_F(IFMapGraphWalkerTest, ConfignopropVrsub) {
    // Config
    string content(FileRead(
        "controller/src/ifmap/testdata/vr_gsc_config_no_prop.xml"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    usleep(5000);

    // VR-reg 
    IFMapClientMock c1("vr1");
    server_.AddClient(&c1);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(3, c1.count());
    TASK_UTIL_EXPECT_EQ(2, c1.node_count());
    TASK_UTIL_EXPECT_EQ(1, c1.link_count());

    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-router"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("global-system-config"), 1);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-router",
                                        "global-system-config"), 1);
    TASK_UTIL_EXPECT_TRUE(c1.LinkExists("virtual-router",
        "global-system-config", "vr1", "gsc1"));

    // Now read the properties and another link update with no real change.
    // Client should receive 2 node updates but no link updates
    content = FileRead("controller/src/ifmap/testdata/vr_gsc_config.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    usleep(5000);
    TASK_UTIL_EXPECT_EQ(5, c1.count());
    TASK_UTIL_EXPECT_EQ(4, c1.node_count());
    TASK_UTIL_EXPECT_EQ(1, c1.link_count());

    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-router"), 2);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("global-system-config"), 2);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-router",
                                        "global-system-config"), 1);

    c1.PrintLinks();
    c1.PrintNodes();
}

// Receive VR-subscribe and then config where nodes have no properties
// Then receive config where the nodes have properties
TEST_F(IFMapGraphWalkerTest, VrsubConfignoprop) {
    // VR-reg 
    IFMapClientMock c1("vr1");
    server_.AddClient(&c1);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, c1.count());
    TASK_UTIL_EXPECT_EQ(0, c1.node_count());
    TASK_UTIL_EXPECT_EQ(0, c1.link_count());

    // Config
    string content(FileRead(
        "controller/src/ifmap/testdata/vr_gsc_config_no_prop.xml"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    usleep(5000);

    TASK_UTIL_EXPECT_EQ(3, c1.count());
    TASK_UTIL_EXPECT_EQ(2, c1.node_count());
    TASK_UTIL_EXPECT_EQ(1, c1.link_count());

    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-router"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("global-system-config"), 1);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-router",
                                        "global-system-config"), 1);
    TASK_UTIL_EXPECT_TRUE(c1.LinkExists("virtual-router",
        "global-system-config", "vr1", "gsc1"));

    // Now read the properties and another link update with no real change.
    // Client should receive 2 node updates but no link updates
    content = FileRead("controller/src/ifmap/testdata/vr_gsc_config.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    usleep(5000);
    TASK_UTIL_EXPECT_EQ(5, c1.count());
    TASK_UTIL_EXPECT_EQ(4, c1.node_count());
    TASK_UTIL_EXPECT_EQ(1, c1.link_count());

    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-router"), 2);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("global-system-config"), 2);
    TASK_UTIL_EXPECT_EQ(c1.LinkKeyCount("virtual-router",
                                        "global-system-config"), 1);

    c1.PrintLinks();
    c1.PrintNodes();
}

// Calculate the white list filter information based on the xsd.
TEST_F(IFMapGraphWalkerTest, PopulateWhiteList) {
    // Populate 'filter_info' with information from the xsd
    vnc_cfg_FilterInfo filter_info;
    vnc_cfg_Server_GenerateGraphFilter(&filter_info);

    // With filter_info as input, let the GTFCalculator give the white_list as
    // output.
    const std::auto_ptr<IFMapTypenameWhiteList>
        white_list(new IFMapTypenameWhiteList());
    const std::auto_ptr<IFMapGraphTraversalFilterCalculator> 
        filter_calculator(new IFMapGraphTraversalFilterCalculator(
            filter_info, white_list.get()));

    // Print the list of nodes and links in the whitelist
    std::cout << "List of nodes to allow: " << std::endl;
    for (std::set<std::string>::iterator it = 
         white_list->include_vertex.begin();
         it != white_list->include_vertex.end(); ++it) {
        std::cout << "\t" << *it << std::endl;
    }
    std::cout << "List of links to allow: " << std::endl;
    for (std::set<std::string>::iterator it = 
         white_list->include_edge.begin();
         it != white_list->include_edge.end(); ++it) {
        std::cout << "\t" << *it << std::endl;
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    bool success = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return success;
}
