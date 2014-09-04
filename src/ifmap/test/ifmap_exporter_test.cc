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
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_update.h"
#include "ifmap/ifmap_update_queue.h"
#include "ifmap/ifmap_update_sender.h"
#include "ifmap/test/ifmap_test_util.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

#include <iostream>
#include <fstream>

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
        : IFMapServer(db, graph, io_service) {
    }
    void SetSender(IFMapUpdateSender *sender) {
        sender_.reset(sender);
    }
};

class IFMapExporterTest : public ::testing::Test {
protected:
    IFMapExporterTest()
            : server_(&db_, &graph_, evm_.io_service()),
              exporter_(server_.exporter()), parser_(NULL) {
    }

    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &graph_);
        parser_ = IFMapServerParser::GetInstance("vnc_cfg");
        vnc_cfg_ParserInit(parser_);
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        bgp_schema_ParserInit(parser_);
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

    void IFMapMsgLink(const string &ltype, const string &rtype,
                  const string &lid, const string &rid) {
        string metadata = ltype + "-" + rtype;
        ifmap_test_util::IFMapMsgLink(&db_, ltype, lid, rtype, rid, metadata);
    }

    void IFMapMsgUnlink(const string &ltype, const string &rtype,
                  const string &lid, const string &rid) {
        string metadata = ltype + "-" + rtype;
        ifmap_test_util::IFMapMsgUnlink(&db_, ltype, lid, rtype, rid, metadata);
    }

    void IFMapMsgNodeAdd(const string &type, const string &id,
                         uint64_t sequence_number, const string &metadata,
                         AutogenProperty *content) {
        ifmap_test_util::IFMapMsgNodeAdd(&db_, type, id, sequence_number,
                                         metadata, content);
    }

    void IFMapMsgNodeDelete(const string &type, const string &id,
                         uint64_t sequence_number, const string &metadata,
                         AutogenProperty *content) {
        ifmap_test_util::IFMapMsgNodeDelete(&db_, type, id, sequence_number,
                                         metadata, content);
    }

    IFMapNode *TableLookup(const string &type, const string &name) {
        IFMapTable *tbl = IFMapTable::FindTable(&db_, type);
        if (tbl == NULL) {
            return NULL;
        }
        return tbl->FindNode(name);
    }

    // Read all the updates in the queue and consider them sent.
    void ProcessQueue() {
        IFMapUpdateQueue *queue = server_.queue();
        IFMapListEntry *next = NULL;
        for (IFMapListEntry *iter = queue->tail_marker(); iter != NULL;
             iter = next) {
            next = queue->Next(iter);
            if (iter->type == IFMapListEntry::MARKER) {
                continue;
            }
            IFMapUpdate *update = static_cast<IFMapUpdate *>(iter);
            update->AdvertiseReset(update->advertise());
            queue->Dequeue(update);
            exporter_->StateUpdateOnDequeue(update, update->advertise(),
                                            update->IsDelete());
        }
    }

    DB db_;
    DBGraph graph_;
    EventManager evm_;
    IFMapServerTest server_;
    IFMapExporter *exporter_;
    IFMapServerParser *parser_;
};

static string FileRead(const string &filename) {
    ifstream file(filename.c_str());
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

TEST_F(IFMapExporterTest, Basic) {
    server_.SetSender(new IFMapUpdateSenderMock(&server_));
    TestClient c1("192.168.1.1");
    server_.ClientRegister(&c1);

    IFMapMsgLink("domain", "project", "user1", "vnc");
    IFMapMsgLink("project", "virtual-network", "vnc", "blue");
    IFMapMsgLink("project", "virtual-network", "vnc", "red");
    IFMapMsgLink("virtual-machine", "virtual-machine-interface", 
                 "vm_x", "vm_x:veth0");
    IFMapMsgLink("virtual-machine-interface", "virtual-network", 
                 "vm_x:veth0", "blue");

    task_util::WaitForIdle();
    IFMapNode *idn = TableLookup("virtual-network", "blue");
    ASSERT_TRUE(idn != NULL);
    IFMapNodeState *state = exporter_->NodeStateLookup(idn);
    if (state != NULL) {
        EXPECT_TRUE(state->interest().empty());
    }

    IFMapMsgLink("virtual-router", "virtual-machine",
                 "192.168.1.1", "vm_x");

    task_util::WaitForIdle();
    
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    EXPECT_FALSE(state->interest().empty());

    IFMapMsgUnlink("virtual-router", "virtual-machine",
                   "192.168.1.1", "vm_x");

    task_util::WaitForIdle();

    idn = TableLookup("virtual-network", "blue");
    if (idn != NULL) {
        state = exporter_->NodeStateLookup(idn);
        if (state != NULL) {
            EXPECT_TRUE(state->interest().empty());
            EXPECT_TRUE(state->update_list().empty());
        }
    }
}

// interest change: subgraph was to be sent to a subset of peers and that
// subset changes (overlapping and non overlapping case).
TEST_F(IFMapExporterTest, InterestChangeIntersect) {
    server_.SetSender(new IFMapUpdateSenderMock(&server_));
    TestClient c1("192.168.1.1");
    TestClient c2("192.168.1.2");
    TestClient c3("192.168.1.3");
    TestClient c4("192.168.1.4");

    server_.ClientRegister(&c1);
    server_.ClientRegister(&c2);
    server_.ClientRegister(&c3);
    server_.ClientRegister(&c4);

    IFMapMsgLink("domain", "project", "user1", "vnc");
    IFMapMsgLink("project", "virtual-network", "vnc", "blue");
    IFMapMsgLink("project", "virtual-network", "vnc", "red");
    IFMapMsgLink("virtual-machine", "virtual-machine-interface", 
                 "vm_x", "vm_x:veth0");
    IFMapMsgLink("virtual-machine-interface", "virtual-network", 
                 "vm_x:veth0", "blue");
    IFMapMsgLink("virtual-machine", "virtual-machine-interface", 
                 "vm_w", "vm_w:veth0");
    IFMapMsgLink("virtual-machine-interface", "virtual-network", 
                 "vm_w:veth0", "red");
    IFMapMsgLink("virtual-machine", "virtual-machine-interface", 
                 "vm_y", "vm_y:veth0");
    IFMapMsgLink("virtual-machine-interface", "virtual-network", 
                 "vm_y:veth0", "blue");
    IFMapMsgLink("virtual-machine", "virtual-machine-interface", 
                 "vm_z", "vm_z:veth0");
    IFMapMsgLink("virtual-machine-interface", "virtual-network", 
                 "vm_z:veth0", "red");

    IFMapMsgLink("virtual-router", "virtual-machine", "192.168.1.1", "vm_x");
    IFMapMsgLink("virtual-router", "virtual-machine", "192.168.1.2", "vm_w");
    IFMapMsgLink("virtual-router", "virtual-machine", "192.168.1.3", "vm_y");
    task_util::WaitForIdle();

    IFMapNode *blue = TableLookup("virtual-network", "blue");
    ASSERT_TRUE(blue != NULL);
    IFMapNodeState *state = exporter_->NodeStateLookup(blue);
    ASSERT_TRUE(state != NULL);

    IFMapUpdate *update = state->GetUpdate(IFMapListEntry::UPDATE);
    ASSERT_TRUE(update != NULL);
    TASK_UTIL_EXPECT_TRUE(update->advertise().test(c1.index()));
    TASK_UTIL_EXPECT_FALSE(update->advertise().test(c2.index()));
    TASK_UTIL_EXPECT_TRUE(update->advertise().test(c3.index()));
    // Call ProcessQueue() since our QueueActive() does not do anything
    ProcessQueue();

    IFMapMsgUnlink("virtual-router", "virtual-machine", "192.168.1.2", "vm_w");
    IFMapMsgUnlink("virtual-router", "virtual-machine", "192.168.1.3", "vm_y");
    IFMapMsgLink("virtual-router", "virtual-machine", "192.168.1.4", "vm_z");
    task_util::WaitForIdle();

    state = exporter_->NodeStateLookup(blue);
    update = state->GetUpdate(IFMapListEntry::UPDATE);
    ASSERT_TRUE(update != NULL);
    TASK_UTIL_EXPECT_TRUE(update->advertise().test(c1.index()));
    TASK_UTIL_EXPECT_FALSE(update->advertise().test(c2.index()));
    TASK_UTIL_EXPECT_FALSE(update->advertise().test(c3.index()));
    TASK_UTIL_EXPECT_FALSE(update->advertise().test(c4.index()));
    // Call ProcessQueue() since our QueueActive() does not do anything
    ProcessQueue();

    IFMapMsgUnlink("virtual-machine-interface", "virtual-network",
                   "vm_z:veth0", "red");
    IFMapMsgLink("virtual-machine-interface", "virtual-network",
                 "vm_z:veth0", "blue");
    task_util::WaitForIdle();

    state = exporter_->NodeStateLookup(blue);
    update = state->GetUpdate(IFMapListEntry::UPDATE);
    ASSERT_TRUE(update != NULL);
    EXPECT_TRUE(update->advertise().test(c1.index()));
    EXPECT_FALSE(update->advertise().test(c3.index()));
    EXPECT_TRUE(update->advertise().test(c4.index()));
    // Call ProcessQueue() since our QueueActive() does not do anything
    ProcessQueue();

    IFMapNode *red = TableLookup("virtual-network", "red");
    ASSERT_TRUE(red != NULL);
    state = exporter_->NodeStateLookup(red);
    ASSERT_TRUE(state != NULL);
    update = state->GetUpdate(IFMapListEntry::UPDATE);
    EXPECT_TRUE(update == NULL);
}

// Verify dependency on add.
TEST_F(IFMapExporterTest, NodeAddDependency) {
    server_.SetSender(new IFMapUpdateSenderMock(&server_));
    TestClient c1("192.168.1.1");
    server_.ClientRegister(&c1);
    
    IFMapMsgLink("domain", "project", "user1", "vnc");
    IFMapMsgLink("project", "virtual-network", "vnc", "blue");
    IFMapMsgLink("project", "virtual-network", "vnc", "red");
    IFMapMsgLink("virtual-machine", "virtual-machine-interface", 
                 "vm_x", "vm_x:veth0");
    IFMapMsgLink("virtual-machine-interface", "virtual-network", 
                 "vm_x:veth0", "blue");

    IFMapMsgLink("virtual-router", "virtual-machine", "192.168.1.1", "vm_x");
    task_util::WaitForIdle();

    IFMapUpdateQueue *queue = server_.queue();
    
    set<IFMapNode *> seen;
    for (IFMapListEntry *iter = queue->tail_marker(); iter != NULL;
         iter = queue->Next(iter)) {
        if (iter->type == IFMapListEntry::MARKER) {
            continue;
        }
        IFMapUpdate *update = static_cast<IFMapUpdate *>(iter);
        EXPECT_TRUE(update->type == IFMapListEntry::UPDATE);
        if (update->data().type == IFMapObjectPtr::NODE) {
            seen.insert(update->data().u.node);
        } else {
            IFMapLink *link = update->data().u.link;
            EXPECT_TRUE(seen.find(link->left()) != seen.end())
                << link->ToString() << " before " << link->left()->ToString();
            EXPECT_TRUE(seen.find(link->right()) != seen.end())
                << link->ToString() << " before " << link->right()->ToString();
        }
    }
    EXPECT_EQ(4, seen.size());
}

// Link is deleted.
TEST_F(IFMapExporterTest, LinkDeleteDependency) {
    server_.SetSender(new IFMapUpdateSenderMock(&server_));
    TestClient c1("192.168.1.1");
    server_.ClientRegister(&c1);
 
    IFMapMsgLink("domain", "project", "user1", "vnc");
    IFMapMsgLink("project", "virtual-network", "vnc", "blue");
    IFMapMsgLink("project", "virtual-network", "vnc", "red");
    IFMapMsgLink("virtual-machine", "virtual-machine-interface", 
                 "vm_x", "vm_x:veth0");
    IFMapMsgLink("virtual-machine-interface", "virtual-network", 
                 "vm_x:veth0", "blue");
    
    IFMapMsgLink("virtual-router", "virtual-machine", "192.168.1.1", "vm_x");
    task_util::WaitForIdle();
 
    // Call ProcessQueue() since our QueueActive() does not do anything
    ProcessQueue();
    IFMapMsgUnlink("virtual-router", "virtual-machine", "192.168.1.1", "vm_x");
    task_util::WaitForIdle();

    set<IFMapNode *> seen;
    IFMapUpdateQueue *queue = server_.queue();
    for (IFMapListEntry *iter = queue->tail_marker(); iter != NULL;
         iter = queue->Next(iter)) {
        if (iter->type == IFMapListEntry::MARKER) {
            continue;
        }
        IFMapUpdate *update = static_cast<IFMapUpdate *>(iter);
        EXPECT_TRUE(update->type == IFMapListEntry::DELETE);
        if (update->data().type == IFMapObjectPtr::NODE) {
            seen.insert(update->data().u.node);
        } else {
            IFMapLink *link = update->data().u.link;
            EXPECT_TRUE(seen.find(link->left()) == seen.end())
                << link->ToString() << " after " << link->left()->ToString();
            EXPECT_TRUE(seen.find(link->right()) == seen.end())
                << link->ToString() << " after " << link->right()->ToString();
        }
    }
}

TEST_F(IFMapExporterTest, CrcChecks) {
    // Round 1 of reading config
    string content = FileRead("controller/src/ifmap/testdata/crc.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    IFMapNode *idn = TableLookup("virtual-router", "host1");
    ASSERT_TRUE(idn != NULL);
    IFMapNodeState *state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_uuid1 = state->crc();

    idn = TableLookup("virtual-router", "host2");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_perm1 = state->crc();

    idn = TableLookup("virtual-router", "host3");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_bool1 = state->crc();

    idn = TableLookup("virtual-router", "host4");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_string1 = state->crc();

    idn = TableLookup("virtual-router", "host5");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_idperms1 = state->crc();

    idn = TableLookup("network-policy", "policy1");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_np_vec_complex1 = state->crc();

    idn = TableLookup("virtual-machine-interface", "vm1");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_vm_vec_simple1 = state->crc();

    // Round 2 of reading config
    content = FileRead("controller/src/ifmap/testdata/crc1.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    idn = TableLookup("virtual-router", "host1");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_uuid2 = state->crc();

    idn = TableLookup("virtual-router", "host2");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_perm2 = state->crc();

    idn = TableLookup("virtual-router", "host3");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_bool2 = state->crc();

    idn = TableLookup("virtual-router", "host4");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_string2 = state->crc();

    idn = TableLookup("virtual-router", "host5");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_idperms2 = state->crc();

    idn = TableLookup("network-policy", "policy1");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_np_vec_complex2 = state->crc();

    idn = TableLookup("virtual-machine-interface", "vm1");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_vm_vec_simple2 = state->crc();

    ASSERT_TRUE(crc_uuid1 != crc_uuid2);
    ASSERT_TRUE(crc_perm1 != crc_perm2);
    ASSERT_TRUE(crc_bool1 != crc_bool2);
    ASSERT_TRUE(crc_string1 != crc_string2);
    // both should be same since config is same
    ASSERT_TRUE(crc_idperms1 == crc_idperms2);
    ASSERT_TRUE(crc_np_vec_complex1 != crc_np_vec_complex2);
    ASSERT_TRUE(crc_vm_vec_simple1 != crc_vm_vec_simple2);

    // Round 3 of reading config
    // Read crc.xml again. After reading, all the crc's should match with the
    // crc's calculated during round 1.
    content = FileRead("controller/src/ifmap/testdata/crc.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    idn = TableLookup("virtual-router", "host1");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_uuid3 = state->crc();

    idn = TableLookup("virtual-router", "host2");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_perm3 = state->crc();

    idn = TableLookup("virtual-router", "host3");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_bool3 = state->crc();

    idn = TableLookup("virtual-router", "host4");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_string3 = state->crc();

    idn = TableLookup("virtual-router", "host5");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_idperms3 = state->crc();

    idn = TableLookup("network-policy", "policy1");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_np_vec_complex3 = state->crc();

    idn = TableLookup("virtual-machine-interface", "vm1");
    ASSERT_TRUE(idn != NULL);
    state = exporter_->NodeStateLookup(idn);
    ASSERT_TRUE(state != NULL);
    IFMapState::crc32type crc_vm_vec_simple3 = state->crc();

    ASSERT_TRUE(crc_uuid1 == crc_uuid3);
    ASSERT_TRUE(crc_perm1 == crc_perm3);
    ASSERT_TRUE(crc_bool1 == crc_bool3);
    ASSERT_TRUE(crc_string1 == crc_string3);
    ASSERT_TRUE(crc_idperms1 == crc_idperms3);
    ASSERT_TRUE(crc_np_vec_complex1 == crc_np_vec_complex3);
    ASSERT_TRUE(crc_vm_vec_simple1 == crc_vm_vec_simple3);
}

TEST_F(IFMapExporterTest, ChangePropertiesIncrementally) {
    server_.SetSender(new IFMapUpdateSenderMock(&server_));
    TestClient c1("vr-test");
    server_.ClientRegister(&c1);

    IFMapMsgLink("domain", "project", "user1", "vnc");
    IFMapMsgLink("project", "virtual-network", "vnc", "blue");
    IFMapMsgLink("project", "virtual-network", "vnc", "red");
    IFMapMsgLink("virtual-machine", "virtual-machine-interface", 
                 "vm_x", "vm_x:veth0");
    IFMapMsgLink("virtual-machine-interface", "virtual-network", 
                 "vm_x:veth0", "blue");
    IFMapMsgLink("virtual-router", "virtual-machine", "vr-test", "vm_x");
    task_util::WaitForIdle();

    // Check that c1's advertise bit is set
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", "vr-test") != NULL);
    IFMapNode *vrnode = TableLookup("virtual-router", "vr-test");
    TASK_UTIL_EXPECT_TRUE(exporter_->NodeStateLookup(vrnode) != NULL);
    IFMapNodeState *state = exporter_->NodeStateLookup(vrnode);
    TASK_UTIL_EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) != NULL);
    IFMapUpdate *update = state->GetUpdate(IFMapListEntry::UPDATE);
    ASSERT_TRUE(update != NULL);
    EXPECT_TRUE(update->advertise().test(c1.index()));
    IFMapState::crc32type crc0 = state->crc();
    ProcessQueue();
    EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) == NULL);
 
    // Add the 'id-perms' property
    autogen::IdPermsType *prop1 = new autogen::IdPermsType();
    IFMapMsgNodeAdd("virtual-router", "vr-test", 1, "id-perms", prop1);
    task_util::WaitForIdle();
    state = exporter_->NodeStateLookup(vrnode);
    TASK_UTIL_EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) != NULL);
    update = state->GetUpdate(IFMapListEntry::UPDATE);
    EXPECT_TRUE(update->advertise().test(c1.index()));
    IFMapState::crc32type crc1 = state->crc();
    ProcessQueue();
    EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) == NULL);
    // Checks. Only 'id-perms' should be set.
    vrnode = TableLookup("virtual-router", "vr-test");
    ASSERT_TRUE(vrnode != NULL);
    TASK_UTIL_EXPECT_TRUE(
        vrnode->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    IFMapObject *obj = vrnode->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    ASSERT_TRUE(obj != NULL);
    autogen::VirtualRouter *vrobj = dynamic_cast<autogen::VirtualRouter *>(obj);
    ASSERT_TRUE(vrobj != NULL);
    EXPECT_TRUE(vrobj->IsPropertySet(autogen::VirtualRouter::ID_PERMS));
    EXPECT_FALSE(vrobj->IsPropertySet(autogen::VirtualRouter::DISPLAY_NAME));
    EXPECT_FALSE(vrobj->IsPropertySet(autogen::VirtualRouter::IP_ADDRESS));
    ASSERT_TRUE(crc0 != crc1);

    // Add the 'display-name' property
    autogen::VirtualRouter::StringProperty *prop2 =
        new autogen::VirtualRouter::StringProperty();
    prop2->data = "myDisplayName";
    IFMapMsgNodeAdd("virtual-router", "vr-test", 1, "display-name", prop2);
    task_util::WaitForIdle();
    state = exporter_->NodeStateLookup(vrnode);
    TASK_UTIL_EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) != NULL);
    update = state->GetUpdate(IFMapListEntry::UPDATE);
    EXPECT_TRUE(update->advertise().test(c1.index()));
    IFMapState::crc32type crc2 = state->crc();
    ProcessQueue();
    EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) == NULL);
    // Checks. 'id-perms' and 'display-name' should be set.
    vrnode = TableLookup("virtual-router", "vr-test");
    ASSERT_TRUE(vrnode != NULL);
    EXPECT_TRUE(vrnode->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    obj = vrnode->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    ASSERT_TRUE(obj != NULL);
    vrobj = dynamic_cast<autogen::VirtualRouter *>(obj);
    ASSERT_TRUE(vrobj != NULL);
    EXPECT_TRUE(vrobj->IsPropertySet(autogen::VirtualRouter::ID_PERMS));
    EXPECT_TRUE(vrobj->IsPropertySet(autogen::VirtualRouter::DISPLAY_NAME));
    EXPECT_FALSE(vrobj->IsPropertySet(autogen::VirtualRouter::IP_ADDRESS));
    ASSERT_TRUE(crc1 != crc2);

    // Remove the 'display-name' property
    prop2 = new autogen::VirtualRouter::StringProperty();
    prop2->data = "myDisplayName";
    IFMapMsgNodeDelete("virtual-router", "vr-test", 1, "display-name", prop2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) != NULL);
    update = state->GetUpdate(IFMapListEntry::UPDATE);
    EXPECT_TRUE(update->advertise().test(c1.index()));
    IFMapState::crc32type crc3 = state->crc();
    ProcessQueue();
    EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) == NULL);
    // Checks. Only 'id-perms' should be set.
    vrnode = TableLookup("virtual-router", "vr-test");
    ASSERT_TRUE(vrnode != NULL);
    EXPECT_TRUE(vrnode->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    obj = vrnode->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    ASSERT_TRUE(obj != NULL);
    vrobj = dynamic_cast<autogen::VirtualRouter *>(obj);
    ASSERT_TRUE(vrobj != NULL);
    EXPECT_TRUE(vrobj->IsPropertySet(autogen::VirtualRouter::ID_PERMS));
    EXPECT_FALSE(vrobj->IsPropertySet(autogen::VirtualRouter::DISPLAY_NAME));
    EXPECT_FALSE(vrobj->IsPropertySet(autogen::VirtualRouter::IP_ADDRESS));
    ASSERT_TRUE(crc2 != crc3);
    ASSERT_TRUE(crc1 == crc3);

    // Add the 'display-name' property again
    prop2 = new autogen::VirtualRouter::StringProperty();
    prop2->data = "myDisplayName";
    IFMapMsgNodeAdd("virtual-router", "vr-test", 1, "display-name", prop2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) != NULL);
    update = state->GetUpdate(IFMapListEntry::UPDATE);
    EXPECT_TRUE(update->advertise().test(c1.index()));
    IFMapState::crc32type crc4 = state->crc();
    ProcessQueue();
    EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) == NULL);
    // Checks. 'id-perms' and 'display-name' should be set.
    vrnode = TableLookup("virtual-router", "vr-test");
    ASSERT_TRUE(vrnode != NULL);
    EXPECT_TRUE(vrnode->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    obj = vrnode->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    ASSERT_TRUE(obj != NULL);
    vrobj = dynamic_cast<autogen::VirtualRouter *>(obj);
    ASSERT_TRUE(vrobj != NULL);
    EXPECT_TRUE(vrobj->IsPropertySet(autogen::VirtualRouter::ID_PERMS));
    EXPECT_TRUE(vrobj->IsPropertySet(autogen::VirtualRouter::DISPLAY_NAME));
    EXPECT_FALSE(vrobj->IsPropertySet(autogen::VirtualRouter::IP_ADDRESS));
    ASSERT_TRUE(crc3 != crc4);
    ASSERT_TRUE(crc2 == crc4);

    // Remove the 'id-perms' property
    prop1 = new autogen::IdPermsType();
    IFMapMsgNodeDelete("virtual-router", "vr-test", 1, "id-perms", prop1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) != NULL);
    update = state->GetUpdate(IFMapListEntry::UPDATE);
    EXPECT_TRUE(update->advertise().test(c1.index()));
    IFMapState::crc32type crc5 = state->crc();
    ProcessQueue();
    EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) == NULL);
    // Checks. Only 'display-name' should be set.
    vrnode = TableLookup("virtual-router", "vr-test");
    ASSERT_TRUE(vrnode != NULL);
    EXPECT_TRUE(vrnode->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) != NULL);
    obj = vrnode->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    ASSERT_TRUE(obj != NULL);
    vrobj = dynamic_cast<autogen::VirtualRouter *>(obj);
    ASSERT_TRUE(vrobj != NULL);
    EXPECT_FALSE(vrobj->IsPropertySet(autogen::VirtualRouter::ID_PERMS));
    EXPECT_TRUE(vrobj->IsPropertySet(autogen::VirtualRouter::DISPLAY_NAME));
    EXPECT_FALSE(vrobj->IsPropertySet(autogen::VirtualRouter::IP_ADDRESS));
    ASSERT_TRUE(crc4 != crc5);

    // Remove the 'display-name' property
    prop2 = new autogen::VirtualRouter::StringProperty();
    prop2->data = "myDisplayName";
    IFMapMsgNodeDelete("virtual-router", "vr-test", 1, "display-name", prop2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) != NULL);
    update = state->GetUpdate(IFMapListEntry::UPDATE);
    EXPECT_TRUE(update->advertise().test(c1.index()));
    IFMapState::crc32type crc6 = state->crc();
    ProcessQueue();
    EXPECT_TRUE(state->GetUpdate(IFMapListEntry::UPDATE) == NULL);
    // Checks. The node should exist since it has a neighbor. But, the object
    // should be gone since all the properties are gone.
    vrnode = TableLookup("virtual-router", "vr-test");
    ASSERT_TRUE(vrnode != NULL);
    TASK_UTIL_EXPECT_TRUE(
            vrnode->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER)) == NULL);
    ASSERT_TRUE(crc5 != crc6);
    ASSERT_TRUE(crc0 == crc6);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    bool success = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return success;
}
