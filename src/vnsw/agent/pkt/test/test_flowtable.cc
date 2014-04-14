/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "test/test_cmn_util.h"
#include "ksync/ksync_sock_user.h"

#define MAX_VNET 4

void RouterIdDepInit(Agent *agent) {
}

const std::string svn_name("svn");
const std::string dvn_name("dvn");

struct TestFlowKey {
    uint16_t        vrfid_;
    const char      *sip_;
    const char      *dip_;
    uint8_t         proto_;
    uint16_t        sport_;
    uint16_t        dport_;
    const std::string      *svn_;
    const std::string      *dvn_;
    uint16_t        ifindex_;
    uint16_t        vn_;
    uint16_t        vm_;

    TestFlowKey(uint16_t vrf, const char *sip, const char *dip, uint8_t proto,
                uint16_t sport, uint16_t dport, const std::string &svn,
                const std::string &dvn, uint16_t ifindex, uint16_t vn, uint16_t vm) :
    
        vrfid_(vrf), sip_(sip), dip_(dip), proto_(proto), sport_(sport),
        dport_(dport), svn_(&svn), dvn_(&dvn), ifindex_(ifindex), vn_(vn),
        vm_(vm) {
    }

    void InitFlowKey(FlowKey *key) const {
        key->vrf = vrfid_;
        key->src.ipv4 = ntohl(inet_addr(sip_));
        key->dst.ipv4 = ntohl(inet_addr(dip_));
        key->protocol = proto_;
        key->src_port = sport_;
        key->dst_port = dport_;
    }
};

#define vm_1_1_ip  "1.1.1.1"
#define vm_1_2_ip  "1.1.1.2"
#define vm_2_1_ip  "2.2.2.2"

struct PortInfo tap1[] = {
    {"tap-1-1", 1, vm_1_1_ip, "00:00:00:01:01:01", 1, 1},
};

struct PortInfo tap2[] = {
    {"tap-1-2", 2, vm_1_2_ip, "00:00:00:01:01:02", 1, 2},
};

struct PortInfo tap3[] = {
    {"tap-2-1", 3, vm_2_1_ip, "00:00:00:02:02:01", 2, 3},
};

int hash_id;

class FlowTableTest : public ::testing::Test {
public:
    bool FlowTableWait(int count) {
        int i = 1000;
        while (i > 0) {
            i--;
            if (Agent::GetInstance()->pkt()->flow_table()->Size() == (size_t) count) {
                break;
            }
            client->WaitForIdle();
            usleep(1);
        }
        return (Agent::GetInstance()->pkt()->flow_table()->Size() == (size_t) count);
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
    }

    void CreateLocalRoute(const char *vrf, const char *ip,
                          VmInterface *intf, int label) {
        Ip4Address addr = Ip4Address::from_string(ip);
        Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->
            AddLocalVmRouteReq(NULL, vrf, addr, 32, intf->GetUuid(),
                               intf->vn()->GetName(), label,
                               SecurityGroupList(), false); 
        client->WaitForIdle();
        EXPECT_TRUE(RouteFind(vrf, addr, 32));
    }

    void CreateRemoteRoute(const char *vrf, const char *remote_vm, 
                           const char *serv, int label, const char *vn) {
        Ip4Address addr = Ip4Address::from_string(remote_vm);
        Ip4Address gw = Ip4Address::from_string(serv);
        Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->
            AddRemoteVmRouteReq(NULL, vrf, addr, 32, gw, TunnelType::AllType(), 
                                label, vn, SecurityGroupList());
        client->WaitForIdle();
        EXPECT_TRUE(RouteFind(vrf, addr, 32));
    }

    void DeleteRoute(const char *vrf, const char *ip) {
        Ip4Address addr = Ip4Address::from_string(ip);
        Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->DeleteReq(NULL, vrf, addr, 32);
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFind(vrf, addr, 32) == false));
    }

    bool FindAcl(const std::list<MatchAclParams> &acl_list,
                 const AclDBEntry *acl) {
        std::list<MatchAclParams>::const_iterator it;
        bool found = false;
        for (it = acl_list.begin(); it != acl_list.end(); it++) {
            if (it->acl.get() == acl) {
                found = true;
                break;
            }
        }
        return found;
    }

    bool ValidateFlow(TestFlowKey *t, TestFlowKey *rev, uint32_t action) {
        bool ret = true;
        FlowKey key;
        t->InitFlowKey(&key);
        FlowEntry *flow = Agent::GetInstance()->pkt()->flow_table()->Find(key);
        EXPECT_TRUE(flow != NULL);
        if (flow == NULL) {
            return false;
        }

        FlowEntry *rflow = NULL;
        if (rev) {
            rev->InitFlowKey(&key);
            rflow = Agent::GetInstance()->pkt()->flow_table()->Find(key);
            EXPECT_EQ(flow->reverse_flow_entry(), rflow);
            if (flow->reverse_flow_entry() != rflow) {
                ret = false;
            }
        }

        // Match action
        EXPECT_TRUE((flow->match_p().action_info.action & action) != 0);
        if ((flow->match_p().action_info.action & action) == 0)
            ret = false;

        // Match Policy from VN
        const VnEntry *vn = flow->data().vn_entry.get();
        if (vn && vn->GetAcl() &&
            FindAcl(flow->match_p().m_acl_l, vn->GetAcl()) == false) {
            EXPECT_STREQ("Policy not found in flow", "");
            ret = false;
        }

        const VmInterface *intf = static_cast<const VmInterface *>
            (flow->data().intf_entry.get());
        if (intf) {
            const VmInterface::SecurityGroupEntrySet sg_list = intf->sg_list().list_;
            VmInterface::SecurityGroupEntrySet::const_iterator it;
            for (it = sg_list.begin(); it != sg_list.end(); it++) {
                if (it->sg_->GetEgressAcl() == NULL &&
                    it->sg_->GetIngressAcl() == NULL) {
                    continue;
                }
                if (FindAcl(flow->match_p().m_sg_acl_l, 
                            (it->sg_->GetEgressAcl())) == false &&
                    FindAcl(flow->match_p().m_sg_acl_l, 
                            (it->sg_->GetIngressAcl())) == false) {
                    EXPECT_STREQ("SG not found in flow", "");
                    ret = false;
                }
            }
        }

        if (rflow == NULL) {
            return ret;
        }

        if (!flow->is_flags_set(FlowEntry::LocalFlow)) {
            EXPECT_EQ(flow->match_p().m_out_acl_l.size(), 0U);
            EXPECT_EQ(flow->match_p().m_out_sg_acl_l.size(), 0U);
            if ((flow->match_p().m_out_acl_l.size() != 0) || 
                (flow->match_p().m_out_sg_acl_l.size() != 0)) {
                ret = false;
            }

            return ret;
        }

        vn = rflow->data().vn_entry.get();
        if (vn && vn->GetAcl() &&
            FindAcl(flow->match_p().m_out_acl_l, vn->GetAcl()) == false){
            EXPECT_STREQ("Out Policy not found in flow", "");
            ret = false;
        }

        intf = static_cast<const VmInterface *>
            (rflow->data().intf_entry.get());
        if (intf) {
            const VmInterface::SecurityGroupEntrySet sg_list = intf->sg_list().list_;
            VmInterface::SecurityGroupEntrySet::const_iterator it;
            for (it = sg_list.begin(); it != sg_list.end(); it++) {
                if (it->sg_->GetEgressAcl() == NULL &&
                    it->sg_->GetIngressAcl() == NULL) {
                    continue;
                }
                if (FindAcl(flow->match_p().m_out_sg_acl_l,
                            it->sg_->GetEgressAcl()) == false &&
                    FindAcl(flow->match_p().m_out_sg_acl_l,
                            it->sg_->GetIngressAcl()) == false) {
                    EXPECT_STREQ("Out SG not found in flow", "");
                    ret = false;
                }
            }
        }


        return ret;
    }

    static void FlowDel(const TestFlowKey *flow) {
        FlowKey key;
        flow->InitFlowKey(&key);
        Agent::GetInstance()->pkt()->flow_table()->Delete(key, true);
        client->WaitForIdle();
    }

    FlowEntry *FlowInit(TestFlowKey *t) {
        FlowKey key;
        t->InitFlowKey(&key);
        FlowEntry *flow = Agent::GetInstance()->pkt()->flow_table()->Allocate(key);

        boost::shared_ptr<PktInfo> pkt_info(new PktInfo(NULL, 0));
        PktFlowInfo info(pkt_info, Agent::GetInstance()->pkt()->flow_table());
        PktInfo *pkt = pkt_info.get();
        info.source_vn = t->svn_;
        info.dest_vn = t->dvn_;
        SecurityGroupList empty_sg_id_l;
        info.source_sg_id_l = &empty_sg_id_l;
        info.dest_sg_id_l = &empty_sg_id_l;

        PktControlInfo ctrl;
        ctrl.vn_ = VnGet(t->vn_);
        ctrl.intf_ = VmPortGet(t->ifindex_);
        ctrl.vm_ = VmGet(t->vm_);

        flow->InitFwdFlow(&info, pkt, &ctrl, &ctrl);
        return flow;
    }

    static void FlowAdd(FlowEntry *fwd, FlowEntry *rev) {
        Agent::GetInstance()->pkt()->flow_table()->Add(fwd, rev);
        client->WaitForIdle();
    }

    static void TestSetup(bool ksync_init) {
        ksync_init_ = ksync_init;
        if (ksync_init_) {
            CreateTapInterfaces("flow", MAX_VNET, fd_table);
            client->WaitForIdle();
        }

        hash_id = 1;
        client->Reset();

        CreateVmportEnv(tap1, 1, 1);
        CreateVmportEnv(tap2, 1, 2);
        CreateVmportEnv(tap3, 1, 3);
        client->WaitForIdle();

        EXPECT_TRUE(VmPortActive(tap1, 0));
        EXPECT_TRUE(VmPortPolicyEnable(tap1, 0));

        EXPECT_TRUE(VmPortActive(tap2, 0));
        EXPECT_TRUE(VmPortPolicyEnable(tap2, 0));

        EXPECT_TRUE(VmPortActive(tap3, 0));
        EXPECT_TRUE(VmPortPolicyEnable(tap3, 0));

        EXPECT_EQ(7U, Agent::GetInstance()->GetInterfaceTable()->Size());
        EXPECT_EQ(3U, Agent::GetInstance()->GetVmTable()->Size());

        EXPECT_EQ(2U,  Agent::GetInstance()->GetVnTable()->Size());
        EXPECT_EQ(3U, Agent::GetInstance()->GetIntfCfgTable()->Size());

        vif1 = VmInterfaceGet(tap1[0].intf_id);
        assert(vif1);

        vif2 = VmInterfaceGet(tap2[0].intf_id);
        assert(vif2);

        vif3 = VmInterfaceGet(tap3[0].intf_id);
        assert(vif3);
    }

    static void TestTearDown() {
        client->Reset();
        DeleteVmportEnv(tap1, 1, true, 1);
        DeleteVmportEnv(tap2, 1, true, 2);
        DeleteVmportEnv(tap3, 1, true, 3);
        FlowTableTest::eth_itf = "eth0";
        PhysicalInterface::DeleteReq(Agent::GetInstance()->GetInterfaceTable(),
                                FlowTableTest::eth_itf);
        client->WaitForIdle();

        EXPECT_FALSE(VmPortFind(tap1, 0));
        EXPECT_FALSE(VmPortFind(tap2, 0));
        EXPECT_FALSE(VmPortFind(tap3, 0));

        EXPECT_EQ(3U, Agent::GetInstance()->GetInterfaceTable()->Size());
        EXPECT_EQ(0U, Agent::GetInstance()->GetIntfCfgTable()->Size());
        EXPECT_EQ(0U, Agent::GetInstance()->GetVmTable()->Size());
        EXPECT_EQ(0U, Agent::GetInstance()->GetVnTable()->Size());
        EXPECT_EQ(0U, Agent::GetInstance()->GetAclTable()->Size());

        if (ksync_init_) {
            DeleteTapIntf(fd_table, MAX_VNET);
        }
        client->WaitForIdle();
    }

    static bool ksync_init_;
    static int fd_table[MAX_VNET];
    static VmInterface *vif1;
    static VmInterface *vif2;
    static VmInterface *vif3;
    static std::string eth_itf;

protected:
    virtual void SetUp() {
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
        //Reset flow age
        client->EnqueueFlowAge();
        client->WaitForIdle();

        key1 = new TestFlowKey(1, "1.1.1.1", "1.1.1.2", 1, 0, 0, svn_name,
                               dvn_name, 1, 1, 1);
        flow1 = FlowInit(key1);
        flow1->set_flags(FlowEntry::LocalFlow);

        key1_r = new TestFlowKey(1, "1.1.1.2", "1.1.1.1", 1, 0, 0, dvn_name,
                                 svn_name, 2, 1, 2);
        flow1_r = FlowInit(key1_r);
        flow1_r->set_flags(FlowEntry::LocalFlow);
        FlowAdd(flow1, flow1_r);

        key2 = new TestFlowKey(1, "1.1.1.1", "1.1.1.3", 1, 0, 0, svn_name,
                               dvn_name, 1, 1, 1);
        flow2 = FlowInit(key2);
        flow2->reset_flags(FlowEntry::LocalFlow);

        key2_r = new TestFlowKey(1, "1.1.1.3", "1.1.1.1", 1, 0, 0, dvn_name,
                                 svn_name, 2, 1, 2);
        flow2_r = FlowInit(key2_r);
        flow2_r->reset_flags(FlowEntry::LocalFlow);
        FlowAdd(flow2, flow2_r);
    }

    virtual void TearDown() {
        FlushFlowTable();
        delete key1;
        delete key1_r;
        delete key2;
        delete key2_r;
    }

    TestFlowKey *key1;
    FlowEntry *flow1;

    TestFlowKey *key1_r;
    FlowEntry *flow1_r;

    TestFlowKey *key2;
    FlowEntry *flow2;

    TestFlowKey *key2_r;
    FlowEntry *flow2_r;
};

bool FlowTableTest::ksync_init_;
int FlowTableTest::fd_table[MAX_VNET];
VmInterface *FlowTableTest::vif1;
VmInterface *FlowTableTest::vif2;
VmInterface *FlowTableTest::vif3;
std::string FlowTableTest::eth_itf;

TEST_F(FlowTableTest, FlowAdd_local_1) {
    EXPECT_TRUE(ValidateFlow(key1, key1_r, (1 << TrafficAction::DROP)));
}

TEST_F(FlowTableTest, FlowAdd_non_local_1) {
    EXPECT_TRUE(ValidateFlow(key2, key2_r, (1 << TrafficAction::DROP)));
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, true, true, (1000000 * 60 * 10),
            FlowStatsCollector::FlowStatsInterval, true, false);
    if (vm.count("config")) {
        FlowTableTest::eth_itf = Agent::GetInstance()->GetIpFabricItfName();
    } else {
        FlowTableTest::eth_itf = "eth0";
        PhysicalInterface::CreateReq(Agent::GetInstance()->GetInterfaceTable(),
                                FlowTableTest::eth_itf, Agent::GetInstance()->GetDefaultVrf());
        client->WaitForIdle();
    }

    FlowTableTest::TestSetup(ksync_init);
    int ret = RUN_ALL_TESTS();
    FlowTableTest::TestTearDown();
    TestShutdown();
    delete client;
    return ret;
}
