/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include <ksync/ksync_sock_user.h>

#define AGE_TIME 10*1000
#define MEDATA_NAT_DPORT 8775

void RouterIdDepInit(Agent *agent) {
}

class FlowTest : public ::testing::Test {
    virtual void SetUp() {
        client->WaitForIdle();
        WAIT_FOR(1000, 100,
                 (0U == Agent::GetInstance()->pkt()->flow_table()->Size()));
    }

    virtual void TearDown() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        WAIT_FOR(1000, 100,
                 (0U == Agent::GetInstance()->pkt()->flow_table()->Size()));
    }
};

struct PortInfo input1[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:01:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.2", "00:00:01:01:01:02", 1, 2},
};

struct PortInfo input2[] = {
    {"vnet3", 3, "2.1.1.1", "00:00:02:01:01:01", 2, 3},
    {"vnet4", 4, "2.1.1.2", "00:00:02:01:01:02", 2, 4},
};

struct PortInfo input3[] = {
    {"vnet5", 5, "3.1.1.1", "00:00:03:01:01:01", 3, 5},
    {"vnet6", 6, "3.1.1.2", "00:00:03:01:01:02", 3, 6},
};

struct PortInfo input4[] = {
    {"vnet7", 7, "7.1.1.1", "00:00:03:01:01:01", 7, 7}
};

VmInterface *vnet[16];
char vnet_addr[16][32];

InetInterface *vhost;
char vhost_addr[32];

InetUnicastAgentRouteTable *vnet_table[16];
PhysicalInterface *eth;
int hash_id;

static bool VmPortSetup(struct PortInfo *input, int count, int aclid, bool fip = false) {
    bool ret = true;
    if (fip) {
        CreateVmportFIpEnv(input, count,  aclid);
    } else {
        CreateVmportEnv(input, count,  aclid);
    }
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
    return ret;
}

static string AddAllowAclXmlString(const char *node_name, const char *name,
                                   int id) {
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
            "                            any\n"
            "                        </protocol>\n"
            "                        <src-address>\n"
            "                            <virtual-network> any </virtual-network>\n"
            "                        </src-address>\n"
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

static void AddAllowAcl(const char *name, int id) {
    std::string s = AddAllowAclXmlString("access-control-list", name, id);
    pugi::xml_document xdoc_;

    pugi::xml_parse_result result = xdoc_.load(s.c_str());
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
}

static void SetupMetadataService() {
    boost::system::error_code ec;
    std::stringstream global_config;
    global_config << "<linklocal-services>\n";
    global_config << "<linklocal-service-entry>\n";
    global_config << "<linklocal-service-name>metdata</linklocal-service-name>\n";
    global_config << "<linklocal-service-ip>169.254.169.254</linklocal-service-ip>\n";
    global_config << "<linklocal-service-port>80</linklocal-service-port>\n";
    global_config << "<ip-fabric-DNS-service-name>";
    global_config << Agent::GetInstance()->router_id().to_string();
    global_config << "</ip-fabric-DNS-service-name>\n";
    global_config << "<ip-fabric-service-ip></ip-fabric-service-ip>\n";
    global_config << "<ip-fabric-service-port>";
    global_config << MEDATA_NAT_DPORT;
    global_config << "</ip-fabric-service-port>\n";
    global_config << "</linklocal-service-entry>\n";
    global_config << "</linklocal-services>";

    char buf[4096];
    int len = 0;
    memset(buf, 0, 4096);
    AddXmlHdr(buf, len);
    AddNodeString(buf, len, "global-vrouter-config",
                  "default-global-system-config:default-global-vrouter-config",
                  1024, global_config.str().c_str());
    AddXmlTail(buf, len);
    ApplyXmlString(buf);
}

static void RemoveMetadataService() {
    std::stringstream global_config;
    global_config << "<linklocal-services>\n";
    global_config << "<linklocal-service-entry>\n";
    global_config << "</linklocal-service-entry>\n";
    global_config << "</linklocal-services>";

    char buf[4096];
    int len = 0;
    memset(buf, 0, 4096);
    AddXmlHdr(buf, len);
    AddNodeString(buf, len, "global-vrouter-config",
                  "default-global-system-config:default-global-vrouter-config",
                  1024, global_config.str().c_str());
    AddXmlTail(buf, len);
    ApplyXmlString(buf);
}

static void Setup() {
    int ret = true;
    hash_id = 1;

    client->Reset();
    if (VmPortSetup(input1, 2, 1) != true) {
        ret = false;
    }

    if (VmPortSetup(input2, 2, 2, true) != true) {
        ret = false;
    }

    if (VmPortSetup(input3, 2, 3, true) != true) {
        ret = false;
    }

    VmPortSetup(input4, 1, 0);

    EXPECT_EQ(10U, Agent::GetInstance()->interface_table()->Size());
    if (Agent::GetInstance()->interface_table()->Size() != 10) {
        ret = false;
    }

    EXPECT_EQ(7U, Agent::GetInstance()->vm_table()->Size());
    if ( Agent::GetInstance()->vm_table()->Size() != 7) {
        ret = false;
    }

    EXPECT_EQ(4U, Agent::GetInstance()->vn_table()->Size());
    if (Agent::GetInstance()->vn_table()->Size() != 4) {
        ret = false;
    }

    EXPECT_EQ(7U, Agent::GetInstance()->interface_config_table()->Size());
    if (Agent::GetInstance()->interface_config_table()->Size() != 7) {
        ret = false;
    }

    // Configure Floating-IP
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddFloatingIp("fip_2", 2, "2.1.1.99");
    AddLink("floating-ip", "fip_2", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    AddFloatingIpPool("fip-pool2", 2);
    AddFloatingIp("fip_3", 3, "3.1.1.100");
    AddLink("floating-ip", "fip_3", "floating-ip-pool", "fip-pool2");
    AddLink("floating-ip-pool", "fip-pool2", "virtual-network",
            "default-project:vn3");
    client->WaitForIdle();

    EXPECT_TRUE(vnet[1]->HasFloatingIp(Address::INET));
    if (vnet[1]->HasFloatingIp(Address::INET) == false) {
        ret = false;
    }
    // Get route tables
    vnet_table[1] = static_cast<InetUnicastAgentRouteTable *>
        (Agent::GetInstance()->vrf_table()->
        GetInet4UnicastRouteTable("vrf1"));
    vnet_table[2] = static_cast<InetUnicastAgentRouteTable *>
        (Agent::GetInstance()->vrf_table()->
        GetInet4UnicastRouteTable("default-project:vn2:vn2"));
    vnet_table[3] = static_cast<InetUnicastAgentRouteTable *>
        (Agent::GetInstance()->vrf_table()->
        GetInet4UnicastRouteTable("default-project:vn3:vn3"));
    EXPECT_TRUE(vnet_table[1] != NULL && vnet_table[2] != NULL &&
                vnet_table[3] != NULL);
    if (vnet_table[1] == NULL || vnet_table[2] == NULL ||
        vnet_table[3] == NULL) {
        ret = false;
    }

    /* Add remote VN route to VN1 */
    Ip4Address addr = Ip4Address::from_string("1.1.1.10");
    Ip4Address gw = Ip4Address::from_string("10.1.1.2");
    Inet4TunnelRouteAdd(NULL, "vrf1", addr, 32, gw, 
                        TunnelType::AllType(), 8, "vn1",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));

    /* Add Local VM route in vrf1 from default-project:vn2:vn2 */
    addr = Ip4Address::from_string("2.1.1.10");
    vnet_table[2]->AddLocalVmRouteReq(NULL, "default-project:vn2:vn2", addr, 32,
                                      vnet[3]->GetUuid(),
                                      vnet[3]->vn()->GetName(),
                                      vnet[3]->label(),
                                      SecurityGroupList(), 0,
                                      PathPreference(), Ip4Address(0));
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2", addr, 32));

    /* Add Remote /24 route of vrf3 to default-project:vn2:vn2 */
    addr = Ip4Address::from_string("20.1.1.0");
    Inet4TunnelRouteAdd(NULL, "default-project:vn2:vn2", addr, 24, gw,
                        TunnelType::AllType(), 8, "vn3",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2", addr, 24));

    /* Add Remote VM route in vrf1 from default-project:vn2:vn2 */
    addr = Ip4Address::from_string("2.1.1.11");
    gw = Ip4Address::from_string("10.1.1.2");
    Inet4TunnelRouteAdd(NULL, "vrf1", addr, 32, gw, 
                        TunnelType::AllType(), 8, "vn2",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));

    eth = EthInterfaceGet("vnet0");
    EXPECT_TRUE(eth != NULL);
    if (eth == NULL) {
        ret = false;
    }

    boost::scoped_ptr<InetInterfaceKey> key(new InetInterfaceKey("vhost0"));
    vhost = static_cast<InetInterface *>(Agent::GetInstance()->interface_table()->FindActiveEntry(key.get()));
    strcpy(vhost_addr, Agent::GetInstance()->router_id().to_string().c_str());

    EXPECT_TRUE(ret);
    assert(ret == true);

    Agent::GetInstance()->uve()->flow_stats_collector()->UpdateFlowAgeTime(AGE_TIME);
    AddAllowAcl("acl1", 1);
    client->WaitForIdle();
}

void Teardown() {
    int ret = true;
    Ip4Address addr;

    // Delete routes
    DeleteRoute("vrf1", "2.1.1.11", 32);

    DeleteRoute("default-project:vn2:vn2", "20.1.1.0", 24);

    DeleteRoute("default-project:vn2:vn2", "2.1.1.10", 32);

    DeleteRoute("vrf1", "1.1.1.10", 32);
    client->WaitForIdle();

    // Delete floating-ip
    DelLink("floating-ip", "fip_3", "floating-ip-pool", "fip-pool2");
    DelLink("floating-ip-pool", "fip-pool2", "virtual-network",
            "default-project:vn3");
    DelLink("floating-ip", "fip_2", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");

    DelFloatingIpPool("fip-pool1");
    DelFloatingIp("fip1");
    DelFloatingIp("fip_2");
    DelFloatingIpPool("fip-pool2");
    DelFloatingIp("fip_3");
    client->WaitForIdle();

    DeleteVmportEnv(input4, 1, true);
    DeleteVmportFIpEnv(input3, 2, true, 3);
    DeleteVmportFIpEnv(input2, 2, true, 2);
    DeleteVmportEnv(input1, 2, true, 1);
    DelNode("routing-instance", "vrf1");
    DelNode("access-control-list", "acl1");
    client->WaitForIdle();
}

static bool NatValidateFlow(int flow_id, const char *vrf, const char *sip,
                            const char *dip, uint8_t proto, uint16_t sport,
                            uint16_t dport, uint32_t label, const char *nat_vrf,
                            const char *nat_sip, const char *nat_dip,
                            uint16_t nat_sport, uint16_t nat_dport,
                            const char *src_vn, const char *dest_vn,
                            int intf_nh_id, int nat_intf_nh_id) {
    bool ret = true;

    client->WaitForIdle();
    if (FlowGetNat(vrf, sip, dip, proto, sport, dport, src_vn, dest_vn, flow_id,
                   nat_vrf, nat_sip, nat_dip, nat_sport, nat_dport,
                   intf_nh_id, nat_intf_nh_id) == false) {
        EXPECT_STREQ("", "Error quering flow");
        ret = false;
    }

    if (FlowDelete(vrf, sip, dip, proto, sport, dport, intf_nh_id) == false) {
        EXPECT_STREQ("", "Error deleting flow");
        ret = false;
    }

    client->WaitForIdle();
    if (FlowFail(vrf, sip, dip, proto, sport, dport, intf_nh_id) == false) {
        EXPECT_STREQ("", "Error deleting forward flow");
        ret = false;
    }

    if (FlowFail(nat_vrf, nat_dip, nat_sip, proto, nat_dport, nat_sport,
                 nat_intf_nh_id) == false) {
        EXPECT_STREQ("", "Error deleting reverse flow");
        ret = false;
    }

    return ret;
}

TEST_F(FlowTest, Mdata_FabricToVm_1) {
    // Packet from fabric to VM using private addresses
    // No route for dst-ip. Pkt to be dropped
    TxIpMplsPacket(eth->id(), "10.1.1.2", vhost_addr, 
                   vnet[1]->label(), "1.1.1.2",
                   vnet[1]->mdata_ip_addr().to_string().c_str(), 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet(vnet[1]->vrf()->GetName(), "1.1.1.2",
                        vnet[1]->mdata_ip_addr().to_string().c_str(), 1, 0, 0,
                        true, "vn1", unknown_vn_.c_str(), 1,
                        false, false, vnet[1]->flow_key_nh()->id(),
                        vnet[1]->flow_key_nh()->id()));
    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), "1.1.1.2",
                           vnet[1]->mdata_ip_addr().to_string().c_str(), 1,
                           0, 0, vnet[1]->flow_key_nh()->id()));

    client->WaitForIdle();
    EXPECT_TRUE(FlowFail(vnet[1]->vrf()->GetName(),"1.1.1.2",
                         vnet[1]->mdata_ip_addr().to_string().c_str(), 1,
                         0, 0, vnet[1]->flow_key_nh()->id()));
}

TEST_F(FlowTest, Mdata_FabricToServer_1) {
    // Packet from fabric to Server using MData service addresses
    SetupMetadataService();
    client->WaitForIdle();
    TxIpMplsPacket(eth->id(), "10.1.1.2", vhost_addr, 
                   vnet[1]->label(), "1.1.1.10", "169.254.169.254", 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet(vnet[1]->vrf()->GetName(), "1.1.1.10",
                        "169.254.169.254", 1, 0, 0, false, "vn1",
                        "vn1", 1, true, false, vnet[1]->flow_key_nh()->id()));
    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), "1.1.1.10",
                           "169.254.169.254", 1, 0, 0,
                           vnet[1]->flow_key_nh()->id()));

    client->WaitForIdle();
    EXPECT_TRUE(FlowFail(vnet[1]->vrf()->GetName(),"1.1.1.10",
                         "169.254.169.254", 1, 0, 0,
                         vnet[1]->flow_key_nh()->id()));

    TxTcpMplsPacket(eth->id(), "10.1.1.2", vhost_addr, 
                   vnet[1]->label(), "1.1.1.10", "169.254.169.254", 1001, 80,
                   false);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet(vnet[1]->vrf()->GetName(), "1.1.1.10",
                        "169.254.169.254", IPPROTO_TCP, 1001, 80, false, 
                        "vn1", "vn1", 1, true, false,
                        vnet[1]->flow_key_nh()->id()));
    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), "1.1.1.10",
                           "169.254.169.254", IPPROTO_TCP, 1001, 80,
                           vnet[1]->flow_key_nh()->id()));
    client->WaitForIdle();
    EXPECT_TRUE(FlowFail(vnet[1]->vrf()->GetName(),"1.1.1.10",
                         "169.254.169.254", IPPROTO_TCP, 1001, 80,
                         vnet[1]->flow_key_nh()->id()));
    RemoveMetadataService();
    client->WaitForIdle();
}

TEST_F(FlowTest, VmToVm_Invalid_1) {
    // Packet between VM using private addresses
    TxIpPacket(vnet[1]->id(), vnet_addr[1],
               vnet[2]->mdata_ip_addr().to_string().c_str(), 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet(vnet[1]->vrf()->GetName(), vnet_addr[1],
                        vnet[2]->mdata_ip_addr().to_string().c_str(), 1, 0,
                        0, true, "vn1", unknown_vn_.c_str(), 1,
                        false, false, vnet[1]->flow_key_nh()->id(),
                        vnet[1]->flow_key_nh()->id()));
    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           vnet[2]->mdata_ip_addr().to_string().c_str(), 1,
                           0, 0, vnet[1]->flow_key_nh()->id()));

    client->WaitForIdle();
    EXPECT_TRUE(FlowFail(vnet[1]->vrf()->GetName(), vnet_addr[1],
                         vnet[2]->mdata_ip_addr().to_string().c_str(), 1,
                         0, 0, vnet[1]->flow_key_nh()->id()));

    // Packet to an invalid IP in same VRF
    TxIpPacket(vnet[1]->id(), vnet_addr[1], "1.1.1.100", 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet(vnet[1]->vrf()->GetName(), vnet_addr[1],
                        "1.1.1.100", 1, 0, 0, true, "vn1",
                        unknown_vn_.c_str(), 1, false, false,
                        vnet[1]->flow_key_nh()->id(),
                        vnet[1]->flow_key_nh()->id()));
    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           "1.1.1.100", 1, 0, 0, vnet[1]->flow_key_nh()->id()));
    client->WaitForIdle();
    EXPECT_TRUE(FlowFail(vnet[1]->vrf()->GetName(), vnet_addr[1],
                         "1.1.1.100", 1, 0, 0, vnet[1]->flow_key_nh()->id()));

    // Packet to an IP not present in this VRF but present in other VRF
    TxIpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[5], 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet(vnet[1]->vrf()->GetName(), vnet_addr[1],
                        vnet_addr[5], 1, 0, 0, true, "vn1",
                        unknown_vn_.c_str(), 1, false, false,
                        vnet[1]->flow_key_nh()->id(),
                        vnet[1]->flow_key_nh()->id()));
    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           vnet_addr[5], 1, 0, 0, vnet[1]->flow_key_nh()->id()));
    client->WaitForIdle();
    EXPECT_TRUE(FlowFail(vnet[1]->vrf()->GetName(), vnet_addr[1],
                         vnet_addr[5], 1, 0, 0, vnet[1]->flow_key_nh()->id()));

}

// Test for traffic from server to VM
TEST_F(FlowTest, ServerToVm_1) {
    SetupMetadataService();
    client->WaitForIdle();
    // Packet with no route for IP-DA
    TxIpPacketUtil(vhost->id(), vhost_addr, "80.80.80.80", 1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet(vhost->vrf()->GetName(), vhost_addr, "80.80.80.80", 
                        1, 0, 0, false, Agent::GetInstance()->fabric_vrf_name().c_str(),
                        // 1, 0, 0, false, Agent::GetInstance()->fabric_vn_name().c_str(),
                        Agent::GetInstance()->fabric_vn_name().c_str(), 1,
                        true, false, vhost->flow_key_nh()->id()));

    EXPECT_TRUE(FlowDelete(vhost->vrf()->GetName(), vhost_addr, "80.80.80.80",
                           1, 0, 0, vhost->flow_key_nh()->id()));

    client->WaitForIdle();
    EXPECT_TRUE(FlowFail(vhost->vrf()->GetName(), vhost_addr, "80.80.80.80", 
                         1, 0, 0, vhost->flow_key_nh()->id()));

    // Ping from server to vnet1
    TxIpPacketUtil(vhost->id(), vhost_addr,
                   vnet[1]->mdata_ip_addr().to_string().c_str(), 1, 1);

    EXPECT_TRUE(NatValidateFlow(1, vhost->vrf()->GetName().c_str(),
                                vhost_addr,
                                vnet[1]->mdata_ip_addr().to_string().c_str(),
                                1, 0, 0, 1, "vrf1", "169.254.169.254",
                                vnet_addr[1], 0, 0,
                                Agent::GetInstance()->fabric_vrf_name().c_str(), "vn1",
                                vhost->flow_key_nh()->id(),
                                vnet[1]->flow_key_nh()->id()));

    // UDP from server to vnet1
    TxUdpPacket(vhost->id(), vhost_addr,
                vnet[1]->mdata_ip_addr().to_string().c_str(), 10, 20);

    EXPECT_TRUE(NatValidateFlow(1, vhost->vrf()->GetName().c_str(),
                                vhost_addr,
                                vnet[1]->mdata_ip_addr().to_string().c_str(),
                                IPPROTO_UDP, 10, 20, 1, "vrf1",
                                "169.254.169.254", vnet_addr[1], 10, 20,
                                Agent::GetInstance()->fabric_vrf_name().c_str(), "vn1",
                                vhost->flow_key_nh()->id(),
                                vnet[1]->flow_key_nh()->id()));

    // TCP from server to vnet1
    TxTcpPacket(vhost->id(), vhost_addr,
                vnet[1]->mdata_ip_addr().to_string().c_str(), 10, 20, false);

    EXPECT_TRUE(NatValidateFlow(1, vhost->vrf()->GetName().c_str(),
                                vhost_addr,
                                vnet[1]->mdata_ip_addr().to_string().c_str(),
                                IPPROTO_TCP, 10, 20, 1, "vrf1", "169.254.169.254",
                                vnet_addr[1], 10, 20,
                                Agent::GetInstance()->fabric_vrf_name().c_str(), "vn1",
                                vhost->flow_key_nh()->id(),
                                vnet[1]->flow_key_nh()->id()));
                                // Agent::GetInstance()->fabric_vn_name().c_str(), "vn1"));
    RemoveMetadataService();
    client->WaitForIdle();
}

// Test for traffic from VM to server
TEST_F(FlowTest, VmToServer_1) {
    SetupMetadataService();
    client->WaitForIdle();
    // HTTP packet from VM to Server
    TxTcpPacket(vnet[1]->id(), vnet_addr[1], "169.254.169.254", 10000, 80, false);
    client->WaitForIdle();

    EXPECT_TRUE(NatValidateFlow(1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[1], "169.254.169.254",
                                IPPROTO_TCP, 10000, 80, 1, 
                                vhost->vrf()->GetName().c_str(),
                                vnet[1]->mdata_ip_addr().to_string().c_str(),
                                vhost_addr, 10000, MEDATA_NAT_DPORT,
                                "vn1",
                                Agent::GetInstance()->fabric_vrf_name().c_str(),
                                vnet[1]->flow_key_nh()->id(),
                                vhost->flow_key_nh()->id()));
                                // Agent::GetInstance()->fabric_vn_name().c_str()));
    client->WaitForIdle();

    TxUdpPacket(vnet[1]->id(), vnet_addr[1], "169.254.169.254",
                10, 20, 1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet(vnet[1]->vrf()->GetName(), vnet_addr[1], 
                        "169.254.169.254", IPPROTO_UDP, 10, 20, false,
                        unknown_vn_.c_str(), unknown_vn_.c_str(), 1, false,
                        false, vnet[1]->flow_key_nh()->id()));
    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           "169.254.169.254", IPPROTO_UDP, 10, 20,
                           vnet[1]->flow_key_nh()->id()));

    client->WaitForIdle();
    EXPECT_TRUE(FlowFail(vnet[1]->vrf()->GetName(), vnet_addr[1],
                         "169.254.169.254", IPPROTO_UDP, 10, 20,
                         vnet[1]->flow_key_nh()->id()));

    TxTcpPacket(vnet[1]->id(), vnet_addr[1], "169.254.169.254",
                10, 20, false);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGet(vnet[1]->vrf()->GetName(), vnet_addr[1], 
                        "169.254.169.254", IPPROTO_TCP, 10, 20, false,
                        unknown_vn_.c_str(), unknown_vn_.c_str(), 1, false,
                        false, vnet[1]->flow_key_nh()->id()));
    EXPECT_TRUE(FlowDelete(vnet[1]->vrf()->GetName(), vnet_addr[1],
                           "169.254.169.254", IPPROTO_TCP, 10, 20,
                           vnet[1]->flow_key_nh()->id()));

    client->WaitForIdle();
    EXPECT_TRUE(FlowFail(vnet[1]->vrf()->GetName(), vnet_addr[1],
                         "169.254.169.254", IPPROTO_TCP, 10, 20,
                         vnet[1]->flow_key_nh()->id()));
    RemoveMetadataService();
    client->WaitForIdle();
}

// Test for traffic from VM to server without Metadata Service and default route with ecmp
// later enable Metadata service and send traffic from server to vm.
TEST_F(FlowTest, VmToServer_ecmp_to_nat) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    client->WaitForIdle();
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
    SecurityGroupList sg_id_list;
    ComponentNHKeyList comp_nh_list;
    int remote_server_ip = 0x0A0A0A0A;
    int label = 16;
    for(int i = 0; i < 2; i++) {
        ComponentNHKeyPtr comp_nh(new ComponentNHKey(
                label,Agent::GetInstance()->fabric_vrf_name(),
                Agent::GetInstance()->router_id(),
                Ip4Address(remote_server_ip++),
                false, TunnelType::AllType()));
        comp_nh_list.push_back(comp_nh);
        label++;
    }
    EcmpTunnelRouteAdd(Agent::GetInstance()->local_peer(), vnet[1]->vrf()->GetName(),
                       Ip4Address::from_string("0.0.0.0"), 0,
                       comp_nh_list, false, vnet[1]->vn()->GetName(),
                       sg_id_list, PathPreference());
    sock->SetBlockMsgProcessing(true);
    // HTTP packet from VM to Server
    TxTcpPacket(vnet[1]->id(), vnet_addr[1], "169.254.169.254", 10000, 80, false);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(vnet[1]->id(), vnet_addr[1], "169.254.169.254",
                               IPPROTO_TCP, 10000, 80, GetFlowKeyNH(1));
    EXPECT_TRUE(entry);
    if (entry) {
        EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
    }

    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());
    // send reverse packet from Server to VM
    TxTcpPacket(vhost->id(), vhost_addr,
                vnet[1]->mdata_ip_addr().to_string().c_str(),
                80, 10000, false);
    sock->SetBlockMsgProcessing(false);
    client->WaitForIdle();
    EXPECT_EQ(3U, Agent::GetInstance()->pkt()->flow_table()->Size());

    FlowDelete(vnet[1]->vrf()->GetName(), "169.254.169.254", vnet_addr[1],
               IPPROTO_TCP, 80, 10000, vnet[1]->flow_key_nh()->id());
    FlowDelete(vhost->vrf()->GetName(), vhost_addr,
               vnet[1]->mdata_ip_addr().to_string().c_str(),
               IPPROTO_TCP, 80, 10000, vhost->flow_key_nh()->id());

    Agent::GetInstance()->fabric_inet4_unicast_table()->
        DeleteReq(Agent::GetInstance()->local_peer(), vnet[1]->vrf()->GetName(),
                  Ip4Address::from_string("0.0.0.0"), 0, NULL);
    client->WaitForIdle();
    client->WaitForIdle();
    WAIT_FOR(1000, 100,
             (0U == Agent::GetInstance()->pkt()->flow_table()->Size()));
}

// Validate destination virtual-network name in flow-table
TEST_F(FlowTest, FlowValidateDestVn_1) {
    TxIpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[3], 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(vnet[1]->flow_key_nh()->id(),
                              vnet_addr[1], vnet_addr[3], 1, 0, 0);
    EXPECT_TRUE(flow != NULL);
    if (flow == NULL)
        return;

    EXPECT_STREQ(flow->data().source_vn.c_str(), "default-project:vn2");
    EXPECT_STREQ(flow->data().dest_vn.c_str(), "default-project:vn2");
}

// FloatingIP test for traffic from VM to local VM
TEST_F(FlowTest, FipVmToLocalVm_1) {
    TxIpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[3], 1);
    EXPECT_TRUE(NatValidateFlow(1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[1], vnet_addr[3], 1, 0, 0, 1,
                                vnet[3]->vrf()->GetName().c_str(),
                                "2.1.1.100", vnet_addr[3], 0, 0,
                                "default-project:vn2", "default-project:vn2",
                                vnet[1]->flow_key_nh()->id(),
                                vnet[3]->flow_key_nh()->id()));

    TxTcpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[3], 10, 20, false);
    EXPECT_TRUE(NatValidateFlow(1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[1], vnet_addr[3], IPPROTO_TCP, 10,
                                20, 1, vnet[3]->vrf()->GetName().c_str(),
                                "2.1.1.100", vnet_addr[3], 10, 20,
                                "default-project:vn2", "default-project:vn2",
                                vnet[1]->flow_key_nh()->id(),
                                vnet[3]->flow_key_nh()->id()));

    TxUdpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[3], 10, 20);
    EXPECT_TRUE(NatValidateFlow(1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[1], vnet_addr[3], IPPROTO_UDP, 10,
                                20, 1, vnet[3]->vrf()->GetName().c_str(),
                                "2.1.1.100", vnet_addr[3], 10, 20,
                                "default-project:vn2", "default-project:vn2",
                                vnet[1]->flow_key_nh()->id(),
                                vnet[3]->flow_key_nh()->id()));

}

// FloatingIP test for traffic from VM to Remote VM
TEST_F(FlowTest, FipVmToRemoteVm_1) {
    TxIpPacket(vnet[1]->id(), vnet_addr[1], "2.1.1.10", 1);
    EXPECT_TRUE(NatValidateFlow(1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[1], "2.1.1.10", 1, 0, 0, 1,
                                "default-project:vn2:vn2", "2.1.1.100",
                                "2.1.1.10", 0, 0, "default-project:vn2",
                                "default-project:vn2",
                                vnet[1]->flow_key_nh()->id(),
                                vnet[3]->flow_key_nh()->id()));

    TxTcpPacket(vnet[1]->id(), vnet_addr[1], "2.1.1.10", 10, 20, false);
    EXPECT_TRUE(NatValidateFlow(1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[1], "2.1.1.10", IPPROTO_TCP, 10, 20,
                                1, "default-project:vn2:vn2", "2.1.1.100",
                                "2.1.1.10", 10, 20, "default-project:vn2",
                                "default-project:vn2",
                                vnet[1]->flow_key_nh()->id(),
                                vnet[3]->flow_key_nh()->id()));

    TxUdpPacket(vnet[1]->id(), vnet_addr[1], "2.1.1.10", 10, 20);
    EXPECT_TRUE(NatValidateFlow(1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[1], "2.1.1.10", IPPROTO_UDP, 10, 20,
                                1, "default-project:vn2:vn2", "2.1.1.100",
                                "2.1.1.10", 10, 20, "default-project:vn2",
                                "default-project:vn2",
                                vnet[1]->flow_key_nh()->id(),
                                vnet[3]->flow_key_nh()->id()));
}

// FloatingIP test for traffic from VM to local VM
TEST_F(FlowTest, LocalVmToFipVm_1) {
    TxIpPacket(vnet[3]->id(), vnet_addr[3], "2.1.1.100", 1);
    EXPECT_TRUE(NatValidateFlow(1, vnet[3]->vrf()->GetName().c_str(),
                                vnet_addr[3], "2.1.1.100", 1, 0, 0, 1,
                                vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[3], vnet_addr[1], 0, 0,
                                "default-project:vn2", "default-project:vn2",
                                vnet[3]->flow_key_nh()->id(),
                                vnet[1]->flow_key_nh()->id()));

    TxTcpPacket(vnet[3]->id(), vnet_addr[3], "2.1.1.100", 1000, 80, false);
    EXPECT_TRUE(NatValidateFlow(1, vnet[3]->vrf()->GetName().c_str(),
                                vnet_addr[3], "2.1.1.100", IPPROTO_TCP, 1000, 
                                80, 1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[3], vnet_addr[1], 1000, 80,
                                "default-project:vn2", "default-project:vn2",
                                vnet[3]->flow_key_nh()->id(),
                                vnet[1]->flow_key_nh()->id()));

    TxUdpPacket(vnet[3]->id(), vnet_addr[3], "2.1.1.100", 1000, 80);
    EXPECT_TRUE(NatValidateFlow(1, vnet[3]->vrf()->GetName().c_str(),
                                vnet_addr[3], "2.1.1.100", IPPROTO_UDP, 1000, 
                                80, 1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[3], vnet_addr[1], 1000, 80,
                                "default-project:vn2", "default-project:vn2",
                                vnet[3]->flow_key_nh()->id(),
                                vnet[1]->flow_key_nh()->id()));
}

// FloatingIP test for traffic from VM to local VM
TEST_F(FlowTest, FipFabricToVm_1) {
    TxIpMplsPacket(eth->id(), "10.1.1.2", vhost_addr, 
                   vnet[1]->label(), vnet_addr[3], "2.1.1.100", 1);

    EXPECT_TRUE(NatValidateFlow(1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[3], "2.1.1.100", 1, 0, 0, 1,
                                vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[3], vnet_addr[1], 0, 0,
                                "default-project:vn2", "default-project:vn2",
                                vnet[1]->flow_key_nh()->id(),
                                vnet[1]->flow_key_nh()->id()));

    TxTcpMplsPacket(eth->id(), "10.1.1.2", vhost_addr, 
                    vnet[1]->label(), vnet_addr[3], "2.1.1.100", 1000, 80,
                    false);

    EXPECT_TRUE(NatValidateFlow(1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[3], "2.1.1.100", IPPROTO_TCP, 1000,
                                80, 1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[3], vnet_addr[1], 1000, 80,
                                "default-project:vn2", "default-project:vn2",
                                vnet[1]->flow_key_nh()->id(),
                                vnet[1]->flow_key_nh()->id()));

    TxUdpMplsPacket(eth->id(), "10.1.1.2", vhost_addr, 
                    vnet[1]->label(), vnet_addr[3], "2.1.1.100", 1000, 80);

    EXPECT_TRUE(NatValidateFlow(1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[3], "2.1.1.100", IPPROTO_UDP, 1000,
                                80, 1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[3], vnet_addr[1], 1000, 80,
                                "default-project:vn2", "default-project:vn2",
                                vnet[1]->flow_key_nh()->id(),
                                vnet[1]->flow_key_nh()->id()));
}

// NAT Flow aging
TEST_F(FlowTest, FlowAging_1) {
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());

    TxIpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[3], 1);
    client->WaitForIdle();

    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Trigger to Age the flow
    client->EnqueueFlowAge();
    client->WaitForIdle();
    //Flow stats would be updated from Kernel flows . Hence they won't be deleted
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    usleep(AGE_TIME*2);
    //Trigger to Age the flow
    client->EnqueueFlowAge();
    client->WaitForIdle();
    //No change in flow-stats and aging time is up
    WAIT_FOR(1000, 1000,
             (0U == Agent::GetInstance()->pkt()->flow_table()->Size()));

    TxIpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[3], 1, 1);
    client->WaitForIdle();
    TxTcpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[3], 10, 20, false, 2);
    client->WaitForIdle();
    TxUdpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[3], 10, 20, false, 3);
    client->WaitForIdle();
    EXPECT_EQ(6U, Agent::GetInstance()->pkt()->flow_table()->Size());

    // Trigger aging cycle
    client->EnqueueFlowAge();
    client->WaitForIdle();
    //Flow stats would be updated from Kernel flows . Hence they won't be deleted
    EXPECT_EQ(6U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Trigger flow-aging
    usleep(AGE_TIME*2);
    client->WaitForIdle();
    client->EnqueueFlowAge();

    client->WaitForIdle();
    //No change in stats. Flows should be aged by now
    WAIT_FOR(1000, 100,
             (0U == Agent::GetInstance()->pkt()->flow_table()->Size()));
}

// Duplicate Nat-Flow test 
TEST_F(FlowTest, DuplicateFlow_1) {
    TxIpPacket(vnet[1]->id(), vnet_addr[1], "2.1.1.10", 1);
    client->WaitForIdle();
    TxIpPacket(vnet[1]->id(), vnet_addr[1], "2.1.1.10", 1);
    client->WaitForIdle();
    EXPECT_TRUE(NatValidateFlow(-1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[1], "2.1.1.10", 1, 0, 0, 1,
                                "default-project:vn2:vn2", "2.1.1.100",
                                "2.1.1.10", 0, 0, "default-project:vn2",
                                "default-project:vn2",
                                vnet[1]->flow_key_nh()->id(),
                                vnet[3]->flow_key_nh()->id()));
}

// Nat to Non-Nat flow conversion test for traffic from VM to local VM
TEST_F(FlowTest, Nat2NonNat) {
    //Create a Nat Flow
    TxIpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[3], 1);
    client->WaitForIdle();

    //Verify Nat Flow creation
    if (FlowGetNat(vnet[1]->vrf()->GetName().c_str(), vnet_addr[1], 
                   vnet_addr[3], 1, 0, 0, "default-project:vn2",
                   "default-project:vn2", 1, vnet[3]->vrf()->GetName().c_str(),
                   "2.1.1.100", vnet_addr[3], 0, 0, vnet[1]->flow_key_nh()->id(),
                   vnet[3]->flow_key_nh()->id()) == false) {
        EXPECT_STREQ("", "Error quering flow");
    }

    //Remove floating IP configuration
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    EXPECT_FALSE(vnet[1]->HasFloatingIp(Address::INET));

    // Deleting floating-ip will remove associated flows also
    WAIT_FOR(1000, 1000,
             (0U == Agent::GetInstance()->pkt()->flow_table()->Size()));

    //Send the traffic again (to convert Nat-flow to Non-Nat flow)
    TxIpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[3], 1);
    client->WaitForIdle();

    EXPECT_TRUE(FlowGet(vnet[1]->vrf()->GetName().c_str(), vnet_addr[1],
                vnet_addr[3], 1, 0, 0, true, "vn1", 
                unknown_vn_.c_str(), 1, false, false,
                vnet[1]->flow_key_nh()->id(), vnet[1]->flow_key_nh()->id()));

    //Delete the flow
    if (FlowDelete(vnet[1]->vrf()->GetName().c_str(), vnet_addr[1], 
                   vnet_addr[3], 1, 0, 0,
                   vnet[1]->flow_key_nh()->id()) == false) {
        client->WaitForIdle();
        EXPECT_STREQ("", "Error deleting flow");
    }

    //Verify flow deletion
    client->WaitForIdle();
    if (FlowFail(vnet[1]->vrf()->GetName().c_str(), vnet_addr[1], 
                 vnet_addr[3], 1, 0, 0,
                 vnet[1]->flow_key_nh()->id()) == false) {
        EXPECT_STREQ("", "Error deleting forward flow");
    }

    client->EnqueueFlowAge();
    client->WaitForIdle();
    //No change in stats. Flows should be aged by now
    WAIT_FOR(1000, 100,
             (0U == Agent::GetInstance()->pkt()->flow_table()->Size()));

    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
}

// Non-Nat to Nat flow conversion test for traffic from VM to local VM
TEST_F(FlowTest, NonNat2Nat) {
    Agent::GetInstance()->uve()->flow_stats_collector()->UpdateFlowAgeTime(FlowStatsCollector::FlowAgeTime);
    Ip4Address addr = Ip4Address::from_string("2.1.1.1");
    Ip4Address gw = Ip4Address::from_string("10.1.1.2");
    Inet4TunnelRouteAdd(NULL, "vrf1", addr, 32, gw,
                        TunnelType::AllType(), 8, "default-project:vn2",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();

    //Create a Non-Nat Flow
    TxIpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[3], 1);
    client->WaitForIdle();

    EXPECT_TRUE(FlowGet(vnet[1]->vrf()->GetName().c_str(), vnet_addr[1], 
                        vnet_addr[3], 1, 0, 0, false, "vn1",
                        "default-project:vn2", 1, true, false,
                        vnet[1]->flow_key_nh()->id()));

    //Add floating IP configuration
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    EXPECT_TRUE(vnet[1]->HasFloatingIp(Address::INET));

    //Send the traffic again
    TxIpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[3], 1);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(vnet[1]->vrf()->vrf_id(), vnet_addr[1],
                            vnet_addr[3], 1, 0, 0,
                            vnet[1]->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_NAT_CHANGE);

    fe = FlowGet(vnet[1]->vrf()->vrf_id(), vnet_addr[3], vnet_addr[1],
                 1, 0, 0, vnet[1]->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_NAT_CHANGE);

    fe = FlowGet(vnet[3]->vrf()->vrf_id(), vnet_addr[3], "2.1.1.100",
                 1, 0, 0, vnet[3]->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_REVERSE_FLOW_CHANGE);

    Agent::GetInstance()->uve()->flow_stats_collector()->UpdateFlowAgeTime(AGE_TIME);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    vnet_table[1]->DeleteReq(NULL, "vrf1", addr, 32, NULL);
    client->WaitForIdle();
    //No change in stats. Flows should be aged by now
    WAIT_FOR(1000, 100,
             (0U == Agent::GetInstance()->pkt()->flow_table()->Size()));
}

// Two floating-IPs for a given interface. Negative test-case
TEST_F(FlowTest, TwoFloatingIp) {
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Create a Nat Flow
    TxIpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[3], 1);
    client->WaitForIdle();

    //Verify Nat Flow creation
    if (FlowGetNat(vnet[1]->vrf()->GetName().c_str(), vnet_addr[1], 
                   vnet_addr[3], 1, 0, 0, "default-project:vn2",
                   "default-project:vn2", 1,
                   vnet[3]->vrf()->GetName().c_str(), 
                   "2.1.1.100", vnet_addr[3], 0, 0, vnet[1]->flow_key_nh()->id(),
                   vnet[3]->flow_key_nh()->id()) == false) {
        EXPECT_STREQ("", "Error quering flow");
    }
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    const VmInterface::FloatingIpList list = vnet[1]->floating_ip_list();
    EXPECT_EQ(1U, list.list_.size());
    //Add one more floating IP to the same VM in the same VRF
    //(for the same dest-VN)
    AddFloatingIp("fip2", 2, "2.1.1.101");
    AddLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool1");
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip2");
    client->WaitForIdle();
    EXPECT_TRUE(vnet[1]->HasFloatingIp(Address::INET));
    const VmInterface::FloatingIpList list2 = vnet[1]->floating_ip_list();
    EXPECT_EQ(2U, list2.list_.size());

    //Verify that Nat-flow still exists
    if (FlowGetNat(vnet[1]->vrf()->GetName().c_str(), vnet_addr[1], 
                   vnet_addr[3], 1, 0, 0, "default-project:vn2",
                   "default-project:vn2", 1,
                   vnet[3]->vrf()->GetName().c_str(), 
                   "2.1.1.100", vnet_addr[3], 0, 0, vnet[1]->flow_key_nh()->id(),
                   vnet[3]->flow_key_nh()->id()) == false) {
        EXPECT_STREQ("", "Error quering flow");
    }

    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Send traffic in reverse direction for the second floating IP
    TxIpPacket(vnet[3]->id(), vnet_addr[3], "2.1.1.101", 1);
    client->WaitForIdle();

    //Verify that Nat-flow still exists
    if (FlowGetNat(vnet[1]->vrf()->GetName().c_str(), vnet_addr[1], 
                   vnet_addr[3], 1, 0, 0, "default-project:vn2",
                   "default-project:vn2", -1,
                   vnet[3]->vrf()->GetName().c_str(), 
                   "2.1.1.101", vnet_addr[3], 0, 0,
                   vnet[1]->flow_key_nh()->id(),
                   vnet[3]->flow_key_nh()->id()) == false) {
        EXPECT_STREQ("", "Error quering flow");
    }

    //Verfiy that flow creation for second floating IP as short-flow
    FlowEntry *fe = FlowGet(vnet[3]->vrf()->vrf_id(), vnet_addr[3], "2.1.1.100",
                            1, 0, 0, vnet[3]->flow_key_nh()->id());

    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_NO_REVERSE_FLOW);
    //cleanup
    client->EnqueueFlowFlush();
    client->WaitForIdle(2);
    WAIT_FOR(1000, 100, 
             (0U == Agent::GetInstance()->pkt()->flow_table()->Size()));

    //Delete the second floating IP created by this test-case
    DelLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool1");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip2");
    DelFloatingIp("fip2");
    client->WaitForIdle();
}

TEST_F(FlowTest, FlowCleanup_on_intf_del_1) {
    SetupMetadataService();
    client->WaitForIdle();
    //Add a flow from vhost interface to VM metadata address
    TxTcpPacket(vhost->id(), vhost_addr, 
                vnet[7]->mdata_ip_addr().to_string().c_str(), 100, 100, false, 2);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGetNat(vhost->vrf()->GetName(), vhost_addr,
                vnet[7]->mdata_ip_addr().to_string().c_str(), 6, 100, 100,
                Agent::GetInstance()->fabric_vrf_name(), "vn7", 2,
                vnet[7]->vrf()->GetName().c_str(),
                "169.254.169.254", vnet_addr[7], 100, 100,
                vhost->flow_key_nh()->id(), vnet[7]->flow_key_nh()->id()));

    TxTcpPacket(vnet[7]->id(), vnet_addr[7], 
                "169.254.169.254", 10, 80, false, 3);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGetNat(vnet[7]->vrf()->GetName(), vnet_addr[7],
                "169.254.169.254", 6, 10, 80,
                "vn7", Agent::GetInstance()->fabric_vrf_name(), 3, 
                vhost->vrf()->GetName().c_str(),
                vnet[7]->mdata_ip_addr().to_string().c_str(), vhost_addr, 10, 
                MEDATA_NAT_DPORT, vnet[7]->flow_key_nh()->id(),
                vhost->flow_key_nh()->id()));
    char mdata_ip[32];
    strcpy(mdata_ip, vnet[7]->mdata_ip_addr().to_string().c_str());

    int nh_id = vhost->flow_key_nh()->id();
    IntfCfgDel(input4, 0);
    client->WaitForIdle();
    EXPECT_TRUE(FlowFail(vhost->vrf()->GetName(), vhost_addr, mdata_ip, 6,
                         100, 100, vhost->flow_key_nh()->id()));
    EXPECT_TRUE(FlowFail("vrf7", "7.1.1.1", "169.254.169.254", 6, 10, 80,
                         nh_id));
    IntfCfgAdd(input1, 0);
    client->WaitForIdle();
    RemoveMetadataService();
    client->WaitForIdle();

    VmPortSetup(input4, 1, 0);
    client->WaitForIdle();
}

TEST_F(FlowTest, FlowCleanup_on_intf_del_2) {
    SetupMetadataService();
    client->WaitForIdle();
    struct PortInfo input[] = {
        {"vnet8", 8, "8.1.1.1", "00:00:08:01:01:01", 8, 8}
    };
    VmPortSetup(input, 1, 8);
    client->WaitForIdle();
    int nh_id = vnet[8]->flow_key_nh()->id();
    char mdata_ip[32];
    strcpy(mdata_ip, vnet[8]->mdata_ip_addr().to_string().c_str());

    TxTcpPacket(vhost->id(), vhost_addr, 
                vnet[8]->mdata_ip_addr().to_string().c_str(), 100, 100, false, 2);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGetNat(vhost->vrf()->GetName(), vhost_addr, mdata_ip,
                           6, 100, 100, Agent::GetInstance()->fabric_vrf_name(), "vn8", 2,
                           // 6, 100, 100, Agent::GetInstance()->fabric_vn_name(), "vn8", 2,
                           "vrf8", "169.254.169.254", vnet_addr[8], 100, 100,
                           vhost->flow_key_nh()->id(),
                           vnet[8]->flow_key_nh()->id()));

    TxTcpPacket(vnet[8]->id(), vnet_addr[8], 
                "169.254.169.254", 10, 80, false, 3);
    client->WaitForIdle();
    EXPECT_TRUE(FlowGetNat("vrf8", vnet_addr[8], "169.254.169.254", 6, 10, 80,
                           "vn8", Agent::GetInstance()->fabric_vrf_name(), 3,
                           // "vn8", Agent::GetInstance()->fabric_vn_name(), 3,
                           vhost->vrf()->GetName().c_str(), mdata_ip,
                           vhost_addr, 10, MEDATA_NAT_DPORT,
                           vnet[8]->flow_key_nh()->id(),
                           vhost->flow_key_nh()->id()));
    client->WaitForIdle();
    DelLink("virtual-machine-interface", "vnet8", "virtual-network", "vn8");
    client->WaitForIdle();
    IntfCfgDel(input, 0);
    client->WaitForIdle();
    EXPECT_TRUE(FlowFail(vhost->vrf()->GetName(), vhost_addr, mdata_ip, 6,
                         100, 100, vhost->flow_key_nh()->id()));
    EXPECT_TRUE(FlowFail("vrf8", "8.1.1.1", "169.254.169.254", 6, 10, 80,
                         nh_id));
    RemoveMetadataService();
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true, 8);
    DelNode("routing-instance", "vrf8");
    client->WaitForIdle();
}

//Ping from floating IP to local interface route, 
//which was leaked due to policy
TEST_F(FlowTest, FIP_traffic_to_leaked_routes) {
    //Leak a route from default-project:vn3:vn3 to default-project:vn2:vn2
    vnet_table[2]->AddLocalVmRouteReq(NULL, "default-project:vn2:vn2",
                                      vnet[5]->ip_addr(), 32,
                                      vnet[5]->GetUuid(), 
                                      vnet[5]->vn()->GetName(),
                                      vnet[5]->label(), SecurityGroupList(), 0,
                                      PathPreference(), Ip4Address(0));
    client->WaitForIdle();

  // HTTP packet from VM to Server
    TxTcpPacket(vnet[1]->id(), vnet_addr[1], vnet_addr[5],
                10000, 80, false);
    client->WaitForIdle();
    EXPECT_TRUE(NatValidateFlow(1, vnet[1]->vrf()->GetName().c_str(),
                                vnet_addr[1], vnet_addr[5], IPPROTO_TCP, 10000,
                                80, 1, vnet[5]->vrf()->GetName().c_str(), 
                                "2.1.1.100", vnet_addr[5],
                                10000, 80, "default-project:vn2",
                                "default-project:vn3",
                                vnet[1]->flow_key_nh()->id(),
                                vnet[5]->flow_key_nh()->id()));
    vnet_table[2]->DeleteReq(NULL, "default-project:vn2:vn2", vnet[5]->ip_addr(), 32, NULL);
    client->WaitForIdle();
}

TEST_F(FlowTest, Fip_preference_over_policy) {
    Ip4Address addr = Ip4Address::from_string("2.1.1.1");
    Ip4Address gw = Ip4Address::from_string("10.1.1.2");
    Inet4TunnelRouteAdd(NULL, "vrf1", addr, 32, gw, 
                        TunnelType::AllType(), 8, "default-project:vn2",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();
    TxUdpPacket(vnet[1]->id(), vnet_addr[1], "2.1.1.1", 10, 20, 1, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //client->EnqueueFlowFlush();
    //client->WaitForIdle();
    vnet_table[1]->DeleteReq(NULL, "vrf1", addr, 32, NULL);
    client->WaitForIdle();

    // since floating IP should be preffered deleteing the route should
    // not remove flow entries.
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());
}

TEST_F(FlowTest, DNAT_Fip_preference_over_policy_1) {
    Ip4Address addr = Ip4Address::from_string("2.1.1.1");
    Ip4Address gw = Ip4Address::from_string("10.1.1.2");
    Inet4TunnelRouteAdd(NULL, "vrf1", addr, 32, gw,
                        TunnelType::AllType(), 8, "vn1",
                        SecurityGroupList(), PathPreference());
    Ip4Address addr1 = Ip4Address::from_string("2.1.1.100");
    Inet4TunnelRouteAdd(NULL, "vrf1", addr1, 32, gw,
                        TunnelType::AllType(), 8, "vn1",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();
    TxIpMplsPacket(eth->id(), "10.1.1.2", vhost_addr,
                   vnet[1]->label(), "2.1.1.1", "2.1.1.100", 1, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    vnet_table[1]->DeleteReq(NULL, "vrf1", addr1, 32, NULL);
    client->WaitForIdle();

    // since floating IP should be preffered deleteing the route should
    // not remove flow entries.
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    vnet_table[1]->DeleteReq(NULL, "vrf1", addr, 32, NULL);
    client->WaitForIdle();
}

TEST_F(FlowTest, DNAT_Fip_preference_over_policy_2) {
    Ip4Address addr = Ip4Address::from_string("2.1.1.1");
    Ip4Address gw = Ip4Address::from_string("10.1.1.2");
    Inet4TunnelRouteAdd(NULL, "vrf1", addr, 32, gw,
                        TunnelType::AllType(), 8, "vn2",
                        SecurityGroupList(), PathPreference());
    Ip4Address addr1 = Ip4Address::from_string("2.1.1.100");
    Inet4TunnelRouteAdd(NULL, "vrf1", addr1, 32, gw,
                        TunnelType::AllType(), 8, "vn2",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();
    TxIpMplsPacket(eth->id(), "10.1.1.2", vhost_addr,
                   vnet[1]->label(), "2.1.1.1", "2.1.1.100", 1, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    vnet_table[1]->DeleteReq(NULL, "vrf1", addr1, 32, NULL);
    client->WaitForIdle();

    // since floating IP should be preffered deleteing the route should
    // not remove flow entries.
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    vnet_table[1]->DeleteReq(NULL, "vrf1", addr, 32, NULL);
    client->WaitForIdle();
}

TEST_F(FlowTest, DNAT_Fip_preference_over_policy) {
    Ip4Address addr = Ip4Address::from_string("2.1.1.1");
    Ip4Address gw = Ip4Address::from_string("10.1.1.2");
    Inet4TunnelRouteAdd(NULL, "vrf1", addr, 32, gw,
                        TunnelType::AllType(), 8, "vn1",
                        SecurityGroupList(), PathPreference());
    Ip4Address addr1 = Ip4Address::from_string("2.1.1.100");
    Inet4TunnelRouteAdd(NULL, "vrf1", addr1, 32, gw,
                        TunnelType::AllType(), 8, "vn1",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();
    TxIpMplsPacket(eth->id(), "10.1.1.2", vhost_addr,
                   vnet[1]->label(), "2.1.1.1", "2.1.1.100", 1, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    vnet_table[1]->DeleteReq(NULL, "vrf1", addr1, 32, NULL);
    client->WaitForIdle();

    // since floating IP should be preffered deleteing the route should
    // not remove flow entries.
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    vnet_table[1]->DeleteReq(NULL, "vrf1", addr, 32, NULL);
    client->WaitForIdle();
}

TEST_F(FlowTest, Prefer_policy_over_fip_LPM_find) {
    Ip4Address addr = Ip4Address::from_string("20.1.1.1");
    Ip4Address gw = Ip4Address::from_string("10.1.1.2");
    Inet4TunnelRouteAdd(NULL, "vrf1", addr, 32, gw,
                        TunnelType::AllType(), 8, "default-project:vn2",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();
    TxUdpPacket(vnet[1]->id(), vnet_addr[1], "20.1.1.1", 10, 20, 1, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    vnet_table[1]->DeleteReq(NULL, "vrf1", addr, 32, NULL);
    client->WaitForIdle();

    // since policy leaked route should be preffered deleteing the route should
    // remove flow entries.
    WAIT_FOR(1000, 100,
             (0U == Agent::GetInstance()->pkt()->flow_table()->Size()));
}

TEST_F(FlowTest, Prefer_fip2_over_fip1_lower_addr) {
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip_2");
    client->WaitForIdle();
    TxUdpPacket(vnet[1]->id(), vnet_addr[1], "20.1.1.1", 10, 20, 1, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip_2");
    client->WaitForIdle();

    // since fip2 route should be preffered removing association of fip2 should
    // remove flow entries.
    WAIT_FOR(1000, 100,
             (0U == Agent::GetInstance()->pkt()->flow_table()->Size()));
}

TEST_F(FlowTest, Prefer_fip2_over_fip3_lower_addr) {
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip_2");
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip_3");
    client->WaitForIdle();
    TxUdpPacket(vnet[1]->id(), vnet_addr[1], "20.1.1.1", 10, 20, 1, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip_3");
    client->WaitForIdle();

    // since fip2 route should be preffered removing association of fip3 should
    // not remove flow entries.
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip_2");
    client->WaitForIdle();

    // since fip2 route should be preffered removing association of fip2 should
    // remove flow entries.
    WAIT_FOR(1000, 100,
             (0U == Agent::GetInstance()->pkt()->flow_table()->Size()));
}

TEST_F(FlowTest, fip1_to_fip2_SNAT_DNAT) {
    AddLink("virtual-machine-interface", "vnet5", "floating-ip", "fip_2");
    client->WaitForIdle();
    TxUdpPacket(vnet[1]->id(), vnet_addr[1], "2.1.1.99", 10, 20, 1, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    FlowEntry *fe = FlowGet(vnet[1]->id(), vnet_addr[1], "2.1.1.99",
                            IPPROTO_UDP, 10, 20, vnet[1]->flow_key_nh()->id());
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::NatFlow));

    // both Source and Destination NAT should be set
    EXPECT_TRUE(fe->key().src_addr != rfe->key().dst_addr);
    EXPECT_TRUE(fe->key().dst_addr != rfe->key().src_addr);

    DelLink("virtual-machine-interface", "vnet5", "floating-ip", "fip_2");
    client->WaitForIdle();

    WAIT_FOR(1000, 100,
             (0U == Agent::GetInstance()->pkt()->flow_table()->Size()));
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    //client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    client = TestInit(init_file, ksync_init, true, true, true,
                      AgentParam::kAgentStatsInterval,
                      AgentParam::kFlowStatsInterval, true, false);
    Setup();
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    Teardown();
    TestShutdown();
    delete client;
    return ret;
}
