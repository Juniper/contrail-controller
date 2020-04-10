/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_config_listener.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "control-node/control_node.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_dependency_tracker.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/test/ifmap_test_util.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using namespace std;

//
// OVERVIEW
//
// This is the test suite for BgpConfigListener class and DependencyTracker
// nested class.  The BgpConfigData that gets populated in BgpConfigManager
// does not trigger the creation of any operational state since there's no
// BgpConfigManager::Observers registered.
//
// A BgpConfigListenerMock class is used. It's GetChangeList method doesn't
// do anything when processing is disabled.
//
// In addition to testing the common code in the BgpConfigListener and the
// DependencyTracker, the tests also cover each of the terms in the policy
// that's specified via the listener's Initialize method. New tests should
// be added as and when the policy is modified.
//
// TEST STEPS
//
// 1.  Setup creates all the IFMap tables and initialize BgpConfigManager.
// 2.  Each test creates the desired configuration by parsing a test config.
// 3.  Some tests create additional configuration using the ifmap_test_util
//     infrastructure.
// 4.  Wait for all configuration to be processed and reach steady state.
// 5.  Disable propagation to the change list in the BgpConfigListenerMock.
// 6.  Trigger the appropriate IFMapNode and/or IFMapLink events using the
//     ifmap_test_util infrastructure.
// 7.  Verify number of entries on the ChangeList in the BgpConfigListener
//     and number of entries in the NodeList and EdgeDescriptorList in the
//     DependencyTracker.
// 8.  Ask the BgpConfigListener to process the items in the NodeList and in
//     the EdgeDescriptorList and propagate the changes into the ChangeList.
// 9.  Verify total number entries on the ChangeList and number of entries
//     of each identifier type.
// 10. Enable propagation to the change list in BgpConfigListenerMock and
//     trigger the BgpConfigManager to process the change list.
// 11. TearDown terminates BgpConfigManager and cleans up all IFMap state.
//

static string ReadFile(const string &filename) {
    ifstream file(filename.c_str());
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

class BgpConfigListenerMock : public BgpConfigListener {
public:
    BgpConfigListenerMock(BgpIfmapConfigManager *manager)
        : BgpConfigListener(manager), no_processing_(false) {
    }

    virtual ~BgpConfigListenerMock() {
    }

    virtual void GetChangeList(ChangeList *change_list) {
        if (no_processing_)
            return;
        BgpConfigListener::GetChangeList(change_list);
    }

    void set_no_processing() { no_processing_ = true; }
    void clear_no_processing() { no_processing_ = false; }

private:
    bool no_processing_;
};

class BgpConfigListenerTest : public ::testing::Test {
protected:

    typedef BgpConfigListener::ChangeList ChangeList;
    typedef IFMapDependencyTracker::NodeList NodeList;
    typedef IFMapDependencyTracker::EdgeDescriptorList EdgeList;

    BgpConfigListenerTest()
      : db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
        server_(&evm_), tracker_(NULL), parser_(&db_) {
        config_manager_ = static_cast<BgpIfmapConfigManager *>(
            server_.config_manager());
        listener_ = new BgpConfigListenerMock(config_manager_);
        config_manager_->listener_.reset(listener_);
        change_list_ = &listener_->change_list_;
    }

    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &graph_);
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        bgp_schema_Server_ModuleInit(&db_, &graph_);
        config_manager_->Initialize(&db_, &graph_, "local");
        tracker_ = listener_->tracker_.get();
    }

    virtual void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
        db_util::Clear(&db_);
    }

    void PauseChangeListPropagation() {
        task_util::WaitForIdle();
        listener_->set_no_processing();
    }

    void PerformChangeListPropagation() {
        task_util::WaitForIdle();
        ConcurrencyScope scope("bgp::Config");
        ChangeList change_list;
        listener_->BgpConfigListener::GetChangeList(&change_list);
        change_list.swap(*change_list_);
        TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
        TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());
    }

    void ResumeChangeListPropagation() {
        task_util::WaitForIdle();
        ConcurrencyScope scope("db::IFMapTable");
        listener_->clear_no_processing();
        listener_->manager_->OnChange();
        task_util::WaitForIdle();
    }

    size_t GetChangeListCount() {
        return change_list_->size();
    }

    size_t GetChangeListCount(const string &id_type) {
        size_t count = 0;
        for (ChangeList::const_iterator it =
             change_list_->begin(); it != change_list_->end(); ++it) {
            if (it->id_type == id_type)
                count++;
        }
        return count;
    }

    size_t GetNodeListCount() {
        return tracker_->node_list().size();
    }

    size_t GetNodeListCount(const string &id_type) {
        size_t count = 0;
        for (NodeList::const_iterator it = tracker_->node_list().begin();
             it != tracker_->node_list().end(); ++it) {
            if (it->first == id_type)
                count++;
        }
        return count;
    }

    size_t GetEdgeListCount() {
        return tracker_->edge_list().size();
    }

    size_t GetEdgeListCount(const string &metadata) {
        size_t count = 0;
        for (EdgeList::const_iterator it = tracker_->edge_list().begin();
             it != tracker_->edge_list().end(); ++it) {
            if (it->metadata == metadata)
                count++;
        }
        return count;
    }

    EventManager evm_;
    DB db_;
    DBGraph graph_;
    BgpServer server_;
    BgpConfigListenerMock *listener_;
    IFMapDependencyTracker *tracker_;
    ChangeList *change_list_;
    BgpIfmapConfigManager *config_manager_;
    BgpConfigParser parser_;
};

//
// Verify test infrastructure which pauses, performs and resumes propagation
// to the change list.
//
TEST_F(BgpConfigListenerTest, Basic) {
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_1.xml");
    PauseChangeListPropagation();
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_NE(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_NE(0U, GetEdgeListCount());
    TASK_UTIL_EXPECT_NE(0U, GetChangeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());
    TASK_UTIL_EXPECT_NE(0U, GetChangeListCount());

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Node event for an object in table that is not relevant to Bgp is ignored.
// Even though the IFMapNode is updated, it doesn't get added to the change
// list or the node list.
//
TEST_F(BgpConfigListenerTest, IrrelevantNodeEvent) {
    ifmap_test_util::IFMapMsgNodeAdd(&db_, "route-target", "target:1:1");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    ifmap_test_util::IFMapNodeNotify(&db_, "route-target", "target:1:1");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Multiple node events for same node should cause it to get on the change
// list multiple times.
//
TEST_F(BgpConfigListenerTest, DuplicateNodeEvent) {
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    string id_name = string(BgpConfigManager::kMasterInstance) + ":local";
    ifmap_test_util::IFMapNodeNotify(&db_, "bgp-router", id_name);
    task_util::WaitForIdle();
    ifmap_test_util::IFMapNodeNotify(&db_, "bgp-router", id_name);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(2U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(5U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("bgp-router"));
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount("bgp-peering"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Delete event for a node for which the configuration keeps no state. This
// should get filtered in the BgpConfigListener itself.  Thus the node will
// not get on the change list or the node list.
//
TEST_F(BgpConfigListenerTest, DeletedNodeEvent1) {
    ifmap_test_util::IFMapMsgNodeAdd(&db_, "virtual-network", "red");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    ifmap_test_util::IFMapMsgNodeDelete(&db_, "virtual-network", "red");
    task_util::WaitForIdle();
    IFMapNode *node =
        ifmap_test_util::IFMapNodeLookup(&db_, "virtual-network", "red");
    TASK_UTIL_EXPECT_TRUE(node == NULL);

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Delete event for a node for which the configuration does keeps state.
// Thus the node will get on the change list but not on the node list.
//
TEST_F(BgpConfigListenerTest, DeletedNodeEvent2) {
    string content_a = ReadFile("controller/src/bgp/testdata/config_listener_test_6a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    string content_b = ReadFile("controller/src/bgp/testdata/config_listener_test_6b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    task_util::WaitForIdle();
    IFMapNode *node =
        ifmap_test_util::IFMapNodeLookup(&db_, "routing-instance", "red");
    TASK_UTIL_EXPECT_TRUE(node->IsDeleted());

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Delete event for a node which is already on the change list and the node
// list.  The node should be marked deleted when propagating the node list
// and hence will be ignored.
//
TEST_F(BgpConfigListenerTest, DeletedNodeEvent3) {
    ifmap_test_util::IFMapMsgNodeAdd(&db_, "virtual-network", "red");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    ifmap_test_util::IFMapNodeNotify(&db_, "virtual-network", "red");
    task_util::WaitForIdle();
    ifmap_test_util::IFMapMsgNodeDelete(&db_, "virtual-network", "red");
    task_util::WaitForIdle();
    IFMapNode *node =
        ifmap_test_util::IFMapNodeLookup(&db_, "virtual-network", "red");
    TASK_UTIL_EXPECT_TRUE(node->IsDeleted());

    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount());

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for an uninteresting link should not cause any edges to be
// added to the edge list.
//
TEST_F(BgpConfigListenerTest, UninterestingLinkEvent) {
    ifmap_test_util::IFMapMsgLink(&db_, "domain", "default-domain",
        "project", "default-project", "domain-project");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "domain-project",
        "domain", "default-domain", "project", "default-project");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Multiple link events for the same link should cause the component edges
// to get added to the edge list multiple times per current implementation.
//
TEST_F(BgpConfigListenerTest, DuplicateLinkEvent) {
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_12.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify virtual-network-routing-instance link 3 times.
    PauseChangeListPropagation();
    for (int idx = 0; idx < 3; ++idx) {
        ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "virtual-network-routing-instance",
            "virtual-network", "red-vn", "routing-instance", "red");
        task_util::WaitForIdle();
    }

    // Edge list should have 3 entries. Note that virtual-network doesn't have
    // a RectionMap entry for virtual-network-routing-instance.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(3U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for a link whose associated node(s) have already been deleted.
// The associated edge(s) should not get on the edge list.
//
// In this case, the routing-instance is deleted so the edge with metadata
// virtual-network-routing-instance is ignored. The reverse edge from the
// virtual-network is not in the reaction-map anyway.
//
TEST_F(BgpConfigListenerTest, DeletedLinkEvent1) {
    string content_a = ReadFile("controller/src/bgp/testdata/config_listener_test_13a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    ifmap_test_util::IFMapMsgLink(&db_, "virtual-network", "red-vn",
        "routing-instance", "red", "virtual-network-routing-instance");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    string content_b = ReadFile("controller/src/bgp/testdata/config_listener_test_13b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    ifmap_test_util::IFMapMsgUnlink(&db_, "virtual-network", "red-vn",
        "routing-instance", "red", "virtual-network-routing-instance");
    task_util::WaitForIdle();

    IFMapNode *node =
        ifmap_test_util::IFMapNodeLookup(&db_, "routing-instance", "red");
    TASK_UTIL_EXPECT_TRUE(node->IsDeleted());

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for a link whose associated node(s) have already been deleted
// and where the associated edge(s) are already on the edge list.  When the
// edge list is processed, they won't be propagated since the associated
// nodes are marked as deleted.
//
// In this case, the routing-instance is deleted so the edge with metadata
// virtual-network-routing-instance is ignored during propagation. The reverse
// edge from the virtual-network is not in the reaction-map and so will not
// be in the edge list to begin with.
//
TEST_F(BgpConfigListenerTest, DeletedLinkEvent2) {
    string content_a = ReadFile("controller/src/bgp/testdata/config_listener_test_13a.xml");
    EXPECT_TRUE(parser_.Parse(content_a));
    ifmap_test_util::IFMapMsgLink(&db_, "virtual-network", "red-vn",
        "routing-instance", "red", "virtual-network-routing-instance");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "virtual-network-routing-instance",
        "virtual-network", "red-vn", "routing-instance", "red");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    string content_b = ReadFile("controller/src/bgp/testdata/config_listener_test_13b.xml");
    EXPECT_TRUE(parser_.Parse(content_b));
    ifmap_test_util::IFMapMsgUnlink(&db_, "virtual-network", "red-vn",
        "routing-instance", "red", "virtual-network-routing-instance");
    task_util::WaitForIdle();

    IFMapNode *node =
        ifmap_test_util::IFMapNodeLookup(&db_, "routing-instance", "red");
    TASK_UTIL_EXPECT_TRUE(node->IsDeleted());

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Node event for a bgp-peering object that involves the local router.
//
TEST_F(BgpConfigListenerTest, BgpPeeringChange1) {

    // Initialize config with local router and 3 remote routers.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify bgp-peering node between local and remote1.
    PauseChangeListPropagation();
    string id_first = string(BgpConfigManager::kMasterInstance) + ":local";
    string id_second = string(BgpConfigManager::kMasterInstance) + ":remote1";
    string id_name = string("attr") + "(" + id_first + "," + id_second + ")";
    ifmap_test_util::IFMapNodeNotify(&db_, "bgp-peering", id_name);
    task_util::WaitForIdle();

    // The bgp-peering should be on the change list but not on the node list
    // since there's no entry for self in the reaction map.
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // Nothing else should get added since the node and edge lists are empty.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("bgp-peering"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Node event for a bgp-peering object that does not involve the local router.
//
TEST_F(BgpConfigListenerTest, BgpPeeringChange2) {

    // Initialize config with local router and 3 remote routers.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify bgp-peering node between remote2 and remote3.
    PauseChangeListPropagation();
    string id_first = string(BgpConfigManager::kMasterInstance) + ":remote2";
    string id_second = string(BgpConfigManager::kMasterInstance) + ":remote3";
    string id_name = string("attr") + "(" + id_first + "," + id_second + ")";
    ifmap_test_util::IFMapNodeNotify(&db_, "bgp-peering", id_name);
    task_util::WaitForIdle();

    // The bgp-peering should be on the change list but not on the node list
    // since there's no entry for self in the reaction map.
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // Nothing else should get added since the node and edge lists are empty.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("bgp-peering"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for bgp-peering link of a bgp-peering object that involves the
// local router.
//
TEST_F(BgpConfigListenerTest, BgpPeeringBgpPeeringChange1) {

    // Initialize config with local router and 3 remote routers.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify bgp-peering link between local and remote1.
    PauseChangeListPropagation();
    string id_first = string(BgpConfigManager::kMasterInstance) + ":local";
    string id_second = string(BgpConfigManager::kMasterInstance) + ":remote1";
    string id_name = string("attr") + "(" + id_first + "," + id_second + ")";
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "bgp-peering",
        "bgp-peering", id_name, "bgp-router", id_first);
    task_util::WaitForIdle();

    // Only one edge should get added to the edge list.
    // This will be the bgp-peering edge from the bgp-peering object.
    // The bgp-peering edge from the bgp-router is not interesting since the
    // reaction map for bgp-router doesn't have an entry for bgp-peering.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The bgp-peering should be on the change list because the propagate
    // list contains self.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("bgp-peering"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for a bgp-peering link of a bgp-peering object that does not
// involve the local router.
//
TEST_F(BgpConfigListenerTest, BgpPeeringBgpPeeringChange2) {

    // Initialize config with local router and 3 remote routers.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify bgp-peering link between remote1 and remote2.
    PauseChangeListPropagation();
    string id_first = string(BgpConfigManager::kMasterInstance) + ":remote1";
    string id_second = string(BgpConfigManager::kMasterInstance) + ":remote2";
    string id_name = string("attr") + "(" + id_first + "," + id_second + ")";
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "bgp-peering",
        "bgp-peering", id_name, "bgp-router", id_first);
    task_util::WaitForIdle();

    // Only one edge should get added to the edge list.
    // This will be the bgp-peering edge from the bgp-peering object.
    // The bgp-peering edge from the bgp-router is not interesting since the
    // reaction map for bgp-router doesn't have an entry for bgp-peering.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The bgp-peering should be on the change list because the propagate
    // list contains self.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("bgp-peering"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for an uninteresting link of the bgp-peering object.  There's
// no such links since the bgp-peering object is a middle node that has no
// links other than bgp-peering.
//
TEST_F(BgpConfigListenerTest, BgpPeeringUninterestingLinkChange) {
}

//
// Node event for the local bgp-router object.
//
TEST_F(BgpConfigListenerTest, BgpRouterChange1) {

    // Initialize config with local router and 3 remote routers.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify bgp-router local.
    PauseChangeListPropagation();
    string id_name = string(BgpConfigManager::kMasterInstance) + ":local";
    ifmap_test_util::IFMapNodeNotify(&db_, "bgp-router", id_name);

    // The bgp-router should be on the change list and on the node list
    // since there's an entry for self in the reaction map.
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The 3 bgp-peerings should be on the change list because the propagate
    // list for bgp-router contains bgp-peering.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(4U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("bgp-router"));
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount("bgp-peering"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Node event for a remote bgp-router object.
//
TEST_F(BgpConfigListenerTest, BgpRouterChange2) {

    // Initialize config with local router and 3 remote routers.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify bgp-router remote1.
    PauseChangeListPropagation();
    string id_name = string(BgpConfigManager::kMasterInstance) + ":remote1";
    ifmap_test_util::IFMapNodeNotify(&db_, "bgp-router", id_name);

    // The bgp-router should be on the change list and on the node list
    // since there's an entry for self in the reaction map.
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The 3 bgp-peerings should be on the change list because the propagate
    // list for bgp-router contains bgp-peering.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(4U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("bgp-router"));
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount("bgp-peering"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for the instance-bgp-router link of a bgp-router object.
//
TEST_F(BgpConfigListenerTest, BgpRouterInstanceLinkChange) {

    // Initialize config with local router and 3 remote routers.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    string instance(BgpConfigManager::kMasterInstance);
    string router = instance + ":local";
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", instance,
        "bgp-router", router, "instance-bgp-router");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify instance-bgp-router link between bgp-router
    // and routing-instance.
    PauseChangeListPropagation();
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "instance-bgp-router",
        "bgp-router", router, "routing-instance", instance);
    task_util::WaitForIdle();

    // Only one edge should get added to the edge list.
    // This is the instance-bgp-router edge from bgp-router.
    // The instance-bgp-router edge from the routing-instance is not
    // interesting since the reaction map for routing-instance doesn't
    // have an entry for instance-bgp-router.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The bgp-peerings are on the change list since the propagate list for
    // instance-bgp-router in bgp-router contains bgp-peering.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount("bgp-peering"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Node event for a routing-instance object with bgp-routers and bgp-peerings.
//
TEST_F(BgpConfigListenerTest, RoutingInstanceChange1) {

    // Initialize config with local router and 3 remote routers, hence 6
    // peerings.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify routing-instance master.
    PauseChangeListPropagation();
    string id_name(BgpConfigManager::kMasterInstance);
    ifmap_test_util::IFMapNodeNotify(&db_, "routing-instance", id_name);

    // The routing-instance should be on the change list and on the node list
    // since there's an entry for self in the reaction map.
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The bgp-peerings are on the change list since the propagate list for
    // self in routing-instance contains instance-bgp-router and propagate
    // list for instance-bgp-router in bgp-router contains bgp-peering.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(7U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(6U, GetChangeListCount("bgp-peering"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Node event for a routing-instance object with connections.
//
TEST_F(BgpConfigListenerTest, RoutingInstanceChange2) {

    // Initialize config with 3 routing-instances - red, blue and green.
    // Add connections between (red, blue) and (red, green).
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_4.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "blue", "connection");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "green", "connection");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify routing-instance red.
    PauseChangeListPropagation();
    string id_name("red");
    ifmap_test_util::IFMapNodeNotify(&db_, "routing-instance", id_name);

    // The routing-instance should be on the change list and on the node list
    // since there's an entry for self in the reaction map.
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // Nothing else should get added since the routing-instance doesn't have
    // any bgp-routers and bgp-peerings.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for a instance-target link of a routing-instance object. The
// routing-instance is connected to all other routing-instances.
//
TEST_F(BgpConfigListenerTest, RoutingInstanceTargetChange1) {

    // Initialize config with 3 routing-instances - red, blue and green.
    // Add connections between (red, blue) and (red, green).
    // Also add a route-target target:1:100.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_4.xml");
    EXPECT_TRUE(parser_.Parse(content));
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "blue", "connection");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "green", "connection");
    ifmap_test_util::IFMapMsgNodeAdd(&db_, "route-target", "target:1:100");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, add link between routing-instance red and the
    // route-target target:1:100.
    //
    // Note that instance-target is a link with attributes so a middle node
    // called instance-target will get created. There will be instance-target
    // links from the instance-target to routing-instance and route-target.
    PauseChangeListPropagation();
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "route-target", "target:1:100", "instance-target");
    task_util::WaitForIdle();

    // Only one edge should get added to the edge list.
    // This will be the instance-target edge from the routing-instance object
    // as the routing-instance reaction map has an entry for instance-target.
    //
    // The instance-target node is not interesting since event policy doesn't
    // have an entry for instance-target.
    // The instance-target edges from the instance-target are not interesting
    // as the event policy doesn't have an entry for instance-target.
    // The instance-target edge from the route-target is not interesting as
    // the event policy doesn't have an entry for route-target.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // All three routing-instances will be on the change list because the
    // routing-instance propagate list for instance-target has self and
    // connection. The red instance gets added because of self and the other
    // instances get added because of further propagation via the connection.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(4U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(4U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for a instance-target link of a routing-instance object. The
// routing-instance is connected to some but not all other routing-instances.
//
TEST_F(BgpConfigListenerTest, RoutingInstanceTargetChange2) {

    // Initialize config with 3 routing-instances - red, blue and green.
    // Add connection between (red, blue).
    // Also add a route-target target:1:100.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_4.xml");
    EXPECT_TRUE(parser_.Parse(content));
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "blue", "connection");
    ifmap_test_util::IFMapMsgNodeAdd(&db_, "route-target", "target:1:100");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, add link between routing-instance red and the
    // route-target target:1:100.
    //
    // Note that instance-target is a link with attributes so a middle node
    // called instance-target will get created. There will be instance-target
    // links from the instance-target to routing-instance and route-target.
    PauseChangeListPropagation();
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "route-target", "target:1:100", "instance-target");
    task_util::WaitForIdle();

    // Only one edge should get added to the edge list.
    // This will be the instance-target edge from the routing-instance object
    // as the routing-instance reaction map has an entry for instance-target.
    //
    // The instance-target node is not interesting since event policy doesn't
    // have an entry for instance-target.
    // The instance-target edges from the instance-target are not interesting
    // as the event policy doesn't have an entry for instance-target.
    // The instance-target edge from the route-target is not interesting as
    // the event policy doesn't have an entry for route-target.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The red and blue routing-instances will be on the change list since the
    // routing-instance propagate list for instance-target has self and
    // connection. The red instance gets added because of self and the blue
    // instance gets added because of further propagation via the connection.
    // The green instance doesn't get added to the change list.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for a instance-target link of a routing-instance object. The
// routing-instance is connected to another routing-instance which in turn
// is connected to a third routing-instance.
//
// Intent is to verify that we do no propagate the change transitively to
// the third routing-instance.
//
TEST_F(BgpConfigListenerTest, RoutingInstanceTargetChange3) {

    // Initialize config with 3 routing-instances - red, blue and green.
    // Add connections between (red, blue) and (blue, green).
    // Also add a route-target target:1:100.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_4.xml");
    EXPECT_TRUE(parser_.Parse(content));
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "blue", "connection");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "blue",
        "routing-instance", "green", "connection");
    ifmap_test_util::IFMapMsgNodeAdd(&db_, "route-target", "target:1:100");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, add link between routing-instance red and the
    // route-target target:1:100.
    //
    // Note that instance-target is a link with attributes so a middle node
    // called instance-target will get created. There will be instance-target
    // links from the instance-target to routing-instance and route-target.
    PauseChangeListPropagation();
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "route-target", "target:1:100", "instance-target");
    task_util::WaitForIdle();

    // Only one edge should get added to the edge list.
    // This will be the instance-target edge from the routing-instance object
    // as the routing-instance reaction map has an entry for instance-target.
    //
    // The instance-target node is not interesting since event policy doesn't
    // have an entry for instance-target.
    // The instance-target edges from the instance-target are not interesting
    // as the event policy doesn't have an entry for instance-target.
    // The instance-target edge from the route-target is not interesting as
    // the event policy doesn't have an entry for route-target.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // Only the red and blue routing-instances will be on the change list as
    // the routing-instance propagate list for instance-target has self and
    // connection. The red instance gets added because of self and the blue
    // instance gets added because of further propagation via the connection.
    //
    // The green routing-instance does not get on the change list because the
    // routing-instance propagate list for connection only contains self, but
    // not connection.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for an uninteresting link of the connection object.  There's
// no such links since the connection object is a middle node that has no
// links other than connection.
//
TEST_F(BgpConfigListenerTest, ConnectionUninterestingLinkChange) {
}

//
// Node event for a connection node of a routing-instance object that is
// connected to some but not all other routing-instances.
//
TEST_F(BgpConfigListenerTest, ConnectionChange1) {

    // Initialize config with 3 routing-instances - red, blue and green.
    // Add connection between (red, blue).
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_4.xml");
    EXPECT_TRUE(parser_.Parse(content));
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "blue", "connection");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify routing-instance connection (red, blue).
    PauseChangeListPropagation();
    ifmap_test_util::IFMapNodeNotify(&db_, "connection", "attr(blue,red)");
    task_util::WaitForIdle();

    // Connection node should be on the change list and node list.
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // Connection node should be on the change list.
    // The red and blue routing-instances should be on the change list since
    // the propagate list for connection in the routing-instance contains self.
    // The red routing-instance gets added to the change list twice.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("connection"));
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Node event for a connection node of a routing-instance object that is
// connected to all other routing-instances.
//
TEST_F(BgpConfigListenerTest, ConnectionChange2) {

    // Initialize config with 3 routing-instances - red, blue and green.
    // Add connections between (red, blue) and (red, green).
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_4.xml");
    EXPECT_TRUE(parser_.Parse(content));
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "blue", "connection");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "green", "connection");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify routing-instance connection (red, blue).
    PauseChangeListPropagation();
    ifmap_test_util::IFMapNodeNotify(&db_, "connection", "attr(blue,red)");
    task_util::WaitForIdle();

    // Connection node should be on the change list and node list.
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // Connection node should be on the change list.
    // The red and blue routing-instances should be on the change list since
    // the propagate list for connection in the routing-instance contains self.
    // The red routing-instance gets added to the change list twice.
    //
    // The green instance shouldn't be on the change list since the propagate
    // list for connection in the routing-instance doesn't contain connection.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("connection"));
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Node event for a connection node of a routing-instance object that is
// connected to some but not all routing-instances. The routing-instance
// is connected to another routing-instance which in turn is connected to
// a third routing-instance.
//
// Intent is to verify that we do no propagate the change transitively to
// the third routing-instance.
//
TEST_F(BgpConfigListenerTest, ConnectionChange3) {

    // Initialize config with 3 routing-instances - red, blue and green.
    // Add connection between (red, blue) and (blue, green).
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_4.xml");
    EXPECT_TRUE(parser_.Parse(content));
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "blue", "connection");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "blue",
        "routing-instance", "green", "connection");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify routing-instance connection (red, blue).
    PauseChangeListPropagation();
    ifmap_test_util::IFMapNodeNotify(&db_, "connection", "attr(blue,red)");
    task_util::WaitForIdle();

    // Connection node should be on the change list and node list.
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // Connection node should be on the change list.
    // The red and blue routing-instances should be on the change list since
    // the propagate list for connection in the routing-instance contains self.
    // The red routing-instance gets added to the change list twice.
    //
    // The green routing-instance does not get on the change list because the
    // routing-instance propagate list for connection only contains self, but
    // not connection.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("connection"));
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for a connection link of a routing-instance object that is
// connected to some but not all other routing-instances.
//
TEST_F(BgpConfigListenerTest, ConnectionChange4) {

    // Initialize config with 3 routing-instances - red, blue and green.
    // Add connection between (red, blue).
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_4.xml");
    EXPECT_TRUE(parser_.Parse(content));
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "blue", "connection");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify connection between red and attr(blue, red).
    PauseChangeListPropagation();
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "connection",
        "routing-instance", "red", "connection", "attr(blue,red)");
    task_util::WaitForIdle();

    // Both edges between midnode and routing-instance should be on edge list.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(2U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The red and blue routing-instances should be on the change list since
    // the propagate list for connection in the routing-instance contains self.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for a connection link of a routing-instance object that is
// connected to all other routing-instances.
//
TEST_F(BgpConfigListenerTest, ConnectionChange5) {

    // Initialize config with 3 routing-instances - red, blue and green.
    // Add connections between (red, blue) and (red, green).
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_4.xml");
    EXPECT_TRUE(parser_.Parse(content));
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "blue", "connection");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "green", "connection");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify connection between red and attr(blue, red).
    PauseChangeListPropagation();
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "connection",
        "routing-instance", "red", "connection", "attr(blue,red)");
    task_util::WaitForIdle();

    // Both edges between midnode and routing-instance should be on edge list.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(2U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The red and blue routing-instances should be on the change list since
    // the propagate list for connection in the routing-instance contains self.
    //
    // The green instance shouldn't be on the change list since the propagate
    // list for connection in the routing-instance doesn't contain connection.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for a connection link of a routing-instance object that is
// connected to some but not all routing-instances. The routing-instance
// is connected to another routing-instance which in turn is connected to
// a third routing-instance.
//
// Intent is to verify that we do no propagate the change transitively to
// the third routing-instance.
//
TEST_F(BgpConfigListenerTest, ConnectionChange6) {

    // Initialize config with 3 routing-instances - red, blue and green.
    // Add connection between (red, blue) and (blue, green).
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_4.xml");
    EXPECT_TRUE(parser_.Parse(content));
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red",
        "routing-instance", "blue", "connection");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "blue",
        "routing-instance", "green", "connection");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Both edges between midnode and routing-instance should be on edge list.
    PauseChangeListPropagation();
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "connection",
        "routing-instance", "red", "connection", "attr(blue,red)");
    task_util::WaitForIdle();

    // Both edges between midnode and routing-instance should be on edge list.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(2U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The red and blue routing-instances should be on the change list since
    // the propagate list for connection in the routing-instance contains self.
    //
    // The green routing-instance does not get on the change list because the
    // routing-instance propagate list for connection only contains self, but
    // not connection.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for a virtual-network-routing-instance link of a routing-instance
// object. The virtual-network is associated only with this routing-instance.
//
TEST_F(BgpConfigListenerTest, RoutingInstanceVirtualNetworkChange1) {

    // Initialize config with 3 routing-instances - red1, red2 and red3.
    // Add virtual-network-routing-instance link between virtual-network red
    // and routing-instance red1.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red1",
        "virtual-network", "red", "virtual-network-routing-instance");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify virtual-network-routing-instance link.
    PauseChangeListPropagation();
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "virtual-network-routing-instance",
        "routing-instance", "red1", "virtual-network", "red");
    task_util::WaitForIdle();

    // Only one edge should get added to the edge list.
    // This is the virtual-network-routing-instance edge from routing-instance.
    // The virtual-network-routing-instance edge from the virtual-network is
    // not interesting since the reaction map for virtual-network doesn't have
    // an entry for virtual-network-routing-instance.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The routing-instance is on the change list since the propagate list for
    // virtual-network-routing-instance in routing-instance contains self.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for a virtual-network-routing-instance link of a routing-instance
// object.  The virtual-network is associated with this as well as some other
// routing-instances.
//
TEST_F(BgpConfigListenerTest, RoutingInstanceVirtualNetworkChange2) {

    // Initialize config with 3 routing-instances - red1, red2 and red3.
    // Add virtual-network-routing-instance link between virtual-network red
    // and routing-instances red1, red2, red3.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red1",
        "virtual-network", "red", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red2",
        "virtual-network", "red", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red3",
        "virtual-network", "red", "virtual-network-routing-instance");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify virtual-network-routing-instance link between
    // red and red1.
    PauseChangeListPropagation();
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "virtual-network-routing-instance",
        "routing-instance", "red1", "virtual-network", "red");
    task_util::WaitForIdle();

    // Only one edge should get added to the edge list.
    // This is the virtual-network-routing-instance edge from routing-instance.
    // The virtual-network-routing-instance edge from the virtual-network is
    // not interesting since the reaction map for virtual-network doesn't have
    // an entry for virtual-network-routing-instance.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The routing-instance red is on the change list since the propagate list
    // for virtual-network-routing-instance in routing-instance contains self.
    // The other instances are not on the change list.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for virtual-network-routing-instance link of a routing-instance
// which is connected to another routing-instances.
//
// Intent is to verify that we do no propagate the change transitively to the
// connected routing-instance.
//
TEST_F(BgpConfigListenerTest, RoutingInstanceVirtualNetworkChange3) {

    // Initialize config with 2 routing-instances - red1 and blue1.
    // Add virtual-network-routing-instance link between virtual-network red
    // and routing-instances red1.
    // Add virtual-network-routing-instance link between virtual-network blue
    // and routing-instances blue1.
    // Add connection between (red1, blue1).
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_8.xml");
    EXPECT_TRUE(parser_.Parse(content));
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red1",
        "virtual-network", "red", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "blue1",
        "virtual-network", "blue", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red1",
        "routing-instance", "blue1", "connection");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify virtual-network-routing-instance link between
    // red and red1.
    PauseChangeListPropagation();
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "virtual-network-routing-instance",
        "routing-instance", "red1", "virtual-network", "red");
    task_util::WaitForIdle();

    // Only one edge should get added to the edge list.
    // This is the virtual-network-routing-instance edge from routing-instance.
    // The virtual-network-routing-instance edge from the virtual-network is
    // not interesting since the reaction map for virtual-network doesn't have
    // an entry for virtual-network-routing-instance.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The routing-instance red1 is on the change list since the propagate list
    // for virtual-network-routing-instance in routing-instance contains self.
    //
    // The blue1 routing-instance does not get on the change list because the
    // routing-instance the propagate list for virtual-network-routing-instance
    // only contains self, but not connection.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Node event for a virtual-network object associated with routing-instances.
// There are no other virtual-networks or routing-instances.
//
TEST_F(BgpConfigListenerTest, VirtualNetworkChange1) {

    // Initialize config with 3 routing-instances - red1, red2 and red3.
    // Add virtual-network-routing-instance link between virtual-network red
    // and routing-instances red1, red2, red3.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red1",
        "virtual-network", "red", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red2",
        "virtual-network", "red", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red3",
        "virtual-network", "red", "virtual-network-routing-instance");
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify virtual-network red.
    PauseChangeListPropagation();
    string id_name("red");
    ifmap_test_util::IFMapNodeNotify(&db_, "virtual-network", id_name);
    task_util::WaitForIdle();

    // The virtual-network should be on the change list and on the node list
    // since there's an entry for self in the reaction map.
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The 3 routing-instances get added to the change list because propagate
    // list for self in virtual-network has virtual-network-routing-instance
    // and propagate list for virtual-network-routing-instance contains self.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(4U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("virtual-network"));
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Node event for a virtual-network object associated with routing-instances.
// There are other virtual-networks associated with other routing-instances.
//
TEST_F(BgpConfigListenerTest, VirtualNetworkChange2) {

    // Initialize config with routing-instances red1, red2, blue1, blue2.
    // Add virtual-network-routing-instance link between virtual-network red
    // and routing-instances red1, red2.
    // Add virtual-network-routing-instance link between virtual-network blue
    // and routing-instances blue1, blue2.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_3.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red1",
        "virtual-network", "red", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red2",
        "virtual-network", "red", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "blue1",
        "virtual-network", "blue", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "blue2",
        "virtual-network", "blue", "virtual-network-routing-instance");
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify virtual-network red.
    PauseChangeListPropagation();
    ifmap_test_util::IFMapNodeNotify(&db_, "virtual-network", "red");
    task_util::WaitForIdle();

    // The virtual-network red should be on the change list and on the node
    // list since there's an entry for self in the reaction map.
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The routing-instances red1, red2 get added to change list as propagate
    // list for self in virtual-network has virtual-network-routing-instance
    // and propagate list for virtual-network-routing-instance contains self.
    // The routing-instances blue1 and blue2 are not added to change list.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("virtual-network"));
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify virtual-network blue.
    PauseChangeListPropagation();
    ifmap_test_util::IFMapNodeNotify(&db_, "virtual-network", "blue");
    task_util::WaitForIdle();

    // The virtual-network blue should be on the change list and on the node
    // list since there's an entry for self in the reaction map.
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // The routing-instances blue1, blue2 get added to change list as propagate
    // list for self in virtual-network has virtual-network-routing-instance
    // and propagate list for virtual-network-routing-instance contains self.
    // The routing-instances red1 and red2 are not added to change list.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("virtual-network"));
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Link event for an uninteresting link of a virtual-network object.
//
TEST_F(BgpConfigListenerTest, VirtualNetworkUninterestingLinkChange) {

    // Initialize config with 3 routing-instances - red1, red2 and red3.
    // Add virtual-network-routing-instance link between virtual-network red
    // and routing-instances red1, red2, red3.
    // Add the project-virtual-network link between the project admin and the
    // virtual-network red.
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red1",
        "virtual-network", "red", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red2",
        "virtual-network", "red", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "red3",
        "virtual-network", "red", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgLink(&db_, "project", "admin",
        "virtual-network", "red", "project-virtual-network");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // Pause propagation, notify project-virtual-network link.
    PauseChangeListPropagation();
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "project-virtual-network",
        "project", "admin", "virtual-network", "red");
    task_util::WaitForIdle();

    // Edge list should be empty since both edges are not interesting.
    //
    // The project-virtual-network edge from virtual-network is not interesting
    // as reaction map for virtual-network has no entry project-virtual-network.
    // The project-virtual-network edge from the project is not interesting as
    // the event policy doesn't have an entry for project.
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    // Perform propagation and verify change list.
    // Nothing should get added since the node and edge lists are empty.
    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
}

//
// Validate that routing policy update triggers both routing-instance and
// routing-policy change
//
TEST_F(BgpConfigListenerTest, RoutingPolicyUpdate_0) {
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_11.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    string id_name = "basic_0";
    ifmap_test_util::IFMapNodeNotify(&db_, "routing-policy", id_name);
    task_util::WaitForIdle();

    // Routing policy is added to change list
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-policy"));
    // routing-instance is added to change list
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(0U,
                        GetChangeListCount("routing-policy-routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// Update of routing instance doesn't trigger routing-policy
//
TEST_F(BgpConfigListenerTest, RoutingPolicyUpdate_1) {
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_11.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    string id_name = "test";
    ifmap_test_util::IFMapNodeNotify(&db_, "routing-instance", id_name);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount("routing-policy"));
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(0U,
                        GetChangeListCount("routing-policy-routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// Update of routing-policy-routing-instance triggers routing-instance,
// routing-policy and routing-policy-routing-instance
//
TEST_F(BgpConfigListenerTest, RoutingPolicyUpdate_2) {
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_9.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    content = ReadFile("controller/src/bgp/testdata/config_listener_test_9a.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-policy"));
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(1U,
                        GetChangeListCount("routing-policy-routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    content = ReadFile("controller/src/bgp/testdata/config_listener_test_9.xml");
    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// If routing policy is linked to multiple routing instances,
// a change of routing-policy will put both routing instances into change list
//
TEST_F(BgpConfigListenerTest, RoutingPolicyUpdate_3) {
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_10.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    // Trigger change on routing-policy which is linked to routing instance
    string id_name = "basic_0";
    ifmap_test_util::IFMapNodeNotify(&db_, "routing-policy", id_name);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(4U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-policy"));
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(0U,
                        GetChangeListCount("routing-policy-routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// Trigger edge change between routing policy and routing instance
// In this test, modify link between mid-node(routing-policy-routing-instance)
// routing-policy
// Validate both routing instance and routing policy are added to change list
//
TEST_F(BgpConfigListenerTest, RoutingPolicyUpdate_4) {
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_9.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    string id_name = "attr(basic_0,test)";
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "routing-policy-routing-instance",
        "routing-policy-routing-instance", id_name, "routing-policy", "basic_0");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(2U,
                        GetEdgeListCount("routing-policy-routing-instance"));

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-policy"));
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(0U,
                        GetChangeListCount("routing-policy-routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// Trigger edge change between routing policy and routing instance
// In this test, modify link between mid-node(routing-policy-routing-instance)
// routing-instance
// Validate both routing instance and routing policy are added to change list
//
TEST_F(BgpConfigListenerTest, RoutingPolicyUpdate_5) {
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_9.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    string id_name = "attr(basic_0,test)";
    ifmap_test_util::IFMapLinkNotify(&db_, &graph_, "routing-policy-routing-instance",
        "routing-policy-routing-instance", id_name, "routing-instance", "test");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(2U,
                        GetEdgeListCount("routing-policy-routing-instance"));

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-policy"));
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(0U,
                        GetChangeListCount("routing-policy-routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// Update of IFMapNode representing the routing-policy-routing-instance,
// puts routing-instance and routing-policy to change list along with
// routing-policy-routing-instance
//
TEST_F(BgpConfigListenerTest, RoutingPolicyUpdate_6) {
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_9.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    string id_name = "attr(basic_0,test)";
    ifmap_test_util::IFMapNodeNotify(&db_, "routing-policy-routing-instance",
                                     id_name);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-policy"));
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(1U,
                        GetChangeListCount("routing-policy-routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// If multiple routing policies are linked to a routing instance,
// a change of routing-policy will put only routing instance and routing policy
// to change list
//
TEST_F(BgpConfigListenerTest, RoutingPolicyUpdate_7) {
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_9.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    // Trigger change on routing-policy which is linked to routing instance
    string id_name = "basic_0";
    ifmap_test_util::IFMapNodeNotify(&db_, "routing-policy", id_name);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-policy"));
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(0U,
                        GetChangeListCount("routing-policy-routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// If multiple routing policies are linked to a routing instance,
// a change of routing-instance will not put routing policies
// to change list
//
TEST_F(BgpConfigListenerTest, RoutingPolicyUpdate_8) {
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_9.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    // Trigger change on routing-policy which is linked to routing instance
    string id_name = "test";
    ifmap_test_util::IFMapNodeNotify(&db_, "routing-instance", id_name);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount("routing-policy"));
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(0U,
                        GetChangeListCount("routing-policy-routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// RI1 --> RP1 <-- RI2
// |                |
//  -----> RP2 <----
// Routing instance refers to two routing policies and each routing policy is
// referred by two routing instances
// Now update the one routing policy and verify that both routing instances are
// in change list
//
TEST_F(BgpConfigListenerTest, RoutingPolicyUpdate_9) {
    string content = ReadFile("controller/src/bgp/testdata/config_listener_test_14.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    // Trigger change on routing-policy which is linked to two routing instances
    string id_name = "basic_0";
    ifmap_test_util::IFMapNodeNotify(&db_, "routing-policy", id_name);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(4U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-policy"));
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(0U,
                        GetChangeListCount("routing-policy-routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    // Trigger change on other routing-policy which is linked to
    // two routing instances
    id_name = "basic_1";
    ifmap_test_util::IFMapNodeNotify(&db_, "routing-policy", id_name);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(4U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-policy"));
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-instance"));
    TASK_UTIL_EXPECT_EQ(0U,
                        GetChangeListCount("routing-policy-routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// Validate that routing instance is added to change list when associated
// route-aggregate object is updated
//
TEST_F(BgpConfigListenerTest, RouteAggregate_0) {
    string content = ReadFile("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    // Trigger change on route-aggregate which is linked routing instance
    string id_name = "vn_subnet";
    ifmap_test_util::IFMapNodeNotify(&db_, "route-aggregate", id_name);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("route-aggregate"));
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// Validate that when route-aggregate is unlinked from routing instance,
// routing-instance is added to change list
//
TEST_F(BgpConfigListenerTest, RouteAggregate_1) {
    string content = ReadFile("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    // unlink routing instance and route aggregate
    ifmap_test_util::IFMapMsgUnlink(&db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount("route-aggregate"));
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// Validate that when route-aggregate object is linked to routing instance
// routing instance object is put into change list
//
TEST_F(BgpConfigListenerTest, RouteAggregate_2) {
    string content = ReadFile("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content));

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // unlink routing instance and route aggregate
    ifmap_test_util::IFMapMsgUnlink(&db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    // link routing instance and route aggregate
    ifmap_test_util::IFMapMsgLink(&db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount("route-aggregate"));
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// When route-aggregate object which is not linked to any routing instance is
// updated, no object is added to change list
//
TEST_F(BgpConfigListenerTest, RouteAggregate_3) {
    string content = ReadFile("controller/src/bgp/testdata/route_aggregate_0.xml");
    EXPECT_TRUE(parser_.Parse(content));

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    // unlink routing instance and route aggregate
    ifmap_test_util::IFMapMsgUnlink(&db_, "routing-instance", "test",
        "route-aggregate", "vn_subnet", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    // Trigger change on route-aggregate which is linked routing instance
    string id_name = "vn_subnet";
    ifmap_test_util::IFMapNodeNotify(&db_, "route-aggregate", id_name);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("route-aggregate"));
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// With routing instance linked to multiple route-aggregate object, if one of
// the route-aggregate object is updated, validate that routing-instance is
// added to changed list
//
TEST_F(BgpConfigListenerTest, RouteAggregate_4) {
    string content = ReadFile("controller/src/bgp/testdata/route_aggregate_1.xml");
    EXPECT_TRUE(parser_.Parse(content));

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    // Trigger change on one of the route-aggregate linked to routing instance
    string id_name = "vn_subnet_0";
    ifmap_test_util::IFMapNodeNotify(&db_, "route-aggregate", id_name);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("route-aggregate"));
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// With multiple routing instances linked to one route-aggregated object,
// trigger change on route-aggregate object and validate that both routing
// instances are added to changed list
//
TEST_F(BgpConfigListenerTest, RouteAggregate_5) {
    string content = ReadFile("controller/src/bgp/testdata/route_aggregate_2.xml");
    EXPECT_TRUE(parser_.Parse(content));

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    // Trigger change on one of the route-aggregate linked to routing instance
    string id_name = "vn_subnet_0";
    ifmap_test_util::IFMapNodeNotify(&db_, "route-aggregate", id_name);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(3U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("route-aggregate"));
    TASK_UTIL_EXPECT_EQ(2U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

//
// With multiple routing instances linked to one route-aggregated object,
// unlink route-aggregate object from one of the routing instance and
// validate that only one routing instance is added to changed list
//
TEST_F(BgpConfigListenerTest, RouteAggregate_6) {
    string content = ReadFile("controller/src/bgp/testdata/route_aggregate_2.xml");
    EXPECT_TRUE(parser_.Parse(content));

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    PauseChangeListPropagation();
    // unlink routing instance and route aggregate
    ifmap_test_util::IFMapMsgUnlink(&db_, "routing-instance", "test_0",
        "route-aggregate", "vn_subnet_0", "route-aggregate-routing-instance");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetNodeListCount());
    TASK_UTIL_EXPECT_EQ(1U, GetEdgeListCount());

    PerformChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount());
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount("route-aggregate"));
    TASK_UTIL_EXPECT_EQ(1U, GetChangeListCount("routing-instance"));

    ResumeChangeListPropagation();
    TASK_UTIL_EXPECT_EQ(0U, GetChangeListCount());

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    ::testing::InitGoogleTest(&argc, argv);
    BgpObjectFactory::Register<BgpConfigManager>(
        boost::factory<BgpIfmapConfigManager *>());
    int error = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return error;
}
