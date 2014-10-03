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
public:
    virtual void SetUp() {client->Reset();}
    virtual void TearDown() {
        DelAcl("acl1");
        client->WaitForIdle();
        client->Reset();
    }
    void AddAcl(std::string name, uint32_t id, const char *acl_rule) {
        char buff[10240];
        sprintf(buff,
                "<?xml version=\"1.0\"?>\n"
                "<config>\n"
                "   <update>\n"
                "       <node type=\"access-control-list\">\n"
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
                "                  %s\n"
                "           </access-control-list-entries>\n"
                "       </node>\n"
                "   </update>\n"
                "</config>\n", name.c_str(), id, acl_rule);

        pugi::xml_document xdoc_;
        pugi::xml_parse_result result = xdoc_.load(buff);
        EXPECT_TRUE(result);
        Agent::GetInstance()->ifmap_parser()->
            ConfigParse(xdoc_.first_child(), 0);
        client->WaitForIdle();
    }

    std::string GetActionString(std::string action) {
        std::string pass = "<action-list>\n"
                           "    <simple-action>pass</simple-action>\n"
                           "</action-list>\n";
        std::string deny = "<action-list>\n"
                           "    <simple-action>deny</simple-action>\n"
                           "</action-list>\n";
        if (action == "pass")  {
            return pass;
        }
        return deny;
    }

    std::string GetMirrorAction(std::string name, std::string vrf,
                                std::string ip) {
        std::ostringstream mirror;
        mirror <<  "<action-list>\n <simple-action>pass</simple-action>\n"
                   "<mirror-to>";
        mirror << "<analyzer-name>" << name << "</analyzer-name>";
        mirror << "<analyzer-ip-address>" << ip << "</analyzer-ip-address>";
        mirror << "<routing-instance>" << vrf << "</routing-instance>";
        mirror << "<udp-port>" << "8159" << "</udp-port>";
        mirror << "</mirror-to>";
        mirror << "</action-list>";
        return mirror.str();
    }

    std::string GetVrfAssignAction(std::string vrf_name) {
        std::ostringstream vrf_action;
        vrf_action << "<action-list>\n"
                      "    <simple-action>pass</simple-action>\n";
        vrf_action << "<assign-routing-instance>" << vrf_name
                   << "</assign-routing-instance>";
        vrf_action << "</action-list>";
        return vrf_action.str();
    }

    std::string GetVnAclString(std::string source_vn, std::string dest_vn,
                               std::string proto, uint32_t sport_start,
                               uint32_t sport_end, uint32_t dport_start,
                               uint32_t dport_end) {
        char buff[10240];
        sprintf(buff,
                "<match-condition>\n"
                "    <protocol>\n"
                "        %s\n"
                "    </protocol>\n"
                "    <src-address>\n"
                "         <virtual-network>\n"
                "              %s\n"
                "         </virtual-network>\n"
                "    </src-address>\n"
                "    <src-port>\n"
                "         <start-port>\n"
                "              %d\n"
                "         </start-port>\n"
                "         <end-port>\n"
                "               %d\n"
                "         </end-port>\n"
                "    </src-port>\n"
                "    <dst-address>\n"
                "         <virtual-network>\n"
                "               %s\n"
                "         </virtual-network>\n"
                "    </dst-address>\n"
                "    <dst-port>\n"
                "         <start-port>\n"
                "              %d\n"
                "         </start-port>\n"
                "         <end-port>\n"
                "              %d\n"
                "         </end-port>\n"
                "    </dst-port>\n"
                "</match-condition>\n",
            proto.c_str(), source_vn.c_str(), sport_start, sport_end,
            dest_vn.c_str(), dport_start, dport_end);
        return std::string(buff);
    }

    void AddVnAcl(std::string name, uint32_t id, std::string source_vn,
                  std::string dest_vn, std::string proto, uint32_t sport_start,
                  uint32_t sport_end, uint32_t dport_start, uint32_t dport_end,
                  std::string action) {
        std::string match_condition = GetVnAclString(source_vn, dest_vn, proto,
                                                     sport_start, sport_end,
                                                     dport_start, dport_end);
        std::string action_list = GetActionString(action);
        char buff[10240];
        sprintf(buff,
                "<acl-rule>\n"
                " %s\n"
                " %s\n"
                "</acl-rule>\n", match_condition.c_str(), action_list.c_str());
        AddAcl(name, id, buff);
    }

    void AddVnAcl(std::string name, uint32_t id, std::string source_vn,
                  std::string dest_vn, std::string action) {
        AddVnAcl(name, id, source_vn, dest_vn, "all", 0, 65535, 0, 65535, "pass");
    }

    std::string GetSgAclString(uint32_t source_id, uint32_t dest_id,
                               std::string proto, uint32_t sport_start,
                               uint32_t sport_end, uint32_t dport_start,
                               uint32_t dport_end) {
        char buff[10240];
        sprintf(buff,
                "<match-condition>\n"
                "     <protocol>\n"
                "         %s\n"
                "     </protocol>\n"
                "     <src-address>\n"
                "          <security-group>\n"
                "                %d\n"
                "          </security-group>\n"
                "     </src-address>\n"
                "     <src-port>\n"
                "          <start-port>\n"
                "               %d\n"
                "          </start-port>\n"
                "          <end-port>\n"
                "             %d\n"
                "          </end-port>\n"
                "     </src-port>\n"
                "     <dst-address>\n"
                "          <security-group>\n"
                "               %d\n"
                "          </security-group>\n"
                "     </dst-address>\n"
                "     <dst-port>\n"
                "          <start-port>\n"
                "              %d\n"
                "          </start-port>\n"
                "          <end-port>\n"
                "              %d\n"
                "          </end-port>\n"
                "     </dst-port>\n"
                "</match-condition>\n",
            proto.c_str(), source_id, sport_start, sport_end,
            dest_id, dport_start, dport_end);
        return std::string(buff);
    }

    void AddSgAcl(std::string name, uint32_t id, uint32_t source_id,
                  uint32_t dest_id, std::string proto, uint32_t sport_start,
                  uint32_t sport_end, uint32_t dport_start, uint32_t dport_end,
                  std::string action) {
        std::string match_condition = GetSgAclString(source_id, dest_id, proto,
                                                     sport_start, sport_end,
                                                     dport_start, dport_end);
        std::string action_list = GetActionString(action);
        char buff[10240];
        sprintf(buff,
                "<acl-rule>\n"
                " %s\n"
                " %s\n"
                "</acl-rule>\n", match_condition.c_str(), action_list.c_str());
        AddAcl(name, id, buff);
    }

    void AddSgAcl(std::string name, uint32_t id, uint32_t source_id,
                  uint32_t dest_id, std::string action) {
        AddSgAcl(name, id, source_id, dest_id, "all", 0, 65535, 0, 65535, "pass");
    }

    std::string GetIpAclString(std::string source_ip, uint32_t source_plen,
                               std::string dest_ip, uint32_t dest_plen,
                               std::string proto, uint32_t sport_start,
                               uint32_t sport_end, uint32_t dport_start,
                               uint32_t dport_end) {
        char buff[10240];
        sprintf(buff,
                "<match-condition>\n"
                "    <protocol>\n"
                "        %s\n"
                "    </protocol>\n"
                "    <src-address>\n"
                "        <subnet>"
                "            <ip-prefix> "
                "                  %s"
                "            </ip-prefix>"
                "            <ip-prefix-len>"
                "                  %d"
                "             </ip-prefix-len>"
                "          </subnet>"
                "     </src-address>\n"
                "     <src-port>\n"
                "         <start-port>\n"
                "               %d\n"
                "         </start-port>\n"
                "         <end-port>\n"
                "              %d\n"
                "         </end-port>\n"
                "     </src-port>\n"
                "     <dst-address>\n"
                "          <subnet>\n"
                "              <ip-prefix>\n"
                "                  %s\n"
                "              </ip-prefix>\n"
                "              <ip-prefix-len>\n"
                "                   %d\n"
                "              </ip-prefix-len>"
                "          </subnet>\n"
                "     </dst-address>\n"
                "     <dst-port>\n"
                "          <start-port>\n"
                "              %d\n"
                "          </start-port>\n"
                "          <end-port>\n"
                "             %d\n"
                "          </end-port>\n"
                "    </dst-port>\n"
                "</match-condition>\n",
            proto.c_str(), source_ip.c_str(), source_plen, sport_start,
            sport_end, dest_ip.c_str(), dest_plen, dport_start, dport_end);
        return std::string(buff);
    }
    void AddIpAcl(std::string name, uint32_t id, std::string source_ip, 
                  uint32_t source_plen, std::string dest_ip, uint32_t dest_plen,
                  std::string proto, uint32_t sport_start, uint32_t sport_end,
                  uint32_t dport_start, uint32_t dport_end, std::string action) {
        std::string match_condition = GetIpAclString(source_ip, source_plen,
                                                     dest_ip, dest_plen, proto,
                                                     sport_start, sport_end,
                                                     dport_start, dport_end);
        std::string action_list = GetActionString(action);
        char buff[10240];
        sprintf(buff,
                "<acl-rule>\n"
                " %s\n"
                " %s\n"
                "</acl-rule>\n", match_condition.c_str(), action_list.c_str());
        AddAcl(name, id, buff);
    }

    void AddIpAcl(std::string name, uint32_t id, std::string source_ip, 
                  uint32_t source_plen, std::string dest_ip, uint32_t dest_plen,
                  std::string action) {
        AddIpAcl(name, id, source_ip, source_plen, dest_ip, dest_plen, "all", 
                 0, 65535, 0, 65535, "pass");
    }
protected:
};

TEST_F(AclTest, vn_acl_no_change_1) {
    AddVnAcl("acl1", 1, "vn1", "vn2", "pass");
    client->AclNotifyWait(1);
    //Enqueue same ACL request
    AddVnAcl("acl1", 1, "vn1", "vn2", "pass");
    client->WaitForIdle();
    //ACL is not changed, hence shouldnt be notified
    EXPECT_TRUE(client->acl_notify() == 1);
}

TEST_F(AclTest, vn_acl_no_change_2) {
    AddVnAcl("acl1", 1, "vn1", "vn2", "tcp", 1, 10, 1, 10, "pass");
    client->AclNotifyWait(1);
    //Enqueue same ACL request
    AddVnAcl("acl1", 1, "vn1", "vn2", "tcp", 1, 10, 1, 10, "pass");
    client->WaitForIdle();
    //ACL is not changed, hence shouldnt be notified
    EXPECT_TRUE(client->acl_notify() == 1);
}

TEST_F(AclTest, vn_change_1) {
    AddVnAcl("acl1", 1, "vn1", "vn2", "pass");
    client->AclNotifyWait(1);
    //Change VN and enqueue request
    AddVnAcl("acl1", 1, "vn1", "vn3", "pass");
    client->AclNotifyWait(2);
}

TEST_F(AclTest, vn_change_2) {
    AddVnAcl("acl1", 1, "vn1", "vn2", "pass");
    client->AclNotifyWait(1);
    //Change action and enqueue a change request
    AddVnAcl("acl1", 1, "vn1", "vn3", "deny");
    client->AclNotifyWait(2);
}

TEST_F(AclTest, vn_change_3) {
    AddVnAcl("acl1", 1, "vn1", "vn2", "tcp", 1, 10, 1, 10, "pass");
    client->AclNotifyWait(1);
    //Change destination port list and enqueue a change request
    AddVnAcl("acl1", 1, "vn1", "vn2", "tcp", 1, 10, 1, 11, "pass");
    client->AclNotifyWait(2);
}

TEST_F(AclTest, vn_change_4) {
    AddVnAcl("acl1", 1, "vn1", "vn2", "tcp", 1, 10, 1, 10, "pass");
    client->AclNotifyWait(1);
    //Change destination port list and enqueue a change request
    AddVnAcl("acl1", 1, "vn1", "vn2", "tcp", 1, 10, 2, 10, "pass");
    client->AclNotifyWait(2);
}

TEST_F(AclTest, vn_change_5) {
    AddVnAcl("acl1", 1, "vn1", "vn2", "tcp", 1, 10, 1, 10, "pass");
    client->AclNotifyWait(1);
    //Change source port list and enqueue a change request
    AddVnAcl("acl1", 1, "vn1", "vn2", "tcp", 2, 10, 1, 10, "pass");
    client->AclNotifyWait(2);
}

TEST_F(AclTest, vn_change_6) {
    AddVnAcl("acl1", 1, "vn1", "vn2", "tcp", 1, 10, 1, 10, "pass");
    client->AclNotifyWait(1);
    //Change source port list and enqueue a change request
    AddVnAcl("acl1", 1, "vn1", "vn2", "tcp", 1, 11, 1, 10, "pass");
    client->AclNotifyWait(2);
}

TEST_F(AclTest, vn_change_7) {
    AddVnAcl("acl1", 1, "vn1", "vn2", "any", 1, 10, 1, 10, "pass");
    client->AclNotifyWait(1);
    //Change source port list and enqueue a change request
    AddVnAcl("acl1", 1, "vn1", "vn2", "tcp", 1, 11, 1, 10, "pass");
    client->AclNotifyWait(2);
}

TEST_F(AclTest, sg_no_change_1) {
    AddSgAcl("acl1", 1, 1, 5, "pass");
    client->AclNotifyWait(1);
    //Enqueue same ACL request
    AddSgAcl("acl1", 1, 1, 5, "pass");
    client->WaitForIdle();
    //ACL is not changed, hence shouldnt be notified
    EXPECT_TRUE(client->acl_notify() == 1);
}

TEST_F(AclTest, sg_change_1) {
    AddSgAcl("acl1", 1, 1, 2, "pass");
    client->AclNotifyWait(1);
    //Change dest sg ID
    AddSgAcl("acl1", 1, 1, 3, "pass");
    client->AclNotifyWait(2);
}

TEST_F(AclTest, sg_change_2) {
    AddSgAcl("acl1", 1, 1, 2, "pass");
    client->AclNotifyWait(1);
    //Change source sg ID
    AddSgAcl("acl1", 1, 3, 2, "pass");
    client->AclNotifyWait(2);
}

TEST_F(AclTest, ip_prefix_no_change_1) {
    AddIpAcl("acl1", 1, "1.1.2.0", 24, "2.2.2.0", 24, "pass");
    client->AclNotifyWait(1);
    //Enqueue same ACL request
    AddIpAcl("acl1", 1, "1.1.2.0", 24, "2.2.2.0", 24, "pass");
    client->WaitForIdle();
    //ACL is not changed, hence shouldnt be notified
    EXPECT_TRUE(client->acl_notify() == 1);
}

TEST_F(AclTest, ip_prefix_change_1) {
    AddIpAcl("acl1", 1, "1.1.1.0", 24, "2.2.2.0", 24, "pass");
    client->AclNotifyWait(1);
    //Change source IP prefix length for match condition
    AddIpAcl("acl1", 1, "1.1.1.0", 23, "2.2.2.0", 24, "pass");
    client->AclNotifyWait(2);
}

TEST_F(AclTest, ip_prefix_change_2) {
    AddIpAcl("acl1", 1, "1.1.1.0", 24, "2.2.2.0", 24, "pass");
    client->AclNotifyWait(1);
    //Change source IP for match condition
    AddIpAcl("acl1", 1, "2.1.1.0", 24, "2.2.2.0", 24, "pass");
    client->AclNotifyWait(2);
}

TEST_F(AclTest, ip_prefix_change_3) {
    AddIpAcl("acl1", 1, "1.1.1.0", 24, "2.2.2.0", 24, "pass");
    client->AclNotifyWait(1);
    //Change dest IP prefix length for match condition
    AddIpAcl("acl1", 1, "1.1.1.0", 24, "2.2.2.0", 20, "pass");
    client->AclNotifyWait(2);
}

TEST_F(AclTest, ip_prefix_change_4) {
    AddIpAcl("acl1", 1, "1.1.1.0", 24, "2.2.2.0", 24, "pass");
    client->AclNotifyWait(1);
    //Change destination IP for match condition
    AddIpAcl("acl1", 1, "1.1.1.0", 23, "1.2.2.0", 24, "pass");
    client->AclNotifyWait(2);
}

TEST_F(AclTest, TestAceAdd_1) {
    std::string ace_1 = GetVnAclString("vn1", "vn2", "any", 1, 10, 1, 10);
    std::string ace_2 = GetVnAclString("vn1", "vn2", "tcp", 1, 10, 1, 10);

    std::string action_list = GetActionString("pass");
    char buff[10240];
    sprintf(buff,
            "<acl-rule>\n"
            " %s\n"
            " %s\n"
            "</acl-rule>\n", ace_1.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(1);
    AclDBEntry *acl = AclGet(1);
    EXPECT_TRUE(acl != NULL);
    EXPECT_TRUE(acl->ace_count() == 1);
    sprintf(buff,
            "<acl-rule>\n %s\n %s\n </acl-rule>\n"
             "<acl-rule>\n %s\n %s\n </acl-rule>\n",
            ace_1.c_str(), action_list.c_str(),
            ace_2.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(2);
    EXPECT_TRUE(acl->ace_count() == 2);
}

TEST_F(AclTest, TestAceDel_1) {
    std::string ace_1 = GetVnAclString("vn1", "vn2", "any", 1, 10, 1, 10);
    std::string ace_2 = GetVnAclString("vn1", "vn2", "tcp", 1, 10, 1, 10);
    std::string action_list = GetActionString("pass");
    char buff[10240];
    sprintf(buff,
            "<acl-rule>\n %s\n %s\n </acl-rule>\n"
            "<acl-rule>\n %s\n %s\n </acl-rule>\n",
            ace_1.c_str(), action_list.c_str(),
            ace_2.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(1);
    AclDBEntry *acl = AclGet(1);
    EXPECT_TRUE(acl != NULL);
    EXPECT_TRUE(acl->ace_count() == 2);

    sprintf(buff,
            "<acl-rule>\n"
            " %s\n"
            " %s\n"
            "</acl-rule>\n", ace_1.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(2);
    EXPECT_TRUE(acl->ace_count() == 1);
}

TEST_F(AclTest, MirrorActionNoChange_1) {
    std::string ace_1 = GetVnAclString("vn1", "vn2", "any", 1, 10, 1, 10);
    std::string action_list = GetMirrorAction("mirror1", "vn1", "10.1.1.1");
    char buff[10240];
    sprintf(buff,
            "<acl-rule>\n"
            " %s\n %s\n"
            "</acl-rule>\n", ace_1.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(1);

    action_list = GetMirrorAction("mirror1", "vn1", "10.1.1.1");
    sprintf(buff,
            "<acl-rule>\n"
            " %s\n %s\n"
            "</acl-rule>\n", ace_1.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(1);
}

TEST_F(AclTest, MirrorActionChange_1) {
    std::string ace_1 = GetVnAclString("vn1", "vn2", "any", 1, 10, 1, 10);
    std::string action_list = GetMirrorAction("mirror1", "vn1", "10.1.1.1");
    char buff[10240];
    sprintf(buff,
            "<acl-rule>\n"
            " %s\n %s\n"
            "</acl-rule>\n", ace_1.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(1);

    //Change destination IP address of analyzer
    action_list = GetMirrorAction("mirror1", "vn1", "10.1.1.2");
    sprintf(buff,
            "<acl-rule>\n"
            " %s\n %s\n"
            "</acl-rule>\n", ace_1.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(2);
}

TEST_F(AclTest, MirrorActionChange_2) {
    std::string ace_1 = GetVnAclString("vn1", "vn2", "any", 1, 10, 1, 10);
    std::string action_list = GetMirrorAction("mirror1", "vn1", "10.1.1.1");
    char buff[10240];
    sprintf(buff,
            "<acl-rule>\n"
            " %s\n %s\n"
            "</acl-rule>\n", ace_1.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(1);

    //Change destination VRF address of analyzer
    action_list = GetMirrorAction("mirror1", "vn2", "10.1.1.2");
    sprintf(buff,
            "<acl-rule>\n"
            " %s\n %s\n"
            "</acl-rule>\n", ace_1.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(2);
}

TEST_F(AclTest, VrfAssignActionNoChange_1) {
    std::string ace_1 = GetVnAclString("vn1", "vn2", "any", 1, 10, 1, 10);
    std::string action_list = GetVrfAssignAction("vn1");
    char buff[10240];
    sprintf(buff,
            "<acl-rule>\n"
            " %s\n %s\n"
            "</acl-rule>\n", ace_1.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(1);

    action_list = GetVrfAssignAction("vn1");
    sprintf(buff,
            "<acl-rule>\n"
            " %s\n %s\n"
            "</acl-rule>\n", ace_1.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(1);
}

TEST_F(AclTest, VrfAssignActionChange_1) {
    std::string ace_1 = GetVnAclString("vn1", "vn2", "any", 1, 10, 1, 10);
    std::string action_list = GetVrfAssignAction("vn1");
    char buff[10240];
    sprintf(buff,
            "<acl-rule>\n"
            " %s\n %s\n"
            "</acl-rule>\n", ace_1.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(1);

    action_list = GetVrfAssignAction("vn2");
    sprintf(buff,
            "<acl-rule>\n"
            " %s\n %s\n"
            "</acl-rule>\n", ace_1.c_str(), action_list.c_str());
    AddAcl("acl1", 1, buff);
    client->AclNotifyWait(2);
}
}


int main (int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
