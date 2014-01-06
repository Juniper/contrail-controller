/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"

VmInterface *vnet[16];
InetInterface *vhost;
char vhost_addr[32];
char vnet_addr[16][32];

PhysicalInterface *eth;
int hash_id;

void RouterIdDepInit() {
}

struct PortInfo input1[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:01:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.2", "00:00:01:01:01:02", 1, 2},
};

struct PortInfo input2[] = {
    {"vnet3", 3, "1.1.1.3", "00:00:01:01:01:03", 1, 3},
    {"vnet4", 4, "1.1.1.4", "00:00:01:01:01:04", 1, 4},
};

static string AddAclXmlString(const char *node_name, const char *name, int id,
                              int proto, const char *action) {
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
    "                            <virtual-network> any </virtual-network>\n"
    "                        </src-address>\n"
    "                        <protocol>%d</protocol>\n"
    "                        <src-port>\n"
    "                            <start-port> 0 </start-port>\n"
    "                            <end-port> 10000 </end-port>\n"
    "                        </src-port>\n"
    "                        <dst-address>\n"
    "                            <virtual-network> any </virtual-network>\n"
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
    "                <acl-rule>\n"
    "                    <match-condition>\n"
    "                        <src-address>\n"
    "                            <virtual-network> any </virtual-network>\n"
    "                        </src-address>\n"
    "                        <protocol>any</protocol>\n"
    "                        <src-port>\n"
    "                            <start-port> 0 </start-port>\n"
    "                            <end-port> 60000 </end-port>\n"
    "                        </src-port>\n"
    "                        <dst-address>\n"
    "                            <virtual-network> any </virtual-network>\n"
    "                        </dst-address>\n"
    "                        <dst-port>\n"
    "                            <start-port> 0 </start-port>\n"
    "                            <end-port> 60000 </end-port>\n"
    "                        </dst-port>\n"
    "                    </match-condition>\n"
    "                    <action-list>\n"
    "                        <simple-action>\n"
    "                            deny\n"
    "                        </simple-action>\n"
    "                    </action-list>\n"
    "                </acl-rule>\n"
    "           </access-control-list-entries>\n"
    "       </node>\n"
    "   </update>\n"
    "</config>\n", node_name, name, id, proto, action);
    string s(buff);
    return s;
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


static void AddAclEntry(const char *name, int id, int proto,
                        const char *action) {
    std::string s = AddAclXmlString("access-control-list", name, id, proto,
                                    action);
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(s.c_str());
    EXPECT_TRUE(result);
    Agent::GetInstance()->GetIfMapAgentParser()->ConfigParse(xdoc_.first_child(), 0);
    client->WaitForIdle();
}

static void AddSgIdAcl(const char *name, int id, int proto,
                       int src_sg_id, int dest_sg_id, const char *action) {
    std::string s = AddSgIdAclXmlString("access-control-list", name, id, proto,
                                         src_sg_id, dest_sg_id, action);
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(s.c_str());
    EXPECT_TRUE(result);
    Agent::GetInstance()->GetIfMapAgentParser()->ConfigParse(xdoc_.first_child(), 0);
    client->WaitForIdle();
}

static void AddSgEntry(const char *sg_name, const char *acl_name, int id,
                       int proto, const char *action) {
    AddAclEntry(acl_name, id, proto, action);
    AddNode("security-group", sg_name, 1);
    AddLink("security-group", sg_name, "access-control-list", acl_name);
}

static void AddSgEntry(const char *sg_name, const char *acl_name, int id,
                       int proto, const char *action, uint32_t sg_id, 
                       uint32_t dest_sg_id) {
    std::stringstream str;
    str << "<security-group-id>" << sg_id << "</security-group-id>" << endl;
    AddSgIdAcl(acl_name, id, proto, sg_id, dest_sg_id, action);
    AddNode("security-group", sg_name, id, str.str().c_str());
    AddLink("security-group", sg_name, "access-control-list", acl_name);
}

const VmInterface *GetVmPort(int id) {
    return static_cast<const VmInterface *>(vnet[id]);
}

static bool VmPortSetup(struct PortInfo *input, int count, int aclid) {
    bool ret = true;

    CreateVmportEnv(input, count,  aclid);
    client->WaitForIdle();

    for (int i = 0; i < count; i++) {
        int id = input[i].intf_id;

        EXPECT_TRUE(VmPortActive(input, i));
        if (VmPortActive(input, i) == false) {
            ret = false;
        }

        if (aclid) {
            EXPECT_TRUE(VmPortPolicyEnable(input, i));
            if (VmPortPolicyEnable(input, i) == false) {
                ret = false;
            }
        }

        vnet[id] = VmInterfaceGet(id);
        if (vnet[id] == NULL) {
            ret = false;
        }

        strcpy(vnet_addr[id], vnet[id]->ip_addr().to_string().c_str());
    }

    eth = EthInterfaceGet("vnet0");
    EXPECT_TRUE(eth != NULL);
    if (eth == NULL) {
        ret = false;
    }

    strcpy(vhost_addr, Agent::GetInstance()->GetRouterId().to_string().c_str());
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
    DeleteVmportEnv(input1, 2, false);
    DeleteVmportEnv(input2, 2, true, 1);
    client->WaitForIdle();

    EXPECT_FALSE(VmPortFind(input1, 0));
    EXPECT_FALSE(VmPortFind(input1, 1));
    EXPECT_FALSE(VmPortFind(input2, 0));
    EXPECT_FALSE(VmPortFind(input2, 1));
}

class SgTest : public ::testing::Test {
    virtual void SetUp() {
        client->WaitForIdle();
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());

        const VmInterface *port = GetVmPort(1);
        EXPECT_EQ(port->sg_list().list_.size(), 0U);
        AddSgEntry("sg1", "sg_acl1", 10, 1, "pass");
        AddLink("virtual-machine-interface", "vnet1", "security-group", "sg1");
        client->WaitForIdle();
        EXPECT_EQ(port->sg_list().list_.size(), 1U);
    }

    virtual void TearDown() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();

        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
        DelLink("virtual-machine-interface", "vnet1", "security-group", "sg1");
        DelLink("security-group", "sg1", "access-control-list", "sg_acl1");
        DelNode("access-control-list", "sg_acl1");
        DelNode("security-group", "sg1");
        client->WaitForIdle();

        const VmInterface *port = GetVmPort(1);
        EXPECT_EQ(port->sg_list().list_.size(), 0U);
    }
};

bool ValidateAction(uint32_t vrfid, char *sip, char *dip, int proto, int sport,
                    int dport, int action) {
    bool ret = true;
    FlowEntry *fe = FlowGet(vrfid, sip, dip, proto, sport, dport);
    FlowEntry *rfe = fe->reverse_flow_entry();

    EXPECT_TRUE((fe->data.match_p.sg_action & (1 << action)) != 0);
    if ((fe->data.match_p.sg_action & (1 << action)) == 0) {
        ret = false;
    }

    if (fe->data.match_p.sg_action & (1 << TrafficAction::TRAP) ||
            rfe->data.match_p.sg_action & (1 << TrafficAction::TRAP)) {
        return ret;
    }

    if (!(fe->data.match_p.sg_action & (1 << TrafficAction::TRAP)) && 
        !(rfe->data.match_p.sg_action & (1 << TrafficAction::TRAP))) {
        EXPECT_EQ(fe->data.match_p.sg_action, rfe->data.match_p.sg_action);
        if (fe->data.match_p.sg_action != rfe->data.match_p.sg_action) {
            ret = false;
        }
    }

    EXPECT_EQ(fe->data.match_p.action_info.action,
              rfe->data.match_p.action_info.action);
    if (fe->data.match_p.action_info.action !=
        rfe->data.match_p.action_info.action) {
        ret = false;
    }

    return ret;
}

// Allow in both forward and reverse directions
TEST_F(SgTest, Flow_Allow_1) {
    TxIpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[2], 1);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->GetVrfId(), vnet_addr[1],
                               vnet_addr[2], 1, 0, 0, TrafficAction::PASS));
    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           vnet_addr[2], 1, 0, 0));
    client->WaitForIdle();
}

// Deny in both forward and reverse directions
TEST_F(SgTest, Flow_Deny_1) {
    TxTcpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[2],
                10, 20);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->GetVrfId(), vnet_addr[1],
                               vnet_addr[2], 6, 10, 20, TrafficAction::DENY));
    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           vnet_addr[2], 6, 10, 20));
}

// Change ACL for forward flow 
TEST_F(SgTest, Fwd_Sg_Change_1) {
    TxIpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[2], 1);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->GetVrfId(), vnet_addr[1],
                               vnet_addr[2], 1, 0, 0, TrafficAction::PASS));

    AddAclEntry("sg_acl1", 10, 1, "deny");
    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->GetVrfId(), vnet_addr[1],
                               vnet_addr[2], 1, 0, 0, TrafficAction::DENY));

    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           vnet_addr[2], 1, 0, 0));
}

// Delete SG from interface
TEST_F(SgTest, Sg_Delete_1) {
    TxTcpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[2],
                10, 20);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->GetVrfId(), vnet_addr[1],
                               vnet_addr[2], 6, 10, 20, TrafficAction::DENY));

    DelLink("virtual-machine-interface", "vnet1", "security-group", "sg1");
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->GetVrfId(), vnet_addr[1],
                               vnet_addr[2], 6, 10, 20, TrafficAction::PASS));

    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           vnet_addr[2], 6, 10, 20));
}

//Add a aggregarate route for destination
//check if updation of sg_id resulting in correct action
TEST_F(SgTest, Sg_Policy_1) {
    //Delete sg added for setup()
    DelLink("virtual-machine-interface", "vnet1", "security-group", "sg1");

    //Add a SG id acl to pass traffic between sg-id 1 and sg-id 2 
    //to vnet1
    AddSgEntry("sg2", "ag2", 20, 1, "pass", 1, 2);
    AddLink("virtual-machine-interface", "vnet1", "security-group", "sg2");

    SecurityGroupList sg_id_list;
    sg_id_list.push_back(2);
    //Add a remote route pointing to SG id 2
    boost::system::error_code ec;
    Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq(NULL, "vrf1",
                                    Ip4Address::from_string("10.10.10.0", ec),
                                    24,
                                    Ip4Address::from_string("10.10.10.10", ec),
                                    TunnelType::AllType(), 
                                    17, "vn1", sg_id_list);
    client->WaitForIdle();

    char remote_ip[] = "10.10.10.1";
    TxIpPacket(vnet[1]->id(), vnet_addr[1], remote_ip, 1);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->GetVrfId(), vnet_addr[1],
                               remote_ip, 1, 0, 0, TrafficAction::PASS));
    client->WaitForIdle();

    //Change the route sg id to 3
    sg_id_list[0] = 3;
    Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq(NULL, "vrf1",
                                    Ip4Address::from_string("10.10.10.0", ec),
                                    24,
                                    Ip4Address::from_string("10.10.10.10", ec),
                                    TunnelType::AllType(),
                                    17, "vn1", sg_id_list);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->GetVrfId(), vnet_addr[1],
                               remote_ip, 1, 0, 0,
                               TrafficAction::DROP));

    client->WaitForIdle();

    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           remote_ip, 1, 0, 0));
    DelLink("virtual-machine-interface", "vnet1", "security-group", "sg2");
    DelNode("security-group", "sg2");
    DelNode("access-control-list", "ag2");
    Inet4UnicastAgentRouteTable::DeleteReq(NULL, "vrf1",
        Ip4Address::from_string("10.10.10.0", ec), 24);
    client->WaitForIdle();
}

//Add a aggregarate route for source
//check if updation of sg_id resulting in correction action
TEST_F(SgTest, Sg_Policy_2) {
    //Delete sg added for setup()
    DelLink("virtual-machine-interface", "vnet1", "security-group", "sg1");
    //Add a SG id acl to pass traffic between sg-id 1 and sg-id 2
    //to vnet1
    AddSgEntry("sg2", "ag2", 20, 1, "pass", 1, 2);
    AddLink("virtual-machine-interface", "vnet1", "security-group", "sg2");

    SecurityGroupList sg_id_list;
    sg_id_list.push_back(2);
    //Add a remote route pointing to SG id 2
    boost::system::error_code ec;
    Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq(NULL, "vrf1",
                                    Ip4Address::from_string("10.10.10.0", ec),
                                    24,
                                    Ip4Address::from_string("10.10.10.10", ec),
                                    TunnelType::AllType(),
                                    17, "vn1", sg_id_list);
    client->WaitForIdle();

    char remote_ip[] = "10.10.10.1";
    TxIpMplsPacket(eth->id(), "10.1.1.10", vhost_addr, vnet[1]->label(),
                   remote_ip, vnet_addr[1], 1);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->GetVrfId(), remote_ip,
                               vnet_addr[1], 1, 0, 0, TrafficAction::PASS));
    client->WaitForIdle();

    //Change the route sg id to 3
    sg_id_list[0] = 3;
    Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq(NULL, "vrf1",
                                    Ip4Address::from_string("10.10.10.0", ec),
                                    24,
                                    Ip4Address::from_string("10.10.10.10", ec),
                                    TunnelType::AllType(),
                                    17, "vn1", sg_id_list);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateAction(vnet[1]->vrf()->GetVrfId(), remote_ip,
                               vnet_addr[1], 1, 0, 0,
                               TrafficAction::DROP));

    client->WaitForIdle();

    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           remote_ip, 1, 0, 0));

    DelLink("virtual-machine-interface", "vnet1", "security-group", "sg2");
    DelNode("security-group", "sg2");
    DelNode("access-control-list", "ag2");
    Inet4UnicastAgentRouteTable::DeleteReq(NULL, "vrf1",
            Ip4Address::from_string("10.10.10.0", ec), 24);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    int ret = 0;

    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    if (Init()) {
        ret = RUN_ALL_TESTS();
        usleep(100000);
        Shutdown();
    }
    TestShutdown();
    delete client;
    return ret;
}
