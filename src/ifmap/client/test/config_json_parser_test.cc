/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/client/config_amqp_client.h"
#include "ifmap/client/config_cassandra_client.h"
#include "ifmap/client/config_client_manager.h"
#include "ifmap/client/config_json_parser.h"
#include "ifmap/ifmap_factory.h"
#include <fstream>
#include <string>

#include "base/logging.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_origin.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/test/ifmap_test_util.h"

#include "rapidjson/document.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;
using namespace rapidjson;

static Document events_;
static size_t cevent_;

class ConfigCassandraClientTest : public ConfigCassandraClient {
public:
    ConfigCassandraClientTest(ConfigClientManager *mgr, EventManager *evm,
        const IFMapConfigOptions &options, ConfigJsonParser *in_parser,
        int num_workers) : ConfigCassandraClient(mgr, evm, options, in_parser,
            num_workers), db_index_(num_workers) {
    }

    virtual void HandleObjectDelete(const string &type, const string &uuid) {
        vector<string> tokens;
        boost::split(tokens, uuid, boost::is_any_of(":"));
        string u = tokens[1];
        ConfigCassandraClient::HandleObjectDelete(type, u);
    }

    virtual void AddFQNameCache(const string &uuid, const string &obj_name) {
        vector<string> tokens;
        boost::split(tokens, uuid, boost::is_any_of(":"));
        string u = tokens[1];
        ConfigCassandraClient::AddFQNameCache(u, obj_name);
    }

    virtual int HashUUID(const string &uuid) const {
        string u = uuid;
        size_t from_front_pos = uuid.find(':');
        if (from_front_pos != string::npos)  {
            u = uuid.substr(from_front_pos+1);
        }
        return ConfigCassandraClient::HashUUID(u);
    }

    virtual bool ReadUuidTableRow(const string &obj_type,
                                  const std::string &uuid_key) {
        vector<string> tokens;
        boost::split(tokens, uuid_key, boost::is_any_of(":"));
        int index = atoi(tokens[0].c_str());
        string u = tokens[1];
        assert(events_[index].IsObject());
        int idx = HashUUID(u);
        db_index_[idx].insert(make_pair(u, index));
        return ParseRowAndEnqueueToParser(obj_type, u, GenDb::ColList());
    }

    bool ParseUuidTableRowResponse(const string &uuid,
            const GenDb::ColList &col_list, CassColumnKVVec *cass_data_vec) {
        // Retrieve event index prepended to uuid, to get to the correct db.
        int idx = HashUUID(uuid);
        UUIDIndexMap::iterator it = db_index_[idx].find(uuid);
        int index = it->second;

        for (Value::ConstMemberIterator k =
             events_[SizeType(index)]["db"][uuid.c_str()].MemberBegin();
             k != events_[SizeType(index)]["db"][uuid.c_str()].MemberEnd(); ++k) {
            ParseUuidTableRowJson(uuid, k->name.GetString(), k->value.GetString(),
                                  0, cass_data_vec);
        }
        db_index_[idx].erase(it);
        return true;
    }
private:
    typedef std::map<string, int> UUIDIndexMap;
    vector<UUIDIndexMap> db_index_;
};

class ConfigJsonParserTest : public ::testing::Test {
protected:
    ConfigJsonParserTest() :
        db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
        ifmap_server_(new IFMapServer(&db_, &graph_, evm_.io_service())),
        config_client_manager_(new ConfigClientManager(&evm_,
                    ifmap_server_.get(), "localhost", config_options_)) {
    }

    virtual void SetUp() {
        cevent_ = 0;
        IFMapLinkTable_Init(&db_, &graph_);
        vnc_cfg_JsonParserInit(config_client_manager_->config_json_parser());
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        bgp_schema_JsonParserInit(config_client_manager_->config_json_parser());
        bgp_schema_Server_ModuleInit(&db_, &graph_);
    }

    virtual void TearDown() {
        IFMapLinkTable_Clear(&db_);
        IFMapTable::ClearTables(&db_);
        config_client_manager_->config_json_parser()->MetadataClear("vnc_cfg");
        task_util::WaitForIdle();
    }

    void ParseEventsJson (string eventsFile) {
        string json_message = FileRead(eventsFile);
        assert(json_message.size() != 0);
        events_.Parse<0>(json_message.c_str());
        if (events_.HasParseError()) {
            size_t pos = events_.GetErrorOffset();
            // GetParseError returns const char *
            std::cout << "Error in parsing JSON message from rabbitMQ at "
                << pos << "with error description"
                << events_.GetParseError() << std::endl;
            exit(-1);
        }
    }

    void FeedEventsJson () {
        while (cevent_++ < events_.Size()) {
            if (events_[SizeType(cevent_-1)]["operation"].GetString() ==
                           string("pause"))
                break;
            config_client_manager_->config_amqp_client()->ProcessMessage(
                events_[SizeType(cevent_-1)]["message"].GetString());
        }
        task_util::WaitForIdle();
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    IFMapNode *NodeLookup(const string &type, const string &name) {
        return ifmap_test_util::IFMapNodeLookup(&db_, type, name);
    }

    IFMapLink *LinkLookup(IFMapNode *left, IFMapNode *right,
                          const string &metadata) {
        IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
                                     db_.FindTable("__ifmap_metadata__.0"));
        IFMapLink *link =  link_table->FindLink(metadata, left, right);
        return (link ? (link->IsDeleted() ? NULL : link) : NULL);
    }

    DB db_;
    DBGraph graph_;
    EventManager evm_;
    const IFMapConfigOptions config_options_;
    boost::scoped_ptr<IFMapServer> ifmap_server_;
    boost::scoped_ptr<ConfigClientManager> config_client_manager_;
};

TEST_F(ConfigJsonParserTest, DISABLED_VirtualNetworkParse) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test_vn.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vnn = NodeLookup("virtual-network", "dd:dp:vn1");
    ASSERT_TRUE(vnn != NULL);
    IFMapObject *obj = vnn->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    ASSERT_TRUE(obj != NULL);

    autogen::VirtualNetwork *vn = static_cast<autogen::VirtualNetwork *>(obj);
    TASK_UTIL_EXPECT_TRUE(vn->IsPropertySet(autogen::VirtualNetwork::ID_PERMS));
    TASK_UTIL_EXPECT_TRUE(
        vn->IsPropertySet(autogen::VirtualNetwork::NETWORK_ID));
    TASK_UTIL_EXPECT_TRUE(
        vn->IsPropertySet(autogen::VirtualNetwork::FLOOD_UNKNOWN_UNICAST));
    TASK_UTIL_EXPECT_FALSE(
        vn->IsPropertySet(autogen::VirtualNetwork::DISPLAY_NAME));
    TASK_UTIL_EXPECT_FALSE(
        vn->IsPropertySet(autogen::VirtualNetwork::ANNOTATIONS));
    TASK_UTIL_EXPECT_FALSE(
        vn->IsPropertySet(autogen::VirtualNetwork::IMPORT_ROUTE_TARGET_LIST));
    TASK_UTIL_EXPECT_FALSE(
        vn->IsPropertySet(autogen::VirtualNetwork::EXPORT_ROUTE_TARGET_LIST));
    TASK_UTIL_EXPECT_EQ(vn->network_id(), 2);
    autogen::PermType2 pt2 = vn->perms2();
    int cmp = pt2.owner.compare("cloud-admin");
    TASK_UTIL_EXPECT_EQ(cmp, 0);
    TASK_UTIL_EXPECT_EQ(pt2.owner_access, 7);

    // No refs but link to parent (project-virtual-network) exists
    IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
        db_.FindTable("__ifmap_metadata__.0"));
    TASK_UTIL_EXPECT_EQ(1, link_table->Size());
    IFMapNode *projn = NodeLookup("project", "dd:dp");
    ASSERT_TRUE(projn != NULL);
    IFMapLink *link = LinkLookup(projn, vnn, "project-virtual-network");
    TASK_UTIL_EXPECT_TRUE(link != NULL);

}

TEST_F(ConfigJsonParserTest, DISABLED_AclParse) {
    string message =
        FileRead("controller/src/ifmap/client/testdata/acl.json");
    assert(message.size() != 0);
    bool add_change = true;
    config_client_manager_->config_json_parser()->Receive(
        "d4cb8100-b9b8-41cd-8fdf-5eb76323f096", message, add_change,
        IFMapOrigin::CASSANDRA);
    task_util::WaitForIdle();

    IFMapTable *table = IFMapTable::FindTable(&db_, "access-control-list");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *acln = NodeLookup("access-control-list", "dd:acl1:iacl");
    ASSERT_TRUE(acln != NULL);
    IFMapObject *obj = acln->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    // No refs but link to parent (security-group-access-control-list)
    IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
        db_.FindTable("__ifmap_metadata__.0"));
    TASK_UTIL_EXPECT_EQ(1, link_table->Size());
    IFMapNode *sgn = NodeLookup("security-group", "dd:acl1");
    ASSERT_TRUE(sgn != NULL);
    IFMapLink *link = LinkLookup(sgn, acln, "security-group-access-control-list");
    TASK_UTIL_EXPECT_TRUE(link != NULL);

    autogen::AccessControlList *acl = static_cast<autogen::AccessControlList *>
                                          (obj);
    TASK_UTIL_EXPECT_TRUE(
        acl->IsPropertySet(autogen::AccessControlList::DISPLAY_NAME));
    TASK_UTIL_EXPECT_TRUE(
        acl->IsPropertySet(autogen::AccessControlList::PERMS2));
    TASK_UTIL_EXPECT_FALSE(
        acl->IsPropertySet(autogen::AccessControlList::ANNOTATIONS));
    int cmp = acl->display_name().compare("ingress-access-control-list");
    TASK_UTIL_EXPECT_EQ(cmp, 0);
    autogen::PermType2 pt2 = acl->perms2();
    cmp = pt2.owner.compare("a35f7a1a65084b6c9ba402710ba0948a");
    TASK_UTIL_EXPECT_EQ(cmp, 0);
    TASK_UTIL_EXPECT_EQ(pt2.owner_access, 5);
}

TEST_F(ConfigJsonParserTest, DISABLED_VmiParseAddDeleteProperty) {
    string message =
        FileRead("controller/src/ifmap/client/testdata/vmi.json");
    assert(message.size() != 0);
    bool add_change = true;
    config_client_manager_->config_json_parser()->Receive(
        "42f6d841-d1c7-40b8-b1c4-ca2ab415c81d", message, add_change,
        IFMapOrigin::CASSANDRA);
    task_util::WaitForIdle();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-machine-interface");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vmin = NodeLookup("virtual-machine-interface",
        "dd:VMI:42f6d841-d1c7-40b8-b1c4-ca2ab415c81d");
    ASSERT_TRUE(vmin != NULL);
    IFMapObject *obj = vmin->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *rin = NodeLookup("routing-instance", "dd:vn1:vn1");
    ASSERT_TRUE(rin != NULL);
    table = IFMapTable::FindTable(&db_, "routing-instance");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *sgn = NodeLookup("security-group", "dd:sg1:default");
    ASSERT_TRUE(sgn != NULL);
    table = IFMapTable::FindTable(&db_, "security-group");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vnn = NodeLookup("virtual-network", "dd:vn1");
    ASSERT_TRUE(vnn != NULL);
    table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *projn = NodeLookup("project", "dd:VMI");
    ASSERT_TRUE(projn);
    table = IFMapTable::FindTable(&db_, "project");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
        db_.FindTable("__ifmap_metadata__.0"));
    // refs: vmi-sg, vmi-vn, vmi-vmirn, vmirn-rn (vmi-rn has metadata)
    // parent: project-virtual-machine-interface
    TASK_UTIL_EXPECT_EQ(5, link_table->Size());
    IFMapLink *link = LinkLookup(vmin, sgn,
        "virtual-machine-interface-security-group");
    TASK_UTIL_EXPECT_TRUE(link != NULL);
    link = LinkLookup(vmin, vnn, "virtual-machine-interface-virtual-network");
    TASK_UTIL_EXPECT_TRUE(link != NULL);

    autogen::VirtualMachineInterface *vmi =
        static_cast<autogen::VirtualMachineInterface *>(obj);
    TASK_UTIL_EXPECT_TRUE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::DISPLAY_NAME));
    TASK_UTIL_EXPECT_TRUE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::PERMS2));
    TASK_UTIL_EXPECT_FALSE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::ANNOTATIONS));

    int cmp =
        vmi->display_name().compare("42f6d841-d1c7-40b8-b1c4-ca2ab415c81d");
    TASK_UTIL_EXPECT_EQ(cmp, 0);
    autogen::PermType2 pt2 = vmi->perms2();
    cmp = pt2.owner.compare("6ca1f1de004d48f99cf4528539b20013");
    TASK_UTIL_EXPECT_EQ(cmp, 0);
    TASK_UTIL_EXPECT_EQ(pt2.owner_access, 7);

    TASK_UTIL_EXPECT_TRUE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::DISABLE_POLICY));
    // vmi1.json does not have the DISABLE_POLICY property. After processing
    // this message, the node should not have this property.
    message = FileRead("controller/src/ifmap/client/testdata/vmi1.json");
    assert(message.size() != 0);
    config_client_manager_->config_json_parser()->Receive(
        "42f6d841-d1c7-40b8-b1c4-ca2ab415c81d", message, add_change,
        IFMapOrigin::CASSANDRA);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::DISABLE_POLICY));
    // All other properties should still be set/unset as before.
    TASK_UTIL_EXPECT_TRUE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::DISPLAY_NAME));
    TASK_UTIL_EXPECT_TRUE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::PERMS2));
    TASK_UTIL_EXPECT_FALSE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::ANNOTATIONS));
}

// In a single message, adds vn1, vn2, vn3.
TEST_F(ConfigJsonParserTest, ServerParserAddInOneShot) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    IFMapNode *vn1 = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn1 != NULL);
    IFMapNode *vn = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
}

// In a multiple messages, adds (vn1, vn2), and vn3.
TEST_F(ConfigJsonParserTest, ServerParserAddInMultipleShots) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.1.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(2, table->Size());

    IFMapNode *vn1 = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn1 != NULL);
    IFMapNode *vn = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);

    // Verify that vn3 is still not added
    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);

    // Resume events processing
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
}

// In a single message, adds vn1, vn2, vn3, then deletes, vn3, then adds vn4,
// vn5, then deletes vn5, vn4 and vn2. Only vn1 should remain.
TEST_F(ConfigJsonParserTest, ServerParser) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vn1 = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn1 != NULL);
    IFMapObject *obj = vn1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vn = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn4");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn5");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
}

// In 4 separate messages: 1) adds vn1, vn2, vn3, 2) deletes vn3, 3) adds vn4,
// vn5, 4) deletes vn5, vn4 and vn2. Only vn1 should remain.
// Same as ServerParser except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParserInParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    IFMapNode *vn1 = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn1 != NULL);
    IFMapObject *obj = vn1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vn2 = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn2 != NULL);
    obj = vn2->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vn3 = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn3 != NULL);
    obj = vn3->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(2, table->Size());
    IFMapNode *node = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(node == NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(4, table->Size());

    IFMapNode *vn4 = NodeLookup("virtual-network", "vn4");
    TASK_UTIL_EXPECT_TRUE(vn4 != NULL);
    obj = vn4->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vn5 = NodeLookup("virtual-network", "vn5");
    TASK_UTIL_EXPECT_TRUE(vn5 != NULL);
    obj = vn5->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    // Only vn1 should exist
    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
    vn = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn4");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn5");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
}

// In a single message, adds vn1, vn2, vn3 and then deletes all of them.
TEST_F(ConfigJsonParserTest, ServerParser1) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
}

// In 2 separate messages, adds vn1, vn2, vn3 and then deletes all of them.
// Same as ServerParser1 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser1InParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test1_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
    IFMapObject *obj = vn->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);
    vn = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
    obj = vn->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);
    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
    obj = vn->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    vn = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
}

// In a single message, adds vn1, vn2, vn3 in separate updateResult stanza's
// and then adds them again in a single stanza 
TEST_F(ConfigJsonParserTest, ServerParser2) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test2.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
    IFMapObject *obj = vn->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    vn = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
    obj = vn->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
    obj = vn->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);
}

// In 4 separate messages: 1) adds vn1, 2) adds vn2, 3) adds vn3 4) adds all of
// them again in a single stanza 
// Same as ServerParser2 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser2InParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test2_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
    IFMapObject *obj = vn->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(2, table->Size());

    vn = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
    obj = vn->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
    obj = vn->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    vn = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
    vn = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn != NULL);
}

// In a single message, deletes vn1, vn2, vn3 in a deleteResult stanza and then
// deletes them again in a single stanza 
TEST_F(ConfigJsonParserTest, ServerParser3) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test3.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
}

// In 2 separate messages, 1) deletes vn1, vn2, vn3 2) deletes them again 
// Same as ServerParser3 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser3InParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test3_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    IFMapNode *vn = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    vn = NodeLookup("virtual-network", "vn1");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn2");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    TASK_UTIL_EXPECT_TRUE(vn == NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete link(vr,vm)
// Both vr and vm nodes should continue to live
TEST_F(ConfigJsonParserTest, ServerParser4) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test4.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1,
                          "virtual-router-virtual-machine") == NULL );
    TASK_UTIL_EXPECT_TRUE(
            vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(
            vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
}

// In 2 separate messages: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete link(vr,vm)
// Same as ServerParser4 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser4InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test4_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1, "virtual-router-virtual-machine") != NULL);
    IFMapLink *link = LinkLookup(vr1, vm1, "virtual-router-virtual-machine");
    TASK_UTIL_EXPECT_TRUE(link != NULL);

    cevent_ = 0;
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test4_p2.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1, "virtual-router-virtual-machine") == NULL);

    vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);
}

// In a single message: 
// 1) create link(vr,vm)         2) delete link(vr,vm)
// Both vr and vm nodes should get deleted since they dont have any properties
TEST_F(ConfigJsonParserTest, ServerParser5) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test5.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 == NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr, then link(vr,vm)
// The vr should disappear and vm should continue to live
TEST_F(ConfigJsonParserTest, ServerParser6) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test6.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    IFMapObject *obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);
}

// In 2 separate messages: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr, then link(vr,vm)
// The vr should disappear and vm should continue to live
// Same as ServerParser6 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser6InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test6_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1, "virtual-router-virtual-machine");
    TASK_UTIL_EXPECT_TRUE(link != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 == NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr, then link(vr,vm)
// 3) add vr-with-properties
// Both vr and vm nodes should continue to live
TEST_F(ConfigJsonParserTest, ServerParser7) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test7.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1, "virtual-router-virtual-machine");
    TASK_UTIL_EXPECT_TRUE(link == NULL);
}

// In 3 separate messages: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr and link(vr,vm)
// 3) add vr-with-properties
// Both vr and vm nodes should continue to live
// Same as ServerParser7 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser7InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test7_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1, "virtual-router-virtual-machine");
    TASK_UTIL_EXPECT_TRUE(link != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 == NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    link = LinkLookup(vr1, vm1, "virtual-router-virtual-machine");
    TASK_UTIL_EXPECT_TRUE(link == NULL);
}

// In a single message: 
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm)
// 3) add link(vr,vm)
// Both vr and vm nodes should continue to live
TEST_F(ConfigJsonParserTest, ServerParser9) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test9.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1, "virtual-router-virtual-machine");
    TASK_UTIL_EXPECT_TRUE(link != NULL);
}

// In 3 separate messages:
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm)
// 3) add link(vr,vm)
// Both vr and vm nodes should continue to live
// Same as ServerParser9 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser9InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test9_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1, "virtual-router-virtual-machine");
    TASK_UTIL_EXPECT_TRUE(link != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1, "virtual-router-virtual-machine") == NULL);

    vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1, "virtual-router-virtual-machine") != NULL);

    vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);
}

// In a single message: 
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm), then delete vr
// The vr should disappear and vm should continue to live
// Similar to ServerParser6, except that in step2, we delete link and then vr
TEST_F(ConfigJsonParserTest, ServerParser10) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test10.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    IFMapObject *obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);
}

// In 2 separate messages: 
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm), then delete vr
// The vr should disappear and vm should continue to live
// Similar to ServerParser6, except that in step2, we delete link and then vr
// Same as ServerParser10 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser10InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test10_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1, "virtual-router-virtual-machine");
    TASK_UTIL_EXPECT_TRUE(link != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);
}

// In a single message: 
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm), then delete vr
// 3) add link(vr,vm)
// vm nodes should continue to live but not vr
TEST_F(ConfigJsonParserTest, ServerParser11) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test11.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    IFMapObject *obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);
}

// In 3 separate messages: 
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm), then delete vr
// 3) add link(vr,vm)
// vm nodes should continue to live but not vr
TEST_F(ConfigJsonParserTest, ServerParser11InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test11_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapLink *link = LinkLookup(vr1, vm1, "virtual-router-virtual-machine");
    TASK_UTIL_EXPECT_TRUE(link != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 == NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    // vr1 should not have any object
    vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 == NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);
}

// In a single message: 
// 1) create link(vr,vm), then link(vr,gsc)
// 2) delete link(vr,vm), then link(vr,gsc)
// No nodes should exist.
TEST_F(ConfigJsonParserTest, ServerParser12) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test12.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 == NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    TASK_UTIL_EXPECT_TRUE(gsc == NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete link(vr,vm), then link(vr,gsc)
TEST_F(ConfigJsonParserTest, ServerParser13) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test13.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());

    usleep(1000);

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    TASK_UTIL_EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1, "virtual-router-virtual-machine") == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, gsc, "global-system-config-virtual-router") == NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete gsc, then link(vr,gsc)
TEST_F(ConfigJsonParserTest, ServerParser14) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test14.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    usleep(1000);

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    TASK_UTIL_EXPECT_TRUE(gsc == NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1, "virtual-router-virtual-machine") != NULL);
}

// In 3 separate messages: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete gsc, then link(vr,gsc)
TEST_F(ConfigJsonParserTest, ServerParser14InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    // Using datafile from test13_p1
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test14_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    TASK_UTIL_EXPECT_TRUE(gsc == NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1, "virtual-router-virtual-machine") != NULL);

    // Using datafile from test13_p2
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    gsc = NodeLookup("global-system-config", "gsc");
    TASK_UTIL_EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1, "virtual-router-virtual-machine") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, gsc, "global-system-config-virtual-router") != NULL);

    // Need new datafile for step 3
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1, "virtual-router-virtual-machine") != NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete vr
TEST_F(ConfigJsonParserTest, DISABLED_ServerParser15) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test15.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());

    usleep(1000);

    // Object should not exist
    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    IFMapObject *obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    TASK_UTIL_EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);
}

// In 3 separate messages: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete vr
TEST_F(ConfigJsonParserTest, ServerParser15InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    // Using datafile from test13_p1
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test15_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    TASK_UTIL_EXPECT_TRUE(gsc == NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1, "virtual-router-virtual-machine") != NULL);

    // Using datafile from test13_p2
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    gsc = NodeLookup("global-system-config", "gsc");
    TASK_UTIL_EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1, "virtual-router-virtual-machine") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, gsc, "global-system-config-virtual-router") != NULL);

    // Need new datafile for step 3
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());

    // Object should not exist
    vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj == NULL);
    TASK_UTIL_EXPECT_TRUE(vr1->HasAdjacencies(&graph_));

    vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    gsc = NodeLookup("global-system-config", "gsc");
    TASK_UTIL_EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);
}

// In a single message: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete link(vr,gsc), then delete gsc, then delete vr
TEST_F(ConfigJsonParserTest, ServerParser16) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test16.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    usleep(1000);

    // Object should not exist
    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 == NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    IFMapObject *obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    TASK_UTIL_EXPECT_TRUE(gsc == NULL);
}

// In 3 separate messages: 
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete link(vr,gsc), then delete gsc, then delete vr
TEST_F(ConfigJsonParserTest, ServerParser16InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    // Using datafile from test13_p1
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test16_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    IFMapNode *vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 != NULL);
    IFMapObject *obj = vr1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    IFMapNode *gsc = NodeLookup("global-system-config", "gsc");
    TASK_UTIL_EXPECT_TRUE(gsc == NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1, "virtual-router-virtual-machine") != NULL);

    // Using datafile from test13_p2
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    gsc = NodeLookup("global-system-config", "gsc");
    TASK_UTIL_EXPECT_TRUE(gsc != NULL);
    obj = gsc->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, vm1, "virtual-router-virtual-machine") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr1, gsc, "global-system-config-virtual-router") != NULL);

    // Need new datafile for step 3
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    // Object should not exist
    vr1 = NodeLookup("virtual-router", "vr1");
    TASK_UTIL_EXPECT_TRUE(vr1 == NULL);

    vm1 = NodeLookup("virtual-machine", "vm1");
    TASK_UTIL_EXPECT_TRUE(vm1 != NULL);
    obj = vm1->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(obj != NULL);

    gsc = NodeLookup("global-system-config", "gsc");
    TASK_UTIL_EXPECT_TRUE(gsc == NULL);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    ConfigAmqpClient::set_disable(true);
    IFMapFactory::Register<ConfigCassandraClient>(
        boost::factory<ConfigCassandraClientTest *>());
    int status = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return status;
}
