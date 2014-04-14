/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include <base/task.h>
#include <base/test/task_test_util.h>

VmInterface *vnet[16];
InetInterface *vhost;
char vhost_addr[32];
char vnet_addr[16][32];

PhysicalInterface *eth;

void RouterIdDepInit(Agent *agent) {
}

typedef enum {
    INGRESS = 0,
    EGRESS = 1,
    BIDIRECTION = 2
} AclDirection;

struct PortInfo tcp_ack_ports[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:01:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.2", "00:00:01:01:01:02", 1, 2},
    {"vnet3", 3, "3.3.3.1", "00:00:01:01:01:03", 3, 3},
};

struct PortInfo tcp_ack_ports_1[] = {
    {"vnet3", 3, "3.3.3.1", "00:00:01:01:01:03", 3, 3},
};

const VmInterface *GetVmPort(int id) {
    return static_cast<const VmInterface *>(vnet[id]);
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
    "           </access-control-list-entries>\n"
    "       </node>\n"
    "   </update>\n"
    "</config>\n", node_name, name, id, src_sg_id, proto, dest_sg_id, action);
    string s(buff);
    return s;
}

static char *AclName(const char *name, AclDirection direction) {
    static char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    strncpy(acl_name, name, max_len);
    if (direction == EGRESS) {
        strncat(acl_name, "egress-access-control-list", max_len);
    } else {
        strncat(acl_name, "ingress-access-control-list", max_len);
    }
    return acl_name;
}

static void AddSgIdAcl(const char *name, int id, int proto,
                       int src_sg_id, int dest_sg_id, const char *action, 
                       AclDirection direction) {
    std::string s = AddSgIdAclXmlString("access-control-list",
                                        AclName(name, direction), id, proto,
                                        src_sg_id, dest_sg_id, action);
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(s.c_str());
    EXPECT_TRUE(result);
    Agent::GetInstance()->GetIfMapAgentParser()->ConfigParse(xdoc_.first_child(), 0);
    client->WaitForIdle();
}

static void DelAclEntry(const char *name, AclDirection direction) {
    DelNode("access-control-list", AclName(name, direction));
    client->WaitForIdle();
}

static void AddSgEntry(const char *sg_name, const char *name, int id,
                       int proto, const char *action, uint32_t sg_id, 
                       uint32_t src_sg_id, uint32_t dest_sg_id,
                       AclDirection direction) {
    std::stringstream str;
    str << "<security-group-id>" << sg_id << "</security-group-id>" << endl;
    AddNode("security-group", sg_name, id, str.str().c_str());
    switch (direction) {
        case INGRESS:
            AddSgIdAcl(name, id, proto, src_sg_id, dest_sg_id, action, direction);
            AddLink("security-group", sg_name, "access-control-list",
                    AclName(name, direction));
            break;
        case EGRESS:
            AddSgIdAcl(name, id, proto, src_sg_id, dest_sg_id, action, direction);
            AddLink("security-group", sg_name, "access-control-list",
                    AclName(name, direction));
            break;
        default:
            assert(0);
    }
}

static bool VmPortSetup(struct PortInfo *input, int count, int aclid) {
    bool ret = true;

    CreateVmportFIpEnv(input, count,  aclid);
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
    if (VmPortSetup(tcp_ack_ports, 2, 0) == false)
        return false;
    if (VmPortSetup(tcp_ack_ports_1, 1, 0) == false)
        return false;

    return true;
}

void Shutdown() {
    DeleteVmportFIpEnv(tcp_ack_ports, 2, true, 1);
    DeleteVmportFIpEnv(tcp_ack_ports_1, 1, true, 1);
    client->WaitForIdle();

    EXPECT_FALSE(VmPortFind(tcp_ack_ports, 0));
    EXPECT_FALSE(VmPortFind(tcp_ack_ports, 1));
    EXPECT_FALSE(VmPortFind(tcp_ack_ports_1, 0));
}

bool ValidateAction(uint32_t vrfid, const char *sip, const char *dip, int proto,
                    int sport, int dport, int action) {
    bool ret = true;
    FlowEntry *fe = FlowGet(vrfid, sip, dip, proto, sport, dport);
    FlowEntry *rfe = fe->reverse_flow_entry();

    EXPECT_TRUE((fe->match_p().sg_action_summary & (1 << action)) != 0);
    if ((fe->match_p().sg_action_summary & (1 << action)) == 0) {
        ret = false;
    }

    if (fe->match_p().sg_action_summary & (1 << TrafficAction::TRAP) ||
            rfe->match_p().sg_action_summary & (1 << TrafficAction::TRAP)) {
        return ret;
    }

    if (!(fe->match_p().sg_action_summary & (1 << TrafficAction::TRAP)) && 
        !(rfe->match_p().sg_action_summary & (1 << TrafficAction::TRAP))) {
        EXPECT_EQ(fe->match_p().sg_action_summary, rfe->match_p().sg_action_summary);
        if (fe->match_p().sg_action_summary != rfe->match_p().sg_action_summary) {
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

class SgTcpAckTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();

        client->WaitForIdle();
        EXPECT_EQ(0U, agent_->pkt()->flow_table()->Size());

        intf1 = GetVmPort(1);
        intf2 = GetVmPort(2);
        intf3 = GetVmPort(3);
        strcpy(intf1_addr, vnet_addr[1]);
        strcpy(intf2_addr, vnet_addr[2]);
        strcpy(intf3_addr, vnet_addr[3]);
        strcpy(intf2_fip_addr, "3.3.3.100");

        EXPECT_EQ(intf1->sg_list().list_.size(), 0U);
        EXPECT_EQ(intf2->sg_list().list_.size(), 0U);

        AddSgEntry("sg_icmp_i", "sg_acl_icmp_i", 1, 1, "pass",
                   10, 11, 10, INGRESS);
        AddSgEntry("sg_icmp_e", "sg_acl_icmp_e", 2, 1, "pass",
                   11, 11, 10, EGRESS);

        AddSgEntry("sg_tcp_i", "sg_acl_tcp_i", 3, 6, "pass",
                   20, 21, 20, INGRESS);
        AddSgEntry("sg_tcp_e", "sg_acl_tcp_e", 4, 6, "pass",
                   21, 21, 20, EGRESS);

        AddLink("virtual-machine-interface", "vnet1", "security-group",
                "sg_icmp_i");
        AddLink("virtual-machine-interface", "vnet2", "security-group",
                "sg_tcp_i");

        AddFloatingIpPool("fip-pool1", 1);
        AddLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn3");
        AddFloatingIp("fip1", 1, "3.3.3.100");
        AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        client->WaitForIdle();
        AddLink("virtual-machine-interface", "vnet2", "floating-ip", "fip1");
        client->WaitForIdle();

        WAIT_FOR(1000, 100, (intf1->sg_list().list_.size() == 1));
        WAIT_FOR(1000, 100, (intf2->sg_list().list_.size() == 1));
        WAIT_FOR(1000, 100, (intf2->floating_ip_list().list_.size() == 1));

        SecurityGroupList sg_id_list;
        sg_id_list.push_back(10);
        sg_id_list.push_back(11);
        sg_id_list.push_back(20);
        sg_id_list.push_back(21);

        //Add a remote route pointing to SG id 2
        boost::system::error_code ec;
        Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq
            (NULL, "vn1:vn1", Ip4Address::from_string("1.1.1.0", ec), 24,
             Ip4Address::from_string("10.10.10.10", ec), TunnelType::AllType(),
             17, "vn1", sg_id_list);
        client->WaitForIdle();

        //Add a remote route for floating-ip VN pointing to SG id 2
        Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq
            (NULL, "vn3:vn3", Ip4Address::from_string("3.3.3.2", ec), 24,
             Ip4Address::from_string("10.10.10.10", ec), TunnelType::AllType(),
             18, "vn3", sg_id_list);
        client->WaitForIdle();


    }

    virtual void TearDown() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        WAIT_FOR(1000, 100, (agent_->pkt()->flow_table()->Size() == 0));

        boost::system::error_code ec;
        Inet4UnicastAgentRouteTable::DeleteReq
            (NULL, "vn1:vn1", Ip4Address::from_string("1.1.1.0", ec), 24);
        client->WaitForIdle();

        Inet4UnicastAgentRouteTable::DeleteReq
            (NULL, "vn3:vn3", Ip4Address::from_string("3.3.3.2", ec), 24);

        client->WaitForIdle();
        DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
        DelLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn3");
        DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        DelFloatingIp("fip1");
        DelFloatingIpPool("fip-pool1");
        client->WaitForIdle();

        DelLink("virtual-machine-interface", "vnet1", "security-group",
                "sg_icmp_i");
        DelLink("virtual-machine-interface", "vnet1", "security-group",
                "sg_icmp_e");
        DelLink("virtual-machine-interface", "vnet1", "security-group",
                "sg_tcp_i");
        DelLink("virtual-machine-interface", "vnet1", "security-group",
                "sg_tcp_e");

        DelLink("virtual-machine-interface", "vnet2", "security-group",
                "sg_icmp_i");
        DelLink("virtual-machine-interface", "vnet2", "security-group",
                "sg_icmp_e");
        DelLink("virtual-machine-interface", "vnet2", "security-group",
                "sg_tcp_i");
        DelLink("virtual-machine-interface", "vnet2", "security-group",
                "sg_tcp_e");

        DelLink("virtual-machine-interface", "vnet3", "security-group",
                "sg_icmp_i");
        DelLink("virtual-machine-interface", "vnet3", "security-group",
                "sg_icmp_e");
        DelLink("virtual-machine-interface", "vnet3", "security-group",
                "sg_tcp_i");
        DelLink("virtual-machine-interface", "vnet3", "security-group",
                "sg_tcp_e");

        DelLink("security-group", "sg_icmp_i", "access-control-list",
                AclName("sg_acl_icmp_i", INGRESS));
        DelLink("security-group", "sg_icmp_e", "access-control-list",
                AclName("sg_acl_icmp_e", EGRESS));

        DelLink("security-group", "sg_tcp_i", "access-control-list",
                AclName("sg_acl_tcp_i", INGRESS));
        DelLink("security-group", "sg_tcp_e", "access-control-list",
                AclName("sg_acl_tcp_e", EGRESS));

        DelAclEntry("sg_acl_icmp_i", INGRESS);
        DelAclEntry("sg_acl_icmp_e", EGRESS);
        DelAclEntry("sg_acl_tcp_i", INGRESS);
        DelAclEntry("sg_acl_tcp_e", EGRESS);

        DelNode("security-group", "sg_icmp_i");
        DelNode("security-group", "sg_icmp_e");

        DelNode("security-group", "sg_tcp_i");
        DelNode("security-group", "sg_tcp_e");
        client->WaitForIdle();

        const VmInterface *port = GetVmPort(1);
        EXPECT_EQ(port->sg_list().list_.size(), 0U);
    }

    Agent *agent_;
    const VmInterface *intf1;
    char intf1_addr[32];
    const VmInterface *intf2;
    char intf2_addr[32];
    char intf2_fip_addr[32];
    const VmInterface *intf3;
    char intf3_addr[32];
};

// Packets from fabric to VM with ICMP ACL
TEST_F(SgTcpAckTest, icmp_acl_1) {
    // Allow flow from fabric to VM
    TxIpMplsPacket(eth->id(), "9.1.1.10", vhost_addr, intf1->label(),
                   "1.1.1.10", intf1_addr, 1);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), "1.1.1.10",
                               intf1_addr, 1, 0, 0, TrafficAction::PASS));

    // Drop flow Non-ICMP flow
    TxIpMplsPacket(eth->id(), "10.1.1.10", vhost_addr, intf1->label(),
                   "1.1.1.10", intf1_addr, 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), "1.1.1.10",
                               intf1_addr, 10, 0, 0, TrafficAction::DROP));

    // Drop TCP flow since ACL allows only ICMP
    TxTcpMplsPacket(eth->id(), "10.1.1.10", vhost_addr, intf1->label(),
                   "1.1.1.10", intf1_addr, 1, 1, false, 20);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), "1.1.1.10",
                               intf1_addr, 6, 1, 1, TrafficAction::DROP));

    // Drop TCP-ACK flow since ACL allows only ICMP
    TxTcpMplsPacket(eth->id(), "10.1.1.10", vhost_addr, intf1->label(),
                   "1.1.1.10", intf1_addr, 2, 2, true, 30);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), "1.1.1.10",
                               intf1_addr, 6, 2, 2, TrafficAction::DROP));
}

// Packets from VM to fabric with ICMP ACL
TEST_F(SgTcpAckTest, icmp_acl_2) {
    // ICMP from. DROP since only ingress allowed
    TxIpPacket(intf1->id(), intf1_addr, "1.1.1.10", 1);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), intf1_addr,
                               "1.1.1.10", 1, 0, 0, TrafficAction::DROP));

    // Non-ICMP packet drop
    TxIpPacket(intf1->id(), intf1_addr, "1.1.1.10", 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), intf1_addr,
                               "1.1.1.10", 10, 0, 0, TrafficAction::DROP));

    // TCP packet drop
    TxTcpPacket(intf1->id(), intf1_addr, "1.1.1.10", 1, 1, false, 20);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), intf1_addr,
                               "1.1.1.10", 6, 1, 1, TrafficAction::DROP));

    // TCP-ACK drop since only ICMP allowed
    TxTcpPacket(intf1->id(), intf1_addr, "1.1.1.10", 2, 2, false, 30);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), intf1_addr,
                               "1.1.1.10", 6, 2, 2, TrafficAction::DROP));
}

// Packet from fabric to VM. ACL : Ingress allow TCP 
TEST_F(SgTcpAckTest, ingress_tcp_acl_1) {
    // TCP Non-ACK from network to VM - PASS
    TxTcpMplsPacket(eth->id(), "10.1.1.10", vhost_addr, intf2->label(),
                   "1.1.1.10", intf2_addr, 1, 1, false, 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), "1.1.1.10",
                               intf2_addr, 6, 1, 1, TrafficAction::PASS));

    // TCP Non-ACK to Floating-IP from network to VM - PASS
    TxTcpMplsPacket(eth->id(), "10.1.1.10", vhost_addr, intf2->label(),
                   "3.3.3.2", intf2_fip_addr, 2, 3, false, 20);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), "3.3.3.2",
                               intf2_fip_addr, 6, 2, 3, TrafficAction::PASS));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               "3.3.3.2", 6, 3, 2, TrafficAction::PASS));

    // TCP ACK from network to VM - PASS
    TxTcpMplsPacket(eth->id(), "10.1.1.10", vhost_addr, intf2->label(),
                   "1.1.1.10", intf2_addr, 3, 3, true, 30);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), "1.1.1.10",
                               intf2_addr, 6, 3, 3, TrafficAction::PASS));

    // TCP ACK to Floating-IP from network to VM - PASS
    TxTcpMplsPacket(eth->id(), "10.1.1.10", vhost_addr, intf2->label(),
                   "3.3.3.2", intf2_fip_addr, 4, 5, true, 40);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), "3.3.3.2",
                               intf2_fip_addr, 6, 4, 5, TrafficAction::PASS));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               "3.3.3.2", 6, 5, 4, TrafficAction::PASS));
}

// Packet from VM to fabric. ACL : Ingress allow TCP
TEST_F(SgTcpAckTest, ingress_tcp_acl_2) {
    // TCP Non-ACK from VM to network - DROP
    TxTcpPacket(intf2->id(), intf2_addr, "1.1.1.10", 1, 1, false, 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               "1.1.1.10", 6, 1, 1, TrafficAction::DROP));

    // TCP Non-ACK to Floating-IP from VM to network - DROP
    TxTcpPacket(intf2->id(), intf2_addr, "3.3.3.2", 2, 3, false, 20);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               "3.3.3.2", 6, 2, 3, TrafficAction::DROP));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), "3.3.3.2",
                               intf2_fip_addr, 6, 3, 2, TrafficAction::DROP));

    // TCP ACK from VM to network - PASS
    TxTcpPacket(intf2->id(), intf2_addr, "1.1.1.10", 4, 4, true, 30);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               "1.1.1.10", 6, 4, 4, TrafficAction::PASS));

    // TCP ACK to Floating-IP from VM to network - PASS
    TxTcpPacket(intf2->id(), intf2_addr, "3.3.3.2", 5, 6, true, 40);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               "3.3.3.2", 6, 5, 6, TrafficAction::PASS));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), "3.3.3.2", 
                               intf2_fip_addr, 6, 6, 5, TrafficAction::PASS));

}

// Packet from fabric to VM. ACL : Egress allow TCP
TEST_F(SgTcpAckTest, egress_tcp_acl_1) {
    DelLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_i");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_e");
    client->WaitForIdle();

    // TCP Non-ACK from network to VM - DROP
    TxTcpMplsPacket(eth->id(), "10.1.1.10", vhost_addr, intf2->label(),
                   "1.1.1.10", intf2_addr, 1, 1, false, 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), "1.1.1.10",
                               intf2_addr, 6, 1, 1, TrafficAction::DROP));

    // TCP Non-ACK to Floating-IP from network to VM - DROP
    TxTcpMplsPacket(eth->id(), "10.1.1.10", vhost_addr, intf2->label(),
                   "3.3.3.2", intf2_fip_addr, 2, 3, false, 20);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), "3.3.3.2",
                               intf2_fip_addr, 6, 2, 3, TrafficAction::DROP));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               "3.3.3.2", 6, 3, 2, TrafficAction::DROP));

    // TCP ACK from network to VM - PASS
    TxTcpMplsPacket(eth->id(), "10.1.1.10", vhost_addr, intf2->label(),
                   "1.1.1.10", intf2_addr, 4, 4, true, 30);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), "1.1.1.10",
                               intf2_addr, 6, 4, 4, TrafficAction::PASS));

    // TCP ACK from to Floating-IP network to VM - PASS
    TxTcpMplsPacket(eth->id(), "10.1.1.10", vhost_addr, intf2->label(),
                   "3.3.3.2",  intf2_fip_addr, 5, 6, true, 40);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), "3.3.3.2",
                               intf2_fip_addr, 6, 5, 6, TrafficAction::PASS));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               "3.3.3.2", 6, 6, 5, TrafficAction::PASS));
}

// Packet from VM to fabric. ACL : Egress allow TCP 
TEST_F(SgTcpAckTest, egress_tcp_acl_2) {
    DelLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_i");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_e");
    client->WaitForIdle();

    // TCP Non-ACK from VM to network - PASS
    TxTcpPacket(intf2->id(), intf2_addr, "1.1.1.10", 1, 1, false, 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               "1.1.1.10", 6, 1, 1, TrafficAction::PASS));

    // TCP Non-ACK from Floating-IP from VM to network - PASS
    TxTcpPacket(intf2->id(), intf2_addr, "3.3.3.2", 2, 3, false, 20);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               "3.3.3.2", 6, 2, 3, TrafficAction::PASS));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), "3.3.3.2", 
                               intf2_fip_addr, 6, 3, 2, TrafficAction::PASS));

    // TCP ACK from VM to network - PASS
    TxTcpPacket(intf2->id(), intf2_addr, "1.1.1.10", 4, 4, true, 30);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               "1.1.1.10", 6, 4, 4, TrafficAction::PASS));

    // TCP Non-ACK from Floating-IP from VM to network - PASS
    TxTcpPacket(intf2->id(), intf2_addr, "3.3.3.2", 5, 6, true, 40);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               "3.3.3.2", 6, 5, 6, TrafficAction::PASS));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), "3.3.3.2", 
                               intf2_fip_addr, 6, 6, 5, TrafficAction::PASS));
}

// Local-VM TCP-ACK and Non-TCP-ACK Packet between VM1 and VM2.
// ACL : Ingress allow TCP on both VM
TEST_F(SgTcpAckTest, local_vm_ingress_tcp_acl_1) {
    DelLink("virtual-machine-interface", "vnet1", "security-group",
            "sg_icmp_i");
    DelLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_i");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "security-group",
            "sg_tcp_i");
    AddLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_i");
    client->WaitForIdle();

    // TCP Non-ACK from VM1 to VM2 - DROP
    TxTcpPacket(intf1->id(), intf1_addr, intf2_addr, 1, 1, false, 1);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), intf1_addr,
                               intf2_addr, 6, 1, 1, TrafficAction::DROP));

    // TCP ACK from VM1 to VM2 - DROP
    TxTcpPacket(intf1->id(), intf1_addr, intf2_addr, 2, 2, true, 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), intf1_addr,
                               intf2_addr, 6, 2, 2, TrafficAction::DROP));

    // TCP Non-ACK from VM2 to VM1 - DROP
    TxTcpPacket(intf1->id(), intf2_addr, intf1_addr, 3, 3, false, 20);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf1_addr, 6, 3, 3, TrafficAction::DROP));

    // TCP Non-ACK from VM2 to VM1 - DROP
    TxTcpPacket(intf1->id(), intf2_addr, intf1_addr, 4, 4, true, 30);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf1_addr, 6, 4, 4, TrafficAction::DROP));
}

// Local-VM TCP-ACK and Non-TCP-ACK Packet between VM1 and VM2.
// ACL : Ingress allow TCP on both VM
TEST_F(SgTcpAckTest, fip_local_vm_ingress_tcp_acl_1) {
    DelLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_i");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_i");
    AddLink("virtual-machine-interface", "vnet3", "security-group",
            "sg_tcp_i");
    client->WaitForIdle();

    // TCP Non-ACK from VM3 to VM2 Floating-IP - DROP
    TxTcpPacket(intf3->id(), intf3_addr, intf2_fip_addr, 1, 2, false, 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf3->vrf()->vrf_id(), intf3_addr,
                               intf2_fip_addr, 6, 1, 2, TrafficAction::DROP));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf3_addr, 6, 2, 1, TrafficAction::DROP));

    // TCP ACK from VM3 to VM2 Floating-IP - DROP
    TxTcpPacket(intf3->id(), intf3_addr, intf2_fip_addr, 3, 4, true, 20);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf3->vrf()->vrf_id(), intf3_addr,
                               intf2_fip_addr, 6, 3, 4, TrafficAction::DROP));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf3_addr, 6, 4, 3, TrafficAction::DROP));

    // TCP Non-ACK from VM2 Floating-IP to VM3 - DROP
    TxTcpPacket(intf2->id(), intf2_addr, intf3_addr, 5, 6, false, 30);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf3_addr, 6, 5, 6, TrafficAction::DROP));
    EXPECT_TRUE(ValidateAction(intf3->vrf()->vrf_id(), intf3_addr,
                               intf2_fip_addr, 6, 6, 5, TrafficAction::DROP));

    // TCP Non-ACK from VM2 to Floating-IP to VM3 - DROP
    TxTcpPacket(intf2->id(), intf2_addr, intf3_addr, 7, 8, true, 40);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf3_addr, 6, 7, 8, TrafficAction::DROP));
    EXPECT_TRUE(ValidateAction(intf3->vrf()->vrf_id(), intf3_addr,
                               intf2_fip_addr, 6, 8, 7, TrafficAction::DROP));
}

// Local-VM TCP-ACK and Non-TCP-ACK Packet between VM1 and VM2.
// ACL : Egress allow TCP on both VM
TEST_F(SgTcpAckTest, local_vm_egress_tcp_acl_1) {
    DelLink("virtual-machine-interface", "vnet1", "security-group",
            "sg_icmp_i");
    DelLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_i");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "security-group",
            "sg_tcp_e");
    AddLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_e");
    client->WaitForIdle();

    // TCP Non-ACK from network VM1 to VM2 - DROP
    TxTcpPacket(intf1->id(), intf1_addr, intf2_addr, 1, 1, false, 1);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), intf1_addr,
                               intf2_addr, 6, 1, 1, TrafficAction::DROP));

    // TCP ACK from to VM1 to VM2 - DROP
    TxTcpPacket(intf1->id(), intf1_addr, intf2_addr, 2, 2, true, 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), intf1_addr,
                               intf2_addr, 6, 2, 2, TrafficAction::DROP));

    // TCP Non-ACK from to VM2 to VM1 - DROP
    TxTcpPacket(intf2->id(), intf2_addr, intf1_addr, 3, 3, false, 20);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf1_addr, 6, 3, 3, TrafficAction::DROP));

    // TCP Non-ACK from to VM2 to VM1 - DROP
    TxTcpPacket(intf2->id(), intf2_addr, intf1_addr, 4, 4, true, 30);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf1_addr, 6, 4, 4, TrafficAction::DROP));
}

// Local-VM TCP-ACK and Non-TCP-ACK Packet between VM1 and VM2.
// ACL : Egress allow TCP on both VM
TEST_F(SgTcpAckTest, fip_local_vm_egress_tcp_acl_1) {
    DelLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_i");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_e");
    AddLink("virtual-machine-interface", "vnet3", "security-group",
            "sg_tcp_e");
    client->WaitForIdle();

    // TCP Non-ACK from network VM3 to VM2 Floating-IP - DROP
    TxTcpPacket(intf3->id(), intf3_addr, intf2_fip_addr, 1, 2, false, 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf3->vrf()->vrf_id(), intf3_addr,
                               intf2_fip_addr, 6, 1, 2, TrafficAction::DROP));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf3_addr, 6, 2, 1, TrafficAction::DROP));

    // TCP ACK from to VM1 to VM2 Floating-IP - DROP
    TxTcpPacket(intf3->id(), intf3_addr, intf2_fip_addr, 3, 4, true, 20);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf3->vrf()->vrf_id(), intf3_addr,
                               intf2_fip_addr, 6, 3, 4, TrafficAction::DROP));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf3_addr, 6, 4, 3, TrafficAction::DROP));

    // TCP Non-ACK from to VM2 Floating-IP to VM3 - DROP
    TxTcpPacket(intf2->id(), intf2_addr, intf3_addr, 5, 6, false, 30);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf3_addr, 6, 5, 6, TrafficAction::DROP));
    EXPECT_TRUE(ValidateAction(intf3->vrf()->vrf_id(), intf3_addr,
                               intf2_fip_addr, 6, 6, 5, TrafficAction::DROP));

    // TCP Non-ACK from to VM2 Floating-IP to VM3 - DROP
    TxTcpPacket(intf2->id(), intf2_addr, intf3_addr, 7, 8, true, 40);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf3_addr, 6, 7, 8, TrafficAction::DROP));
    EXPECT_TRUE(ValidateAction(intf3->vrf()->vrf_id(), intf3_addr,
                               intf2_fip_addr, 6, 8, 7, TrafficAction::DROP));
}

// Local-VM Non-TCP-ACK Packet between VM1 and VM2.
// ACL : vnet1 - Ingress allow TCP 
//     : vnet2 - Engress allow TCP 
TEST_F(SgTcpAckTest, local_vm_ingress_egress_acl_1) {
    DelLink("virtual-machine-interface", "vnet1", "security-group",
            "sg_icmp_i");
    DelLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_i");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "security-group",
            "sg_tcp_i");
    AddLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_e");
    client->WaitForIdle();

    // TCP Non-ACK from VM1 to VM2 - DROP
    TxTcpPacket(intf1->id(), intf1_addr, intf2_addr, 1, 1, false, 1);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), intf1_addr,
                               intf2_addr, 6, 1, 1, TrafficAction::DROP));

    // TCP Non-ACK from VM2 to VM1 - PASS
    TxTcpPacket(intf2->id(), intf2_addr, intf1_addr, 2, 2, false, 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf1_addr, 6, 2, 2, TrafficAction::PASS));
}

// Local-VM Non-TCP-ACK Packet between VM1 and VM2.
// ACL : vnet1 - Ingress allow TCP 
//     : vnet2 - Engress allow TCP 
TEST_F(SgTcpAckTest, fip_local_vm_ingress_egress_acl_1) {
    DelLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_i");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet3", "security-group",
            "sg_tcp_i");
    AddLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_e");
    client->WaitForIdle();

    // TCP Non-ACK from VM3 to VM2 Floatin-IP - DROP
    TxTcpPacket(intf3->id(), intf3_addr, intf2_fip_addr, 1, 2, false, 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf3->vrf()->vrf_id(), intf3_addr,
                               intf2_fip_addr, 6, 1, 2, TrafficAction::DROP));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf3_addr, 6, 2, 1, TrafficAction::DROP));

    // TCP Non-ACK from VM2 Floating-IP to VM3 - PASS
    TxTcpPacket(intf2->id(), intf2_addr, intf3_addr, 3, 4, false, 20);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf3_addr, 6, 3, 4, TrafficAction::PASS));
    EXPECT_TRUE(ValidateAction(intf3->vrf()->vrf_id(), intf3_addr,
                               intf2_fip_addr, 6, 4, 3, TrafficAction::PASS));
}

// Local-VM TCP-ACK Packet between VM1 and VM2.
// ACL : vnet1 - Ingress allow TCP 
//     : vnet2 - Engress allow TCP 
TEST_F(SgTcpAckTest, local_vm_ingress_egress_acl_2) {
    DelLink("virtual-machine-interface", "vnet1", "security-group",
            "sg_icmp_i");
    DelLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_i");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "security-group",
            "sg_tcp_i");
    AddLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_e");
    client->WaitForIdle();

    // TCP ACK from VM1 to VM2 - PASS
    TxTcpPacket(intf1->id(), intf1_addr, intf2_addr, 1, 1, true, 1);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf1->vrf()->vrf_id(), intf1_addr,
                               intf2_addr, 6, 1, 1, TrafficAction::PASS));

    // TCP ACK from VM2 to VM1 - PASS
    TxTcpPacket(intf2->id(), intf2_addr, intf1_addr, 2, 2, true, 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf1_addr, 6, 2, 2, TrafficAction::PASS));
}

// Local-VM TCP-ACK Packet between VM1 and VM2.
// ACL : vnet1 - Ingress allow TCP 
//     : vnet2 - Engress allow TCP 
TEST_F(SgTcpAckTest, fip_local_vm_ingress_egress_acl_2) {
    DelLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_i");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet3", "security-group",
            "sg_tcp_i");
    AddLink("virtual-machine-interface", "vnet2", "security-group",
            "sg_tcp_e");
    client->WaitForIdle();

    // TCP ACK from VM3 to VM2 Floating-IP - PASS
    TxTcpPacket(intf3->id(), intf3_addr, intf2_fip_addr, 1, 2, true, 10);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf3->vrf()->vrf_id(), intf3_addr,
                               intf2_fip_addr, 6, 1, 2, TrafficAction::PASS));
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf3_addr, 6, 2, 1, TrafficAction::PASS));

    // TCP ACK from VM2 Floating-IP to VM3 - PASS
    TxTcpPacket(intf2->id(), intf2_addr, intf3_addr, 3, 4, true, 20);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateAction(intf2->vrf()->vrf_id(), intf2_addr,
                               intf3_addr, 6, 3, 4, TrafficAction::PASS));
    EXPECT_TRUE(ValidateAction(intf3->vrf()->vrf_id(), intf3_addr,
                               intf2_fip_addr, 6, 4, 3, TrafficAction::PASS));
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
    SetLoggingDisabled(true);
    TestShutdown();
    delete client;
    return ret;
}
