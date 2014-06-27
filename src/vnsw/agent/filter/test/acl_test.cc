/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <net/address.h>
#include <base/logging.h>
#include <testing/gunit.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <pugixml/pugixml.hpp>

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <io/event_manager.h>

#include <test_cmn_util.h>
#include <cfg/cfg_init.h>

#include <oper/operdb_init.h>
#include <oper/mirror_table.h>
#include <filter/packet_header.h>
#include <filter/acl.h>
#include <filter/acl_entry_spec.h>

#include <pkt/pkt_init.h>

using namespace std;

void RouterIdDepInit(Agent *agent) {
}

namespace {
class AclTest : public ::testing::Test {
protected:
};

static string AddAclXmlString(const char *node_name, const char *name, int id) {
    char buff[10240];
    sprintf(buff, 
            "<?xml version=\"1.0\"?>\n"
            "<config>\n"
            "   <update>\n"
            "       <node type=\"%s\">\n"
            "           <name>%s</name>\n"
            "           <id-perms>\n"
            "               <permissions>\n"
            "                   <owner></owner>\n"
            "                   <owner_access>0</owner_access>\n"
            "                   <group></group>\n"
            "                   <group_access>0</group_access>\n"
            "                   <other_access>0</other_access>\n"
            "               </permissions>\n"
            "               <uuid>\n"
            "                   <uuid-mslong>0</uuid-mslong>\n"
            "                   <uuid-lslong>%d</uuid-lslong>\n"
            "               </uuid>\n"
            "           </id-perms>\n"
            "           <access-control-list-entries>\n"
            "                <acl-rule>\n"
            "                    <match-condition>\n"
            "                        <protocol>\n"
            "                            tcp\n"
            "                        </protocol>\n"
            "                        <src-address>\n"
            "                            <virtual-network>\n"
            "                                vn1\n"
            "                            </virtual-network>\n"
            "                        </src-address>\n"
            "                        <src-port>\n"
            "                            <start-port>\n"
            "                                10\n"
            "                            </start-port>\n"
            "                            <end-port>\n"
            "                                20\n"
            "                            </end-port>\n"
            "                        </src-port>\n"
            "                        <dst-address>\n"
            "                            <virtual-network>\n"
            "                                vn2\n"
            "                            </virtual-network>\n"
            "                        </dst-address>\n"
            "                        <dst-port>\n"
            "                        </dst-port>\n"
            "                    </match-condition>\n"
            "                    <action-list>\n"
            "                        <simple-action>\n"
            "                            pass\n"
            "                        </simple-action>\n"
            "                    </action-list>\n"
            "                </acl-rule>\n"
            "           </access-control-list-entries>\n"
            "       </node>\n"
            "   </update>\n"
            "</config>\n", node_name, name, id);
    string s(buff);
    return s;
}

static string AddNodeXmlString(const char *node_name, const char *name, int id) {
    char buff[1024];

    sprintf(buff, 
            "<?xml version=\"1.0\"?>\n"
            "<config>\n"
            "   <update>\n"
            "       <node type=\"%s\">\n"
            "           <name>%s</name>\n"
            "           <id-perms>\n"
            "               <permissions>\n"
            "                   <owner></owner>\n"
            "                   <owner_access>0</owner_access>\n"
            "                   <group></group>\n"
            "                   <group_access>0</group_access>\n"
            "                   <other_access>0</other_access>\n"
            "               </permissions>\n"
            "               <uuid>\n"
            "                   <uuid-mslong>0</uuid-mslong>\n"
            "                   <uuid-lslong>%d</uuid-lslong>\n"
            "               </uuid>\n"
            "           </id-perms>\n"
            "       </node>\n"
            "   </update>\n"
            "</config>\n", node_name, name, id);
    string s(buff);
    return s;
}

void AddAcl(const char *name, int id) {
    std::string s = AddAclXmlString("access-control-list", name, id);
    pugi::xml_document xdoc_;

    pugi::xml_parse_result result = xdoc_.load(s.c_str());
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
}

void AddAclFromFile() {
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result =
            xdoc_.load_file("controller/src/vnsw/agent/filter/test/acl_cfg_test.xml");
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);    
}

// Create and delete ACEs
TEST_F(AclTest, Basic) {

    boost::uuids::string_generator gen;

    AclTable *table = Agent::GetInstance()->acl_table();
    assert(table);
    LOG(DEBUG, "db.acl.0:0x" << table);

    AclSpec acl_spec;
    uuid acl_id = gen("00000000-0000-0000-0000-000000000010");
    acl_spec.acl_id = acl_id;

    AclEntrySpec ae_spec;
    ae_spec.id = 10;
    acl_spec.acl_entry_specs_.push_back(ae_spec);
    
    DBRequest req;
    AclKey *key = new AclKey(acl_id);
    AclData *pd = new AclData(acl_spec);

    req.key.reset(key);
    req.data.reset(pd);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    table->Enqueue(&req);

    AclSpec acl_spec1;
    acl_id = gen("00000000-0000-0000-0000-000000000001");
    acl_spec1.acl_id = acl_id;

    AclEntrySpec ae_spec1;
    ae_spec1.id = 10;
    ae_spec1.terminal = true;
    ActionSpec action;
    action.ta_type = TrafficAction::SIMPLE_ACTION;
    action.simple_action = TrafficAction::PASS;
    ae_spec1.action_l.push_back(action);
    acl_spec1.acl_entry_specs_.push_back(ae_spec1);
    
    DBRequest req1;
    AclKey *key1 = new AclKey(acl_id);
    AclData *pd1 = new AclData(acl_spec1);

    req1.key.reset(key1);
    req1.data.reset(pd1);
    req1.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    table->Enqueue(&req1);
    client->WaitForIdle();
    
    AclKey key_1 = AclKey(acl_id);
    EXPECT_TRUE(NULL != table->FindActiveEntry(&key_1));

    DBRequest req2;
    AclKey *key2 = new AclKey(acl_id);
    req2.key.reset(key2);
    req2.oper = DBRequest::DB_ENTRY_DELETE;
    table->Enqueue(&req2);
    client->WaitForIdle();

    EXPECT_TRUE(NULL == table->FindActiveEntry(&key_1));

}

TEST_F(AclTest, Basic1) {
    boost::uuids::string_generator gen;
    AclTable *table = Agent::GetInstance()->acl_table();
    assert(table);

    AclSpec acl_spec;
    uuid acl_id = gen("00000000-0000-0000-0000-000000000012");
    acl_spec.acl_id = acl_id;

    AclEntrySpec ae_spec;
    ae_spec.id = 12;
    ae_spec.terminal = false;
    ActionSpec action;
    action.ta_type = TrafficAction::SIMPLE_ACTION;
    action.simple_action = TrafficAction::DENY;
    ae_spec.action_l.push_back(action);
    acl_spec.acl_entry_specs_.push_back(ae_spec);
    

    AclEntrySpec ae_spec1;
    ae_spec1.id = 10;
    ae_spec1.terminal = true;
    action.ta_type = TrafficAction::SIMPLE_ACTION;
    action.simple_action = TrafficAction::PASS;
    ae_spec1.action_l.push_back(action);
    acl_spec.acl_entry_specs_.push_back(ae_spec1);

    DBRequest req;
    AclKey *key = new AclKey(acl_id);
    AclData *pd = new AclData(acl_spec);

    req.key.reset(key);
    req.data.reset(pd);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    table->Enqueue(&req);
    client->WaitForIdle();

    DBRequest req2;
    AclKey *key2 = new AclKey(acl_id);
    req2.key.reset(key2);
    req2.oper = DBRequest::DB_ENTRY_DELETE;
    table->Enqueue(&req2);
    client->WaitForIdle();

}


TEST_F(AclTest, PacketMatching) {

    boost::uuids::string_generator gen;
    AclTable *table = Agent::GetInstance()->acl_table();
    assert(table);

    AclSpec acl_spec;
    uuid acl_id = gen("00000000-0000-0000-0000-000000000012");
    acl_spec.acl_id = acl_id;

    AclEntrySpec ae_spec;
    ae_spec.id = 12;
    ae_spec.terminal = false;
    ActionSpec action;
    action.ta_type = TrafficAction::SIMPLE_ACTION;
    action.simple_action = TrafficAction::DENY;
    ae_spec.action_l.push_back(action);
    acl_spec.acl_entry_specs_.push_back(ae_spec);

    ae_spec.id = 10;
    ae_spec.terminal = false;
    action.ta_type = TrafficAction::SIMPLE_ACTION;
    action.simple_action = TrafficAction::PASS;
    ae_spec.action_l.push_back(action);
    acl_spec.acl_entry_specs_.push_back(ae_spec);

    DBRequest req;
    AclKey *key = new AclKey(acl_id);
    AclData *pd = new AclData(acl_spec);

    req.key.reset(key);
    req.data.reset(pd);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    table->Enqueue(&req);
    client->WaitForIdle();

    AclKey key_1 = AclKey(acl_id);
    AclDBEntry *acl1 = static_cast<AclDBEntry *>(table->FindActiveEntry(&key_1));
    EXPECT_TRUE(acl1 != NULL);

    PacketHeader *packet1 = new PacketHeader();
    AclEntryIDList ace_list;
    packet1->src_ip = 0x01010102;
    packet1->dst_ip = 0xFFFFFFFF;
    packet1->protocol = 10;
    packet1->dst_port = 99;
    MatchAclParams m_acl;
    m_acl.acl = acl1;
    EXPECT_TRUE(acl1->PacketMatch(*packet1, m_acl, NULL));
    uint32_t action_val = ((1 << TrafficAction::DENY) | (1 << TrafficAction::PASS));
    EXPECT_EQ(action_val, m_acl.action_info.action);
    delete packet1;
}

TEST_F(AclTest, Config) {
    pugi::xml_document xdoc_;
    xdoc_.load_file("controller/src/vnsw/agent/filter/test/acl_cfg_test.xml");
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);    
    client->WaitForIdle();

    AclTable *table = Agent::GetInstance()->acl_table();
    boost::uuids::string_generator gen;
    uuid acl_id = gen("65babf07-3bcb-4d38-b920-be3355f11126");
    AclKey key_1 = AclKey(acl_id);
    AclDBEntry *acl1 = static_cast<AclDBEntry *>(table->FindActiveEntry(&key_1));
    EXPECT_TRUE(acl1 != NULL);

    PacketHeader *packet1 = new PacketHeader();
    MatchAclParams m_acl;
    std::string vn11("vn11");
    std::string vn21("vn21");
    packet1->src_policy_id = &vn11;
    packet1->dst_policy_id = &vn21;
    packet1->protocol = 10;
    packet1->dst_port = 100;
    packet1->src_port = 100;
    m_acl.acl = acl1;
    EXPECT_TRUE(acl1->PacketMatch(*packet1, m_acl, NULL));
    uint32_t action = (1 << TrafficAction::DENY);
    EXPECT_EQ(action, m_acl.action_info.action);
    delete packet1;
}


} //namespace

int main (int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
