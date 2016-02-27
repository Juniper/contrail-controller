/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include <base/task.h>
#include <base/test/task_test_util.h>

VmInterface *vnet[16];
char vnet_addr[16][128];

int hash_id;

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input1[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:01:01:01:01", 1, 1, "::1.1.1.1"},
    {"vnet2", 2, "1.1.1.2", "00:00:01:01:01:02", 1, 2, "::1.1.1.2"},
};

struct PortInfo input2[] = {
    {"vnet3", 3, "1.1.1.3", "00:00:01:01:01:03", 1, 3, "::1.1.1.3"},
    {"vnet4", 4, "1.1.1.4", "00:00:01:01:01:04", 1, 4, "::1.1.1.4"},
};

typedef enum {
    INGRESS = 0,
    EGRESS = 1,
    BIDIRECTION = 2
} AclDirection;

static string AddAclXmlString(const char *node_name, const char *name, int id,
                              int proto, const char *action, const char *sip,
                              const char *dip) {
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
    "                        <src-address>\n"
    "                            <subnet>\n"
    "                                <ip-prefix>%s</ip-prefix>\n"
    "                                <ip-prefix-len>0</ip-prefix-len>\n"
    "                            </subnet>\n"
    "                        </src-address>\n"
    "                        <protocol>%d</protocol>\n"
    "                        <src-port>\n"
    "                            <start-port> 0 </start-port>\n"
    "                            <end-port> 10000 </end-port>\n"
    "                        </src-port>\n"
    "                        <dst-address>\n"
    "                            <subnet>\n"
    "                                <ip-prefix>%s</ip-prefix>\n"
    "                                <ip-prefix-len>0</ip-prefix-len>\n"
    "                            </subnet>\n"
    "                        </dst-address>\n"
    "                        <dst-port>\n"
    "                            <start-port> 0 </start-port>\n"
    "                            <end-port> 10000 </end-port>\n"
    "                        </dst-port>\n"
    "                    </match-condition>\n"
    "                    <action-list>\n"
    "                        <simple-action>\n"
    "                            %s\n"
    "                        </simple-action>\n"
    "                    </action-list>\n"
    "                </acl-rule>\n"
    "           </access-control-list-entries>\n"
    "       </node>\n"
    "   </update>\n"
    "</config>\n", node_name, name, id, sip, proto, dip, action);
    string s(buff);
    return s;
}

static void AddAclEntry(const char *name, int id, int proto,
                        const char *action, AclDirection direction,
                        const char *sip, const char *dip) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    strncpy(acl_name, name, max_len);
    if (direction == EGRESS) {
        strncat(acl_name, "egress-access-control-list", max_len);
    } else {
        strncat(acl_name, "ingress-access-control-list", max_len);
    }
    std::string s = AddAclXmlString("access-control-list", acl_name, id, proto,
                                    action, sip, dip);
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(s.c_str());
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
    client->WaitForIdle();
}

static string AddSgIdAclXmlString(const char *node_name, const char *name, int id,
                                  int proto, int src_sg_id, int dest_sg_id,
                                  const char *action) {
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
    "                        <src-address>\n"
    "                            <security-group> %d </security-group>\n"
    "                        </src-address>\n"
    "                        <protocol>%d</protocol>\n"
    "                        <src-port>\n"
    "                            <start-port> 0 </start-port>\n"
    "                            <end-port> 10000 </end-port>\n"
    "                        </src-port>\n"
    "                        <dst-address>\n"
    "                            <security-group> %d </security-group>\n"
    "                        </dst-address>\n"
    "                        <dst-port>\n"
    "                            <start-port> 0 </start-port>\n"
    "                            <end-port> 10000 </end-port>\n"
    "                        </dst-port>\n"
    "                        <ethertype>IPv6</ethertype>\n"
    "                    </match-condition>\n"
    "                    <action-list>\n"
    "                        <simple-action>\n"
    "                            %s\n"
    "                        </simple-action>\n"
    "                    </action-list>\n"
    "                </acl-rule>\n"
    "                <acl-rule>\n"
    "                    <match-condition>\n"
    "                        <src-address>\n"
    "                            <security-group> %d </security-group>\n"
    "                        </src-address>\n"
    "                        <protocol>%d</protocol>\n"
    "                        <src-port>\n"
    "                            <start-port> 0 </start-port>\n"
    "                            <end-port> 10000 </end-port>\n"
    "                        </src-port>\n"
    "                        <dst-address>\n"
    "                            <security-group> %d </security-group>\n"
    "                        </dst-address>\n"
    "                        <dst-port>\n"
    "                            <start-port> 0 </start-port>\n"
    "                            <end-port> 10000 </end-port>\n"
    "                        </dst-port>\n"
    "                        <ethertype>IPv6</ethertype>\n"
    "                    </match-condition>\n"
    "                    <action-list>\n"
    "                        <simple-action>\n"
    "                            %s\n"
    "                        </simple-action>\n"
    "                    </action-list>\n"
    "                </acl-rule>\n"
    "           </access-control-list-entries>\n"
    "       </node>\n"
    "   </update>\n"
    "</config>\n", node_name, name, id, src_sg_id, proto, dest_sg_id, action,
                   dest_sg_id, proto, src_sg_id, action);
    string s(buff);
    return s;
}

static void AddSgIdAcl(const char *name, int id, int proto,
                       int src_sg_id, int dest_sg_id, const char *action,
                       AclDirection direction) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    strncpy(acl_name, name, max_len);
    if (direction == EGRESS) {
        strncat(acl_name, "egress-access-control-list", max_len);
    } else {
        strncat(acl_name, "ingress-access-control-list", max_len);
    }
    std::string s = AddSgIdAclXmlString("access-control-list", acl_name, id, proto,
                                        src_sg_id, dest_sg_id, action);
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(s.c_str());
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
    client->WaitForIdle();
}

static void AddSgEntry(const char *sg_name, const char *name, int id,
                       int proto, const char *action, uint32_t sg_id,
                       uint32_t dest_sg_id, AclDirection direction) {
    std::stringstream str;
    str << "<security-group-id>" << sg_id << "</security-group-id>" << endl;
    AddNode("security-group", sg_name, id, str.str().c_str());
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    strncpy(acl_name, name, max_len);
    switch (direction) {
        case INGRESS:
            AddSgIdAcl(name, id, proto, sg_id, dest_sg_id, action, direction);
            strncat(acl_name, "ingress-access-control-list", max_len);
            AddLink("security-group", sg_name, "access-control-list", acl_name);
            break;
        case EGRESS:
            AddSgIdAcl(name, id, proto, sg_id, dest_sg_id, action, direction);
            strncat(acl_name, "egress-access-control-list", max_len);
            AddLink("security-group", sg_name, "access-control-list", acl_name);
            break;
        case BIDIRECTION:
            AddSgIdAcl(name, id, proto, sg_id, dest_sg_id, action, EGRESS);
            strncat(acl_name, "egress-access-control-list", max_len);
            AddLink("security-group", sg_name, "access-control-list", acl_name);

            strncpy(acl_name, name, max_len);
            AddSgIdAcl(name, id+1, proto, sg_id, dest_sg_id, action, INGRESS);
            strncat(acl_name, "ingress-access-control-list", max_len);
            AddLink("security-group", sg_name, "access-control-list", acl_name);
            break;
    }
}

static void DelSgAcl(const char *name) {
    char acl_name[1024];
    strcpy(acl_name, name);
    strcat(acl_name, "ingress-access-control-list");
    DelNode("access-control-list", acl_name);
    client->WaitForIdle();

    strcpy(acl_name, name);
    strcat(acl_name, "egress-access-control-list");
    DelNode("access-control-list", acl_name);
    client->WaitForIdle();
}

static void DelSgAclLink(const char *sg_name, const char *acl_name) {
    char buff[1024];

    strcpy(buff, acl_name);
    strcat(buff, "egress-access-control-list");
    DelLink("security-group", sg_name, "access-control-list", buff);

    strcpy(buff, acl_name);
    strcat(buff, "ingress-access-control-list");
    DelLink("security-group", sg_name, "access-control-list", buff);
}

static void AddSgEntry(const char *sg_name, const char *name, int id,
                       int proto, const char *action, AclDirection direction,
                       const char *sip, const char *dip) {

    AddSg(sg_name, 1);
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    strncpy(acl_name, name, max_len);
    switch (direction) {
        case INGRESS:
            AddAclEntry(name, id, proto, action, direction, sip, dip);
            strncat(acl_name, "ingress-access-control-list", max_len);
            AddLink("security-group", sg_name, "access-control-list", acl_name);
            break;
        case EGRESS:
            AddAclEntry(name, id, proto, action, direction, sip, dip);
            strncat(acl_name, "egress-access-control-list", max_len);
            AddLink("security-group", sg_name, "access-control-list", acl_name);
            break;
        case BIDIRECTION:
            AddAclEntry(name, id, proto, action, EGRESS, sip, dip);
            strncat(acl_name, "egress-access-control-list", max_len);
            AddLink("security-group", sg_name, "access-control-list", acl_name);

            strncpy(acl_name, name, max_len);
            strncat(acl_name, "ingress-access-control-list", max_len);
            AddAclEntry(name, id+1, proto, action, INGRESS, sip, dip);
            AddLink("security-group", sg_name, "access-control-list", acl_name);
            break;
    }
}

const VmInterface *GetVmPort(int id) {
    return static_cast<const VmInterface *>(vnet[id]);
}

static bool VmPortSetup(struct PortInfo *input, int count, int aclid) {
    bool ret = true;

    CreateV6VmportEnv(input, count,  aclid);
    client->WaitForIdle();

    for (int x = 0; x < count; x++) {
        int id = input[x].intf_id;

        WAIT_FOR(100, 1000, (VmPortActive(input, x)) == true);
        WAIT_FOR(100, 1000, (VmPortV6Active(input, x)) == true);
        if (VmPortActive(input, x) == false) {
            ret = false;
        }

        if (aclid) {
            WAIT_FOR(100, 1000, (VmPortPolicyEnable(input, x)) == true);
            if (VmPortPolicyEnable(input, x) == false) {
                ret = false;
            }
        }

        vnet[id] = VmInterfaceGet(id);
        if (vnet[id] == NULL) {
            ret = false;
        }

        strcpy(vnet_addr[id], vnet[id]->primary_ip6_addr().to_string().c_str());
    }

    return ret;
}

bool Init() {
    if (VmPortSetup(input1, 2, 0) == false)
        return false;

    if (VmPortSetup(input2, 2, 0) == false)
        return false;

    return true;
}

void Shutdown() {
    DeleteVmportEnv(input1, 2, true, 0, NULL, NULL, true, true);
    DeleteVmportEnv(input2, 2, true, 0, NULL, NULL, true, true);
    client->WaitForIdle();

    WAIT_FOR(100, 1000, (VmPortFind(input1, 0)) == false);
    WAIT_FOR(100, 1000, (VmPortFind(input1, 1)) == false);
    WAIT_FOR(100, 1000, (VmPortFind(input2, 0)) == false);
    WAIT_FOR(100, 1000, (VmPortFind(input2, 1)) == false);
}

class SgTestV6 : public ::testing::Test {
public:
    void CreateSG(const char *action, const char *sip, const char * dip,
                  int proto) {
        AddSgEntry("sg1", "sg_acl1", 10, proto, action, BIDIRECTION, sip, dip);
        AddLink("virtual-machine-interface", "vnet1", "security-group", "sg1");
        client->WaitForIdle();
        WAIT_FOR(100, 1000, (vnet[1]->sg_list().list_.size() == 1U));
    }

    void DeleteSG() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        char acl_name[1024];
        uint16_t max_len = sizeof(acl_name) - 1;

        WAIT_FOR(100, 1000, (proto_->FlowCount() == 0U));
        DelLink("virtual-machine-interface", "vnet1", "security-group", "sg1");
        client->WaitForIdle();
        strncpy(acl_name, "sg_acl1", max_len);
        strncat(acl_name, "egress-access-control-list", max_len);
        DelLink("security-group", "sg1", "access-control-list", acl_name);
        DelNode("access-control-list", acl_name);

        strncpy(acl_name, "sg_acl1", max_len);
        strncat(acl_name, "ingress-access-control-list", max_len);
        DelLink("security-group", "sg1", "access-control-list", acl_name);
        DelNode("access-control-list", acl_name);
        DelNode("security-group", "sg1");
        client->WaitForIdle();

        WAIT_FOR(100, 1000, (vnet[1]->sg_list().list_.size() == 0U));
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        proto_ = agent_->pkt()->get_flow_proto();
    }

    Agent *agent_;
    FlowProto *proto_;
};

bool ValidateAction(uint32_t vrfid, char *sip, char *dip, int proto, int sport,
                    int dport, int action, uint32_t nh_id) {
    bool ret = true;
    uint16_t lookup_dport = dport;
    if (proto == IPPROTO_ICMPV6) {
        lookup_dport = ICMP6_ECHO_REPLY;
    }
    FlowEntry *fe = FlowGet(vrfid, sip, dip, proto, sport, lookup_dport, nh_id);
    FlowEntry *rfe = fe->reverse_flow_entry();

    EXPECT_TRUE((fe->match_p().sg_action & (1 << action)) != 0);
    if ((fe->match_p().sg_action & (1 << action)) == 0) {
        ret = false;
    }

    if (fe->match_p().sg_action & (1 << TrafficAction::TRAP) ||
            rfe->match_p().sg_action & (1 << TrafficAction::TRAP)) {
        return ret;
    }

    if (!(fe->match_p().sg_action & (1 << TrafficAction::TRAP)) &&
        !(rfe->match_p().sg_action & (1 << TrafficAction::TRAP))) {
        EXPECT_EQ(fe->match_p().sg_action, rfe->match_p().sg_action);
        if (fe->match_p().sg_action != rfe->match_p().sg_action) {
            ret = false;
        }
    }

    EXPECT_EQ(fe->match_p().action_info.action,
              rfe->match_p().action_info.action);
    if (fe->match_p().action_info.action !=
        rfe->match_p().action_info.action) {
        ret = false;
    }

    return ret;
}

//Create SG with only IPv4 rules. Verify that IPv6 flow created will have
//action as drop
TEST_F(SgTestV6, Flow_Drop_1) {
    CreateSG("pass", "0.0.0.0", "0.0.0.0", IPPROTO_ICMPV6);
    TxIp6Packet(vnet[1]->id(), vnet_addr[1], vnet_addr[2], IPPROTO_ICMPV6);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->vrf_id(), vnet_addr[1],
                               vnet_addr[2], IPPROTO_ICMPV6, 0, 0,
                               TrafficAction::DENY,
                               vnet[1]->flow_key_nh()->id()));
    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1], vnet_addr[2],
                           IPPROTO_ICMPV6, 0, 0, vnet[1]->flow_key_nh()->id()));
    client->WaitForIdle();
    DeleteSG();
}

// Allow in both forward and reverse directions
TEST_F(SgTestV6, Flow_Allow_1) {
    CreateSG("pass", "::", "::", IPPROTO_ICMPV6);
    TxIp6Packet(vnet[1]->id(), vnet_addr[1], vnet_addr[2], IPPROTO_ICMPV6);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->vrf_id(), vnet_addr[1],
                               vnet_addr[2], IPPROTO_ICMPV6, 0, 0,
                               TrafficAction::PASS,
                               vnet[1]->flow_key_nh()->id()));
    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1], vnet_addr[2],
                           IPPROTO_ICMPV6, 0, 0, vnet[1]->flow_key_nh()->id()));
    client->WaitForIdle();
    DeleteSG();
}

// Deny in both forward and reverse directions
TEST_F(SgTestV6, Flow_Deny_1) {
    CreateSG("deny", "::", "::", 6);
    TxTcp6Packet(vnet[1]->id(), vnet_addr[1], vnet_addr[2],
                10, 20, false);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->vrf_id(), vnet_addr[1],
                               vnet_addr[2], 6, 10, 20, TrafficAction::DENY,
                               vnet[1]->flow_key_nh()->id()));
    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1], vnet_addr[2],
                           6, 10, 20, vnet[1]->flow_key_nh()->id()));
    DeleteSG();
}

// Change ACL for forward flow
TEST_F(SgTestV6, Fwd_Sg_Change_1) {
    CreateSG("pass", "::", "::", IPPROTO_ICMPV6);
    TxIp6Packet(vnet[1]->id(), vnet_addr[1], vnet_addr[2], IPPROTO_ICMPV6);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->vrf_id(), vnet_addr[1],
                               vnet_addr[2], IPPROTO_ICMPV6, 0, 0,
                               TrafficAction::PASS,
                               vnet[1]->flow_key_nh()->id()));

    AddAclEntry("sg_acl1", 10, IPPROTO_ICMPV6, "deny", EGRESS, "::", "::");
    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->vrf_id(), vnet_addr[1],
                               vnet_addr[2], IPPROTO_ICMPV6, 0, 0,
                               TrafficAction::DENY,
                               vnet[1]->flow_key_nh()->id()));

    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           vnet_addr[2], IPPROTO_ICMPV6, 0, 0,
                           vnet[1]->flow_key_nh()->id()));
    DeleteSG();
}

//Add a aggregarate route for destination
//check if updation of sg_id resulting in correct action
TEST_F(SgTestV6, DISABLED_Sg_Policy_1) {
    //Add a SG id acl to pass traffic between sg-id 1 and sg-id 2
    //to vnet1
    AddSgEntry("sg2", "ag2", 20, IPPROTO_ICMPV6, "pass", 1, 2, BIDIRECTION);
    AddLink("virtual-machine-interface", "vnet1", "security-group", "sg2");

    SecurityGroupList sg_id_list;
    sg_id_list.push_back(2);
    //Add a remote route pointing to SG id 2
    boost::system::error_code ec;
    Ip6Address addr6 = Ip6Address::from_string("::11.1.1.0", ec);
    Inet6TunnelRouteAdd(NULL, "vrf1", addr6, 120,
                        Ip4Address::from_string("10.10.10.10", ec),
                        TunnelType::AllType(),
                        vnet[1]->label(), "vn1", sg_id_list, PathPreference());
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (RouteFindV6("vrf1", addr6, 120) == true));

    char remote_ip[] = "::11.1.1.10";
    TxIp6Packet(vnet[1]->id(), vnet_addr[1], remote_ip, IPPROTO_ICMPV6);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->vrf_id(), vnet_addr[1],
                               remote_ip, IPPROTO_ICMPV6, 0, 0,
                               TrafficAction::PASS,
                               vnet[1]->flow_key_nh()->id()));
    client->WaitForIdle();

    //Change the route sg id to 3
    sg_id_list[0] = 3;
    Inet6TunnelRouteAdd(NULL, "vrf1", addr6, 120,
                        Ip4Address::from_string("10.10.10.10", ec),
                        TunnelType::AllType(), vnet[1]->label(), "vn1", sg_id_list,
                        PathPreference());
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->vrf_id(), vnet_addr[1],
                               remote_ip, IPPROTO_ICMPV6, 0, 0,
                               TrafficAction::DENY,
                               vnet[1]->flow_key_nh()->id()));

    client->WaitForIdle();

    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           remote_ip, IPPROTO_ICMPV6, 0, 0,
                           vnet[1]->flow_key_nh()->id()));
    DelLink("virtual-machine-interface", "vnet1", "security-group", "sg2");
    DelSgAclLink("sg2", "ag2");
    DelSgAcl("ag2");
    DelNode("security-group", "sg2");
    InetUnicastAgentRouteTable::DeleteReq(NULL, "vrf1", addr6, 120, NULL);
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (RouteFindV6("vrf1", addr6, 120) == false));
}


//Add a aggregarate route for source
//check if updation of sg_id resulting in correction action
TEST_F(SgTestV6, Sg_Policy_2) {
    //Add a SG id acl to pass traffic between sg-id 1 and sg-id 2
    //to vnet1
    AddSgEntry("sg2", "ag2", 20, IPPROTO_ICMPV6, "pass", 1, 2, INGRESS);
    AddLink("virtual-machine-interface", "vnet1", "security-group", "sg2");

    SecurityGroupList sg_id_list;
    sg_id_list.push_back(2);
    //Add a remote route pointing to SG id 2
    boost::system::error_code ec;
    Ip6Address addr6 = Ip6Address::from_string("::11.1.1.0", ec);
    Inet6TunnelRouteAdd(Agent::GetInstance()->local_peer(), "vrf1", addr6, 120,
                        Ip4Address::from_string("10.10.10.10", ec),
                        TunnelType::AllType(),
                        vnet[1]->label(), "vn1", sg_id_list, PathPreference());
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (RouteFindV6("vrf1", addr6, 120) == true));

    char remote_ip[] = "::11.1.1.10";
    PhysicalInterface *eth = EthInterfaceGet("vnet0");
    EXPECT_TRUE(eth != NULL);
    TxIp6MplsPacket(eth->id(), "10.1.1.10",
                   Agent::GetInstance()->router_id().to_string().c_str(),
                   vnet[1]->label(), remote_ip, vnet_addr[1], IPPROTO_ICMPV6);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->vrf_id(), remote_ip,
                               vnet_addr[1], IPPROTO_ICMPV6, 0, 0,
                               TrafficAction::PASS,
                               vnet[1]->flow_key_nh()->id()));
    client->WaitForIdle();

    //Change the route sg id to 3
    sg_id_list[0] = 3;
    Inet6TunnelRouteAdd(Agent::GetInstance()->local_peer(), "vrf1", addr6, 120,
                        Ip4Address::from_string("10.10.10.10", ec),
                        TunnelType::AllType(),
                        vnet[1]->label(), "vn1", sg_id_list, PathPreference());
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->vrf_id(), remote_ip,
                               vnet_addr[1], IPPROTO_ICMPV6, 0, 0,
                               TrafficAction::DENY,
                               vnet[1]->flow_key_nh()->id()));
    client->WaitForIdle();

    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           remote_ip, IPPROTO_ICMPV6, 0, 0,
                           vnet[1]->flow_key_nh()->id()));

    DelLink("virtual-machine-interface", "vnet1", "security-group", "sg2");
    DelNode("security-group", "sg2");
    DelSgAclLink("sg2", "ag2");
    DelSgAcl("ag2");
    InetUnicastAgentRouteTable::DeleteReq(Agent::GetInstance()->local_peer(),
            "vrf1", Ip6Address::from_string("::11.1.1.0", ec), 120, NULL);
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (RouteFindV6("vrf1", addr6, 120) == false));
}

int main(int argc, char *argv[]) {
    int ret = 0;

    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false, true,
                      (10 * 60 * 1000), (10 * 60 * 1000), true, true,
                      (10 * 60 * 1000));
    if (Init()) {
        ret = RUN_ALL_TESTS();
        usleep(100000);
        client->WaitForIdle();
        Shutdown();
    }
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
