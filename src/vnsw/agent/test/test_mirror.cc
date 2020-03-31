/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"
#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mirror_table.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <boost/assign/list_of.hpp>

using namespace boost::assign;
std::string eth_itf;
int entry_count;

std::string analyzer = "TestAnalyzer";

void RouterIdDepInit(Agent *agent) {
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
}

class MirrorTableTest : public ::testing::Test {
protected:
    MirrorTableTest(): count_(entry_count),
   agent_(Agent::GetInstance()), nh_count_(0) {
    }

    virtual void SetUp() {
        client->Reset();
        nh_count_ = agent_->nexthop_table()->Size();
        PhysicalInterface::CreateReq(agent_->interface_table(),
                                eth_itf, agent_->fabric_vrf_name(),
                                PhysicalInterface::FABRIC,
                                PhysicalInterface::ETHERNET, false,
                                boost::uuids::nil_uuid(), Ip4Address(0),
                                Interface::TRANSPORT_ETHERNET);
        fabric_gw_ip_ = Ip4Address::from_string("10.1.1.254");
        uint16_t sport = 10000;
        unsigned long ip = 0x0a010102;
        for (int i = 0; i < count_; i++) {
            sport_.push_back(sport++);
            dport_.push_back(sport++);
            sip_.push_back(Ip4Address(ip++));
            dip_.push_back(Ip4Address(ip++));
        }
        bgp_peer_ = CreateBgpPeer("127.0.0.1", "remote");
    }

    virtual void TearDown() {
        WAIT_FOR(1002, 1000, agent_->nexthop_table()->Size() == nh_count_);
        WAIT_FOR(1000, 1000, agent_->vrf_table()->Size() == 2);
        DeleteBgpPeer(bgp_peer_);
    }

    void AddAllMirrorEntry() {
        for (int i = 0; i < count_; i++) {
            std::stringstream str;
            str << analyzer << i;
            MirrorTable::AddMirrorEntry(str.str(), agent_->fabric_vrf_name(), sip_[i], sport_[i], dip_[i], dport_[i]);
        }
        client->WaitForIdle();
    }

    void DelAllMirrorEntry() {
        for (int i = 0; i < count_; i++) {
            std::stringstream str;
            str << analyzer << i;
            MirrorTable::DelMirrorEntry(str.str());
        }
        client->WaitForIdle();
    }

    void AddAllArpEntry() {
        for (int i = 0; i < count_; i++) {
            AddArp(dip_[i].to_string().c_str(), "0a:0b:0c:0d:0e:0f",
                   eth_itf.c_str());
        }
        client->WaitForIdle();
    }

    void DelAllArpEntry() {
        for (int i = 0; i < count_; i++) {
            agent_->fabric_inet4_unicast_table()->DeleteReq(
                                                         agent_->local_peer(),
                                                         agent_->fabric_vrf_name(),
                                                         dip_[i], 32, NULL);
        }
        client->WaitForIdle();
    }

    bool MirrorEntryFind(int i) {
        std::stringstream str;
        str << analyzer << i;
        MirrorEntryKey key(str.str());
        return (agent_->mirror_table()->FindActiveEntry(&key) != NULL);
    }

    bool MirrorNHFind(int i) {
        MirrorNHKey key(agent_->fabric_vrf_name(), sip_[i], sport_[i], dip_[i],
                        dport_[i]);
        return (agent_->nexthop_table()->FindActiveEntry(&key) != NULL);
    }

    MirrorEntry *GetMirrorEntry(int i) {
        std::stringstream str;
        str << analyzer << i;
        MirrorEntryKey key(str.str());
        return static_cast<MirrorEntry *>(agent_->mirror_table()->FindActiveEntry(&key));
    }

    int count_;
    std::vector<uint16_t> sport_;
    std::vector<uint16_t> dport_;
    std::vector<Ip4Address> sip_;
    std::vector<Ip4Address> dip_;
    Ip4Address fabric_gw_ip_;
    Agent *agent_;
    uint32_t nh_count_;
    BgpPeer *bgp_peer_;
};

TEST_F(MirrorTableTest, MirrorEntryAddDel_1) {
    AddAllMirrorEntry();

    MirrorEntry *mirr_entry;
    //Verify all mirror entry and mirror NH are added
    for (int i = 0; i < count_; i++) {
        EXPECT_TRUE(MirrorEntryFind(i));
        mirr_entry = GetMirrorEntry(i);
        if (mirr_entry) {
            const NextHop *nh = mirr_entry->GetNH();
            EXPECT_TRUE(nh->IsValid() == false);
        }
    }

    //Resolve ARP for dest server IP and make sure NH
    //are valid now
    AddAllArpEntry();
    //Verify all mirror entry and mirror NH are added
    for (int i = 0; i < count_; i++) {
        EXPECT_TRUE(MirrorEntryFind(i));
        mirr_entry = GetMirrorEntry(i);
        if (mirr_entry) {
            const NextHop *nh = mirr_entry->GetNH();
            EXPECT_TRUE(nh->IsValid() == true);
        }
    }
    MirrorEntryReq *mirror_list_req = new MirrorEntryReq();
    std::vector<int> result = {1};
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    mirror_list_req->HandleRequest();
    client->WaitForIdle();
    mirror_list_req->Release();
    client->WaitForIdle();

    EXPECT_TRUE(agent_->mirror_table()->GetInstance()->IsConfigured());
    DelAllMirrorEntry();
    //make sure all Mirror entry are deleted
    for (int i = 0; i < count_; i++) {
        EXPECT_FALSE(MirrorEntryFind(i));
    }
    //Verify that mirror NH is deleted
    for (int i = 0; i < count_; i++) {
        EXPECT_FALSE(MirrorNHFind(i));
    }
    DelAllArpEntry();
    usleep(1000);
    EXPECT_FALSE(agent_->mirror_table()->GetInstance()->IsConfigured());
}

TEST_F(MirrorTableTest, MirrorEntryAddDel_2) {
    AddAllMirrorEntry();

    MirrorEntry *mirr_entry;
    //Verify all mirror entry and mirror NH are added
    for (int i = 0; i < count_; i++) {
        EXPECT_TRUE(MirrorEntryFind(i));
        mirr_entry = GetMirrorEntry(i);
        if (mirr_entry) {
            const NextHop *nh = mirr_entry->GetNH();
            EXPECT_TRUE(nh->IsValid() == false);
        }
    }

    //Resolve ARP for dest server IP and make sure NH
    //are valid now
    AddAllArpEntry();
    //Verify all mirror entry and mirror NH are added
    for (int i = 0; i < count_; i++) {
        EXPECT_TRUE(MirrorEntryFind(i));
        mirr_entry = GetMirrorEntry(i);
        if (mirr_entry) {
            const NextHop *nh = mirr_entry->GetNH();
            EXPECT_TRUE(nh->IsValid() == true);
        }
    }

    DelAllArpEntry();
    //Delete ARP entry and make sure mirror NH are invalid
    for (int i = 0; i < count_; i++) {
        EXPECT_TRUE(MirrorEntryFind(i));
        mirr_entry = GetMirrorEntry(i);
        if (mirr_entry) {
            const NextHop *nh = mirr_entry->GetNH();
            EXPECT_TRUE(nh->IsValid() == false);
        }
    }

    DelAllMirrorEntry();
    //make sure all Mirror entry are deleted
    for (int i = 0; i < count_; i++) {
        EXPECT_FALSE(MirrorEntryFind(i));
    }
    //Verify that mirror NH is deleted
    for (int i = 0; i < count_; i++) {
        EXPECT_FALSE(MirrorNHFind(i));
    }
    DelAllArpEntry();
}

TEST_F(MirrorTableTest, MirrorEntryAddDel_3) {
    Ip4Address vhost_ip(agent_->router_id());
    //Add mirror entry pointing to same vhost IP
    std::stringstream str;
    str << analyzer;
    MirrorTable::AddMirrorEntry(analyzer, agent_->fabric_vrf_name(),
                                vhost_ip, 0x1, vhost_ip, 0x2);
    client->WaitForIdle();
    //Mirror NH would point to a route, whose nexthop would be RCV NH
    MirrorEntryKey key(analyzer);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    const MirrorNH *mirr_nh = static_cast<const MirrorNH *>(mirr_entry->GetNH());
    //Make sure mirror nh internally points to receive router
    const NextHop *nh = mirr_nh->GetRt()->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::RECEIVE);

    MirrorTable::DelMirrorEntry(analyzer);
    client->WaitForIdle();
    mirr_entry = static_cast<const MirrorEntry *>
                 (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
}

TEST_F(MirrorTableTest, MirrorEntryAddDel_4) {
    Ip4Address vhost_ip(agent_->router_id());
    Ip4Address remote_server = Ip4Address::from_string("1.1.1.1");
    //Add mirror entry pointing to same vhost IP
    std::string ana = analyzer + "r";
    MirrorTable::AddMirrorEntry(ana, agent_->fabric_vrf_name(),
                                vhost_ip, 0x1, remote_server, 0x2);
    client->WaitForIdle();
    //Mirror NH would point to a gateway route
    MirrorEntryKey key(ana);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    const MirrorNH *mirr_nh = static_cast<const MirrorNH *>(mirr_entry->GetNH());
    //Gateway route not resolved, hence mirror entry would
    //be pointing to invalid NH
    const NextHop *nh = mirr_nh->GetRt()->GetActiveNextHop();
    EXPECT_TRUE(nh->IsValid() == false);

    //Resolve ARP for subnet gateway route
    AddArp(fabric_gw_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f",
           eth_itf.c_str());
    client->WaitForIdle();
    EXPECT_TRUE(nh->IsValid() == true);

    //Del Arp for gateway IP, and verify nexthop becomes invalid
    DelArp(fabric_gw_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f",
           eth_itf.c_str());
    client->WaitForIdle();
    EXPECT_TRUE(nh->IsValid() == false);

    MirrorTable::DelMirrorEntry(ana);
    client->WaitForIdle();
    mirr_entry = static_cast<const MirrorEntry *>
                 (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
    client->WaitForIdle();
    usleep(1000);
    client->WaitForIdle();
}

TEST_F(MirrorTableTest, MirrorEntryAddDel_5) {
    Ip4Address vhost_ip(agent_->router_id());
    Ip4Address remote_server = Ip4Address::from_string("1.1.1.1");
    //Add mirror entry pointing to same vhost IP
    std::string ana = analyzer + "r";
    std::string analyzer1 = analyzer + "1";
    std::string analyzer2 = "analyzer2";
    MirrorTable::AddMirrorEntry(ana, "vrf3",
                                vhost_ip, 0x1, remote_server, 0x2);
    MirrorTable::AddMirrorEntry(analyzer1, "vrf3",
                                vhost_ip, 0x1, remote_server, 0x2);
    MirrorTable::AddMirrorEntry(analyzer2 , "vrf2",
                                vhost_ip, 0x1, remote_server, 0x2);
    client->WaitForIdle();
    //Mirror NH would point to a gateway route
    MirrorEntryKey key(ana);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    const MirrorNH *mirr_nh = static_cast<const MirrorNH *>(mirr_entry->GetNH());
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::DISCARD);

    MirrorEntryKey key1(analyzer1);
    const MirrorEntry *mirr_entry1 = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key1));
    EXPECT_TRUE(mirr_entry1 != NULL);
    const MirrorNH *mirr_nh1 = static_cast<const MirrorNH *>(mirr_entry1->GetNH());
    EXPECT_TRUE(mirr_nh1->GetType() == NextHop::DISCARD);


    AddVrf("vrf3");
    client->WaitForIdle();

    mirr_nh = static_cast<const MirrorNH *>(mirr_entry->GetNH());
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::MIRROR);

    mirr_nh1 = static_cast<const MirrorNH *>(mirr_entry1->GetNH());
    EXPECT_TRUE(mirr_nh1->GetType() == NextHop::MIRROR);

    MirrorEntryKey key2(analyzer2);
    const MirrorEntry *mirr_entry2 = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key2));
    EXPECT_TRUE(mirr_entry2 != NULL);
    const MirrorNH *mirr_nh2 = static_cast<const MirrorNH *>(mirr_entry2->GetNH());
    EXPECT_TRUE(mirr_nh2->GetType() == NextHop::DISCARD);

    MirrorTable::DelMirrorEntry(ana);
    MirrorTable::DelMirrorEntry(analyzer1);
    MirrorTable::DelMirrorEntry(analyzer2);
    client->WaitForIdle();
    mirr_entry = static_cast<const MirrorEntry *>
                 (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
    client->WaitForIdle();
    usleep(1000);
    DelVrf("vrf3");
    client->WaitForIdle();
}

TEST_F(MirrorTableTest, MirrorInvalidVrf_1) {
    Ip4Address vhost_ip(agent_->router_id());
    string analyser("analyzer-no-vrf");
    string vrf("invalid-vrf");

    //Add mirror entry pointing to same vhost IP
    MirrorTable::AddMirrorEntry(analyzer, vrf, vhost_ip, 0x1, vhost_ip, 0x2);
    client->WaitForIdle();

    //Mirror NH would point to a route, whose nexthop would be RCV NH
    MirrorEntryKey key(analyzer);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
        (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);

    //Make sure mirror nh internally points to receive router
    const MirrorNH *mirr_nh = static_cast<const MirrorNH *>(mirr_entry->GetNH());
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::DISCARD);

    MirrorTable::DelMirrorEntry(analyzer);
    client->WaitForIdle();
    mirr_entry = static_cast<const MirrorEntry *>
        (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
}

TEST_F(MirrorTableTest, MirrorEntryAddDel_6) {
    AddVrf("vrf3");
    client->WaitForIdle();

    Ip4Address vhost_ip(agent_->router_id());
    Ip4Address remote_server = Ip4Address::from_string("1.1.1.1");
    //Add mirror entry pointing to same vhost IP
    std::string ana = analyzer + "r";
    std::string analyzer1 = analyzer + "1";
    std::string analyzer2 = "analyzer2";
    MirrorTable::AddMirrorEntry(ana, "vrf3",
                                vhost_ip, 0x1, remote_server, 0x2);
    MirrorTable::AddMirrorEntry(analyzer1, "vrf3",
                                vhost_ip, 0x1, remote_server, 0x2);
    MirrorTable::AddMirrorEntry(analyzer2 , "vrf2",
                                vhost_ip, 0x1, remote_server, 0x2);
    client->WaitForIdle();
    //Mirror NH would point to a gateway route
    MirrorEntryKey key(ana);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    const MirrorNH *mirr_nh = static_cast<const MirrorNH *>(mirr_entry->GetNH());
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::MIRROR);

    MirrorEntryKey key1(analyzer1);
    const MirrorEntry *mirr_entry1 = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key1));
    EXPECT_TRUE(mirr_entry1 != NULL);
    const MirrorNH *mirr_nh1 = static_cast<const MirrorNH *>(mirr_entry1->GetNH());
    EXPECT_TRUE(mirr_nh1->GetType() == NextHop::MIRROR);


    DelVrf("vrf3");
    client->WaitForIdle();

    mirr_nh = static_cast<const MirrorNH *>(mirr_entry->GetNH());
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::DISCARD);

    mirr_nh1 = static_cast<const MirrorNH *>(mirr_entry1->GetNH());
    EXPECT_TRUE(mirr_nh1->GetType() == NextHop::DISCARD);

    MirrorEntryKey key2(analyzer2);
    const MirrorEntry *mirr_entry2 = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key2));
    EXPECT_TRUE(mirr_entry2 != NULL);
    const MirrorNH *mirr_nh2 = static_cast<const MirrorNH *>(mirr_entry2->GetNH());
    EXPECT_TRUE(mirr_nh2->GetType() == NextHop::DISCARD);

    MirrorTable::DelMirrorEntry(ana);
    MirrorTable::DelMirrorEntry(analyzer1);
    MirrorTable::DelMirrorEntry(analyzer2);
    client->WaitForIdle();
    mirr_entry = static_cast<const MirrorEntry *>
                 (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
    client->WaitForIdle();
}

//This test is to verify the Dynamic without Juniper header config
//Add Mirror Entry and check it is attached to the existing
//Tunnel NH created by BridgeTunnelRouteAdd
// Check that Static NH changed to MirrorNH after moving the nh mode
TEST_F(MirrorTableTest, StaticMirrorEntryAdd_6) {
    Ip4Address vhost_ip(agent_->router_id());
    Ip4Address remote_server = Ip4Address::from_string("1.1.1.1");
    Ip4Address remote_vm_ip4_2 = Ip4Address::from_string("2.2.2.11");
    //Add mirror entry pointing to same vhost IP
    std::string ana = analyzer + "r";
    std::string remote_vm_mac_str_;
    MacAddress remote_vm_mac = MacAddress::FromString("00:00:01:01:01:11");
    TunnelType::TypeBmap bmap;
    bmap = 1 << TunnelType::VXLAN;
    AddVrf("vrf3");
    client->WaitForIdle();
    BridgeTunnelRouteAdd(bgp_peer_, "vrf3", bmap, remote_server,
                         1, remote_vm_mac, remote_vm_ip4_2, 32);
    client->WaitForIdle();
    MirrorTable::AddMirrorEntry(ana, "vrf3", vhost_ip, 0x1, remote_server,
                                      0x2, 0 , 2, remote_vm_mac);
    client->WaitForIdle();

    MirrorEntryKey key(ana);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    const NextHop *mirr_nh = mirr_entry->GetNH();
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::TUNNEL);

    EvpnAgentRouteTable::DeleteReq(agent_->local_peer(), "vrf3", remote_vm_mac,
                                   remote_vm_ip4_2, 32, 0, NULL);
    MirrorTable::AddMirrorEntry(ana, "vrf3",
                                vhost_ip, 0x1, remote_server, 0x2);
    client->WaitForIdle();

    mirr_entry = static_cast<const MirrorEntry *>
        (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    mirr_nh = static_cast<const MirrorNH *>(mirr_entry->GetNH());
    mirr_nh = mirr_entry->GetNH();
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::MIRROR);
    DelVrf("vrf3");
    client->WaitForIdle();
    mirr_nh = mirr_entry->GetNH();
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::DISCARD);

    MirrorTable::DelMirrorEntry(ana);
    client->WaitForIdle();
    mirr_entry = static_cast<const MirrorEntry *>
                 (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
    client->WaitForIdle();
}

//This test is to verify the Dynamic without Juniper header config
//Add Mirror Entry without mirror VRF so that Mirror entry will create the
//vrf and attach to the Tunnel NH created by BridgeTunnelRouteAdd
// Change the VRF and check NH refrence is released
TEST_F(MirrorTableTest, StaticMirrorEntryAdd_7) {
    Ip4Address vhost_ip(agent_->router_id());
    Ip4Address remote_server = Ip4Address::from_string("1.1.1.1");
    Ip4Address remote_vm_ip4_2 = Ip4Address::from_string("2.2.2.11");
    //Add mirror entry pointing to same vhost IP
    std::string ana = analyzer + "r";
    MacAddress remote_vm_mac = MacAddress::FromString("00:00:01:01:01:11");
    TunnelType::TypeBmap bmap;
    bmap = 1 << TunnelType::VXLAN;
    client->WaitForIdle();

    MirrorTable::AddMirrorEntry(ana, "vrf3", vhost_ip, 0x1, remote_server,
                                      0x2, 0 , 2, remote_vm_mac);

    BridgeTunnelRouteAdd(bgp_peer_, "vrf3", bmap, remote_server,
                         1, remote_vm_mac, remote_vm_ip4_2, 32);
    client->WaitForIdle();

    MirrorEntryKey key(ana);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    const NextHop *mirr_nh = mirr_entry->GetNH();
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::TUNNEL);

    EvpnAgentRouteTable::DeleteReq(agent_->local_peer(), "vrf3", remote_vm_mac,
                                   remote_vm_ip4_2, 32, 0, NULL);
    client->WaitForIdle();

    MirrorTable::AddMirrorEntry(ana, "vrf4", vhost_ip, 0x1, remote_server,
                                      0x2, 0 , 2, remote_vm_mac);
    BridgeTunnelRouteAdd(bgp_peer_, "vrf4", bmap, remote_server,
                         1, remote_vm_mac, remote_vm_ip4_2, 32);

    client->WaitForIdle();
    mirr_entry = static_cast<const MirrorEntry *>
        (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    mirr_nh = mirr_entry->GetNH();
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::TUNNEL);
    EvpnAgentRouteTable::DeleteReq(agent_->local_peer(), "vrf4", remote_vm_mac,
                                   remote_vm_ip4_2, 32, 0, NULL);
    client->WaitForIdle();
    MirrorTable::DelMirrorEntry(ana);
    client->WaitForIdle();
    mirr_entry = static_cast<const MirrorEntry *>
                 (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
    client->WaitForIdle();
}

static void CreateTunnelNH(const string &vrf_name, const Ip4Address &sip,
                           const Ip4Address &dip, bool policy,
                           TunnelType::TypeBmap bmap){
    DBRequest req;
    TunnelNHData *data = new TunnelNHData();

    NextHopKey *key = new TunnelNHKey(vrf_name, sip, dip, policy,
                                      TunnelType::ComputeType(bmap));
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(data);
    Agent::GetInstance()->nexthop_table()->Enqueue(&req);
}

static void DeleteTunnelNH(const string &vrf_name, const Ip4Address &sip,
                           const Ip4Address &dip, bool policy,
                           TunnelType::TypeBmap bmap){
    DBRequest req;
    TunnelNHData *data = new TunnelNHData();

    NextHopKey *key = new TunnelNHKey(vrf_name, sip, dip, policy,
                                      TunnelType::ComputeType(bmap));
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(key);
    req.data.reset(data);
    Agent::GetInstance()->nexthop_table()->Enqueue(&req);
}

void AddResolveRoute(const Ip4Address &server_ip, uint32_t plen) {
        Agent* agent = Agent::GetInstance();
        VmInterfaceKey vhost_key(AgentKey::ADD_DEL_CHANGE,
                                      boost::uuids::nil_uuid(),
                                      agent->vhost_interface()->name());

                agent->fabric_inet4_unicast_table()->AddResolveRoute(
                agent->local_peer(),
                agent->fabric_vrf_name(), server_ip, plen, vhost_key,
                0, false, "", SecurityGroupList(), TagList());
        client->WaitForIdle();
}

void DeleteRoute(const Peer *peer, const std::string &vrf_name,
                     const Ip4Address &addr, uint32_t plen) {
    Agent::GetInstance()->fabric_inet4_unicast_table()->DeleteReq(peer, vrf_name,
                                                                            addr, plen, NULL);
    client->WaitForIdle();
    client->WaitForIdle();
}

//This test is to verify the Static without Juniper header config
//create a route & add tunnelnh through test
//check that Mirror entry attached to vxlan tunnel nh
TEST_F(MirrorTableTest, StaticMirrorEntryAdd_8) {
    Ip4Address vhost_ip(agent_->router_id());
    //Add mirror entry pointing to same vhost IP
    Ip4Address remote_server = Ip4Address::from_string("8.8.8.8");
    MacAddress remote_vm_mac = MacAddress::FromString("00:00:08:08:08:08");
    std::string ana = analyzer + "r";
    TunnelType::TypeBmap bmap;
    bmap = 1 << TunnelType::VXLAN;
    AddResolveRoute(remote_server, 32);
    client->WaitForIdle();
    AddArp("8.8.8.8", "00:00:08:08:08:08", agent_->fabric_interface_name().c_str());
    client->WaitForIdle();
    CreateTunnelNH(agent_->fabric_vrf_name(), vhost_ip, remote_server, false, bmap);
    client->WaitForIdle();
    MirrorTable::AddMirrorEntry(ana, "vrf3", vhost_ip, 0x1, remote_server,
                                      0x2, 0 , 4, remote_vm_mac);
    client->WaitForIdle();

    MirrorEntryKey key(ana);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    const NextHop *mirr_nh = mirr_entry->GetNH();
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::TUNNEL);

    client->WaitForIdle();
    DeleteTunnelNH(agent_->fabric_vrf_name(), vhost_ip, remote_server, false, bmap);
    client->WaitForIdle();
    DelArp("8.8.8.8", "00:00:08:08:08:08", agent_->fabric_interface_name().c_str());
    client->WaitForIdle();
    DeleteRoute(agent_->local_peer(), agent_->fabric_vrf_name(), remote_server,
                32);
    client->WaitForIdle();
    MirrorTable::DelMirrorEntry(ana);
    client->WaitForIdle();

    mirr_entry = static_cast<const MirrorEntry *>
                 (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
    client->WaitForIdle();
}
//This test is to verify the Static without Juniper header config
//create a route in resolved state and check that Mirror entry creates tunnel nh
//and attaches to it.
TEST_F(MirrorTableTest, StaticMirrorEntryAdd_9) {
    Ip4Address vhost_ip(agent_->router_id());
    //Add mirror entry pointing to same vhost IP
    Ip4Address remote_server = Ip4Address::from_string("8.8.8.8");
    MacAddress remote_vm_mac = MacAddress::FromString("00:00:08:08:08:08");
    std::string ana = analyzer + "r";
    AddResolveRoute(remote_server, 16);

    client->WaitForIdle();

    MirrorTable::AddMirrorEntry(ana, "vrf3", vhost_ip, 0x1, remote_server,
                                      0x2, 2 , 4, remote_vm_mac);
    client->WaitForIdle();
    AddArp("8.8.8.8", "00:00:08:08:08:08", agent_->fabric_interface_name().c_str());
    client->WaitForIdle();
    MirrorEntryKey key(ana);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    const NextHop *mirr_nh = mirr_entry->GetNH();
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::TUNNEL);
    // change the Vxlan look for change reflected
    MirrorTable::AddMirrorEntry(ana, "vrf3", vhost_ip, 0x1, remote_server,
                                      0x2, 3 , 4, remote_vm_mac);
    client->WaitForIdle();
    MirrorEntryKey key_chg(ana);
    const MirrorEntry *mirr_entry_chg = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key_chg));
    EXPECT_TRUE(mirr_entry_chg != NULL);
    EXPECT_TRUE(mirr_entry_chg->GetVni() == 3);

    DelArp("8.8.8.8", "00:00:08:08:08:08", agent_->fabric_interface_name().c_str());
    client->WaitForIdle();
    DeleteRoute(agent_->local_peer(), agent_->fabric_vrf_name(), remote_server,
                16);
    DeleteRoute(agent_->local_peer(), agent_->fabric_vrf_name(), remote_server,
                32);
    client->WaitForIdle();
    MirrorTable::DelMirrorEntry(ana);
    client->WaitForIdle();

    mirr_entry = static_cast<const MirrorEntry *>
                 (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
    client->WaitForIdle();
}
// This test case is to move the mode from dynamic without juniper hdr
// to dynamic with juniper header after moving see that internally created
// VRF is deleted and check that MirrorNH points to discard NH
TEST_F(MirrorTableTest, StaticMirrorEntryAdd_10) {
    Ip4Address vhost_ip(agent_->router_id());
    Ip4Address remote_server = Ip4Address::from_string("1.1.1.1");
    Ip4Address remote_vm_ip4_2 = Ip4Address::from_string("2.2.2.11");
    //Add mirror entry pointing to same vhost IP
    std::string ana = analyzer + "r";
    MacAddress remote_vm_mac = MacAddress::FromString("00:00:01:01:01:11");
    TunnelType::TypeBmap bmap;
    bmap = 1 << TunnelType::VXLAN;
    client->WaitForIdle();

    MirrorTable::AddMirrorEntry(ana, "vrf3", vhost_ip, 0x1, remote_server,
                                      0x2, 0 , 2, remote_vm_mac);

    BridgeTunnelRouteAdd(bgp_peer_, "vrf3", bmap, remote_server,
                         1, remote_vm_mac, remote_vm_ip4_2, 32);
    client->WaitForIdle();

    MirrorEntryKey key(ana);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    const NextHop *mirr_nh = mirr_entry->GetNH();
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::TUNNEL);

    EvpnAgentRouteTable::DeleteReq(agent_->local_peer(), "vrf3", remote_vm_mac,
                                   remote_vm_ip4_2, 32, 0, NULL);
    client->WaitForIdle();
    MirrorTable::AddMirrorEntry(ana, "vrf3",
                                vhost_ip, 0x1, remote_server, 0x2);
    client->WaitForIdle();

    mirr_entry = static_cast<const MirrorEntry *>
        (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    mirr_nh = static_cast<const MirrorNH *>(mirr_entry->GetNH());
    mirr_nh = mirr_entry->GetNH();
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::DISCARD);
    client->WaitForIdle();
    MirrorTable::DelMirrorEntry(ana);
    client->WaitForIdle();
    mirr_entry = static_cast<const MirrorEntry *>
                 (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
    client->WaitForIdle();
}

TEST_F(MirrorTableTest, MirrorEntryAdd_nic_assisted_1) {
    std::stringstream str;
    str << analyzer;
    MirrorTable::AddMirrorEntry(analyzer, 15);
    client->WaitForIdle();
    MirrorEntryKey key(analyzer);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    MirrorTable::DelMirrorEntry(analyzer);
    client->WaitForIdle();
    mirr_entry = static_cast<const MirrorEntry *>
                 (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
}
// Add Mirror entry with Dynamic mode.
// now move it to nic assisted.
// Check that there is no reference holding.
// move from non nic assisted to nic assisted
TEST_F(MirrorTableTest, MirrorEntryAdd_nic_assisted_2) {
    Ip4Address vhost_ip(agent_->router_id());
    Ip4Address remote_server = Ip4Address::from_string("1.1.1.1");
    Ip4Address remote_vm_ip4_2 = Ip4Address::from_string("2.2.2.11");
    //Add mirror entry pointing to same vhost IP
    std::string ana = analyzer + "r";
    MacAddress remote_vm_mac = MacAddress::FromString("00:00:01:01:01:11");
    TunnelType::TypeBmap bmap;
    bmap = 1 << TunnelType::VXLAN;
    client->WaitForIdle();

    MirrorTable::AddMirrorEntry(ana, "vrf3", vhost_ip, 0x1, remote_server,
                                      0x2, 0 , 2, remote_vm_mac);

    BridgeTunnelRouteAdd(bgp_peer_, "vrf3", bmap, remote_server,
                         1, remote_vm_mac, remote_vm_ip4_2, 32);
    client->WaitForIdle();

    MirrorEntryKey key(ana);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
                                    (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    const NextHop *mirr_nh = mirr_entry->GetNH();
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::TUNNEL);

    EvpnAgentRouteTable::DeleteReq(agent_->local_peer(), "vrf3", remote_vm_mac,
                                   remote_vm_ip4_2, 32, 0, NULL);
    client->WaitForIdle();
    MirrorTable::AddMirrorEntry(ana, 15);

    client->WaitForIdle();

    mirr_entry = static_cast<const MirrorEntry *>
        (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    EXPECT_TRUE(mirr_entry->nic_assisted_mirroring_vlan() == 15);
    EXPECT_TRUE(mirr_entry->nic_assisted_mirroring() == true);
    client->WaitForIdle();
    MirrorTable::DelMirrorEntry(ana);
    client->WaitForIdle();
    mirr_entry = static_cast<const MirrorEntry *>
                 (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
    client->WaitForIdle();
}
// Add a test case to move from nic assisted to non nic
TEST_F(MirrorTableTest, MirrorEntryAdd_nic_assisted_3) {
    Ip4Address vhost_ip(agent_->router_id());
    Ip4Address remote_server = Ip4Address::from_string("1.1.1.1");
    Ip4Address remote_vm_ip4_2 = Ip4Address::from_string("2.2.2.11");
    //Add mirror entry pointing to same vhost IP
    std::string ana = analyzer + "r";
    MacAddress remote_vm_mac = MacAddress::FromString("00:00:01:01:01:11");
    TunnelType::TypeBmap bmap;
    bmap = 1 << TunnelType::VXLAN;
    client->WaitForIdle();
    MirrorEntryKey key(ana);
    MirrorTable::AddMirrorEntry(ana, 15);

    client->WaitForIdle();

    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
        (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    EXPECT_TRUE(mirr_entry->nic_assisted_mirroring_vlan() == 15);
    EXPECT_TRUE(mirr_entry->nic_assisted_mirroring() == true);


    MirrorTable::AddMirrorEntry(ana, "vrf3", vhost_ip, 0x1, remote_server,
                                      0x2, 0 , 2, remote_vm_mac);
    client->WaitForIdle();

    BridgeTunnelRouteAdd(bgp_peer_, "vrf3", bmap, remote_server,
                         1, remote_vm_mac, remote_vm_ip4_2, 32);
    client->WaitForIdle();

    mirr_entry = static_cast<const MirrorEntry *>
        (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    EXPECT_TRUE(mirr_entry->nic_assisted_mirroring() != true);
    const NextHop *mirr_nh = mirr_entry->GetNH();
    EXPECT_TRUE(mirr_nh->GetType() == NextHop::TUNNEL);

    EvpnAgentRouteTable::DeleteReq(agent_->local_peer(), "vrf3", remote_vm_mac,
                                   remote_vm_ip4_2, 32, 0, NULL);
    client->WaitForIdle();
    MirrorTable::DelMirrorEntry(ana);
    client->WaitForIdle();
    mirr_entry = static_cast<const MirrorEntry *>
                 (agent_->mirror_table()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    entry_count = 10;
    client = TestInit(init_file, ksync_init, true, false);
    eth_itf = Agent::GetInstance()->fabric_interface_name();

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
