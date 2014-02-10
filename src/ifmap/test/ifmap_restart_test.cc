/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_exporter.h"

#include "base/logging.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "io/event_manager.h"
#include "ifmap/autogen.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_object.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_update.h"
#include "ifmap/ifmap_update_queue.h"
#include "ifmap/ifmap_update_sender.h"
#include "ifmap/test/ifmap_test_util.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace boost::asio;
using namespace std;

class TestClient : public IFMapClient {
public:
    TestClient(const string &addr)
        : identifier_(addr) {
    }

    virtual const string &identifier() const {
        return identifier_;
    }

    virtual bool SendUpdate(const std::string &msg) {
        cout << "Sending update\n";
        return true;
    }

private:
    string identifier_;
};

class IFMapUpdateSenderMock : public IFMapUpdateSender {
public:
    // Use the original server and its queue
    IFMapUpdateSenderMock(IFMapServer *server) : 
        IFMapUpdateSender(server, server->queue()) {
    }
    virtual void QueueActive() { return; }
    virtual void SendActive(int index) { return; }
};

class IFMapServerTest : public IFMapServer {
public:
    IFMapServerTest(DB *db, DBGraph *graph, boost::asio::io_service *io_service)
        : IFMapServer(db, graph, io_service), sequence_number_(0) {
    }
    void SetSender(IFMapUpdateSender *sender) {
        // sender_ accessible since we are friends with base class
        sender_.reset(sender);
    }
    // Override base class routine to return our own seq-num
    virtual uint64_t get_ifmap_channel_sequence_number() {
        return sequence_number_;
    }
    void set_ifmap_channel_sequence_number(uint64_t seq_num) {
        sequence_number_ = seq_num;
    }

private:
    uint64_t sequence_number_;
};

class IFMapRestartTest : public ::testing::Test {
protected:
    IFMapRestartTest()
        : server_(&db_, &graph_, evm_.io_service()),
          exporter_(server_.exporter()) {
    }

    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &graph_);
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        exporter_->Initialize(&db_);
    }

    virtual void TearDown() {
        exporter_->Shutdown();
        task_util::WaitForIdle();
        IFMapLinkTable_Clear(&db_);
        IFMapTable::ClearTables(&db_);
        task_util::WaitForIdle();
        db_.Clear();
        evm_.Shutdown();
    }

    void IFMapMsgLink(const string &ltype, const string &rtype,
                      const string &lid, const string &rid,
                      uint64_t sequence_number) {
        string metadata = ltype + "-" + rtype;
        ifmap_test_util::IFMapMsgLink(&db_, ltype, lid, rtype, rid, metadata,
                                      sequence_number);
    }

    void IFMapMsgUnlink(const string &ltype, const string &rtype,
                        const string &lid, const string &rid) {
        string metadata = ltype + "-" + rtype;
        ifmap_test_util::IFMapMsgUnlink(&db_, ltype, lid, rtype, rid, metadata);
    }

    void IFMapMsgPropertyAdd(const string &type, const string &id,
                             const string &metadata, AutogenProperty *content,
                             uint64_t sequence_number) {
        ifmap_test_util::IFMapMsgPropertyAdd(&db_, type, id, metadata, content,
                                             sequence_number);
    }

    void IFMapMsgPropertyDelete(const string &type, const string &id,
                                const string &metadata) {
        ifmap_test_util::IFMapMsgPropertyDelete(&db_, type, id, metadata);
    }

    IFMapNode *TableLookup(const string &type, const string &name) {
        IFMapTable *tbl = IFMapTable::FindTable(&db_, type);
        if (tbl == NULL) {
            return NULL;
        }
        return tbl->FindNode(name);
    }

    void StaleNodesProcTimeout() {
        server_.StaleNodesProcTimeout();
    }

    DB db_;
    DBGraph graph_;
    EventManager evm_;
    IFMapServerTest server_;
    IFMapExporter *exporter_;
};

TEST_F(IFMapRestartTest, BasicTest) {
    server_.SetSender(new IFMapUpdateSenderMock(&server_));
    TestClient c1("192.168.1.1");
    server_.ClientRegister(&c1);

    // Create nodes and links with seq-num 1
    IFMapMsgPropertyAdd("domain", "user1", "d-u1", new AutogenProperty(), 1);
    IFMapMsgPropertyAdd("project", "vnc", "p-v", new AutogenProperty(), 1);
    IFMapMsgLink("domain", "project", "user1", "vnc", 1);
    task_util::WaitForIdle();

    IFMapNode *idn1 = TableLookup("domain", "user1");
    ASSERT_TRUE(idn1 != NULL);
    TASK_UTIL_EXPECT_TRUE(
        idn1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    IFMapObject *obj = idn1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    ASSERT_TRUE(obj != NULL);
    ASSERT_TRUE(obj->sequence_number() == 1);

    IFMapNode *idn2 = TableLookup("project", "vnc");
    ASSERT_TRUE(idn2 != NULL);
    TASK_UTIL_EXPECT_TRUE(
        idn2->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    obj = idn2->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    ASSERT_TRUE(obj != NULL);
    ASSERT_TRUE(obj->sequence_number() == 1);

    bool exists = false;
    IFMapLink *ifl = static_cast<IFMapLink *>(graph_.GetEdge(idn1, idn2));
    ASSERT_TRUE(ifl != NULL);
    ASSERT_TRUE(ifl->sequence_number(IFMapOrigin::MAP_SERVER, &exists) == 1);

    // Update nodes and links with seq-num 2
    IFMapMsgPropertyAdd("domain", "user1", "d-u1", new AutogenProperty(), 2);
    IFMapMsgPropertyAdd("project", "vnc", "p-v", new AutogenProperty(), 2);
    IFMapMsgLink("domain", "project", "user1", "vnc", 2);
    task_util::WaitForIdle();

    // All the nodes should exist
    idn1 = TableLookup("domain", "user1");
    ASSERT_TRUE(idn1 != NULL);
    TASK_UTIL_EXPECT_TRUE(
        idn1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    obj = idn1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    ASSERT_TRUE(obj != NULL);
    ASSERT_TRUE(obj->sequence_number() == 2);

    idn2 = TableLookup("project", "vnc");
    ASSERT_TRUE(idn2 != NULL);
    TASK_UTIL_EXPECT_TRUE(
        idn2->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    obj = idn2->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    ASSERT_TRUE(obj != NULL);
    ASSERT_TRUE(obj->sequence_number() == 2);

    ifl = static_cast<IFMapLink *>(graph_.GetEdge(idn1, idn2));
    ASSERT_TRUE(ifl != NULL);
    ASSERT_TRUE(ifl->sequence_number(IFMapOrigin::MAP_SERVER, &exists) == 2);

    // Update the channel's seq-num to 2 and trigger cleanup
    server_.set_ifmap_channel_sequence_number(2);
    StaleNodesProcTimeout();
    task_util::WaitForIdle();

    // The nodes had seq-num 2 and all of them should still exist after cleanup
    idn1 = TableLookup("domain", "user1");
    ASSERT_TRUE(idn1 != NULL);
    idn2 = TableLookup("project", "vnc");
    ASSERT_TRUE(idn2 != NULL);
    ifl = static_cast<IFMapLink *>(graph_.GetEdge(idn1, idn2));
    ASSERT_TRUE(ifl != NULL);

    // Update the channel's seq-num to 3 and trigger cleanup
    server_.set_ifmap_channel_sequence_number(3);
    StaleNodesProcTimeout();
    task_util::WaitForIdle();

    // Nodes had seq-num 2 and all of them should be gone
    idn1 = TableLookup("domain", "user1");
    ASSERT_TRUE(idn1 == NULL);
    idn2 = TableLookup("project", "vnc");
    ASSERT_TRUE(idn2 == NULL);
}

// create 3 nodes and 2 links. Update seq-num for 2 nodes and 1 link. The
// remaining should be cleaned up.
TEST_F(IFMapRestartTest, BasicTest1) {
    server_.SetSender(new IFMapUpdateSenderMock(&server_));
    TestClient c1("192.168.1.1");
    server_.ClientRegister(&c1);

    // Create nodes and links with seq-num 1
    IFMapMsgPropertyAdd("domain", "user1", "d-u1", new AutogenProperty(), 1);
    IFMapMsgPropertyAdd("project", "vnc", "p-v", new AutogenProperty(), 1);
    IFMapMsgLink("domain", "project", "user1", "vnc", 1);

    IFMapMsgPropertyAdd("virtual-network", "blue", "v-b",
                        new AutogenProperty(), 1);
    IFMapMsgLink("project", "virtual-network", "vnc", "blue", 1);
    task_util::WaitForIdle();

    IFMapNode *idn1 = TableLookup("domain", "user1");
    ASSERT_TRUE(idn1 != NULL);
    IFMapNode *idn2 = TableLookup("project", "vnc");
    ASSERT_TRUE(idn2 != NULL);
    IFMapNode *idn3 = TableLookup("virtual-network", "blue");
    ASSERT_TRUE(idn3 != NULL);

    bool exists = false;
    IFMapLink *ifl = static_cast<IFMapLink *>(graph_.GetEdge(idn1, idn2));
    ASSERT_TRUE(ifl != NULL);
    ASSERT_TRUE(ifl->sequence_number(IFMapOrigin::MAP_SERVER, &exists) == 1);
    ifl = static_cast<IFMapLink *>(graph_.GetEdge(idn2, idn3));
    ASSERT_TRUE(ifl != NULL);
    ASSERT_TRUE(ifl->sequence_number(IFMapOrigin::MAP_SERVER, &exists) == 1);

    // Update domain-project part of graph with seq-num 2. Dont update the
    // virtual-network part.
    IFMapMsgPropertyAdd("domain", "user1", "d-u1", new AutogenProperty(), 2);
    IFMapMsgPropertyAdd("project", "vnc", "p-v", new AutogenProperty(), 2);
    IFMapMsgLink("domain", "project", "user1", "vnc", 2);

    // All the nodes should still exist
    idn1 = TableLookup("domain", "user1");
    ASSERT_TRUE(idn1 != NULL);
    idn2 = TableLookup("project", "vnc");
    ASSERT_TRUE(idn2 != NULL);
    idn3 = TableLookup("virtual-network", "blue");
    ASSERT_TRUE(idn3 != NULL);
    ifl = static_cast<IFMapLink *>(graph_.GetEdge(idn1, idn2));
    ASSERT_TRUE(ifl != NULL);
    ifl = static_cast<IFMapLink *>(graph_.GetEdge(idn2, idn3));
    ASSERT_TRUE(ifl != NULL);

    // Update the channel's seq-num to 2 and trigger cleanup
    server_.set_ifmap_channel_sequence_number(2);
    StaleNodesProcTimeout();
    task_util::WaitForIdle();

    // The nodes with seq-num 1 should be gone i.e. virtual-network blue
    idn1 = TableLookup("domain", "user1");
    ASSERT_TRUE(idn1 != NULL);
    idn2 = TableLookup("project", "vnc");
    ASSERT_TRUE(idn2 != NULL);
    ifl = static_cast<IFMapLink *>(graph_.GetEdge(idn1, idn2));
    ASSERT_TRUE(ifl != NULL);

    idn3 = TableLookup("virtual-network", "blue");
    ASSERT_TRUE(idn3 == NULL);

    IFMapMsgUnlink("domain", "project", "user1", "vnc");
    IFMapMsgPropertyDelete("domain", "user1", "d-u1");
    IFMapMsgPropertyDelete("project", "vnc", "p-v");
    task_util::WaitForIdle();
    idn1 = TableLookup("domain", "user1");
    ASSERT_TRUE(idn1 == NULL);
    idn2 = TableLookup("project", "vnc");
    ASSERT_TRUE(idn2 == NULL);
}

TEST_F(IFMapRestartTest, PropertiesTest) {
    server_.SetSender(new IFMapUpdateSenderMock(&server_));
    TestClient c1("192.168.1.1");
    server_.ClientRegister(&c1);

    autogen::DomainLimitsType *dl = new autogen::DomainLimitsType();
    autogen::ApiAccessListType *aal = new autogen::ApiAccessListType();

    // Create node with seq-num 1 and 2 properties
    IFMapMsgPropertyAdd("domain", "user1", "domain-limits", dl, 1);
    IFMapMsgPropertyAdd("domain", "user1", "api-access-list", aal, 1);
    task_util::WaitForIdle();

    IFMapNode *idn1 = TableLookup("domain", "user1");
    ASSERT_TRUE(idn1 != NULL);
    TASK_UTIL_EXPECT_TRUE(
        idn1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    IFMapObject *obj = idn1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    ASSERT_TRUE(obj != NULL);
    ASSERT_TRUE(obj->sequence_number() == 1);

    autogen::Domain *domain = dynamic_cast<autogen::Domain *>(obj);
    ASSERT_TRUE(domain != NULL);
    TASK_UTIL_EXPECT_TRUE(domain->IsPropertySet(autogen::Domain::LIMITS));
    bool bret = domain->IsPropertySet(autogen::Domain::LIMITS);
    ASSERT_TRUE(bret == true);
    TASK_UTIL_EXPECT_TRUE(
        domain->IsPropertySet(autogen::Domain::API_ACCESS_LIST));
    bret = domain->IsPropertySet(autogen::Domain::API_ACCESS_LIST);
    ASSERT_TRUE(bret == true);

    // Update the node with seq-num 2 and 1 property i.e. access-list is gone
    dl = new autogen::DomainLimitsType();
    IFMapMsgPropertyAdd("domain", "user1", "domain-limits", dl, 2);
    task_util::WaitForIdle();

    // Update the channel's seq-num to 2 and trigger cleanup
    server_.set_ifmap_channel_sequence_number(2);
    StaleNodesProcTimeout();
    task_util::WaitForIdle();

    idn1 = TableLookup("domain", "user1");
    ASSERT_TRUE(idn1 != NULL);

    obj = idn1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    ASSERT_TRUE(obj != NULL);
    ASSERT_TRUE(obj->sequence_number() == 2);

    // LIMITS should be set but API_ACCESS_LIST should not be
    domain = dynamic_cast<autogen::Domain *>(obj);
    ASSERT_TRUE(domain != NULL);
    TASK_UTIL_EXPECT_TRUE(domain->IsPropertySet(autogen::Domain::LIMITS));
    bret = domain->IsPropertySet(autogen::Domain::LIMITS);
    ASSERT_TRUE(bret == true);
    TASK_UTIL_EXPECT_FALSE(
        domain->IsPropertySet(autogen::Domain::API_ACCESS_LIST));
    bret = domain->IsPropertySet(autogen::Domain::API_ACCESS_LIST);
    ASSERT_TRUE(bret == false);

    IFMapMsgPropertyDelete("domain", "user1", "domain-limits");
    task_util::WaitForIdle();
    idn1 = TableLookup("domain", "user1");
    ASSERT_TRUE(idn1 == NULL);

    task_util::WaitForIdle();
}

TEST_F(IFMapRestartTest, LinkAttr) {
    server_.SetSender(new IFMapUpdateSenderMock(&server_));
    TestClient c1("192.168.1.1");
    server_.ClientRegister(&c1);

    // Create nodes and links with seq-num 1
    IFMapMsgPropertyAdd("virtual-network", "vn1", "vn-vn1",
                        new AutogenProperty(), 1);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-network", "vn1") != NULL);
    IFMapNode *left = TableLookup("virtual-network", "vn1");
    EXPECT_TRUE(left != NULL);
    TASK_UTIL_EXPECT_TRUE(
        left->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    IFMapObject *obj = left->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
    EXPECT_TRUE(obj->sequence_number() == 1);

    IFMapMsgPropertyAdd("network-ipam", "ipam1", "ipam-ipam1",
                        new AutogenProperty(), 1);
    TASK_UTIL_EXPECT_TRUE(TableLookup("network-ipam", "ipam1") != NULL);
    IFMapNode *right = TableLookup("network-ipam", "ipam1");
    EXPECT_TRUE(right != NULL);
    TASK_UTIL_EXPECT_TRUE(
        right->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    obj = right->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
    EXPECT_TRUE(obj->sequence_number() == 1);

    IFMapMsgLink("virtual-network", "network-ipam", "vn1", "ipam1", 1);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-network-network-ipam",
                                      "attr(ipam1,vn1)") != NULL);
    IFMapNode *midnode = TableLookup("virtual-network-network-ipam",
                                     "attr(ipam1,vn1)");
    EXPECT_TRUE(midnode != NULL);

    bool exists = false;
    IFMapLink *ifl = static_cast<IFMapLink *>(graph_.GetEdge(left, midnode));
    EXPECT_TRUE(ifl != NULL);
    EXPECT_TRUE(ifl->sequence_number(IFMapOrigin::MAP_SERVER, &exists) == 1);
    ifl = static_cast<IFMapLink *>(graph_.GetEdge(midnode, right));
    EXPECT_TRUE(ifl != NULL);
    EXPECT_TRUE(ifl->sequence_number(IFMapOrigin::MAP_SERVER, &exists) == 1);

    // Update the channel's seq-num to 2 and trigger cleanup
    server_.set_ifmap_channel_sequence_number(2);
    StaleNodesProcTimeout();
    task_util::WaitForIdle();

    // Nodes had seq-num 2 and all of them should be gone
    left = TableLookup("virtual-network", "vn1");
    EXPECT_TRUE(left == NULL);
    right = TableLookup("network-ipam", "ipam1");
    EXPECT_TRUE(right == NULL);
    midnode = TableLookup("virtual-network-network-ipam", "attr(ipam1,vn1)");
    EXPECT_TRUE(midnode == NULL);

    task_util::WaitForIdle();
}

TEST_F(IFMapRestartTest, LinkAttrWithProperties) {
    server_.SetSender(new IFMapUpdateSenderMock(&server_));
    TestClient c1("192.168.1.1");
    server_.ClientRegister(&c1);

    // Create vn node with 2 properties and seq-num 1
    autogen::VirtualNetworkType *vnt = new autogen::VirtualNetworkType();
    autogen::IdPermsType *ipt = new autogen::IdPermsType();
    IFMapMsgPropertyAdd("virtual-network", "vn1", "virtual-network-properties",
                        vnt, 1);
    IFMapMsgPropertyAdd("virtual-network", "vn1", "id-perms", ipt, 1);

    // Check if the node and object exist
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-network", "vn1") != NULL);
    IFMapNode *left = TableLookup("virtual-network", "vn1");
    EXPECT_TRUE(left != NULL);
    TASK_UTIL_EXPECT_TRUE(
        left->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    IFMapObject *obj = left->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
    EXPECT_TRUE(obj->sequence_number() == 1);

    // Check if the properties are set
    autogen::VirtualNetwork *vn = dynamic_cast<autogen::VirtualNetwork *>(obj);
    EXPECT_TRUE(vn != NULL);
    TASK_UTIL_EXPECT_TRUE(
        vn->IsPropertySet(autogen::VirtualNetwork::PROPERTIES));
    bool bret = vn->IsPropertySet(autogen::VirtualNetwork::PROPERTIES);
    EXPECT_TRUE(bret == true);
    TASK_UTIL_EXPECT_TRUE(vn->IsPropertySet(autogen::VirtualNetwork::ID_PERMS));
    bret = vn->IsPropertySet(autogen::VirtualNetwork::ID_PERMS);
    EXPECT_TRUE(bret == true);

    // Create nw-ipam node with 2 properties and seq-num 1
    autogen::IpamType *ipamt = new autogen::IpamType();
    autogen::IdPermsType *ipt2 = new autogen::IdPermsType();
    IFMapMsgPropertyAdd("network-ipam", "ipam1", "network-ipam-mgmt", ipamt, 1);
    IFMapMsgPropertyAdd("network-ipam", "ipam1", "id-perms", ipt2, 1);

    // Check if the node and object exist
    TASK_UTIL_EXPECT_TRUE(TableLookup("network-ipam", "ipam1") != NULL);
    IFMapNode *right = TableLookup("network-ipam", "ipam1");
    EXPECT_TRUE(right != NULL);
    TASK_UTIL_EXPECT_TRUE(
        right->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    obj = right->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
    EXPECT_TRUE(obj->sequence_number() == 1);

    // Check if the properties are set
    autogen::NetworkIpam *nwipam = dynamic_cast<autogen::NetworkIpam *>(obj);
    EXPECT_TRUE(nwipam != NULL);
    TASK_UTIL_EXPECT_TRUE(nwipam->IsPropertySet(autogen::NetworkIpam::MGMT));
    bret = nwipam->IsPropertySet(autogen::NetworkIpam::MGMT);
    EXPECT_TRUE(bret == true);
    TASK_UTIL_EXPECT_TRUE(
        nwipam->IsPropertySet(autogen::NetworkIpam::ID_PERMS));
    bret = nwipam->IsPropertySet(autogen::NetworkIpam::ID_PERMS);
    EXPECT_TRUE(bret == true);

    // Add a link. Check if the midnode exists.
    IFMapMsgLink("virtual-network", "network-ipam", "vn1", "ipam1", 1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-network-network-ipam",
                                      "attr(ipam1,vn1)") != NULL);
    IFMapNode *midnode = TableLookup("virtual-network-network-ipam",
                                     "attr(ipam1,vn1)");
    EXPECT_TRUE(midnode != NULL);

    // Check if the 2 links exist.
    bool exists = false;
    IFMapLink *ifl = static_cast<IFMapLink *>(graph_.GetEdge(left, midnode));
    EXPECT_TRUE(ifl != NULL);
    EXPECT_TRUE(ifl->sequence_number(IFMapOrigin::MAP_SERVER, &exists) == 1);
    ifl = static_cast<IFMapLink *>(graph_.GetEdge(midnode, right));
    EXPECT_TRUE(ifl != NULL);
    EXPECT_TRUE(ifl->sequence_number(IFMapOrigin::MAP_SERVER, &exists) == 1);

    // Update the nodes with seq-num 2 and remove 1 property each
    ipt = new autogen::IdPermsType();
    IFMapMsgPropertyAdd("virtual-network", "vn1", "id-perms", ipt, 2);
    ipt2 = new autogen::IdPermsType();
    IFMapMsgPropertyAdd("network-ipam", "ipam1", "id-perms", ipt2, 2);
    task_util::WaitForIdle();
    IFMapMsgLink("virtual-network", "network-ipam", "vn1", "ipam1", 2);
    task_util::WaitForIdle();

    // Update the channel's seq-num to 2 and trigger cleanup
    server_.set_ifmap_channel_sequence_number(2);
    StaleNodesProcTimeout();
    task_util::WaitForIdle();

    // Sequence numbers match. Everything should exist.
    left = TableLookup("virtual-network", "vn1");
    EXPECT_TRUE(left != NULL);
    right = TableLookup("network-ipam", "ipam1");
    EXPECT_TRUE(right != NULL);
    midnode = TableLookup("virtual-network-network-ipam", "attr(ipam1,vn1)");
    EXPECT_TRUE(midnode != NULL);

    task_util::WaitForIdle();

    // Update only the vn with seq-num 3
    ipt = new autogen::IdPermsType();
    IFMapMsgPropertyAdd("virtual-network", "vn1", "id-perms", ipt, 3);

    // Update the channel's seq-num to 3 and trigger cleanup
    server_.set_ifmap_channel_sequence_number(3);
    StaleNodesProcTimeout();
    task_util::WaitForIdle();

    // Only the vn should exist. We should not find the other nodes.
    left = TableLookup("virtual-network", "vn1");
    EXPECT_TRUE(left != NULL);
    right = TableLookup("network-ipam", "ipam1");
    EXPECT_TRUE(right == NULL);
    midnode = TableLookup("virtual-network-network-ipam", "attr(ipam1,vn1)");
    EXPECT_TRUE(midnode == NULL);

    task_util::WaitForIdle();

    // Update the channel's seq-num to 4 and trigger cleanup
    server_.set_ifmap_channel_sequence_number(4);
    StaleNodesProcTimeout();
    task_util::WaitForIdle();

    // The vn should also be cleaned up.
    left = TableLookup("virtual-network", "vn1");
    EXPECT_TRUE(left == NULL);

    task_util::WaitForIdle();
}

TEST_F(IFMapRestartTest, MultipleAttrChangesWithSeqNumChange) {
    server_.SetSender(new IFMapUpdateSenderMock(&server_));
    TestClient c1("192.168.1.1");
    server_.ClientRegister(&c1);

    // Create vn node with 2 properties and seq-num 1
    autogen::VirtualNetworkType *vnt = new autogen::VirtualNetworkType();
    autogen::IdPermsType *ipt = new autogen::IdPermsType();
    IFMapMsgPropertyAdd("virtual-network", "vn1", "virtual-network-properties",
                        vnt, 1);
    IFMapMsgPropertyAdd("virtual-network", "vn1", "id-perms", ipt, 1);

    // Check if the node and object exist
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-network", "vn1") != NULL);
    IFMapNode *left = TableLookup("virtual-network", "vn1");
    EXPECT_TRUE(left != NULL);
    TASK_UTIL_EXPECT_TRUE(
        left->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    IFMapObject *obj = left->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
    EXPECT_TRUE(obj->sequence_number() == 1);

    // Check if the properties are set
    autogen::VirtualNetwork *vn = dynamic_cast<autogen::VirtualNetwork *>(obj);
    EXPECT_TRUE(vn != NULL);
    TASK_UTIL_EXPECT_TRUE(
        vn->IsPropertySet(autogen::VirtualNetwork::PROPERTIES));
    bool bret = vn->IsPropertySet(autogen::VirtualNetwork::PROPERTIES);
    EXPECT_TRUE(bret == true);
    TASK_UTIL_EXPECT_TRUE(vn->IsPropertySet(autogen::VirtualNetwork::ID_PERMS));
    bret = vn->IsPropertySet(autogen::VirtualNetwork::ID_PERMS);
    EXPECT_TRUE(bret == true);

    // Create nw-ipam node with 2 properties and seq-num 1
    autogen::IpamType *ipamt = new autogen::IpamType();
    autogen::IdPermsType *ipt2 = new autogen::IdPermsType();
    IFMapMsgPropertyAdd("network-ipam", "ipam1", "network-ipam-mgmt", ipamt, 1);
    IFMapMsgPropertyAdd("network-ipam", "ipam1", "id-perms", ipt2, 1);

    // Check if the node and object exist
    TASK_UTIL_EXPECT_TRUE(TableLookup("network-ipam", "ipam1") != NULL);
    IFMapNode *right = TableLookup("network-ipam", "ipam1");
    EXPECT_TRUE(right != NULL);
    TASK_UTIL_EXPECT_TRUE(
        right->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    obj = right->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);
    EXPECT_TRUE(obj->sequence_number() == 1);

    // Check if the properties are set
    autogen::NetworkIpam *nwipam = dynamic_cast<autogen::NetworkIpam *>(obj);
    EXPECT_TRUE(nwipam != NULL);
    TASK_UTIL_EXPECT_TRUE(nwipam->IsPropertySet(autogen::NetworkIpam::MGMT));
    bret = nwipam->IsPropertySet(autogen::NetworkIpam::MGMT);
    EXPECT_TRUE(bret == true);
    TASK_UTIL_EXPECT_TRUE(
        nwipam->IsPropertySet(autogen::NetworkIpam::ID_PERMS));
    bret = nwipam->IsPropertySet(autogen::NetworkIpam::ID_PERMS);
    EXPECT_TRUE(bret == true);

    // Add a link. Check if the midnode exists.
    IFMapMsgLink("virtual-network", "network-ipam", "vn1", "ipam1", 1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-network-network-ipam",
                                      "attr(ipam1,vn1)") != NULL);
    IFMapNode *midnode = TableLookup("virtual-network-network-ipam",
                                     "attr(ipam1,vn1)");
    EXPECT_TRUE(midnode != NULL);

    // Check if the 2 links exist.
    bool exists = false;
    IFMapLink *ifl = static_cast<IFMapLink *>(graph_.GetEdge(left, midnode));
    EXPECT_TRUE(ifl != NULL);
    EXPECT_TRUE(ifl->sequence_number(IFMapOrigin::MAP_SERVER, &exists) == 1);
    ifl = static_cast<IFMapLink *>(graph_.GetEdge(midnode, right));
    EXPECT_TRUE(ifl != NULL);
    EXPECT_TRUE(ifl->sequence_number(IFMapOrigin::MAP_SERVER, &exists) == 1);

    // Update the nodes with seq-num 2 and remove 1 property each
    ipt = new autogen::IdPermsType();
    IFMapMsgPropertyAdd("virtual-network", "vn1", "id-perms", ipt, 2);
    ipt2 = new autogen::IdPermsType();
    IFMapMsgPropertyAdd("network-ipam", "ipam1", "id-perms", ipt2, 2);
    task_util::WaitForIdle();
    IFMapMsgLink("virtual-network", "network-ipam", "vn1", "ipam1", 2);
    task_util::WaitForIdle();

    // Update the channel's seq-num to 2 and trigger cleanup
    server_.set_ifmap_channel_sequence_number(2);
    StaleNodesProcTimeout();
    task_util::WaitForIdle();

    // Sequence numbers match. Everything should exist.
    left = TableLookup("virtual-network", "vn1");
    EXPECT_TRUE(left != NULL);
    right = TableLookup("network-ipam", "ipam1");
    EXPECT_TRUE(right != NULL);
    midnode = TableLookup("virtual-network-network-ipam", "attr(ipam1,vn1)");
    EXPECT_TRUE(midnode != NULL);

    task_util::WaitForIdle();

    // Update the VN with seq-num 3 and 1 property
    ipt = new autogen::IdPermsType();
    IFMapMsgPropertyAdd("virtual-network", "vn1", "id-perms", ipt, 3);
    IFMapMsgLink("virtual-network", "network-ipam", "vn1", "ipam1", 3);
    task_util::WaitForIdle();

    // Update the channel's seq-num to 3 and trigger cleanup
    server_.set_ifmap_channel_sequence_number(3);
    StaleNodesProcTimeout();
    task_util::WaitForIdle();

    // All the nodes should exist
    left = TableLookup("virtual-network", "vn1");
    EXPECT_TRUE(left != NULL);
    right = TableLookup("network-ipam", "ipam1");
    EXPECT_TRUE(right != NULL);
    TASK_UTIL_EXPECT_TRUE(
        right->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) == NULL);
    midnode = TableLookup("virtual-network-network-ipam", "attr(ipam1,vn1)");
    EXPECT_TRUE(midnode != NULL);

    task_util::WaitForIdle();

    // Update the channel's seq-num to 4 and trigger cleanup
    server_.set_ifmap_channel_sequence_number(4);
    StaleNodesProcTimeout();
    task_util::WaitForIdle();

    // All the nodes should be cleaned up
    left = TableLookup("virtual-network", "vn1");
    EXPECT_TRUE(left == NULL);
    right = TableLookup("network-ipam", "ipam1");
    EXPECT_TRUE(right == NULL);
    midnode = TableLookup("virtual-network-network-ipam", "attr(ipam1,vn1)");
    EXPECT_TRUE(midnode == NULL);

    task_util::WaitForIdle();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    bool success = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return success;
}
