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

class ConfigCassandraClientTest : public ConfigCassandraClient {
public:
    ConfigCassandraClientTest(ConfigClientManager *mgr, EventManager *evm,
        const IFMapConfigOptions &options, ConfigJsonParser *in_parser,
        int num_workers) : ConfigCassandraClient(mgr, evm, options, in_parser,
            num_workers) {
    }

    virtual bool ReadUuidTableRow(const std::string &uuid_key) {
        return ParseRowAndEnqueueToParser(uuid_key, GenDb::ColList());
    }

    bool ParseUuidTableRowResponse(const string &uuid,
            const GenDb::ColList &col_list, CassColumnKVVec *cass_data_vec) {
        // Retrieve event index prepended to uuid, to get to the correct db.
        vector<string> tokens;
        boost::split(tokens, uuid, boost::is_any_of(":"));
        int index = atoi(tokens[0].c_str());
        string u = tokens[1];
        assert(events_[index].IsObject());
        for (Value::ConstMemberIterator i = events_[index].MemberBegin();
                i != events_[index].MemberEnd(); ++i) {
            if (string(i->name.GetString()) == "db") {
                assert(i->value.IsObject());
                for (Value::ConstMemberIterator j = i->value.MemberBegin();
                    j != i->value.MemberEnd(); ++j) {
                    if (string(j->name.GetString()) == u) {
                        for (Value::ConstMemberIterator k =
                                j->value.MemberBegin();
                                k != j->value.MemberEnd(); ++k) {
                            ParseUuidTableRowJson(u, k->name.GetString(),
                                    k->value.GetString(), cass_data_vec);
                        }
                        break;
                    }
                }
                break;
            }
        }
        return true;
    }
};

class ConfigJsonParserTest : public ::testing::Test {
protected:
    ConfigJsonParserTest() :
        db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
        parser_(&db_),
        ifmap_server_(new IFMapServer(&db_, new DBGraph(), evm_.io_service())),
        config_client_manager_(new ConfigClientManager(&evm_,
                    ifmap_server_.get(), "localhost", config_options_)) {
    }

    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &graph_);
        vnc_cfg_JsonParserInit(&parser_);
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        bgp_schema_JsonParserInit(&parser_);
        bgp_schema_Server_ModuleInit(&db_, &graph_);
    }

    virtual void TearDown() {
        IFMapLinkTable_Clear(&db_);
        IFMapTable::ClearTables(&db_);
        parser_.MetadataClear("vnc_cfg");
        task_util::WaitForIdle();
    }

    void ParseEventsJson (string eventsFile) {
        string json_message =
            FileRead("controller/src/ifmap/testdata/server_parser_test.json");
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
        for (SizeType index = 0; index < events_.Size(); index++) {
            for (Value::ConstMemberIterator i = events_[index].MemberBegin();
                i != events_[index].MemberEnd(); ++i) {
                if (string(i->name.GetString()) == "rabbit_message") {
                    config_client_manager_->config_amqp_client()->
                        ProcessMessage(i->value.GetString());
                    task_util::WaitForIdle();
                }
            }
        }
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
    ConfigJsonParser parser_;
    const IFMapConfigOptions config_options_;
    boost::scoped_ptr<IFMapServer> ifmap_server_;
    boost::scoped_ptr<ConfigClientManager> config_client_manager_;
};

TEST_F(ConfigJsonParserTest, VirtualNetworkParse) {
    string message =
        FileRead("controller/src/ifmap/client/testdata/vn.json");
    assert(message.size() != 0);
    bool add_change = true;
    parser_.Receive(message, add_change, IFMapOrigin::CASSANDRA);
    task_util::WaitForIdle();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vnn = NodeLookup("virtual-network", "dd:dp:vn1");
    ASSERT_TRUE(vnn != NULL);
    IFMapObject *obj = vnn->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    ASSERT_TRUE(obj != NULL);

    // No refs but link to parent (project-virtual-network) exists
    IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
        db_.FindTable("__ifmap_metadata__.0"));
    TASK_UTIL_EXPECT_EQ(1, link_table->Size());
    IFMapNode *projn = NodeLookup("project", "dd:dp");
    ASSERT_TRUE(projn != NULL);
    IFMapLink *link = LinkLookup(projn, vnn, "project-virtual-network");
    EXPECT_TRUE(link != NULL);

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
}

TEST_F(ConfigJsonParserTest, AclParse) {
    string message =
        FileRead("controller/src/ifmap/client/testdata/acl.json");
    assert(message.size() != 0);
    bool add_change = true;
    parser_.Receive(message, add_change, IFMapOrigin::CASSANDRA);
    task_util::WaitForIdle();

    IFMapTable *table = IFMapTable::FindTable(&db_, "access-control-list");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *acln = NodeLookup("access-control-list", "dd:acl1:iacl");
    ASSERT_TRUE(acln != NULL);
    IFMapObject *obj = acln->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(obj != NULL);

    // No refs but link to parent (security-group-access-control-list)
    IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
        db_.FindTable("__ifmap_metadata__.0"));
    TASK_UTIL_EXPECT_EQ(1, link_table->Size());
    IFMapNode *sgn = NodeLookup("security-group", "dd:acl1");
    ASSERT_TRUE(sgn != NULL);
    IFMapLink *link = LinkLookup(sgn, acln, "security-group-access-control-list");
    EXPECT_TRUE(link != NULL);

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

TEST_F(ConfigJsonParserTest, VmiParseAddDeleteProperty) {
    string message =
        FileRead("controller/src/ifmap/client/testdata/vmi.json");
    assert(message.size() != 0);
    bool add_change = true;
    parser_.Receive(message, add_change, IFMapOrigin::CASSANDRA);
    task_util::WaitForIdle();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-machine-interface");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vmin = NodeLookup("virtual-machine-interface",
        "dd:VMI:42f6d841-d1c7-40b8-b1c4-ca2ab415c81d");
    ASSERT_TRUE(vmin != NULL);
    IFMapObject *obj = vmin->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(obj != NULL);

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
    EXPECT_TRUE(link != NULL);
    link = LinkLookup(vmin, vnn, "virtual-machine-interface-virtual-network");
    EXPECT_TRUE(link != NULL);

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
    parser_.Receive(message, add_change, IFMapOrigin::CASSANDRA);
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

// In a single message, adds vn1, vn2, vn3, then deletes, vn3, then adds vn4,
// vn5, then deletes vn5, vn4 and vn2. Only vn1 should remain.
TEST_F(ConfigJsonParserTest, DISABLED_ServerParser) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test.json");

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vn1 = NodeLookup("virtual-network", "vn1");
    EXPECT_TRUE(vn1 != NULL);
    IFMapObject *obj = vn1->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *vn = NodeLookup("virtual-network", "vn2");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn3");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn4");
    EXPECT_TRUE(vn == NULL);
    vn = NodeLookup("virtual-network", "vn5");
    EXPECT_TRUE(vn == NULL);
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

