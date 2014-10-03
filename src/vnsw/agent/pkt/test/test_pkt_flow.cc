/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "pkt/flow_table.h"

#define MAX_VNET 4

void RouterIdDepInit(Agent *agent) {
}

struct TestFlowKey {
    uint16_t        vrfid_;
    const char      *sip_;
    const char      *dip_;
    uint8_t         proto_;
    uint16_t        sport_;
    uint16_t        dport_;
};

#define vm1_ip "11.1.1.1"
#define ip6_vm1_ip "::11.1.1.1"
#define vm2_ip "11.1.1.2"
#define ip6_vm2_ip "::11.1.1.2"
#define vm3_ip "12.1.1.1"
#define ip6_vm3_ip "::12.1.1.1"
#define vm4_ip "13.1.1.1"
#define ip6_vm4_ip "::13.1.1.1"
#define vm5_ip "14.1.1.1"
#define ip6_vm5_ip "::14.1.1.1"
#define vm1_fip "14.1.1.100"
#define ip6_vm1_fip "::14.1.1.100"
#define vm1_fip2 "14.1.1.101"
#define ip6_vm1_fip2 "::14.1.1.101"
#define vm2_fip "14.1.1.100"
#define ip6_vm2_fip "::14.1.1.100"
#define remote_vm1_ip "11.1.1.3"
#define ip6_remote_vm1_ip "::11.1.1.3"
#define remote_vm3_ip "12.1.1.3"
#define ip6_remote_vm3_ip "::12.1.1.3"
#define remote_vm4_ip "13.1.1.2"
#define ip6_remote_vm4_ip "::13.1.1.2"
#define remote_router_ip "10.1.1.2"
#define ip6_remote_router_ip "::10.1.1.2"
#define vhost_ip_addr "10.1.2.1"
#define ip6_vhost_ip_addr "::10.1.2.1"
#define linklocal_ip "169.254.1.10"
#define ip6_linklocal_ip "::169.254.1.10"
#define linklocal_port 4000
#define fabric_port 8000

#define vm_a_ip "16.1.1.1"
#define vm_b_ip "16.1.1.2"

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

static void NHNotify(DBTablePartBase *partition, DBEntryBase *entry) {
}

class FlowTest : public ::testing::Test {
public:
    FlowTest() : peer_(NULL), agent_(Agent::GetInstance()) {
        boost::scoped_ptr<InetInterfaceKey> key(new InetInterfaceKey("vhost0"));
        vhost = static_cast<InetInterface *>(Agent::GetInstance()->
                interface_table()->FindActiveEntry(key.get()));
    }

    bool FlowTableWait(size_t count) {
        int i = 1000;
        while (i > 0) {
            i--;
            if (agent()->pkt()->flow_table()->Size() == count) {
                break;
            }
            client->WaitForIdle();
            usleep(1);
        }
        return (agent()->pkt()->flow_table()->Size() == count);
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    }

    void CreateLocalRoute(const char *vrf, const char *ip,
                          VmInterface *intf, int label) {
        Ip4Address addr = Ip4Address::from_string(ip);
        agent()->fabric_inet4_unicast_table()->
            AddLocalVmRouteReq(peer_, vrf, addr, 32, intf->GetUuid(),
                               intf->vn()->GetName(), label,
                               SecurityGroupList(), false, PathPreference(),
                               Ip4Address(0));
        client->WaitForIdle();
        EXPECT_TRUE(RouteFind(vrf, addr, 32));
    }

    void CreateRemoteRoute(const char *vrf, const char *remote_vm, 
                           const char *serv, int label, const char *vn) {
        Ip4Address addr = Ip4Address::from_string(remote_vm);
        Ip4Address gw = Ip4Address::from_string(serv);
        Inet4TunnelRouteAdd(peer_, vrf, addr, 32, gw, TunnelType::MplsType(), label, vn,
                            SecurityGroupList(), PathPreference());
        client->WaitForIdle(2);
        WAIT_FOR(1000, 500, (RouteFind(vrf, addr, 32) == true));
    }

    void DeleteRoute(const char *vrf, const char *ip) {
        Ip4Address addr = Ip4Address::from_string(ip);
        agent()->fabric_inet4_unicast_table()->DeleteReq(peer_, 
                                                vrf, addr, 32, NULL);
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFind(vrf, addr, 32) == false));
    }

    void DeleteRemoteRoute(const char *vrf, const char *ip) {
        Ip4Address addr = Ip4Address::from_string(ip);
        agent()->fabric_inet4_unicast_table()->DeleteReq(peer_, 
                vrf, addr, 32, NULL);
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFind(vrf, addr, 32) == false));
    }

    static void RunFlowAudit() {
        FlowTableKSyncObject *ksync_obj = 
            Agent::GetInstance()->ksync()->flowtable_ksync_obj();
        ksync_obj->AuditProcess();
        ksync_obj->AuditProcess();
    }

    static bool KFlowHoldAdd(uint32_t hash_id, int vrf, const char *sip, 
                             const char *dip, int proto, int sport, int dport,
                             int nh_id) {
        FlowTableKSyncObject *ksync_obj = 
            Agent::GetInstance()->ksync()->flowtable_ksync_obj();
        if (hash_id >= ksync_obj->flow_table_entries_count()) {
            return false;
        }
        if (ksync_init_) {
            return false;
        }

        vr_flow_entry *vr_flow = KSyncSockTypeMap::GetFlowEntry(hash_id);

        vr_flow_req req;
        req.set_fr_index(hash_id);
        req.set_fr_flow_sip(inet_addr(sip));
        req.set_fr_flow_dip(inet_addr(dip));
        req.set_fr_flow_proto(proto);
        req.set_fr_flow_sport(htons(sport));
        req.set_fr_flow_dport(htons(dport));
        req.set_fr_flow_vrf(vrf);
        req.set_fr_flow_nh_id(nh_id);

        vr_flow->fe_action = VR_FLOW_ACTION_HOLD;
        KSyncSockTypeMap::SetFlowEntry(&req, true);

        return true;
    }

    static void KFlowPurgeHold() {
        if (ksync_init_) {
            return;
        }
        FlowTableKSyncObject *ksync_obj = 
            Agent::GetInstance()->ksync()->flowtable_ksync_obj();

        for (size_t count = 0; count < ksync_obj->flow_table_entries_count();
             count++) {
            vr_flow_entry *vr_flow = KSyncSockTypeMap::GetFlowEntry(count);
            vr_flow->fe_action = VR_FLOW_ACTION_DROP;
            vr_flow_req req;
            req.set_fr_index(hash_id);
            KSyncSockTypeMap::SetFlowEntry(&req, false);
        }

        return;
    }

    static void FlowAdd(int hash_id, int vrf, const char *sip, const char *dip,
                        int proto, int sport, int dport, const char *nat_sip,
                        const char *nat_dip, int nat_vrf) {
        boost::shared_ptr<PktInfo> pkt_1(new PktInfo(Agent::GetInstance(),
                                                        100, 0, 0));
        PktFlowInfo flow_info_1(pkt_1, Agent::GetInstance()->pkt()->flow_table());
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
        //agent()->pkt()->flow_table()->Add(pkt, flow_info);
        client->WaitForIdle();
    }
    
    static int GetFlowPassCount(int total_flows, int age_time_usecs) {
        int age_time_millisec = age_time_usecs / 1000;
        int default_age_time_millisec = FlowStatsCollector::FlowAgeTime / 1000;
        int max_flows = (FlowStatsCollector::MaxFlows * age_time_millisec) / default_age_time_millisec;
        int flow_multiplier = (max_flows * FlowStatsCollector::FlowStatsMinInterval)/age_time_millisec;

        int flow_timer_interval = std::min((age_time_millisec * flow_multiplier)/total_flows, 1000);
        int flow_count_per_pass = std::max((flow_timer_interval * total_flows)/age_time_millisec, 100);

        int ret = total_flows / flow_count_per_pass;
        if (total_flows % flow_count_per_pass) {
            ret++;
        }
        return ret;
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

    void CheckSandeshResponse(Sandesh *sandesh, int flows) {
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
                "                    <rule-uuid>%s</rule-uuid>\n"
                "                </acl-rule>\n"
                "                <acl-rule>\n"
                "                    <match-condition>\n"
                "                        <src-address>\n"
                "                            <virtual-network> vn6 </virtual-network>\n"
                "                        </src-address>\n"
                "                        <protocol>any</protocol>\n"
                "                        <src-port>\n"
                "                            <start-port> 0 </start-port>\n"
                "                            <end-port> 60000 </end-port>\n"
                "                        </src-port>\n"
                "                        <dst-address>\n"
                "                            <virtual-network> vn6 </virtual-network>\n"
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
        flow5 = VmInterfaceGet(input4[0].intf_id);
        assert(flow5);
        flow6 = VmInterfaceGet(input4[1].intf_id);
        assert(flow6);
    }

    void FlowTeardown() {
        client->Reset();
        DeleteVmportEnv(input4, 2, true, 0);
        client->WaitForIdle(5);
        client->PortDelNotifyWait(2);
    }
protected:
    virtual void SetUp() {
        unsigned int vn_count = 0;
        EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
        hash_id = 1;
        client->Reset();
        CreateVmportEnv(input, 3, 1);
        client->WaitForIdle(5);
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
        EXPECT_EQ(3U, agent()->interface_config_table()->Size());

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
        vn_count++;
        EXPECT_TRUE(VmPortActive(input2, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input2, 0));
        EXPECT_EQ(8U, agent()->interface_table()->Size());
        EXPECT_EQ(4U, agent()->vm_table()->Size());
        EXPECT_EQ(vn_count, agent()->vn_table()->Size());
        EXPECT_EQ(4U, agent()->interface_config_table()->Size());
        EXPECT_EQ(2U, agent()->acl_table()->Size());

        flow3 = VmInterfaceGet(input2[0].intf_id);
        assert(flow3);

        /* Create interface flow4 in default-project:vn4 */
        client->Reset();
        CreateVmportFIpEnv(input3, 1);
        client->WaitForIdle(5);
        vn_count++;
        EXPECT_TRUE(VmPortActive(input3, 0));
        EXPECT_EQ(9U, agent()->interface_table()->Size());
        EXPECT_EQ(5U, agent()->vm_table()->Size());
        EXPECT_EQ(vn_count, agent()->vn_table()->Size());
        EXPECT_EQ(5U, agent()->interface_config_table()->Size());
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
        peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                              "xmpp channel");
        Ip4Address gw_ip = Ip4Address::from_string("11.1.1.254");
        //Add a gateway route pointing to pkt0
        VrfEntry *vrf = VrfGet("vrf5");
        static_cast<InetUnicastAgentRouteTable *>(
                vrf->GetInet4UnicastRouteTable())->AddHostRoute("vrf5",
                gw_ip, 32, "vn5");
        client->WaitForIdle();
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();
        VrfEntry *vrf = VrfGet("vrf5");
        Ip4Address gw_ip = Ip4Address::from_string("11.1.1.254");
        Agent::GetInstance()->fabric_inet4_unicast_table()->DeleteReq(
            Agent::GetInstance()->local_peer(), "vrf5", gw_ip, 32, NULL);
        client->WaitForIdle();
        DeleteVmportEnv(input, 3, true, 1);
        client->WaitForIdle(3);
        client->PortDelNotifyWait(3);
        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));
        EXPECT_FALSE(VmPortFind(input, 2));
        EXPECT_EQ(6U, agent()->interface_table()->Size());
        EXPECT_EQ(2U, agent()->interface_config_table()->Size());

        client->Reset();
        DeleteVmportEnv(input2, 1, true, 2);
        client->WaitForIdle(3);
        client->PortDelNotifyWait(1);
        EXPECT_EQ(5U, agent()->interface_table()->Size());
        EXPECT_EQ(1U, agent()->interface_config_table()->Size());
        EXPECT_FALSE(VmPortFind(input2, 0));

        client->Reset();
        DeleteVmportFIpEnv(input3, 1, true);
        client->WaitForIdle(3);
        client->PortDelNotifyWait(1);
        EXPECT_EQ(4U, agent()->interface_table()->Size());
        EXPECT_EQ(0U, agent()->interface_config_table()->Size());
        EXPECT_FALSE(VmPortFind(input3, 0));

        EXPECT_EQ(0U, agent()->vm_table()->Size());
        EXPECT_EQ(0U, agent()->vn_table()->Size());
        EXPECT_EQ(0U, agent()->acl_table()->Size());
        DeleteBgpPeer(peer_);
    }

    Agent *agent() {return agent_;}

private:
    static bool ksync_init_;
    BgpPeer *peer_;
    Agent *agent_;
};

bool FlowTest::ksync_init_;
//Ingress flow test (VMport to VMport - Same VN)
//Flow creation using IP and TCP packets
TEST_F(FlowTest, FlowAdd_1) {
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                       flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(Address::INET, vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                       flow1->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_TCP, 1000, 200,
                       "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(Address::INET, vm2_ip, vm1_ip, IPPROTO_TCP, 200, 1000,
                       "vrf5", flow1->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 4);
    EXPECT_EQ(4U, agent()->pkt()->flow_table()->Size());

    FetchAllFlowRecords *all_flow_records_sandesh = new FetchAllFlowRecords();
    Sandesh::set_response_callback(boost::bind(&FlowTest::CheckSandeshResponse,
                                               this, _1, 4));
    all_flow_records_sandesh->HandleRequest();
    client->WaitForIdle();
    all_flow_records_sandesh->Release();

    FetchFlowRecord *flow_record_sandesh = new FetchFlowRecord();
    flow_record_sandesh->set_nh(GetFlowKeyNH(input[0].intf_id));
    flow_record_sandesh->set_sip(vm1_ip);
    flow_record_sandesh->set_dip(vm2_ip);
    flow_record_sandesh->set_src_port(1000);
    flow_record_sandesh->set_dst_port(200);
    flow_record_sandesh->set_protocol(IPPROTO_TCP);
    flow_record_sandesh->HandleRequest();
    client->WaitForIdle();
    flow_record_sandesh->Release();

    //Verify the ingress and egress flow counts
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    agent()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(4U, in_count);
    EXPECT_EQ(4U, out_count);
}

TEST_F(FlowTest, Ip6_FlowAdd_1) {
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow

        {  TestFlowPkt(Address::INET6, ip6_vm1_ip, ip6_vm2_ip, IPPROTO_ICMPV6,
                       0, 0, "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(Address::INET6, ip6_vm2_ip, ip6_vm1_ip, IPPROTO_ICMPV6,
                       0, 0, "vrf5", flow1->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(Address::INET6, ip6_vm1_ip, ip6_vm2_ip, IPPROTO_TCP,
                       1000, 200, "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(Address::INET6, ip6_vm2_ip, ip6_vm1_ip, IPPROTO_TCP,
                       200, 1000, "vrf5", flow1->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 4);
    EXPECT_EQ(4U, agent()->pkt()->flow_table()->Size());

    FetchAllFlowRecords *all_flow_records_sandesh = new FetchAllFlowRecords();
    Sandesh::set_response_callback(boost::bind(&FlowTest::CheckSandeshResponse,
                                               this, _1, 4));
    all_flow_records_sandesh->HandleRequest();
    client->WaitForIdle();
    all_flow_records_sandesh->Release();

    FetchFlowRecord *flow_record_sandesh = new FetchFlowRecord();
    flow_record_sandesh->set_nh(GetFlowKeyNH(input[0].intf_id));
    flow_record_sandesh->set_sip(ip6_vm1_ip);
    flow_record_sandesh->set_dip(ip6_vm2_ip);
    flow_record_sandesh->set_src_port(1000);
    flow_record_sandesh->set_dst_port(200);
    flow_record_sandesh->set_protocol(IPPROTO_TCP);
    flow_record_sandesh->HandleRequest();
    client->WaitForIdle();
    flow_record_sandesh->Release();

    //Verify the ingress and egress flow counts
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    agent()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(4U, in_count);
    EXPECT_EQ(4U, out_count);
}

//Egress flow test (IP fabric to VMPort - Same VN)
//Flow creation using GRE packets
TEST_F(FlowTest, FlowAdd_2) {
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());

    //Create PHYSICAL interface to receive GRE packets on it.
    PhysicalInterfaceKey key(eth_itf);
    Interface *intf = static_cast<Interface *>
        (agent()->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);

    //Create remote VM route. This will be used to figure out destination VN for
    //flow
    CreateRemoteRoute("vrf5", remote_vm1_ip, remote_router_ip, 30, "vn5");
    client->WaitForIdle();

    CreateRemoteRoute("vrf5", remote_vm3_ip, remote_router_ip, 32, "vn5");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM to local VM
        {
            TestFlowPkt(Address::INET, remote_vm1_ip, vm1_ip, 1, 0, 0, "vrf5",
                    remote_router_ip, flow0->label()),
            { 
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        },
        //Send a ICMP reply from local to remote VM
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        },
        //Send a TCP flow from remote VM to local VM
        {
            TestFlowPkt(Address::INET, remote_vm3_ip, vm3_ip, IPPROTO_TCP, 1001, 1002,
                    "vrf5", remote_router_ip, flow2->label()),
            {
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        },
        //Send a TCP reply from local VM to remote VM
        {
            TestFlowPkt(Address::INET, vm3_ip, remote_vm3_ip, IPPROTO_TCP, 1002, 1001,
                    "vrf5", flow2->id()),
            {
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        }
    };

    CreateFlow(flow, 4);
    EXPECT_EQ(4U, agent()->pkt()->flow_table()->Size());

    //Verify ingress and egress flow count
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    agent()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //1. Remove remote VM routes
    DeleteRemoteRoute("vrf5", remote_vm1_ip);
    DeleteRemoteRoute("vrf5", remote_vm3_ip);
    client->WaitForIdle();
}

//Ingress flow test (VMport to VMport - Different VNs)
//Flow creation using IP and TCP packets
TEST_F(FlowTest, FlowAdd_3) {

    /* Add Local VM route of vrf3 to vrf5 */
    CreateLocalRoute("vrf5", vm4_ip, flow3, 19);
    /* Add Local VM route of vrf5 to vrf3 */
    CreateLocalRoute("vrf3", vm1_ip, flow0, 16);

    TestFlow flow[] = {
        //Send a ICMP request from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an ICMP reply from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, 1, 0, 0, "vrf3",
                    flow3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 200, 300, "vrf5",
                    flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 300, 200, "vrf3",
                    flow3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };

    CreateFlow(flow, 4);
    client->WaitForIdle();
    //Verify ingress and egress flow count of VN "vn5"
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    agent()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //Verify ingress and egress flow count of VN "vn3"
    fe = flow[1].pkt_.FlowFetch();
    vn = fe->data().vn_entry.get();
    agent()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //1. Remove remote VM routes
    DeleteRoute("vrf5", vm4_ip);
    DeleteRoute("vrf3", vm1_ip);
    client->WaitForIdle();
}

//Egress flow test (IP fabric to VMport - Different VNs)
//Flow creation using GRE packets
TEST_F(FlowTest, FlowAdd_4) {
    /* Add remote VN route to vrf5 */
    CreateRemoteRoute("vrf5", remote_vm4_ip, remote_router_ip, 8, "vn3");
    Ip4Address rid1 = agent()->router_id();
    std::string router_ip_str = rid1.to_string();

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, remote_vm4_ip, vm1_ip, 1, 0, 0, "vrf5",
                    remote_router_ip, 16),
            { 
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a ICMP reply from local VM in vn5 to remote VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm4_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send a TCP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, remote_vm4_ip, vm1_ip, IPPROTO_TCP, 1006, 1007,
                    "vrf5", remote_router_ip, 16),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP reply from local VM in vn5 to remote VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm4_ip, IPPROTO_TCP, 1007, 1006,
                    "vrf5", flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        }
    };

    CreateFlow(flow, 4); 
    client->WaitForIdle();
    //Verify ingress and egress flow count of VN "vn5"
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    agent()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //Verify ingress and egress flow count of VN "vn3"
    fe = flow[1].pkt_.FlowFetch();
    vn = fe->data().vn_entry.get();
    agent()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //1. Remove remote VM routes
    DeleteRemoteRoute("vrf5", remote_vm4_ip);
    client->WaitForIdle();
}

//Duplicate Ingress flow test (VMport to VMport - Same VN)
//Flow creation using IP
TEST_F(FlowTest, FlowAdd_5) {
    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify ingress and egress flow count of VN "vn5"
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    agent()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //Send duplicate flow creation request
    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify ingress and egress flow count for VN "vn5" does not change
    fe = flow[0].pkt_.FlowFetch();
    vn = fe->data().vn_entry.get();
    agent()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

}

//Duplicate Ingress flow test for flow having reverse flow (VMport to VMport - Same VN)
//Flow creation using TCP packets
TEST_F(FlowTest, FlowAdd_6) {
    TestFlow fwd_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_TCP, 1000, 200, "vrf5",
                    flow0->id()),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    TestFlow rev_flow[] = {
        {
            TestFlowPkt(Address::INET, vm2_ip, vm1_ip, IPPROTO_TCP, 200, 1000, "vrf5",
                    flow1->id()),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        }
    };


    CreateFlow(fwd_flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify ingress and egress flow count of VN "vn5"
    uint32_t in_count, out_count;
    const FlowEntry *fe = fwd_flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    agent()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //Send request for reverse flow
    CreateFlow(rev_flow, 1);
    //Send request for reverse flow again
    CreateFlow(rev_flow, 1);
    //Send request for forward flow again 
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify ingress and egress flow count for VN "vn5" does not change
    fe = fwd_flow[0].pkt_.FlowFetch();
    vn = fe->data().vn_entry.get();
    agent()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);
}

TEST_F(FlowTest, FlowIndexChange) {
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            { }
        }
    };
    TestFlow flow_new[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 3),
            { }
        }
    };
    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    vr_flow_entry *vr_flow = KSyncSockTypeMap::GetFlowEntry(1);
    EXPECT_TRUE(((vr_flow->fe_flags & VR_FLOW_FLAG_ACTIVE) == VR_FLOW_FLAG_ACTIVE));

    /* Change Index for flow. */
    CreateFlow(flow_new, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    /* verify the entry on hash id 1 being deleted */
    EXPECT_TRUE(((vr_flow->fe_flags & VR_FLOW_FLAG_ACTIVE) != VR_FLOW_FLAG_ACTIVE));

    DeleteFlow(flow_new, 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

// Validate flows to pkt 0 interface
TEST_F(FlowTest, Flow_On_PktIntf) {
    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm1_ip, "11.1.1.254", 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new VerifyVrf("vrf5", "vrf5")
            }
        }
    };

    CreateFlow(flow, 1);
}


// Validate short flows
TEST_F(FlowTest, ShortFlow_1) {
    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm1_ip, "115.115.115.115", 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new ShortFlow()
            }
        }
    };

    CreateFlow(flow, 1);
    int nh_id = InterfaceTable::GetInstance()->FindInterface(flow0->id())->flow_key_nh()->id();
    FlowEntry *fe = FlowGet(1, vm1_ip, "115.115.115.115", 1,
                            0, 0, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_NO_DST_ROUTE);
}

TEST_F(FlowTest, FlowAge_1) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = 
        agent()->uve()->flow_stats_collector()->flow_age_time_intvl();
    //Set the flow age time to 100 microsecond
    agent()->uve()->flow_stats_collector()->UpdateFlowAgeTime(
            tmp_age_time);

    //Create bidirectional flow
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        },
        {
            TestFlowPkt(Address::INET, vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                    flow1->id(), 2),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    client->WaitForIdle();

    // Sleep for age-time
    usleep(tmp_age_time + 10);

    //Trigger flow-aging and make sure they are not removed because 
    //of difference in stats between oper flow and Kernel flow
    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Update reverse-flow stats to postpone aging of forward-flow
    KSyncSockTypeMap::IncrFlowStats(2, 1, 30);

    //Trigger flow-aging
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    //Verify that forward-flow is not removed even though it is eligible to be removed
    //because reverse-flow is not aged
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    // Sleep for age-time
    usleep(tmp_age_time + 10);

    //Trigger to Age the flow
    client->EnqueueFlowAge();
    client->WaitForIdle();

    //Verify that both flows get removed after reverse-flow ages
    WAIT_FOR(100, 1, (0U == agent()->pkt()->flow_table()->Size()));

    //Restore flow aging time
    agent()->uve()->
        flow_stats_collector()->UpdateFlowAgeTime(bkp_age_time);
}

// Aging with more than 2 entries
TEST_F(FlowTest, FlowAge_3) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = 
        agent()->uve()->flow_stats_collector()->flow_age_time_intvl();
    //Set the flow age time to 100 microsecond
    agent()->uve()->
        flow_stats_collector()->UpdateFlowAgeTime(tmp_age_time);

    //Create bidirectional flow
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            { }
        },
        {
            TestFlowPkt(Address::INET, vm1_ip, vm3_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 2),
            { }
        },
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 3),
            { }
        },
        {
            TestFlowPkt(Address::INET, vm2_ip, vm3_ip, 1, 0, 0, "vrf5",
                    flow1->id(), 4),
            { }
        },
    };

    CreateFlow(flow, 4);
    EXPECT_EQ(8U, agent()->pkt()->flow_table()->Size());

    // Flow entries are created with #pkts = 1. 
    // Do first sleep for aging to work correctly below
    client->EnqueueFlowAge();
    client->WaitForIdle();

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (0U == agent()->pkt()->flow_table()->Size()));
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());

    // Delete of 2 linked flows
    CreateFlow(flow, 2);
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (0U == agent()->pkt()->flow_table()->Size()));
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());

    // Delete 2 out of 4 linked entries
    CreateFlow(flow, 2);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (2U == agent()->pkt()->flow_table()->Size()));
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());
    EXPECT_TRUE(FlowGet(1, vm1_ip, vm2_ip, 1, 0, 0, false, -1, -1,
                        GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowGet(1, vm2_ip, vm1_ip, 1, 0, 0, false, -1, -1,
                        GetFlowKeyNH(input[1].intf_id)));

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 1, (0U == agent()->pkt()->flow_table()->Size()));
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());

    //Restore flow aging time
    agent()->uve()->
        flow_stats_collector()->UpdateFlowAgeTime(bkp_age_time);
}

TEST_F(FlowTest, ScaleFlowAge_1) {
    int tmp_age_time = 200 * 1000;
    int bkp_age_time = 
        agent()->uve()->flow_stats_collector()->flow_age_time_intvl();
    int total_flows = 200;

    for (int i = 0; i < total_flows; i++) {
        Ip4Address dip(0x1010101 + i);
        //Add route for all of them
        CreateRemoteRoute("vrf5", dip.to_string().c_str(), remote_router_ip, 
                10, "vn5");
        TestFlow flow[]=  {
            {
                TestFlowPkt(Address::INET, vm1_ip, dip.to_string(), 1, 0, 0, "vrf5", 
                        flow0->id(), i),
                { }
            },
            {
                TestFlowPkt(Address::INET, dip.to_string(), vm1_ip, 1, 0, 0, "vrf5",
                        flow0->id(), i + 100),
                { }
            }
        };
        CreateFlow(flow, 2);
    }
    EXPECT_EQ((total_flows * 2), 
            agent()->pkt()->flow_table()->Size());
    //Set the flow age time to 200 milliseconds
    agent()->uve()->
        flow_stats_collector()->UpdateFlowAgeTime(tmp_age_time);

    agent()->uve()->flow_stats_collector()->run_counter_ = 0;

    int passes = GetFlowPassCount((total_flows * 2), tmp_age_time);
    client->EnqueueFlowAge();
    client->WaitForIdle(5);
    WAIT_FOR(5000, 1000, (agent()->uve()->flow_stats_collector()->run_counter_ >= passes));
    usleep(tmp_age_time + 1000);
        WAIT_FOR(5000, 1000, (agent()->uve()->flow_stats_collector()->run_counter_ >= (passes * 2)));
        client->WaitForIdle(2);

    WAIT_FOR(5000, 500, (0U == agent()->pkt()->flow_table()->Size()));
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());

    //Restore flow aging time
    agent()->uve()->
        flow_stats_collector()->UpdateFlowAgeTime(bkp_age_time);
}

TEST_F(FlowTest, Nat_FlowAge_1) {
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = 
        agent()->uve()->flow_stats_collector()->flow_age_time_intvl();
    //Set the flow age time to 100 microsecond
    agent()->uve()->
        flow_stats_collector()->UpdateFlowAgeTime(tmp_age_time);

    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            { 
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0)
            }
        }
    };

    CreateFlow(flow, 1);
    // Update stats so that flow is not aged
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);

    // Sleep for age-time
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm5_ip, 1, 0, 0, 
                        false, -1, -1, GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(), vm5_ip,
                        vm1_fip, 1, 0, 0, false, -1, -1,
                        GetFlowKeyNH(input3[0].intf_id)));

    // Sleep for age-time
    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, (agent()->pkt()->flow_table()->Size() == 0U));

    //Restore flow aging time
    agent()->uve()->
        flow_stats_collector()->UpdateFlowAgeTime(bkp_age_time);
}

TEST_F(FlowTest, NonNatFlowAdd_1) {
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {}
        },
        {
            TestFlowPkt(Address::INET, vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                    flow1->id()),
            {}
        }   
    };

    CreateFlow(flow, 1);

    // Add duplicate flow
    CreateFlow(flow, 1);

    // Add reverse flow
    CreateFlow(flow + 1, 1);

    // Delete forward flow. It should delete both flows
    DeleteFlow(flow, 1);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0,
                         GetFlowKeyNH(input[1].intf_id)));

    // Add forward and reverse flow
    CreateFlow(flow, 2);
    // Delete reverse flow. Should delete both flows
    DeleteFlow(flow + 1, 1);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0,
                         GetFlowKeyNH(input[1].intf_id)));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NonNatDupFlowAdd_1) {
    TestFlow flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                                flow0->id()),
            {}
        },
        {
             TestFlowPkt(Address::INET, vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                                flow1->id()),
            {}
        }   
    };

    // Add forward and reverse flow
    CreateFlow(flow, 2); 

    // Add duplicate forward and reverse flow
    CreateFlow(flow, 2);

    // Delete forward flow. It should delete both flows
    DeleteFlow(flow, 1);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NonNatAddOldNat_1) {
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    TestFlow non_nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            { }
        }
    };

    // Add NAT flow
    CreateFlow(nat_flow, 1);

    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1"); 
    client->WaitForIdle();
        
    // Add Non-NAT forward flow
    CreateFlow(non_nat_flow, 1);
    //Make sure NAT reverse flow is also deleted
    EXPECT_TRUE(FlowFail(VrfGet("default-project:vn4:vn4")->vrf_id(),
                          vm5_ip, vm1_fip, 1,
                          0, 0, GetFlowKeyNH(input[0].intf_id)));

    DeleteFlow(non_nat_flow, 1); 
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm5_ip, vm1_ip, 1,
                         0, 0, GetFlowKeyNH(input[0].intf_id)));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (agent()->pkt()->flow_table()->Size() == 0U));
}


TEST_F(FlowTest, NonNatAddOldNat_2) {
#if 0
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, 
                 "default-project:vn4:vn4", flow4->id()),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0) 
            }
        }
    };

    TestFlow non_nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, "vn4:vn4", 
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, 
                 "default-project:vn4:vn4", flow4->id()),
            {
                new VerifyVn(unknown_vn_, unknown_vn_)
            }
        }
    };

    // Add NAT flow
    CreateFlow(nat_flow, 1);

    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1"); 
    client->WaitForIdle();
    //Make sure NAT reverse flow is deleted
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm1_ip, vm5_ip, 1, 
                         0, 0));
    EXPECT_TRUE(FlowFail(VrfGet("default-project:vn4:vn4")->vrf_id(), vm5_ip,
                         vm1_fip, 1, 0, 0));

    // Add Non-NAT reverse flow
    CreateFlow(non_nat_flow, 1);
    DeleteFlow(non_nat_flow, 1); 
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm1_ip, vm5_ip, 1, 
                         0, 0));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
#endif
}

TEST_F(FlowTest, NonNatAddOldNat_3) {
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5", 
                    flow0->id()),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    TestFlow non_nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new VerifyVn("vn5", unknown_vn_)
            }
        }
    };

    // Add NAT flow
    CreateFlow(nat_flow, 1);

    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1"); 
    client->WaitForIdle();
    //Make sure NAT reverse flow is also deleted
    EXPECT_TRUE(FlowFail(VrfGet("default-project:vn4:vn4")->vrf_id(), vm5_ip,
                         vm1_fip, 1, 0, 0, GetFlowKeyNH(input3[0].intf_id)));

    // Add Non-NAT forward flow
    CreateFlow(non_nat_flow, 1);
    DeleteFlow(non_nat_flow, 1); 
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm5_ip, vm1_ip, 1, 
                0, 0, GetFlowKeyNH(input3[0].intf_id)));
    //Make sure NAT reverse flow is not present
    EXPECT_TRUE(FlowFail(VrfGet("default-project:vn4:vn4")->vrf_id(),
                         vm5_ip, vm1_fip, 1, 0, 0,
                         GetFlowKeyNH(input3[0].intf_id)));

    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (agent()->pkt()->flow_table()->Size() == 0U));
}

TEST_F(FlowTest, NatFlowAdd_1) {
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);
    //Add duplicate flow
    CreateFlow(nat_flow, 1);
    
    //Send a reverse nat flow packet
    TestFlow nat_rev_flow[] = {
        {
            TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, "default-project:vn4:vn4",
                    flow4->id(), 2),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0)
            }
        }
    };
    CreateFlow(nat_rev_flow, 1);

    //Delete a forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    EXPECT_TRUE(FlowFail(VrfGet("default-project:vn4:vn4")->vrf_id(), vm5_ip,
                         vm1_fip, 1, 0, 0, GetFlowKeyNH(input3[0].intf_id)));

    CreateFlow(nat_flow, 1); 
    DeleteFlow(nat_rev_flow, 1);
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm1_ip, vm5_ip, 1, 0, 0,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatFlowAdd_2) {
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0)
            }
        }
    };

    CreateFlow(nat_flow, 1);
    //Add duplicate flow
    CreateFlow(nat_flow, 1);
    //Send a reverse nat flow packet
    TestFlow nat_rev_flow[] = {
        {
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, 
                 "default-project:vn4:vn4", flow4->id(), 2),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0)
            }
        }
    };
    CreateFlow(nat_rev_flow, 1);

    //Delete a forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    EXPECT_TRUE(FlowFail(VrfGet("vrf3")->vrf_id(), vm5_ip, vm1_fip, 1, 0, 0,
                         GetFlowKeyNH(input3[0].intf_id)));
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, NatAddOldNonNat_1) {
    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1"); 
    client->WaitForIdle();

    TestFlow non_nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyVn("vn5", unknown_vn_) 
            }
        }
    };
    CreateFlow(non_nat_flow, 1);

    //Associate a floating IP with flow0
    AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
    client->WaitForIdle();
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };
    CreateFlow(nat_flow, 1);
    DeleteFlow(nat_flow, 1);

    EXPECT_TRUE(FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(), vm5_ip,
                        vm1_fip, 1, 0, 0,
                        GetFlowKeyNH(input3[0].intf_id)) == NULL);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (agent()->pkt()->flow_table()->Size() == 0U));
}

TEST_F(FlowTest, NatAddOldNonNat_2) {
    //Disassociate fip
    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
    client->WaitForIdle();

    TestFlow non_nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyVn("vn5", unknown_vn_) 
            }
        }
    };
    CreateFlow(non_nat_flow, 1);

    //Associate a floating IP with flow0
    AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
    client->WaitForIdle();
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, 
                    "default-project:vn4:vn4", flow4->id(), 1),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0) 
            }
        }
    };
    CreateFlow(nat_flow, 1);
    DeleteFlow(nat_flow, 1);

    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, 
                vm5_ip, 1, 0, 0, GetFlowKeyNH(input3[0].intf_id)) == NULL);

    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (agent()->pkt()->flow_table()->Size() == 0U));
}

TEST_F(FlowTest, NatAddOldNat_1) {
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);

    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");  
    AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip2"); 
    client->WaitForIdle();

    TestFlow new_nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip2, 1, 0, 0) 
            }
        }
    };
    CreateFlow(new_nat_flow, 1);
    DeleteFlow(new_nat_flow, 1);
 
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (agent()->pkt()->flow_table()->Size() == 0U));
}

TEST_F(FlowTest, NatAddOldNat_2) {
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);

    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");  
    AddLink("virtual-machine-interface", "flow1", "floating-ip", "fip1"); 
    client->WaitForIdle();

    TestFlow new_nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm2_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow1->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0)
            }
        }
    };
    CreateFlow(new_nat_flow, 1);
    DeleteFlow(new_nat_flow, 1);

    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (agent()->pkt()->flow_table()->Size() == 0U));
}

TEST_F(FlowTest, NatAddOldNat_3) {
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);

    DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");  
    AddLink("virtual-machine-interface", "flow1", "floating-ip", "fip1"); 
    client->WaitForIdle();

    TestFlow new_nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, 
                    "default-project:vn4:vn4", flow4->id(), 1),
            {
                new VerifyNat(vm2_ip, vm5_ip, 1, 0, 0)
            }
        }
    };
    CreateFlow(new_nat_flow, 1);
    DeleteFlow(new_nat_flow, 1);

    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (agent()->pkt()->flow_table()->Size() == 0U));
}

//Create same Nat flow with different flow handles
TEST_F(FlowTest, TwoNatFlow) {
    TestFlow nat_flow[] = {
        {
             TestFlowPkt(Address::INET, vm1_ip, vm5_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm5_ip, vm1_fip, 1, 0, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);
    TestFlow nat_rev_flow[] = {
        {
             TestFlowPkt(Address::INET, vm5_ip, vm1_fip, 1, 0, 0, 
                 "default-project:vn4:vn4", flow4->id(), 1),
            {
                new VerifyNat(vm1_ip, vm5_ip, 1, 0, 0)
            }
        }
    };
    CreateFlow(nat_rev_flow, 1);

    DeleteFlow(nat_flow, 1);    
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, FlowAudit) {
    KFlowPurgeHold();
    EXPECT_TRUE(KFlowHoldAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, 0));
    EXPECT_TRUE(KFlowHoldAdd(2, 1, "2.2.2.2", "3.3.3.3", 1, 0, 0, 0));
    RunFlowAudit();
    EXPECT_TRUE(FlowTableWait(2));
    FlowEntry *fe = FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, 0);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_AUDIT_ENTRY);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (agent()->pkt()->flow_table()->Size() == 0U));
    KFlowPurgeHold();

    string vrf_name =
        Agent::GetInstance()->vrf_table()->FindVrfFromId(1)->GetName();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "2.2.2.2", 1, 0, 0, vrf_name,
                    flow0->id(), 1),
            {
            }
        }
    };

    CreateFlow(flow, 1);

    EXPECT_TRUE(FlowTableWait(2));
    EXPECT_TRUE(KFlowHoldAdd(10, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, 0));
    RunFlowAudit();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    usleep(500);
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = 
        agent()->uve()->flow_stats_collector()->flow_age_time_intvl();
    //Set the flow age time to 10 microsecond
    agent()->uve()->
        flow_stats_collector()->UpdateFlowAgeTime(tmp_age_time);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (agent()->pkt()->flow_table()->Size() == 0U));
    agent()->uve()->
        flow_stats_collector()->UpdateFlowAgeTime(bkp_age_time);
    KFlowPurgeHold();
}

//Test flow deletion on ACL deletion
TEST_F(FlowTest, AclDelete) {
    AddAcl("acl1", 1, "vn5" , "vn5", "pass");
    client->WaitForIdle();
    uint32_t sport = 30;
    for (uint32_t i = 0; i < 1; i++) {
        sport++;
        TestFlow flow[] = {
            {
                TestFlowPkt(Address::INET, vm2_ip, vm1_ip, IPPROTO_TCP, sport, 40, "vrf5",
                            flow1->id(), 1),
                {
                    new VerifyVn("vn5", "vn5")
                }
            }
        };
        CreateFlow(flow, 1);
    }

    //Delete the acl
    DelOperDBAcl(1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

//Src port and dest port should be ignored for non TCP and UDP flows
TEST_F(FlowTest, ICMPPortIgnoreTest) {
    AddAcl("acl1", 1, "vn5" , "vn5", "pass");
    client->WaitForIdle();
    for (uint32_t i = 0; i < 1; i++) {
        TestFlow flow[] = {
            {
                TestFlowPkt(Address::INET, vm2_ip, vm1_ip, IPPROTO_ICMP, 0, 0, "vrf5",
                            flow1->id(), 1),
                {
                    new VerifyVn("vn5", "vn5"),
                    new VerifyFlowAction(TrafficAction::PASS)
                }
            }
        };
        CreateFlow(flow, 1);
    }

    //Delete the acl
    DelOperDBAcl(1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, FlowOnDeletedInterface) {
    struct PortInfo input[] = {
        {"flow5", 11, "11.1.1.3", "00:00:00:01:01:01", 5, 6},
    };
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    int nh_id = GetFlowKeyNH(input[0].intf_id);

    InterfaceRef intf(VmInterfaceGet(input[0].intf_id));
    //Delete the interface with reference help
    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();

    TxTcpPacket(intf->id(), "11.1.1.3", vm1_ip, 30, 40, false, 1,
               VrfGet("vrf5")->vrf_id());
    client->WaitForIdle();

    //Flow find should fail as interface is delete marked, and packet get dropped
    // in packet parsing
    FlowEntry *fe = FlowGet(VrfGet("vrf5")->vrf_id(), "11.1.1.3", vm1_ip,
                            IPPROTO_TCP, 30, 40, nh_id);
    EXPECT_TRUE(fe == NULL);
}

TEST_F(FlowTest, FlowOnDeletedVrf) {
    struct PortInfo input[] = {
        {"flow5", 11, "11.1.1.3", "00:00:00:01:01:01", 5, 6},
    };
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    int nh_id = GetFlowKeyNH(input[0].intf_id);

    uint32_t vrf_id = VrfGet("vrf5")->vrf_id();
    InterfaceRef intf(VmInterfaceGet(input[0].intf_id));
    //Delete the VRF
    DelVrf("vrf5");
    client->WaitForIdle();

    TxTcpPacket(intf->id(), "11.1.1.3", vm1_ip, 30, 40, false, 1, vrf_id);
    client->WaitForIdle();

    //Flow find should fail as interface is delete marked
    FlowEntry *fe = FlowGet(vrf_id, "11.1.1.3", vm1_ip,
                            IPPROTO_TCP, 30, 40, 0);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_UNAVIALABLE_INTERFACE);
    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();
}

TEST_F(FlowTest, Flow_with_encap_change) {
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());

    //Create PHYSICAL interface to receive GRE packets on it.
    PhysicalInterfaceKey key(eth_itf);
    Interface *intf = static_cast<Interface *>
        (agent()->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);
    CreateRemoteRoute("vrf5", remote_vm1_ip, remote_router_ip, 30, "vn5");
    client->WaitForIdle();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {}
        },
        {
            TestFlowPkt(Address::INET, remote_vm1_ip, vm1_ip, 1, 0, 0, "vrf5",
                    remote_router_ip, 16),
            {}
        }   
    };

    CreateFlow(flow, 1);
    // Add reverse flow
    CreateFlow(flow + 1, 1);

    FlowEntry *fe = 
        FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                GetFlowKeyNH(input[0].intf_id));
    const NextHop *nh = (fe->data().nh_state_.get())->nh();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    const TunnelNH *tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    AddEncapList("MPLSoUDP", "MPLSoGRE", "VXLAN");
    client->WaitForIdle();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->data().nh_state_.get() != NULL);
    nh = (fe->data().nh_state_.get())->nh();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_UDP);

    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    client->WaitForIdle();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->data().nh_state_.get() != NULL);
    nh = (fe->data().nh_state_.get())->nh();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    AddEncapList("VXLAN", "MPLSoUDP", "MPLSoGRE");
    client->WaitForIdle();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->data().nh_state_.get() != NULL);
    nh = (fe->data().nh_state_.get())->nh();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_UDP);

    DelEncapList();
    client->WaitForIdle();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->data().nh_state_.get() != NULL);
    nh = (fe->data().nh_state_.get())->nh();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    DeleteFlow(flow, 1);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowTableWait(0));
    DeleteRemoteRoute("vrf5", remote_vm1_ip);
    client->WaitForIdle();
}

TEST_F(FlowTest, Flow_return_error) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());

    //Create PHYSICAL interface to receive GRE packets on it.
    PhysicalInterfaceKey key(eth_itf);
    Interface *intf = static_cast<Interface *>
        (agent()->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);
    CreateRemoteRoute("vrf5", remote_vm1_ip, remote_router_ip, 30, "vn5");
    client->WaitForIdle();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {}
        }
    };

    flow[0].pkt_.set_allow_wait_for_idle(false);
    sock->SetBlockMsgProcessing(true);
    /* Failure to allocate reverse flow index, convert to short flow and age */
    sock->SetKSyncError(KSyncSockTypeMap::KSYNC_FLOW_ENTRY_TYPE, -ENOSPC);
    CreateFlow(flow, 1);

    uint32_t vrf_id = VrfGet("vrf5")->vrf_id();
    FlowEntry *fe = FlowGet(vrf_id, vm1_ip, remote_vm1_ip, 1, 0, 0,
                            GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) != true);

    sock->SetBlockMsgProcessing(false);
    flow[0].pkt_.set_allow_wait_for_idle(true);
    client->WaitForIdle();
    if (fe != NULL) {
        WAIT_FOR(1000, 500, (fe->is_flags_set(FlowEntry::ShortFlow) == true));
        EXPECT_TRUE(fe->short_flow_reason() == FlowEntry::SHORT_FAILED_VROUTER_INSTALL);
    }

    client->EnqueueFlowAge();
    client->WaitForIdle();

    EXPECT_TRUE(FlowTableWait(0));

    flow[0].pkt_.set_allow_wait_for_idle(false);
    sock->SetBlockMsgProcessing(true);
    /* EBADF failure to write an entry, covert to short flow and age */
    sock->SetKSyncError(KSyncSockTypeMap::KSYNC_FLOW_ENTRY_TYPE, -EBADF);
    CreateFlow(flow, 1);

    fe = FlowGet(vrf_id, vm1_ip, remote_vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) != true);
    sock->SetBlockMsgProcessing(false);
    flow[0].pkt_.set_allow_wait_for_idle(true);
    client->WaitForIdle();
    if (fe != NULL) {
        WAIT_FOR(1000, 500, (fe->is_flags_set(FlowEntry::ShortFlow) == true));
        EXPECT_TRUE(fe->short_flow_reason() == FlowEntry::SHORT_FAILED_VROUTER_INSTALL);
    }

    client->EnqueueFlowAge();
    client->WaitForIdle();

    EXPECT_TRUE(FlowTableWait(0));

    DeleteRemoteRoute("vrf5", remote_vm1_ip);
    client->WaitForIdle();
    sock->SetKSyncError(KSyncSockTypeMap::KSYNC_FLOW_ENTRY_TYPE, 0);
}

//Test for subnet broadcast flow
TEST_F(FlowTest, Subnet_broadcast_Flow) {
    IpamInfo ipam_info[] = {
        {"11.1.1.0", 24, "11.1.1.200", true},
    };
    AddIPAM("vn5", ipam_info, 1);
    client->WaitForIdle();

    //Get the route
    if (!RouteFind("vrf5", "11.1.1.255", 32)) {
        return;
    }

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, vm1_ip, "11.1.1.255", 1, 0, 0, "vrf5",
                       flow0->id()),
        {}
        },
        {  TestFlowPkt(Address::INET, vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                       flow1->id()),
        {}
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(4U, agent()->pkt()->flow_table()->Size());

    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev_fe = fe->reverse_flow_entry();
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::Multicast));
    EXPECT_TRUE(rev_fe->is_flags_set(FlowEntry::Multicast));
    EXPECT_TRUE(fe->data().match_p.action_info.action == 
                (1 << TrafficAction::PASS));
    EXPECT_TRUE(rev_fe->data().match_p.action_info.action == 
                (1 << TrafficAction::PASS));
    //fe->data.match_p.flow_action.action

    //Verify the ingress and egress flow counts
    uint32_t in_count, out_count;
    const VnEntry *vn = fe->data().vn_entry.get();
    agent()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(3U, in_count);
    EXPECT_EQ(3U, out_count);

    DelIPAM("vn5");
    client->WaitForIdle();
}

TEST_F(FlowTest, Flow_ksync_nh_state_find_failure) {
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());

    CreateRemoteRoute("vrf5", remote_vm1_ip, remote_router_ip, 30, "vn5");
    client->WaitForIdle();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1001),
            {}
        }
    };

    DBTableBase *table = Agent::GetInstance()->nexthop_table();
    NHKSyncObject *nh_object = Agent::GetInstance()->ksync()->nh_ksync_obj();
    DBTableBase::ListenerId nh_listener =
        table->Register(boost::bind(&NHNotify, _1, _2));

    vr_flow_entry *vr_flow = KSyncSockTypeMap::GetFlowEntry(1001);
    EXPECT_TRUE((vr_flow->fe_flags & VR_FLOW_FLAG_ACTIVE) == 0);

    nh_object->set_test_id(nh_listener);
    CreateFlow(flow, 1);

    FlowEntry *fe = 
        FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                GetFlowKeyNH(input[0].intf_id));


    EXPECT_TRUE((vr_flow->fe_flags & VR_FLOW_FLAG_ACTIVE) != 0);
    DeleteFlow(flow, 1);
    EXPECT_TRUE(FlowTableWait(0));
    nh_object->set_test_id(-1);
    table->Unregister(nh_listener);
    DeleteRemoteRoute("vrf5", remote_vm1_ip);
    client->WaitForIdle();
}

TEST_F(FlowTest, Flow_entry_reuse) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());

    CreateRemoteRoute("vrf5", remote_vm1_ip, remote_router_ip, 30, "vn5");
    client->WaitForIdle();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1001),
            {}
        }
    };
    TestFlow flow1[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1002),
            {}
        }
    };  
    CreateFlow(flow, 1);
    EXPECT_TRUE(FlowTableWait(2));
    FlowEntry *fe = 
        FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, remote_vm1_ip, 1, 0, 0,
                GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->flow_handle() == 1001);

    flow[0].pkt_.set_allow_wait_for_idle(false);
    flow1[0].pkt_.set_allow_wait_for_idle(false);
    sock->SetBlockMsgProcessing(true);
    DeleteFlow(flow, 1);
    EXPECT_TRUE(FlowTableWait(2));
    CreateFlow(flow1, 1);
    sock->SetBlockMsgProcessing(false);
    flow[0].pkt_.set_allow_wait_for_idle(true);
    flow1[0].pkt_.set_allow_wait_for_idle(true);
    WAIT_FOR(1000, 1000, (fe->deleted() == false));
    client->WaitForIdle();
    FlowTableKSyncEntry *fe_ksync = 
        Agent::GetInstance()->ksync()->flowtable_ksync_obj()->Find(fe);
    WAIT_FOR(1000, 1000, (fe_ksync->GetState() == KSyncEntry::IN_SYNC));

    EXPECT_TRUE(fe->flow_handle() == 1002);
    DeleteFlow(flow1, 1);
    EXPECT_TRUE(FlowTableWait(0));
    DeleteRemoteRoute("vrf5", remote_vm1_ip);
    client->WaitForIdle();
}

// Linklocal flow add & delete
TEST_F(FlowTest, LinkLocalFlow_1) {
    Agent::GetInstance()->set_router_id(Ip4Address::from_string(vhost_ip_addr));
    std::string fabric_ip("1.2.3.4");
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back("1.2.3.4");
    TestLinkLocalService service = { "test_service", linklocal_ip, linklocal_port,
                                     "", fabric_ip_list, fabric_port };
    AddLinkLocalConfig(&service, 1);
    client->WaitForIdle();

    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3000, linklocal_port, "vrf5",
                        flow0->id(), 1),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());
    const FlowEntry *fe = nat_flow[0].pkt_.FlowFetch();
    uint16_t linklocal_src_port = fe->linklocal_src_port();

    FetchAllFlowRecords *all_flow_records_sandesh = new FetchAllFlowRecords();
    Sandesh::set_response_callback(boost::bind(&FlowTest::CheckSandeshResponse,
                                               this, _1, 2));
    all_flow_records_sandesh->HandleRequest();
    client->WaitForIdle();
    all_flow_records_sandesh->Release();

    EXPECT_TRUE(FlowGet(0, fabric_ip.c_str(), vhost_ip_addr, IPPROTO_TCP,
                        fabric_port, linklocal_src_port,
                        vhost->flow_key_nh()->id()));
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, linklocal_ip,
                        IPPROTO_TCP, 3000, linklocal_port,
                        GetFlowKeyNH(input[0].intf_id)));
    
    // Check that a reverse pkt will not create a new flow
    TestFlow reverse_flow[] = {
        {
            TestFlowPkt(Address::INET, fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port, linklocal_src_port, "vrf5",
                        flow0->id(), 1),
            {
                new VerifyNat(vm1_ip, linklocal_ip, IPPROTO_TCP, 3000, linklocal_port) 
            }
        }
    };
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());
    EXPECT_TRUE(FlowGet(0, fabric_ip.c_str(), vhost_ip_addr, IPPROTO_TCP,
                        fabric_port, linklocal_src_port,
                        vhost->flow_key_nh()->id()));
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, linklocal_ip,
                        IPPROTO_TCP, 3000, linklocal_port,
                        GetFlowKeyNH(input[0].intf_id)));

    //Delete forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowFail(0, fabric_ip.c_str(), vhost_ip_addr, IPPROTO_TCP,
                         fabric_port, linklocal_src_port,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm1_ip, linklocal_ip,
                         IPPROTO_TCP, 3000, linklocal_port,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowTableWait(0));

    DelLinkLocalConfig();
    client->WaitForIdle();
}

TEST_F(FlowTest, LinkLocalFlow_Fail1) {
    Agent::GetInstance()->set_router_id(Ip4Address::from_string(vhost_ip_addr));
    std::string fabric_ip("1.2.3.4");
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back("1.2.3.4");
    TestLinkLocalService services[3] = {
        { "test_service1", linklocal_ip, linklocal_port, "", fabric_ip_list, fabric_port },
        { "test_service2", linklocal_ip, linklocal_port+1, "", fabric_ip_list, fabric_port+1 },
        { "test_service3", linklocal_ip, linklocal_port+2, "", fabric_ip_list, fabric_port+2 }
    };
    AddLinkLocalConfig(services, 3);
    client->WaitForIdle();

    // Only two link local flows are allowed simultaneously from a VM;
    // try creating 3 and check that 2 flows are created along with reverse flows,
    // while one flow along with its reverse is a short flow
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3000, linklocal_port, "vrf5",
                        flow0->id(), 1),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port, 0) 
            }
        },
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3001, linklocal_port+1, "vrf5",
                        flow0->id(), 1),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port+1, 0) 
            }
        },
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3002, linklocal_port+2, "vrf5",
                        flow0->id(), 1),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port+2, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 3);
    client->WaitForIdle();
    EXPECT_EQ(6, Agent::GetInstance()->pkt()->flow_table()->Size());
    uint16_t linklocal_src_port[3];
    for (uint32_t i = 0; i < 3; i++) {
        const FlowEntry *fe = nat_flow[i].pkt_.FlowFetch();
        linklocal_src_port[i] = fe->linklocal_src_port();

        EXPECT_TRUE(FlowGet(0, fabric_ip.c_str(), vhost_ip_addr,
                            IPPROTO_TCP, fabric_port+i, linklocal_src_port[i],
                            vhost->flow_key_nh()->id()));
        EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, linklocal_ip,
                            IPPROTO_TCP, 3000+i, linklocal_port+i,
                            GetFlowKeyNH(input[0].intf_id)));
        if (i == 2) {
            EXPECT_TRUE(fe->is_flags_set(FlowEntry::ShortFlow));
            EXPECT_TRUE(fe->short_flow_reason() == FlowEntry::SHORT_LINKLOCAL_SRC_NAT);
            EXPECT_EQ(linklocal_src_port[i], 0);
        }
    }
    
    //Delete forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    DeleteFlow(nat_flow + 1, 1);
    DeleteFlow(nat_flow + 2, 1);
    client->WaitForIdle();
    for (uint32_t i = 0; i < 3; i++) {
        EXPECT_TRUE(FlowFail(0, fabric_ip.c_str(), vhost_ip_addr,
                             IPPROTO_TCP, fabric_port+i,
                             linklocal_src_port[i],
                             vhost->flow_key_nh()->id()));
        EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm1_ip,
                             linklocal_ip, IPPROTO_TCP, 3000+i,
                             linklocal_port+i, GetFlowKeyNH(input[0].intf_id)));
    }
    EXPECT_TRUE(FlowTableWait(0));

    DelLinkLocalConfig();
    client->WaitForIdle();
}

TEST_F(FlowTest, LinkLocalFlow_Fail2) {
    Agent::GetInstance()->set_router_id(Ip4Address::from_string(vhost_ip_addr));
    std::string fabric_ip("1.2.3.4");
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back("1.2.3.4");
    TestLinkLocalService services[4] = {
        { "test_service1", linklocal_ip, linklocal_port, "", fabric_ip_list, fabric_port },
        { "test_service2", linklocal_ip, linklocal_port+1, "", fabric_ip_list, fabric_port+1 },
        { "test_service3", linklocal_ip, linklocal_port+2, "", fabric_ip_list, fabric_port+2 },
        { "test_service4", linklocal_ip, linklocal_port+3, "", fabric_ip_list, fabric_port+3 }
    };
    AddLinkLocalConfig(services, 4);
    client->WaitForIdle();

    // Only three link local flows are allowed simultaneously in the agent;
    // try creating 4 and check that 3 flows are created along with reverse flows,
    // while one flow along with its reverse is a short flow
    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3000, linklocal_port, "vrf5",
                        flow0->id(), 1),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port, 0) 
            }
        },
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3001, linklocal_port+1, "vrf5",
                        flow0->id(), 1),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port+1, 0) 
            }
        },
        {
            TestFlowPkt(Address::INET, vm2_ip, linklocal_ip, IPPROTO_TCP, 3002, linklocal_port+2, "vrf5",
                        flow1->id(), 1),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port+2, 0) 
            }
        },
        {
            TestFlowPkt(Address::INET, vm2_ip, linklocal_ip, IPPROTO_TCP, 3003, linklocal_port+3, "vrf5",
                        flow1->id(), 1),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port+3, 0) 
            }
        }
    };

    CreateFlow(nat_flow, 4);
    client->WaitForIdle();
    EXPECT_EQ(8, Agent::GetInstance()->pkt()->flow_table()->Size());
    uint16_t linklocal_src_port[4];
    for (uint32_t i = 0; i < 4; i++) {
        const FlowEntry *fe = nat_flow[i].pkt_.FlowFetch();
        linklocal_src_port[i] = fe->linklocal_src_port();

        EXPECT_TRUE(FlowGet(0, fabric_ip.c_str(), vhost_ip_addr, IPPROTO_TCP,
                            fabric_port+i, linklocal_src_port[i],
                            vhost->flow_key_nh()->id()));
        if (i <= 1)
            EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, linklocal_ip,
                                IPPROTO_TCP, 3000+i, linklocal_port+i,
                                GetFlowKeyNH(input[0].intf_id)));
        else
            EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm2_ip, linklocal_ip,
                                IPPROTO_TCP, 3000+i, linklocal_port+i,
                                GetFlowKeyNH(input[1].intf_id)));
        if (i == 3) {
            EXPECT_TRUE(fe->is_flags_set(FlowEntry::ShortFlow));
            EXPECT_TRUE(fe->short_flow_reason() == FlowEntry::SHORT_LINKLOCAL_SRC_NAT);
            EXPECT_EQ(linklocal_src_port[i], 0);
        }
    }
    
    //Delete forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    DeleteFlow(nat_flow + 1, 1);
    DeleteFlow(nat_flow + 2, 1);
    DeleteFlow(nat_flow + 3, 1);
    client->WaitForIdle();
    for (uint32_t i = 0; i < 4; i++) {
        EXPECT_TRUE(FlowFail(0, fabric_ip.c_str(), vhost_ip_addr,
                             IPPROTO_TCP, fabric_port+i,
                             linklocal_src_port[i],
                             vhost->flow_key_nh()->id()));
        if (i <= 1)
            EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm1_ip,
                                 linklocal_ip, IPPROTO_TCP, 3000+i,
                                 linklocal_port+i,
                                 GetFlowKeyNH(input[0].intf_id)));
        else
            EXPECT_TRUE(FlowFail(VrfGet("vrf5")->vrf_id(), vm2_ip, linklocal_ip,
                                 IPPROTO_TCP, 3000+i, linklocal_port+i,
                                 GetFlowKeyNH(input[0].intf_id)));
    }
    EXPECT_TRUE(FlowTableWait(0));

    DelLinkLocalConfig();
    client->WaitForIdle();
}

// Check that flow limit per VM works
TEST_F(FlowTest, FlowLimit_1) {
    Agent::GetInstance()->set_router_id(Ip4Address::from_string(vhost_ip_addr));
    uint32_t vm_flows = Agent::GetInstance()->pkt()->flow_table()->max_vm_flows();
    Agent::GetInstance()->pkt()->flow_table()->set_max_vm_flows(3);

    /* Add Local VM route of vrf3 to vrf5 */
    CreateLocalRoute("vrf5", vm4_ip, flow3, 19);
    /* Add Local VM route of vrf5 to vrf3 */
    CreateLocalRoute("vrf3", vm1_ip, flow0, 16);

    TestFlow flow[] = {
        //Send a ICMP request from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, 1, 0, 0, "vrf5", 
                    flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an ICMP reply from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, 1, 0, 0, "vrf3", 
                    flow3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 200, 300, "vrf5", 
                    flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 300, 200, "vrf3", 
                    flow3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };

    CreateFlow(flow, 4);
    client->WaitForIdle();
    int nh_id = InterfaceTable::GetInstance()->FindInterface(flow3->id())->flow_key_nh()->id();
    EXPECT_EQ(4U, Agent::GetInstance()->pkt()->flow_table()->Size());
    FlowEntry *fe = FlowGet(VrfGet("vrf3")->vrf_id(), vm4_ip, vm1_ip,
                            IPPROTO_TCP, 300, 200, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_FLOW_LIMIT);
    EXPECT_TRUE(agent()->stats()->flow_drop_due_to_max_limit() > 0);

    //1. Remove remote VM routes
    DeleteRoute("vrf5", vm4_ip);
    DeleteRoute("vrf3", vm1_ip);
    client->WaitForIdle();
    client->WaitForIdle();
    Agent::GetInstance()->pkt()->flow_table()->set_max_vm_flows(vm_flows);
}

// Check that flow limit per VM doesnt include the short flows in the system
TEST_F(FlowTest, FlowLimit_2) {
    Agent::GetInstance()->set_router_id(Ip4Address::from_string(vhost_ip_addr));
    uint32_t vm_flows = Agent::GetInstance()->pkt()->flow_table()->max_vm_flows();
    Agent::GetInstance()->pkt()->flow_table()->set_max_vm_flows(3);

    TestFlow short_flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm1_ip, "115.115.115.115", 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new ShortFlow()
            }
        }
    };
    CreateFlow(short_flow, 1);
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());
    int nh_id = InterfaceTable::GetInstance()->FindInterface(flow0->id())->flow_key_nh()->id();
    FlowEntry *fe = FlowGet(1, vm1_ip, "115.115.115.115", 1,
                            0, 0, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_NO_DST_ROUTE);

    /* Add Local VM route of vrf3 to vrf5 */
    CreateLocalRoute("vrf5", vm4_ip, flow3, 19);
    /* Add Local VM route of vrf5 to vrf3 */
    CreateLocalRoute("vrf3", vm1_ip, flow0, 16);

    TestFlow flow[] = {
        //Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 100, 101, "vrf5",
                    flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 101, 100, "vrf3",
                    flow3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 200, 300, "vrf5",
                    flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 300, 200, "vrf3",
                    flow3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };

    CreateFlow(flow, 4);
    client->WaitForIdle();
    EXPECT_EQ(6U, Agent::GetInstance()->pkt()->flow_table()->Size());
    nh_id = InterfaceTable::GetInstance()->FindInterface(flow3->id())->flow_key_nh()->id();
    fe = FlowGet(VrfGet("vrf3")->vrf_id(), vm4_ip, vm1_ip,
                 IPPROTO_TCP, 300, 200, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_FLOW_LIMIT);
    fe = FlowGet(VrfGet("vrf3")->vrf_id(), vm4_ip, vm1_ip,
                 IPPROTO_TCP, 101, 100, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == false);
    nh_id = InterfaceTable::GetInstance()->FindInterface(flow0->id())->flow_key_nh()->id();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm4_ip,
                 IPPROTO_TCP, 100, 101, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == false);

    //1. Remove remote VM routes
    DeleteRoute("vrf5", vm4_ip);
    DeleteRoute("vrf3", vm1_ip);
    client->WaitForIdle();
    client->WaitForIdle();
    Agent::GetInstance()->pkt()->flow_table()->set_max_vm_flows(vm_flows);
}

TEST_F(FlowTest, Flow_introspect_delete_all) {
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());

    CreateRemoteRoute("vrf5", remote_vm1_ip, remote_router_ip, 30, "vn5");
    client->WaitForIdle();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {}
        }
    };

    CreateFlow(flow, 1);

    uint32_t vrf_id = VrfGet("vrf5")->vrf_id();
    FlowEntry *fe = FlowGet(vrf_id, vm1_ip, remote_vm1_ip, 1, 0, 0,
                            GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe != NULL);

    DeleteAllFlowRecords *delete_all_sandesh = new DeleteAllFlowRecords();
    Sandesh::set_response_callback(boost::bind(&FlowTest::CheckSandeshResponse,
                                               this, _1, 0));
    delete_all_sandesh->HandleRequest();
    EXPECT_TRUE(FlowTableWait(0));
    delete_all_sandesh->Release();

    DeleteRemoteRoute("vrf5", remote_vm1_ip);
    client->WaitForIdle();
}

TEST_F(FlowTest, Flow_Source_Vn_1) {
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, "17.1.1.1", 1, 0, 0, "vrf5",
                        flow0->id(), 1),
            {
                new VerifyVn("vn5", unknown_vn_),
            }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());
}

/* Create flow for a VN which has ACL attached to it.
 * Make sure that ACL does not have any matching entries for flow
 * Very that network policy UUID is set to IMPLICIT_DENY and SG UUID
 * is set to NOT_EVALUATED
 */
TEST_F(FlowTest, FlowPolicyUuid_1) {
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                       flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(Address::INET, vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                       flow1->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_DENY),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::NOT_EVALUATED),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
}

TEST_F(FlowTest, FlowPolicyUuid_2) {
    AddAclEntry("acl4", 4, 1, "deny", "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2");
    FlowSetup();
    AddLink("virtual-network", "vn6", "access-control-list", "acl4");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::NOT_EVALUATED),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    DelLink("virtual-network", "vn6", "access-control-list", "acl4");
    FlowTeardown();
    DelNode("access-control-list", "acl4");
    client->WaitForIdle(5);
}

/* Create a Local flow for a VN which does not have any ACL attached.
 * Verify that network ACE UUID is set to IMPLICIT_ALLOW.
 * Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure only INGRESS SG ACL on port 'A'
 * Send traffic (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is not set
 */
TEST_F(FlowTest, FlowPolicyUuid_3) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_DENY),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg1");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure only INGRESS SG ACL on port 'A'
 * Send traffic (SIP Y and DIP X) in only one direction from port 'B'
 * Verify that SG UUID is set
 */
TEST_F(FlowTest, FlowPolicyUuid_4) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward flow
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg1");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure only INGRESS SG ACL on port 'A'
 * Send bidirectional traffic from both directions
 * Verify that SG UUID is set
 */
TEST_F(FlowTest, FlowPolicyUuid_5) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_DENY),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg1");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure only EGRESS SG ACL on port 'A'
 * Send traffic (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set
 */
TEST_F(FlowTest, FlowPolicyUuid_6) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg1");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure only EGRESS SG ACL on port 'A'
 * Send traffic (SIP Y and DIP X) in only one direction from port 'B'
 * Verify that SG UUID is NOT set
 */
TEST_F(FlowTest, FlowPolicyUuid_7) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_DENY),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg1");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure only EGRESS SG ACL on port 'A'
 * Send bidirectional traffic from both directions
 * Verify that SG UUID is NOT set
 */
TEST_F(FlowTest, FlowPolicyUuid_8) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

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
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure SG ACLs (both direction) on port 'A'
 * Send bidirectional traffic from both directions
 * Verify that SG UUID is set
 */
TEST_F(FlowTest, FlowPolicyUuid_9) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", BIDIRECTION,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2",
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

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
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure EGRESS SG ACL on port 'A' with action 'deny'
 * Configure INGRESS SG ACL on port 'B' with action 'pass'
 * Send traffic (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set to deny sg rule uuid
 */
TEST_F(FlowTest, FlowPolicyUuid_10) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e", "sg_acl_e", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    EXPECT_EQ(flow6->sg_list().list_.size(), 0U);
    AddSgEntry("sg_i", "sg_acl_i", 11, 1, "deny", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_i");
    client->WaitForIdle();
    EXPECT_EQ(flow6->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_i");

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_i", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    DelNode("security-group", "sg_i");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure EGRESS SG ACL on port 'A' with action 'pass'
 * Configure INGRESS SG ACL on port 'B' with action 'pass'
 * Send traffic (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set to EGRESS sg rule uuid
 */
TEST_F(FlowTest, FlowPolicyUuid_11) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e", "sg_acl_e", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    EXPECT_EQ(flow6->sg_list().list_.size(), 0U);
    AddSgEntry("sg_i", "sg_acl_i", 11, 1, "pass", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_i");
    client->WaitForIdle();
    EXPECT_EQ(flow6->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_i");

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_i", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    DelNode("security-group", "sg_i");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure EGRESS SG ACL on port 'A' with action 'pass'
 * Configure INGRESS SG ACL on port 'B' with action 'deny'
 * Send TCP Ack pkt (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set to deny sg rule uuid
 */
TEST_F(FlowTest, FlowPolicyUuid_12) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e", "sg_acl_e", 10, 6, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    EXPECT_EQ(flow6->sg_list().list_.size(), 0U);
    AddSgEntry("sg_i", "sg_acl_i", 11, 6, "deny", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_i");
    client->WaitForIdle();
    EXPECT_EQ(flow6->sg_list().list_.size(), 1U);

    TxTcpPacket(flow5->id(), vm_a_ip, vm_b_ip, 1, 2, true, 10);
    client->WaitForIdle();

    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, vm_b_ip, 6,
                                  1, 2, flow5->flow_key_nh()->id());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_i");

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_i", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    DelNode("security-group", "sg_i");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure EGRESS SG ACL on port 'A' with action 'pass'
 * Configure EGRESS SG ACL on port 'B' with action 'deny'
 * (Because of above config, flows on port A (X - Y) will be marked as
 * 'implicit_deny'. It is implicity denied because port 'B' does not have
 * any matching 'ingress' rule
 * Send TCP Ack pkt (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set to IMPLICIT_DENY
 */
TEST_F(FlowTest, FlowPolicyUuid_13) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e", "sg_acl_e", 10, 6, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    EXPECT_EQ(flow6->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e2", "sg_acl_e2", 11, 6, "deny", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_e2");
    client->WaitForIdle();
    EXPECT_EQ(flow6->sg_list().list_.size(), 1U);

    //Send TCP Ack pkt
    TxTcpPacket(flow5->id(), vm_a_ip, vm_b_ip, 1, 2, true, 10);
    client->WaitForIdle();

    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, vm_b_ip, 6,
                                  1, 2, flow5->flow_key_nh()->id());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_DENY),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_e2");

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_e2", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e2", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    DelNode("security-group", "sg_e2");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure EGRESS SG ACL on port 'A' with action 'pass'
 * Configure INGRESS SG ACL on port 'B' with action 'pass'
 * Configure EGRESS SG ACL on port 'B' with action 'deny'
 * Configure INGRESS SG ACL on port 'A' with action 'deny'
 * Send TCP Ack pkt (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set to first pass UUID
 */
TEST_F(FlowTest, FlowPolicyUuid_14) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    EXPECT_EQ(flow6->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e", "sg_acl_e", 10, 6, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    AddSgEntry("sg_i", "sg_acl_i", 11, 6, "pass", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d3", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_i");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);
    EXPECT_EQ(flow6->sg_list().list_.size(), 1U);

    AddSgEntry("sg_e2", "sg_acl_e2", 12, 6, "deny", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_e2");
    AddSgEntry("sg_i2", "sg_acl_i2", 13, 6, "deny", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e3", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_i2");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 2U);
    EXPECT_EQ(flow6->sg_list().list_.size(), 2U);

    //Send TCP Ack pkt
    TxTcpPacket(flow5->id(), vm_a_ip, vm_b_ip, 1, 2, true, 10);
    client->WaitForIdle();

    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, vm_b_ip, 6,
                                  1, 2, flow5->flow_key_nh()->id());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2",
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, agent()->pkt()->flow_table()->Size());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_e2");
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_i2");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_i");

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_e2", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e2", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_i", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_i2", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i2", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    DelNode("security-group", "sg_e2");
    DelNode("security-group", "sg_i");
    DelNode("security-group", "sg_i2");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* For link_local flows verify network policy UUID and SG UUID are set to
 * the value of link_local UUID value
 */
TEST_F(FlowTest, FlowPolicyUuid_15) {
    Agent::GetInstance()->set_router_id(Ip4Address::from_string(vhost_ip_addr));
    std::string fabric_ip("1.2.3.4");
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back("1.2.3.4");
    TestLinkLocalService service = { "test_service", linklocal_ip, linklocal_port,
                                     "", fabric_ip_list, fabric_port };
    AddLinkLocalConfig(&service, 1);
    client->WaitForIdle();

    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3000,
                        linklocal_port, "vrf5", flow0->id(), 1),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port, 0)
            }
        }
    };

    CreateFlow(nat_flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());
    const FlowEntry *fe = nat_flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::LINKLOCAL_FLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::LINKLOCAL_FLOW),
                 fe->sg_rule_uuid().c_str());

    //Delete forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));

    DelLinkLocalConfig();
    client->WaitForIdle();
}

/* For multicast flows verify network policy UUID and SG UUID are set to
 * the value of multicast UUID value
 */
TEST_F(FlowTest, FlowPolicyUuid_16) {
    IpamInfo ipam_info[] = {
        {"11.1.1.0", 24, "11.1.1.200", true},
    };
    AddIPAM("vn5", ipam_info, 1);
    client->WaitForIdle();

    if (!RouteFind("vrf5", "11.1.1.255", 32)) {
        return;
    }
    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, vm1_ip, "11.1.1.255", 1, 0, 0, "vrf5", 
                       flow0->id()),
        {}
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev_fe = fe->reverse_flow_entry();
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::Multicast));
    EXPECT_TRUE(rev_fe->is_flags_set(FlowEntry::Multicast));
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::MULTICAST_FLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::MULTICAST_FLOW),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    DeleteFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
    DelIPAM("vn5");
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = 
        TestInit(init_file, ksync_init, true, true, true, (1000000 * 60 * 10), (3 * 60 * 1000));
    if (vm.count("config")) {
        eth_itf = Agent::GetInstance()->fabric_interface_name();
    } else {
        eth_itf = "eth0";
        PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                                eth_itf,
                                Agent::GetInstance()->fabric_vrf_name(),
                                false);
        client->WaitForIdle();
    }

    FlowTest::TestSetup(ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
