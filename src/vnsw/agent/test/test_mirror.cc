/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

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
    MirrorTableTest(): count_(entry_count) {
    }

    virtual void SetUp() {
        client->Reset();
        PhysicalInterface::CreateReq(Agent::GetInstance()->GetInterfaceTable(),
                                eth_itf, Agent::GetInstance()->GetDefaultVrf());
        fabric_gw_ip_ = Ip4Address::from_string("10.1.1.254");
        uint16_t sport = 10000;
        unsigned long ip = 0x0a010102;
        for (int i = 0; i < count_; i++) {
            sport_.push_back(sport++);
            dport_.push_back(sport++);
            sip_.push_back(Ip4Address(ip++));
            dip_.push_back(Ip4Address(ip++));
        }
    }

    virtual void TearDown() {
    }

    void AddAllMirrorEntry() {
        for (int i = 0; i < count_; i++) {
            std::stringstream str;
            str << analyzer << i;
            MirrorTable::AddMirrorEntry(str.str(), Agent::GetInstance()->GetDefaultVrf(), sip_[i], sport_[i], dip_[i], dport_[i]);
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
            Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->DeleteReq(
                                                         Agent::GetInstance()->local_peer(),
                                                         Agent::GetInstance()->GetDefaultVrf(),
                                                         dip_[i], 32);
        }
        client->WaitForIdle();
    }

    bool MirrorEntryFind(int i) {
        std::stringstream str;
        str << analyzer << i;
        MirrorEntryKey key(str.str());
        return (Agent::GetInstance()->GetMirrorTable()->FindActiveEntry(&key) != NULL);
    }

    bool MirrorNHFind(int i) {
        MirrorNHKey key(Agent::GetInstance()->GetDefaultVrf(), sip_[i], sport_[i], dip_[i],
                        dport_[i]);
        return (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&key) != NULL);
    }

    MirrorEntry *GetMirrorEntry(int i) {
        std::stringstream str;
        str << analyzer << i;
        MirrorEntryKey key(str.str());
        return static_cast<MirrorEntry *>(Agent::GetInstance()->GetMirrorTable()->FindActiveEntry(&key));
    }

    int count_;
    std::vector<uint16_t> sport_;
    std::vector<uint16_t> dport_;
    std::vector<Ip4Address> sip_;
    std::vector<Ip4Address> dip_;
    Ip4Address fabric_gw_ip_;
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
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    mirror_list_req->HandleRequest();
    client->WaitForIdle();
    mirror_list_req->Release();
    client->WaitForIdle();

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
}

TEST_F(MirrorTableTest, MirrorEntryAddDel_3) {
    Ip4Address vhost_ip(Agent::GetInstance()->GetRouterId());
    //Add mirror entry pointing to same vhost IP
    std::stringstream str;
    str << analyzer;
    MirrorTable::AddMirrorEntry(analyzer, Agent::GetInstance()->GetDefaultVrf(), 
                                vhost_ip, 0x1, vhost_ip, 0x2);
    client->WaitForIdle();
    //Mirror NH would point to a route, whose nexthop would be RCV NH
    MirrorEntryKey key(analyzer);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
                                    (Agent::GetInstance()->GetMirrorTable()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry != NULL);
    const MirrorNH *mirr_nh = static_cast<const MirrorNH *>(mirr_entry->GetNH());
    //Make sure mirror nh internally points to receive router
    const NextHop *nh = mirr_nh->GetRt()->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::RECEIVE);

    MirrorTable::DelMirrorEntry(analyzer);
    client->WaitForIdle();
    mirr_entry = static_cast<const MirrorEntry *>
                 (Agent::GetInstance()->GetMirrorTable()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
}
 
TEST_F(MirrorTableTest, MirrorEntryAddDel_4) {
    Ip4Address vhost_ip(Agent::GetInstance()->GetRouterId());
    Ip4Address remote_server = Ip4Address::from_string("1.1.1.1");
    //Add mirror entry pointing to same vhost IP
    std::string ana = analyzer + "r";
    MirrorTable::AddMirrorEntry(ana, Agent::GetInstance()->GetDefaultVrf(),
                                vhost_ip, 0x1, remote_server, 0x2);
    client->WaitForIdle();
    //Mirror NH would point to a gateway route
    MirrorEntryKey key(ana);
    const MirrorEntry *mirr_entry = static_cast<const MirrorEntry *>
                                    (Agent::GetInstance()->GetMirrorTable()->FindActiveEntry(&key));
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
                 (Agent::GetInstance()->GetMirrorTable()->FindActiveEntry(&key));
    EXPECT_TRUE(mirr_entry == NULL);
    client->WaitForIdle();
    usleep(1000);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    entry_count = 10;
    client = TestInit(init_file, ksync_init, true, false);
    if (vm.count("config")) {
        eth_itf = Agent::GetInstance()->GetIpFabricItfName();
    } else {
        eth_itf = "eth0";
    }

    return RUN_ALL_TESTS();
}
