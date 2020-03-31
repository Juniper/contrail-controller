/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config_yaml.h"

#include <sstream>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "bgp/bgp_factory.h"
#include "testing/gunit.h"

using namespace std;
using boost::assign::list_of;

class BgpYamlConfigManagerTest : public ::testing::Test {
  protected:
    typedef map<string, const BgpInstanceConfig *> InstanceMap;
    typedef map<string, const BgpNeighborConfig *> NeighborMap;

    BgpYamlConfigManagerTest()
            : manager_(NULL),
              proto_(NULL) {
        BgpConfigManager::Observers obs;
        obs.instance = boost::bind(
            &BgpYamlConfigManagerTest::BgpInstanceObserver, this, _1, _2);
        obs.protocol = boost::bind(
            &BgpYamlConfigManagerTest::BgpProtocolObserver, this, _1, _2);
        obs.neighbor = boost::bind(
            &BgpYamlConfigManagerTest::BgpNeighborObserver, this, _1, _2);
        manager_.RegisterObservers(obs);
    }

    virtual void SetUp() {
    }
    virtual void TearDown() {
    }

    void BgpProtocolObserver(const BgpProtocolConfig *proto,
                             BgpConfigManager::EventType event) {
        proto_ = proto;
    }

    void BgpInstanceObserver(const BgpInstanceConfig *instance,
                             BgpConfigManager::EventType event) {
        switch (event) {
            case BgpConfigManager::CFG_ADD: {
                pair<InstanceMap::iterator, bool> result =
                        instances_.insert(
                            make_pair(instance->name(), instance));
                ASSERT_TRUE(result.second);
                break;
            }
            case BgpConfigManager::CFG_CHANGE: {
                InstanceMap::iterator loc =
                        instances_.find(instance->name());
                ASSERT_TRUE(loc != instances_.end());
                loc->second = instance;
                break;
            }
            case BgpConfigManager::CFG_DELETE: {
                ASSERT_EQ(1, instances_.erase(instance->name()));
                break;
            }
            default: {
                break;
            }
        }
    }
    void BgpNeighborObserver(const BgpNeighborConfig *neighbor,
                             BgpConfigManager::EventType event) {
        switch (event) {
            case BgpConfigManager::CFG_ADD: {
                pair<NeighborMap::iterator, bool> result =
                        neighbors_.insert(
                            make_pair(neighbor->name(), neighbor));
                ASSERT_TRUE(result.second);
                break;
            }
            case BgpConfigManager::CFG_CHANGE: {
                NeighborMap::iterator loc =
                        neighbors_.find(neighbor->name());
                ASSERT_TRUE(loc != neighbors_.end());
                loc->second = neighbor;
                break;
            }
            case BgpConfigManager::CFG_DELETE: {
                ASSERT_EQ(1, neighbors_.erase(neighbor->name()));
                break;
            }
            default: {
                break;
            }
        }

    }

    const BgpNeighborConfig *FindNeighbor(const string &name) {
        NeighborMap::iterator loc = neighbors_.find(name);
        if (loc != neighbors_.end()) {
            return loc->second;
        }
        return NULL;
    }

    BgpYamlConfigManager manager_;
    const BgpProtocolConfig *proto_;
    NeighborMap neighbors_;
    InstanceMap instances_;
};

TEST_F(BgpYamlConfigManagerTest, 2Neighbors) {
    string config = "---\n"
            "bgp:\n"
            "  neighbors:\n"
            "    192.168.0.1:\n"
            "      peer-as: 10458\n"
            "      timers:\n"
            "        hold-time: 60\n"
            "      port: 8179\n"
            "      identifier: 192.168.0.1\n"
            "      address-families:\n"
            "        - inet\n"
            "        - inet-vpn\n"
            "    192.168.0.2:\n"
            "      peer-as: 64512\n";
    istringstream input(config);
    string error_msg;
    EXPECT_TRUE(manager_.Parse(&input, &error_msg));
    EXPECT_EQ(2, neighbors_.size());
    const BgpNeighborConfig *n1 = FindNeighbor("192.168.0.1");
    EXPECT_TRUE(n1 != NULL);
    if (n1 != NULL) {
        EXPECT_EQ(10458, n1->peer_as());
        EXPECT_EQ(60, n1->hold_time());
        EXPECT_EQ(8179, n1->port());
        EXPECT_EQ(htonl(Ip4Address::from_string("192.168.0.1").to_ulong()),
                  n1->peer_identifier());
        EXPECT_EQ({"inet", "inet-vpn"}, n1->GetAddressFamilies());
    }
}

TEST_F(BgpYamlConfigManagerTest, BadHoldTime1) {
    string config = "---\n"
            "bgp:\n"
            "  neighbors:\n"
            "    192.168.0.1:\n"
            "      peer-as: 10458\n"
            "      timers:\n"
            "         hold-time: foo\n";
    istringstream input(config);
    string error_msg;
    EXPECT_FALSE(manager_.Parse(&input, &error_msg));
    EXPECT_EQ("Invalid hold-time value: not an integer", error_msg);
}

TEST_F(BgpYamlConfigManagerTest, BadHoldTime2) {
    string config = "---\n"
            "bgp:\n"
            "  neighbors:\n"
            "    192.168.0.1:\n"
            "      peer-as: 10458\n"
            "      timers:\n"
            "         hold-time: 60000\n";
    istringstream input(config);
    string error_msg;
    EXPECT_FALSE(manager_.Parse(&input, &error_msg));
    EXPECT_EQ("Invalid hold-time value (out of range): 60000", error_msg);
}

TEST_F(BgpYamlConfigManagerTest, BadPortValue) {
    string config = "---\n"
            "bgp:\n"
            "  neighbors:\n"
            "    192.168.0.1:\n"
            "      port: 1000000\n";
    istringstream input(config);
    string error_msg;
    EXPECT_FALSE(manager_.Parse(&input, &error_msg));
    EXPECT_EQ("Invalid port value (out of range): 1000000", error_msg);
}

TEST_F(BgpYamlConfigManagerTest, InvalidAF) {
    string config = "---\n"
            "bgp:\n"
            "  neighbors:\n"
            "    192.168.0.1:\n"
            "      address-families:\n"
            "        - foo\n"
            "        - inet-vpn\n";
    istringstream input(config);
    string error_msg;
    EXPECT_FALSE(manager_.Parse(&input, &error_msg));
    EXPECT_EQ("Invalid address family value: foo", error_msg);
}

TEST_F(BgpYamlConfigManagerTest, ResolveIBGP_AS) {
    string config = "---\n"
            "bgp:\n"
            "  autonomous-system: 10458\n"
            "  neighbors:\n"
            "    192.168.0.1:\n"
            "      peer-type: ibgp\n";
    istringstream input(config);
    string error_msg;
    EXPECT_TRUE(manager_.Parse(&input, &error_msg));
    const BgpNeighborConfig *n1 = FindNeighbor("192.168.0.1");
    EXPECT_TRUE(n1 != NULL);
    if (n1 != NULL) {
        EXPECT_EQ(10458, n1->peer_as());
    }
}

static bool StartsWith(const string &lhs, const string &rhs) {
    return boost::algorithm::starts_with(lhs, rhs);
}
TEST_F(BgpYamlConfigManagerTest, ResolveIBGP_Mismatch) {
    string config = "---\n"
            "bgp:\n"
            "  autonomous-system: 10458\n"
            "  neighbors:\n"
            "    192.168.0.1:\n"
            "      peer-as: 64512\n"
            "      peer-type: ibgp\n";
    istringstream input(config);
    string error_msg;
    EXPECT_FALSE(manager_.Parse(&input, &error_msg));
    EXPECT_PRED2(StartsWith, error_msg,
                 "Neighbor 192.168.0.1: autonomous-system mismatch");
}

TEST_F(BgpYamlConfigManagerTest, ResolveEBGP_Mismatch) {
    string config = "---\n"
            "bgp:\n"
            "  autonomous-system: 10458\n"
            "  neighbors:\n"
            "    192.168.0.1:\n"
            "      peer-as: 10458\n"
            "      peer-type: ebgp\n";
    istringstream input(config);
    string error_msg;
    EXPECT_FALSE(manager_.Parse(&input, &error_msg));
    EXPECT_PRED2(StartsWith, error_msg,
                 "Neighbor 192.168.0.1: EBGP peer");
}

TEST_F(BgpYamlConfigManagerTest, ResolvePeerType) {
    string config = "---\n"
            "bgp:\n"
            "  autonomous-system: 64512\n"
            "  neighbors:\n"
            "    192.168.0.1:\n"
            "      peer-as: 10458\n";
    istringstream input(config);
    string error_msg;
    EXPECT_TRUE(manager_.Parse(&input, &error_msg));
    const BgpNeighborConfig *n1 = FindNeighbor("192.168.0.1");
    EXPECT_TRUE(n1 != NULL);
    if (n1 != NULL) {
        EXPECT_EQ(BgpNeighborConfig::EBGP, n1->peer_type());
    }
}

TEST_F(BgpYamlConfigManagerTest, 2Groups) {
    string config = "---\n"
            "bgp:\n"
            "  peer-groups:\n"
            "    one:\n"
            "      peer-as: 10458\n"
            "      neighbors:\n"
            "        192.168.0.1:\n"
            "          identifier: 192.168.0.1\n"
            "        192.168.0.2:\n"
            "          identifier: 192.168.0.2\n"
            "    two:\n"
            "      peer-as: 109\n"
            "      neighbors:\n"
            "        192.168.0.3:\n"
            "          identifier: 192.168.0.3\n"
            "        192.168.0.4:\n"
            "          identifier: 192.168.0.4\n";
    istringstream input(config);
    string error_msg;
    EXPECT_TRUE(manager_.Parse(&input, &error_msg));
    EXPECT_EQ(4, neighbors_.size());
    const BgpNeighborConfig *n1 = FindNeighbor("192.168.0.1");
    EXPECT_TRUE(n1 != NULL);
    if (n1 != NULL) {
        EXPECT_EQ("one", n1->group_name());
        EXPECT_EQ(10458, n1->peer_as());
        EXPECT_EQ(BgpNeighborConfig::EBGP, n1->peer_type());
    }
    const BgpNeighborConfig *n3 = FindNeighbor("192.168.0.3");
    if (n3 != NULL) {
        EXPECT_EQ("two", n3->group_name());
        EXPECT_EQ(109, n3->peer_as());
        EXPECT_EQ(BgpNeighborConfig::EBGP, n3->peer_type());
    }
}

TEST_F(BgpYamlConfigManagerTest, GroupResolvePeerType) {
    string config = "---\n"
            "bgp:\n"
            "  autonomous-system: 64512\n"
            "  peer-groups:\n"
            "    one:\n"
            "      peer-type: ibgp\n"
            "      neighbors:\n"
            "        192.168.0.1:\n"
            "          identifier: 192.168.0.1\n"
            "        192.168.0.2:\n"
            "          identifier: 192.168.0.2\n"
            "  neighbors:\n"
            "    192.168.0.3:\n"
            "      peer-as: 10458\n";
    istringstream input(config);
    string error_msg;
    EXPECT_TRUE(manager_.Parse(&input, &error_msg));
    EXPECT_EQ(3, neighbors_.size());
    const BgpNeighborConfig *n1 = FindNeighbor("192.168.0.1");
    EXPECT_TRUE(n1 != NULL);
    if (n1 != NULL) {
        EXPECT_EQ("one", n1->group_name());
        EXPECT_EQ(64512, n1->peer_as());
    }
    const BgpNeighborConfig *n3 = FindNeighbor("192.168.0.3");
    if (n3 != NULL) {
        EXPECT_EQ("", n3->group_name());
        EXPECT_EQ(10458, n3->peer_as());
    }
}

void GlobalSetUp() {
    LoggingInit();
    BgpObjectFactory::Register<BgpConfigManager>(
        boost::factory<BgpYamlConfigManager *>());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    GlobalSetUp();
    int error = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return error;
}
