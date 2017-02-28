/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <algorithm>
#include <net/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "pkt/flow_table.h"

#define MAX_VNET 4

void RouterIdDepInit(Agent *agent) {
}

struct TestFlowKey {
    uint32_t        vrfid_;
    const char      *sip_;
    const char      *dip_;
    uint8_t         proto_;
    uint16_t        sport_;
    uint16_t        dport_;
};

#define vm1_ip "11.1.1.1"
#define vm2_ip "11.1.1.2"
#define vm3_ip "12.1.1.1"
#define vm4_ip "13.1.1.1"
#define vm5_ip "14.1.1.1"
#define vm1_fip "14.1.1.100"
#define vm1_fip2 "14.1.1.101"
#define vm2_fip "14.1.1.100"
#define remote_vm1_ip "11.1.1.3"
#define remote_vm3_ip "12.1.1.3"
#define remote_vm4_ip "13.1.1.2"
#define remote_router_ip "10.1.1.2"
#define vhost_ip_addr "10.1.2.1"
#define linklocal_ip "169.254.1.10"
#define linklocal_port 4000
#define fabric_port 8000

#define vm_a_ip "16.1.1.1"
#define vm_b_ip "16.1.1.2"

#define remote_vm1_ip_5 "11.1.1.5"
#define remote_vm1_ip_subnet "11.1.1.0"
#define remote_vm1_ip_plen 24

int fd_table[MAX_VNET];
InetInterface *vhost;
struct PortInfo input[] = {
        {"flow0", 6, vm1_ip, "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, vm2_ip, "00:00:00:01:01:02", 5, 2},
        {"flow2", 8, vm3_ip, "00:00:00:01:01:03", 5, 3},
};

struct PortInfo input2[] = {
        {"flow3", 9, vm4_ip, "00:00:00:01:01:04", 3, 4},
};

struct PortInfo input3[] = {
        {"flow4", 10, vm5_ip, "00:00:00:01:01:06", 4, 5},
};

struct PortInfo input4[] = {
        {"flow5", 3, vm_a_ip, "00:00:00:01:01:07", 6, 6},
        {"flow6", 4, vm_b_ip, "00:00:00:01:01:08", 6, 7},
};

IpamInfo ipam_info[] = {
    {"11.1.1.0", 24, "11.1.1.10"},
    {"12.1.1.0", 24, "12.1.1.10"},
};

IpamInfo ipam_info2[] = {
    {"13.1.1.0", 24, "13.1.1.10"},
};

IpamInfo ipam_info3[] = {
    {"14.1.1.0", 24, "14.1.1.10"},
};

IpamInfo ipam_info4[] = {
    {"16.1.1.0", 24, "16.1.1.10"},
};

typedef enum {
    INGRESS = 0,
    EGRESS = 1,
    BIDIRECTION = 2
} AclDirection;

int hash_id;
VmInterface *flow0;
VmInterface *flow1;
VmInterface *flow2;
VmInterface *flow3;
VmInterface *flow4;
VmInterface *flow5;
VmInterface *flow6;
std::string eth_itf;
PhysicalInterface *eth;

static void NHNotify(DBTablePartBase *partition, DBEntryBase *entry) {
}

class FlowTest : public ::testing::Test {
public:
    FlowTest() : peer_(NULL), agent_(Agent::GetInstance()) {
        flow_proto_ = agent_->pkt()->get_flow_proto();
        boost::scoped_ptr<InetInterfaceKey> key(new InetInterfaceKey("vhost0"));
        vhost = static_cast<InetInterface *>(Agent::GetInstance()->
                interface_table()->FindActiveEntry(key.get()));
        flow_stats_collector_ = Agent::GetInstance()->flow_stats_manager()->
                                    default_flow_stats_collector_obj();
    }

    bool FlowTableWait(size_t count) {
        int i = 1000;
        while (i > 0) {
            i--;
            if (get_flow_proto()->FlowCount() == count) {
                break;
            }
            client->WaitForIdle();
            usleep(1);
        }
        return (get_flow_proto()->FlowCount() == count);
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    }

    void CreateLocalRoute(const char *vrf, const char *ip,
                          VmInterface *intf, int label) {
        Ip4Address addr = Ip4Address::from_string(ip);
        VnListType vn_list;
        vn_list.insert(intf->vn()->GetName());
        agent()->fabric_inet4_unicast_table()->
            AddLocalVmRouteReq(agent()->local_peer(), vrf, addr, 32,
                               intf->GetUuid(), vn_list, label,
                               SecurityGroupList(), CommunityList(), false,
                               PathPreference(), Ip4Address(0),
                               EcmpLoadBalance(), false, false);
        client->WaitForIdle();
        EXPECT_TRUE(RouteFind(vrf, addr, 32));
    }

    void CreateRemoteRoute(const char *vrf, const char *remote_vm, uint8_t plen,
                           const char *serv, int label, const char *vn) {
        Ip4Address addr = Ip4Address::from_string(remote_vm);
        Ip4Address gw = Ip4Address::from_string(serv);
        Inet4TunnelRouteAdd(peer_, vrf, addr, plen, gw, TunnelType::MplsType(),
                            label, vn, SecurityGroupList(), PathPreference());
        client->WaitForIdle(2);
        WAIT_FOR(1000, 500, (RouteFind(vrf, addr, plen) == true));
    }
    void CreateRemoteRoute(const char *vrf, const char *remote_vm,
                           const char *serv, int label, const char *vn) {
        CreateRemoteRoute(vrf, remote_vm, 32, serv, label, vn);
    }
    void DeleteRoute(const char *vrf, const char *ip, uint8_t plen) {
        Ip4Address addr = Ip4Address::from_string(ip);
        agent()->fabric_inet4_unicast_table()->DeleteReq(agent()->local_peer(),
                                                vrf, addr, plen, NULL);
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFind(vrf, addr, plen) == false));
    }

    void DeleteRemoteRoute(const char *vrf, const char *ip, uint8_t plen) {
        Ip4Address addr = Ip4Address::from_string(ip);
        agent()->fabric_inet4_unicast_table()->DeleteReq(peer_, 
                vrf, addr, plen,
                new ControllerVmRoute(static_cast<BgpPeer *>(peer_)));
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFind(vrf, addr, 32) == false));
    }

    void DeleteRoute(const char *vrf, const char *ip) {
        DeleteRoute(vrf, ip, 32);
    }

    void DeleteRemoteRoute(const char *vrf, const char *ip) {
        DeleteRemoteRoute(vrf, ip, 32);
    }

    static void FlowAdd(int hash_id, int vrf, const char *sip, const char *dip,
                        int proto, int sport, int dport, const char *nat_sip,
                        const char *nat_dip, int nat_vrf) {
        Agent *agent = Agent::GetInstance();
        boost::shared_ptr<PktInfo> pkt_1(new PktInfo(agent,
                                                     100, PktHandler::FLOW, 0));
        PktFlowInfo flow_info_1(agent, pkt_1,
                                agent->pkt()->get_flow_proto()->GetTable(0));
        PktFlowInfo *flow_info = &flow_info_1;
        MatchPolicy policy;
        string svn = "svn";
        string dvn = "dvn";

        PktInfo *pkt = pkt_1.get();
        pkt->vrf = vrf;
        pkt->ip_saddr = Ip4Address(ntohl(inet_addr(sip)));
        pkt->ip_daddr = Ip4Address(ntohl(inet_addr(dip)));
        pkt->ip_proto = proto;
        pkt->sport = sport;
        pkt->dport = dport;
        policy.action_info.action = (1 << TrafficAction::PASS);

        flow_info->nat_vrf = nat_vrf;
        if (nat_sip) {
            flow_info->nat_ip_saddr = Ip4Address(ntohl(inet_addr(nat_sip)));
        } else {
            flow_info->nat_ip_saddr = pkt->ip_saddr;
        }

        if (nat_dip) {
            flow_info->nat_ip_daddr = Ip4Address(ntohl(inet_addr(nat_dip)));
        } else {
            flow_info->nat_ip_daddr = pkt->ip_daddr;
        }
        flow_info->nat_sport = sport;
        flow_info->nat_dport = dport;

        if (pkt->ip_saddr != flow_info->nat_ip_saddr ||
            pkt->ip_daddr != flow_info->nat_ip_daddr) {
            flow_info->nat_done = true;
        } else {
            flow_info->nat_done = false;
        }
        pkt->agent_hdr.cmd_param = hash_id;
        PktControlInfo in;
        PktControlInfo out;

        flow_info->Add(pkt, &in, &out);
        client->WaitForIdle();
    }
    
    static int GetFlowPassCount(int total_flows, int age_time_usecs) {
        return 0;
    }

    static void TestTearDown() {
        client->Reset();
        if (ksync_init_) {
            DeleteTapIntf(fd_table, MAX_VNET);
        }
        client->WaitForIdle();
    }

    static void TestSetup(bool ksync_init) {
        ksync_init_ = ksync_init;
        if (ksync_init_) {
            CreateTapInterfaces("flow", MAX_VNET, fd_table);
            client->WaitForIdle();
        }
    }

    void CheckSandeshResponse(Sandesh *sandesh, uint32_t flows) {
        if (memcmp(sandesh->Name(), "FlowRecordsResp",
                   strlen("FlowRecordsResp")) == 0) {
            FlowRecordsResp *resp = static_cast<FlowRecordsResp *>(sandesh);
            EXPECT_TRUE(resp->get_flow_list().size() == flows);
        } else if (memcmp(sandesh->Name(), "FlowRecordResp",
                   strlen("FlowRecordResp")) == 0) {
            FlowRecordResp *resp = static_cast<FlowRecordResp *>(sandesh);
            EXPECT_TRUE(resp->get_record().sip == vm1_ip);
            EXPECT_TRUE(resp->get_record().dip == vm2_ip);
            EXPECT_TRUE(resp->get_record().src_port == 1000);
            EXPECT_TRUE(resp->get_record().dst_port == 200);
            EXPECT_TRUE(resp->get_record().protocol == IPPROTO_TCP);
        }
    }

    std::string AddAclXmlString(const char *name, int id, int proto,
                                const char *action, const char* uuid) {
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
                "                <dynamic>false</dynamic>\n"
                "                <acl-rule>\n"
                "                    <match-condition>\n"
                "                        <src-address>\n"
                "                            <virtual-network>any</virtual-network>\n"
                "                        </src-address>\n"
                "                        <protocol>%d</protocol>\n"
                "                        <src-port>\n"
                "                            <start-port>0</start-port>\n"
                "                            <end-port>10000</end-port>\n"
                "                        </src-port>\n"
                "                        <dst-address>\n"
                "                            <virtual-network>any</virtual-network>\n"
                "                        </dst-address>\n"
                "                        <dst-port>\n"
                "                            <start-port>0</start-port>\n"
                "                            <end-port>10000</end-port>\n"
                "                        </dst-port>\n"
                "                    </match-condition>\n"
                "                    <action-list>\n"
                "                        <simple-action>%s</simple-action>\n"
                "                    </action-list>\n"
                "                    <rule-uuid>%s</rule-uuid>\n"
                "                </acl-rule>\n"
                "                <acl-rule>\n"
                "                    <match-condition>\n"
                "                        <src-address>\n"
                "                            <virtual-network>vn6</virtual-network>\n"
                "                        </src-address>\n"
                "                        <protocol>any</protocol>\n"
                "                        <src-port>\n"
                "                            <start-port>0</start-port>\n"
                "                            <end-port>60000</end-port>\n"
                "                        </src-port>\n"
                "                        <dst-address>\n"
                "                            <virtual-network>vn6</virtual-network>\n"
                "                        </dst-address>\n"
                "                        <dst-port>\n"
                "                            <start-port>0</start-port>\n"
                "                            <end-port>60000</end-port>\n"
                "                        </dst-port>\n"
                "                    </match-condition>\n"
                "                    <action-list>\n"
                "                        <simple-action>deny</simple-action>\n"
                "                    </action-list>\n"
                "                    <rule-uuid>fe6a4dcb-dde4-48e6-8957-856a7aacb2e1</rule-uuid>\n"
                "                </acl-rule>\n"
                "           </access-control-list-entries>\n"
                "       </node>\n"
                "   </update>\n"
                "</config>\n", name, id, proto, action, uuid);
        string s(buff);
        return s;
    }

    void AddAclEntry(const char *name, int id, int proto,
                     const char *action, const char *uuid_str) {
        std::string s = AddAclXmlString(name, id, proto, action, uuid_str);
        pugi::xml_document xdoc_;
        pugi::xml_parse_result result = xdoc_.load(s.c_str());
        EXPECT_TRUE(result);
        Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
        client->WaitForIdle();
    }

    std::string AddAclSubnetXML(const char *name, int id, int proto,
                                const char *action, const char* uuid,
                                const char *src_net, const char *dst_net) {
        char buff[12000];
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
                "                <dynamic>false</dynamic>\n"
                "                <acl-rule>\n"
                "                    <match-condition>\n"
                "                        <src-address>%s</src-address>\n"
                "                        <protocol>%d</protocol>\n"
                "                        <src-port>\n"
                "                            <start-port>0</start-port>\n"
                "                            <end-port>10000</end-port>\n"
                "                        </src-port>\n"
                "                        <dst-address>%s</dst-address>\n"
                "                        <dst-port>\n"
                "                            <start-port>0</start-port>\n"
                "                            <end-port>10000</end-port>\n"
                "                        </dst-port>\n"
                "                    </match-condition>\n"
                "                    <action-list>\n"
                "                        <simple-action>%s</simple-action>\n"
                "                    </action-list>\n"
                "                    <rule-uuid>%s</rule-uuid>\n"
                "                </acl-rule>\n"
                "                <acl-rule>\n"
                "                    <match-condition>\n"
                "                        <src-address>\n"
                "                            <virtual-network>vn6</virtual-network>\n"
                "                        </src-address>\n"
                "                        <protocol>any</protocol>\n"
                "                        <src-port>\n"
                "                            <start-port>0</start-port>\n"
                "                            <end-port>60000</end-port>\n"
                "                        </src-port>\n"
                "                        <dst-address>\n"
                "                            <virtual-network>vn6</virtual-network>\n"
                "                        </dst-address>\n"
                "                        <dst-port>\n"
                "                            <start-port>0</start-port>\n"
                "                            <end-port>60000</end-port>\n"
                "                        </dst-port>\n"
                "                    </match-condition>\n"
                "                    <action-list>\n"
                "                        <simple-action>deny</simple-action>\n"
                "                    </action-list>\n"
                "                    <rule-uuid>\n"
                "                        fe6a4dcb-dde4-48e6-8957-856a7aacb2e1\n"
                "                     </rule-uuid>\n"
                "                </acl-rule>\n"
                "           </access-control-list-entries>\n"
                "       </node>\n"
                "   </update>\n"
                "</config>\n", name, id, src_net, proto, dst_net, action, uuid);
        string s(buff);
        return s;
    }

    void AddAclEntry(const char *name, int id, int proto,
                     const char *action, const char *uuid_str,
                     const char *src_subnet, const char *dst_subnet) {
        std::string s = AddAclSubnetXML(name, id, proto, action, uuid_str,
                                        src_subnet, dst_subnet);
        pugi::xml_document xdoc_;
        pugi::xml_parse_result result = xdoc_.load(s.c_str());
        EXPECT_TRUE(result);
        Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
        client->WaitForIdle();
    }

    std::string AddAclXmlString(const char *name, int id, int proto,
                                const char *simple_action,
                                const char *log_action,
                                const char* uuid) {
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
                "                <dynamic>false</dynamic>\n"
                "                <acl-rule>\n"
                "                    <match-condition>\n"
                "                        <src-address>\n"
                "                            <virtual-network>any</virtual-network>\n"
                "                        </src-address>\n"
                "                        <protocol>%d</protocol>\n"
                "                        <src-port>\n"
                "                            <start-port>0</start-port>\n"
                "                            <end-port>10000</end-port>\n"
                "                        </src-port>\n"
                "                        <dst-address>\n"
                "                            <virtual-network>any</virtual-network>\n"
                "                        </dst-address>\n"
                "                        <dst-port>\n"
                "                            <start-port>0</start-port>\n"
                "                            <end-port>10000</end-port>\n"
                "                        </dst-port>\n"
                "                    </match-condition>\n"
                "                    <action-list>\n"
                "                        <simple-action>%s</simple-action>\n"
                "                        <log>%s</log>\n"
                "                    </action-list>\n"
                "                    <rule-uuid>%s</rule-uuid>\n"
                "                </acl-rule>\n"
                "                <acl-rule>\n"
                "                    <match-condition>\n"
                "                        <src-address>\n"
                "                            <virtual-network>vn6</virtual-network>\n"
                "                        </src-address>\n"
                "                        <protocol>any</protocol>\n"
                "                        <src-port>\n"
                "                            <start-port>0</start-port>\n"
                "                            <end-port>60000</end-port>\n"
                "                        </src-port>\n"
                "                        <dst-address>\n"
                "                            <virtual-network>vn6</virtual-network>\n"
                "                        </dst-address>\n"
                "                        <dst-port>\n"
                "                            <start-port>0</start-port>\n"
                "                            <end-port>60000</end-port>\n"
                "                        </dst-port>\n"
                "                    </match-condition>\n"
                "                    <action-list>\n"
                "                        <simple-action>deny</simple-action>\n"
                "                    </action-list>\n"
                "                    <rule-uuid>fe6a4dcb-dde4-48e6-8957-856a7aacb2e1</rule-uuid>\n"
                "                </acl-rule>\n"
                "           </access-control-list-entries>\n"
                "       </node>\n"
                "   </update>\n"
                "</config>\n", name, id, proto, simple_action, log_action,
            uuid);
        string s(buff);
        return s;
    }
    void AddAclLogActionEntry(const char *name, int id, int proto,
                              const char *simple_action, const char *log_action,
                              const char *uuid_str) {
        std::string s = AddAclXmlString(name, id, proto, simple_action,
                                        log_action, uuid_str);
        pugi::xml_document xdoc_;
        pugi::xml_parse_result result = xdoc_.load(s.c_str());
        EXPECT_TRUE(result);
        Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(),
                                                          0);
        client->WaitForIdle();
    }

    void AddSgEntry(const char *sg_name, const char *name, int id,
                    int proto, const char *action, AclDirection direction,
                    const char *uuid1, const char* uuid2) {

        AddSg(sg_name, id);
        char acl_name[1024];
        uint16_t max_len = sizeof(acl_name) - 1;
        strncpy(acl_name, name, max_len);
        switch (direction) {
            case INGRESS:
                strncat(acl_name, "ingress-access-control-list", max_len);
                AddAclEntry(acl_name, id, proto, action, uuid1);
                AddLink("security-group", sg_name, "access-control-list", acl_name);
                break;
            case EGRESS:
                strncat(acl_name, "egress-access-control-list", max_len);
                AddAclEntry(acl_name, id, proto, action, uuid1);
                AddLink("security-group", sg_name, "access-control-list", acl_name);
                break;
            case BIDIRECTION:
                strncat(acl_name, "egress-access-control-list", max_len);
                AddAclEntry(acl_name, id, proto, action, uuid1);
                AddLink("security-group", sg_name, "access-control-list", acl_name);

                strncpy(acl_name, name, max_len);
                strncat(acl_name, "ingress-access-control-list", max_len);
                AddAclEntry(acl_name, id, proto, action, uuid2);
                AddLink("security-group", sg_name, "access-control-list", acl_name);
                break;
        }
    }

    void FlowSetup() {
        CreateVmportEnv(input4, 2, 0);
        client->WaitForIdle(5);
        AddIPAM("vn6", ipam_info4, 1);
        client->WaitForIdle();
        flow5 = VmInterfaceGet(input4[0].intf_id);
        assert(flow5);
        flow6 = VmInterfaceGet(input4[1].intf_id);
        assert(flow6);
    }

    void FlowTeardown() {
        client->Reset();
        DeleteVmportEnv(input4, 2, true, 0);
        client->WaitForIdle(5);
        DelIPAM("vn6");
        client->WaitForIdle();
        client->PortDelNotifyWait(2);
    }
protected:
    virtual void SetUp() {
        eth = EthInterfaceGet("vnet0");
        EXPECT_TRUE(eth != NULL);
        strcpy(router_id_, agent_->router_id().to_string().c_str());

        unsigned int vn_count = 0;
        EXPECT_EQ(0U, get_flow_proto()->FlowCount());
        hash_id = 1;
        client->Reset();
        CreateVmportEnv(input, 3, 1);
        client->WaitForIdle(5);
        AddIPAM("vn5", ipam_info, 2);
        client->WaitForIdle();
        vn_count++;

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortActive(input, 2));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 2));
        EXPECT_EQ(7U, agent()->interface_table()->Size());
        EXPECT_EQ(3U, agent()->vm_table()->Size());
        EXPECT_EQ(vn_count, agent()->vn_table()->Size());
        EXPECT_EQ(3U, PortSubscribeSize(agent()));

        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmInterfaceGet(input[1].intf_id);
        assert(flow1);
        flow2 = VmInterfaceGet(input[2].intf_id);
        assert(flow2);

        /* Create interface flow3 in vn3 , vm4. Associate vn3 with acl2 */
        client->Reset();
        CreateVmportEnv(input2, 1, 2);
        client->WaitForIdle(5);
        AddIPAM("vn3", ipam_info2, 1);
        client->WaitForIdle();
        vn_count++;
        EXPECT_TRUE(VmPortActive(input2, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input2, 0));
        EXPECT_EQ(8U, agent()->interface_table()->Size());
        EXPECT_EQ(4U, agent()->vm_table()->Size());
        EXPECT_EQ(vn_count, agent()->vn_table()->Size());
        EXPECT_EQ(4U, PortSubscribeSize(agent()));
        EXPECT_EQ(2U, agent()->acl_table()->Size());

        flow3 = VmInterfaceGet(input2[0].intf_id);
        assert(flow3);

        /* Create interface flow4 in default-project:vn4 */
        client->Reset();
        CreateVmportFIpEnv(input3, 1);
        client->WaitForIdle(5);
        AddIPAM("default-project:vn4", ipam_info3, 1);
        client->WaitForIdle();
        vn_count++;
        EXPECT_TRUE(VmPortActive(input3, 0));
        EXPECT_EQ(9U, agent()->interface_table()->Size());
        EXPECT_EQ(5U, agent()->vm_table()->Size());
        EXPECT_EQ(vn_count, agent()->vn_table()->Size());
        EXPECT_EQ(5U, PortSubscribeSize(agent()));
        flow4 = VmInterfaceGet(input3[0].intf_id);
        assert(flow4);
        // Configure Floating-IP
        AddFloatingIpPool("fip-pool1", 1);
        AddFloatingIp("fip1", 1, "14.1.1.100");
        AddFloatingIp("fip2", 1, "14.1.1.101");
        AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        AddLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool1");
        AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
                "default-project:vn4");
        AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
        client->WaitForIdle();
        boost::system::error_code ec;
        bgp_peer_ = peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                              "xmpp channel");
        Ip4Address gw_ip = Ip4Address::from_string("11.1.1.254");
        //Add a gateway route pointing to pkt0
        VrfEntry *vrf = VrfGet("vrf5");
        static_cast<InetUnicastAgentRouteTable *>(
                vrf->GetInet4UnicastRouteTable())->AddHostRoute("vrf5",
                gw_ip, 32, "vn5", false);
        client->WaitForIdle();

        FlowStatsTimerStartStop(agent_, true);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();
        Ip4Address gw_ip = Ip4Address::from_string("11.1.1.254");
        Agent::GetInstance()->fabric_inet4_unicast_table()->DeleteReq(
            Agent::GetInstance()->local_peer(), "vrf5", gw_ip, 32, NULL);
        client->WaitForIdle();
        DeleteVmportEnv(input, 3, true, 1);
        client->WaitForIdle(3);
        DelIPAM("vn5");
        client->WaitForIdle();
        client->PortDelNotifyWait(3);
        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));
        EXPECT_FALSE(VmPortFind(input, 2));
        EXPECT_EQ(6U, agent()->interface_table()->Size());
        EXPECT_EQ(2U, PortSubscribeSize(agent()));

        client->Reset();
        DeleteVmportEnv(input2, 1, true, 2);
        client->WaitForIdle(3);
        DelIPAM("vn3");
        client->WaitForIdle();
        client->PortDelNotifyWait(1);
        EXPECT_EQ(5U, agent()->interface_table()->Size());
        EXPECT_EQ(1U, PortSubscribeSize(agent()));
        EXPECT_FALSE(VmPortFind(input2, 0));

        client->Reset();
        DeleteVmportFIpEnv(input3, 1, true);
        client->WaitForIdle(3);
        DelIPAM("default-project:vn4");
        client->WaitForIdle();
        client->PortDelNotifyWait(1);
        EXPECT_EQ(4U, agent()->interface_table()->Size());
        EXPECT_EQ(0U, PortSubscribeSize(agent()));
        EXPECT_FALSE(VmPortFind(input3, 0));

        EXPECT_EQ(0U, agent()->vm_table()->Size());
        EXPECT_EQ(0U, agent()->vn_table()->Size());
        EXPECT_EQ(0U, agent()->acl_table()->Size());
        EXPECT_EQ(1U, agent()->vrf_table()->Size());
        DeleteBgpPeer(peer_);
        FlowStatsTimerStartStop(agent_, false);
        client->WaitForIdle(3);
        WAIT_FOR(100, 1, (0U == get_flow_proto()->FlowCount()));
    }

    Agent *agent() {return agent_;}
    FlowProto *get_flow_proto() const { return flow_proto_; }

    FlowEventQueue *GetFlowEventQueue(uint32_t table_index) {
        return flow_proto_->flow_event_queue_[table_index];
    }

    DeleteFlowEventQueue *GetDeleteFlowEventQueue(uint32_t table_index) {
        return flow_proto_->flow_delete_queue_[table_index];
    }

    KSyncFlowEventQueue *GetKSyncFlowEventQueue(uint32_t table_index) {
        return flow_proto_->flow_ksync_queue_[table_index];
    }

    UpdateFlowEventQueue *GetUpdateFlowEventQueue() {
        return &flow_proto_->flow_update_queue_;
    }

protected:
    static bool ksync_init_;
    BgpPeer *peer_;
public:
    Agent *agent_;
    FlowProto *flow_proto_;
    FlowStatsCollectorObject* flow_stats_collector_;
    char router_id_[80];
};

bool FlowTest::ksync_init_;
