/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */


#include <boost/assign/list_of.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_server.h"
#include "bgp/routing-policy/routing_policy_match.h"
#include "bgp/rtarget/rtarget_address.h"
#include "control-node/control_node.h"
#include "net/community_type.h"

using boost::assign::list_of;
using std::find;
using std::string;

class PeerMock : public IPeer {
public:
    PeerMock(bool is_xmpp, const string &address_str)
        : is_xmpp_(is_xmpp) {
        boost::system::error_code ec;
        address_ = Ip4Address::from_string(address_str, ec);
        assert(ec.value() == 0);
        address_str_ = address_.to_string();
    }
    virtual ~PeerMock() { }
    virtual const std::string &ToString() const { return address_str_; }
    virtual const std::string &ToUVEKey() const { return address_str_; }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) { return true; }
    virtual BgpServer *server() { return NULL; }
    virtual BgpServer *server() const { return NULL; }
    virtual IPeerClose *peer_close() { return NULL; }
    virtual IPeerClose *peer_close() const { return NULL; }
    virtual void UpdateCloseRouteStats(Address::Family family,
        const BgpPath *old_path, uint32_t path_flags) const {
    }
    virtual IPeerDebugStats *peer_stats() { return NULL; }
    virtual const IPeerDebugStats *peer_stats() const { return NULL; }
    virtual bool IsReady() const { return true; }
    virtual bool IsXmppPeer() const { return is_xmpp_; }
    virtual bool IsRegistrationRequired() const { return false; }
    virtual void Close(bool graceful) { }
    BgpProto::BgpPeerType PeerType() const {
        return is_xmpp_ ? BgpProto::XMPP : BgpProto::EBGP;
    }
    virtual uint32_t bgp_identifier() const {
        return htonl(address_.to_ulong());
    }
    virtual const std::string GetStateName() const { return ""; }
    virtual void UpdateTotalPathCount(int count) const { }
    virtual int GetTotalPathCount() const { return 0; }
    virtual bool IsAs4Supported() const { return false; }
    virtual void UpdatePrimaryPathCount(int count,
        Address::Family family) const { }
    virtual int GetPrimaryPathCount() const { return 0; }
    virtual void ProcessPathTunnelEncapsulation(const BgpPath *path,
        BgpAttr *attr, ExtCommunityDB *extcomm_db, const BgpTable *table)
        const {
    }
    virtual const std::vector<std::string> GetDefaultTunnelEncap(
        Address::Family family) const {
        return std::vector<std::string>();
    }
    virtual void MembershipRequestCallback(BgpTable *table) { }
    virtual bool MembershipPathCallback(DBTablePartBase *tpart,
        BgpRoute *route, BgpPath *path) { return false; }
    virtual bool CanUseMembershipManager() const { return true; }
    virtual bool IsInGRTimerWaitState() const { return false; }

private:
    bool is_xmpp_;
    Ip4Address address_;
    std::string address_str_;
};

class MatchExtCommunityTest : public ::testing::Test {
protected:
    MatchExtCommunityTest() : server_(&evm_), attr_db_(server_.attr_db()) {
    }

    void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    EventManager evm_;
    BgpServer server_;
    BgpAttrDB *attr_db_;
};

TEST_F(MatchExtCommunityTest, MatchAny1) {
    vector<string> communities = list_of("target:23:11")("target:43:11");
    MatchExtCommunity match(communities, false);

    ExtCommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    RouteTarget val0 = RouteTarget::FromString("target:23:11");
    RouteTarget val1 = RouteTarget::FromString("target:43:11");
    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue()-1);
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val0.GetExtCommunityValue()+1);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue()-1);
    comm_spec.communities.push_back(val0.GetExtCommunityValue()+1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val1.GetExtCommunityValue()-1);
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue()+1);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val1.GetExtCommunityValue()-1);
    comm_spec.communities.push_back(val1.GetExtCommunityValue()+1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));
}

TEST_F(MatchExtCommunityTest, MatchAny2) {
    vector<string> communities = list_of("target:33:.*")("target:53:.*");
    MatchExtCommunity match(communities, false);

    RouteTarget val0 = RouteTarget::FromString("target:33:11");
    RouteTarget val1 = RouteTarget::FromString("target:53:11");
    ExtCommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue() - 0x100000000);
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val0.GetExtCommunityValue() + 0x100000000);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue() - 0x100000000);
    comm_spec.communities.push_back(val0.GetExtCommunityValue() + 0x100000000);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val1.GetExtCommunityValue() - 0x100000000);
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue() + 0x100000000);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val1.GetExtCommunityValue() - 0x100000000);
    comm_spec.communities.push_back(val1.GetExtCommunityValue() + 0x100000000);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));
}

TEST_F(MatchExtCommunityTest, MatchAny3) {
    vector<string> communities =
        list_of("target:23:11")("target:33:.*")("target:43:11")("target:53:.*");
    MatchExtCommunity match(communities, false);

    RouteTarget val0 = RouteTarget::FromString("target:43:11");
    RouteTarget val1 = RouteTarget::FromString("target:53:11");
    ExtCommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue() - 0x100000000);
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val0.GetExtCommunityValue() + 0x100000000);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue() - 0x100000000);
    comm_spec.communities.push_back(val0.GetExtCommunityValue() + 0x100000000);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val1.GetExtCommunityValue() - 0x100000000);
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue() + 0x100000000);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val1.GetExtCommunityValue() - 0x100000000);
    comm_spec.communities.push_back(val1.GetExtCommunityValue() + 0x100000000);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));
}

TEST_F(MatchExtCommunityTest, MatchAll1) {
    vector<string> communities = list_of("target:23:11")("target:43:11");
    MatchExtCommunity match(communities, true);

    ExtCommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    RouteTarget val0 = RouteTarget::FromString("target:23:11");
    RouteTarget val1 = RouteTarget::FromString("target:43:11");
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue()-1);
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue()+1);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue()+1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue()-1);
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue()-1);
    comm_spec.communities.push_back(val1.GetExtCommunityValue()+1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));
}

TEST_F(MatchExtCommunityTest, MatchAll2) {
    vector<string> communities = list_of("target:33:.*")("target:53:.*");
    MatchExtCommunity match(communities, true);

    ExtCommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    RouteTarget val0 = RouteTarget::FromString("target:33:11");
    RouteTarget val1 = RouteTarget::FromString("target:53:11");
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val1.GetExtCommunityValue() - 0x100000000);
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val0.GetExtCommunityValue() + 0x100000000);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue() - 0x100000000);
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val0.GetExtCommunityValue() + 0x100000000);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val1.GetExtCommunityValue() - 0x100000000);
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue() + 0x100000000);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));
}

TEST_F(MatchExtCommunityTest, MatchAll3) {
    vector<string> communities = list_of("target:33:.*")("target:43:11")
                                        ("target:53:.*");
    MatchExtCommunity match(communities, true);

    RouteTarget val0 = RouteTarget::FromString("target:33:11");
    RouteTarget val1 = RouteTarget::FromString("target:43:11");
    RouteTarget val2 = RouteTarget::FromString("target:53:11");
    ExtCommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue());
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val2.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue() - 0x100000000);
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue() + 0x100000000);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val1.GetExtCommunityValue() - 1);
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue() + 1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue() - 1);
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue() + 1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val1.GetExtCommunityValue() - 1);
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue() + 1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue() - 1);
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue() + 1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));
}

TEST_F(MatchExtCommunityTest, MatchAll4) {
    vector<string> communities = list_of("target:33:.*")("target:33:11")
                                        ("target:53:.*");
    MatchExtCommunity match(communities, true);

    RouteTarget val0 = RouteTarget::FromString("target:33:11");
    RouteTarget val1 = RouteTarget::FromString("target:43:11");
    RouteTarget val2 = RouteTarget::FromString("target:53:11");
    ExtCommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue());
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue() - 1);
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue() + 1);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue());
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val2.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(val0.GetExtCommunityValue() - 0x100000000);
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    comm_spec.communities.push_back(val1.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue());
    comm_spec.communities.push_back(val2.GetExtCommunityValue() + 0x100000000);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));
}

//
// community values from hex.
//
TEST_F(MatchExtCommunityTest, FromHexValues) {
    vector<string> communities = list_of(".*200170000000b")("0708000000bc5bf7");
    MatchExtCommunity match(communities, false);
    MatchExtCommunity match2(communities, true);
    // 0708000000bc5bf7 will match to hex string, but .*200170000000b will be
    // treated as regex string hence regex_strings().size() should be 1 now.
    // This is because of fix done for bug CEM-9843
    EXPECT_EQ(1, match.regex_strings().size());
    EXPECT_EQ(1, match2.regex_strings().size());

    ExtCommunitySpec comm_spec;
    BgpAttrSpec spec;
    RouteTarget val0 = RouteTarget::FromString("target:23:11");
    comm_spec.communities.push_back(val0.GetExtCommunityValue());
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);

    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));
    EXPECT_FALSE(match2.Match(NULL, NULL, attr.get()));
}

class MatchCommunityTest : public ::testing::Test {
protected:
    MatchCommunityTest() : server_(&evm_), attr_db_(server_.attr_db()) {
    }

    void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    EventManager evm_;
    BgpServer server_;
    BgpAttrDB *attr_db_;
};

TEST_F(MatchCommunityTest, MatchAll1) {
    vector<string> communities = list_of("23:11")("43:11");
    MatchCommunity match(communities, true);

    uint32_t value0 = CommunityType::CommunityFromString(communities[0]);
    uint32_t value1 = CommunityType::CommunityFromString(communities[1]);
    CommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value1);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 1);
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value1 + 1);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value1 + 1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 1);
    comm_spec.communities.push_back(value1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 1);
    comm_spec.communities.push_back(value1 + 1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));
}

TEST_F(MatchCommunityTest, MatchAll2) {
    vector<string> communities = list_of("33:.*")("53:.*");
    MatchCommunity match(communities, true);

    uint32_t value0 = CommunityType::CommunityFromString("33:11");
    uint32_t value1 = CommunityType::CommunityFromString("53:11");
    CommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value1);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value0);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 65536);
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value1 + 65536);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 65536);
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value0 + 65536);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value1 - 65536);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value1 + 65536);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));
}

TEST_F(MatchCommunityTest, MatchAll3) {
    vector<string> communities = list_of("33:.*")("43:11")("53:.*");
    MatchCommunity match(communities, true);

    uint32_t value0 = CommunityType::CommunityFromString("33:11");
    uint32_t value1 = CommunityType::CommunityFromString("43:11");
    uint32_t value2 = CommunityType::CommunityFromString("53:11");
    CommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value2);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value2);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value0);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 65536);
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value2);
    comm_spec.communities.push_back(value2 + 65536);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value1 - 1);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value1 + 1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 1);
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value2);
    comm_spec.communities.push_back(value2 + 1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value1 - 1);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value2);
    comm_spec.communities.push_back(value2 + 1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 1);
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value2);
    comm_spec.communities.push_back(value2 + 1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));
}

TEST_F(MatchCommunityTest, MatchAll4) {
    vector<string> communities = list_of("33:.*")("33:11")("53:.*");
    MatchCommunity match(communities, true);

    uint32_t value0 = CommunityType::CommunityFromString("33:11");
    uint32_t value1 = CommunityType::CommunityFromString("43:11");
    uint32_t value2 = CommunityType::CommunityFromString("53:11");
    CommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value2);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 1);
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value2);
    comm_spec.communities.push_back(value2 + 1);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value2);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value2);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value0);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 65536);
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value2);
    comm_spec.communities.push_back(value2 + 65536);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));
}

TEST_F(MatchCommunityTest, MatchAny1) {
    vector<string> communities = list_of("23:11")("43:11");
    MatchCommunity match(communities, false);

    uint32_t value0 = CommunityType::CommunityFromString(communities[0]);
    uint32_t value1 = CommunityType::CommunityFromString(communities[1]);
    CommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 + 1);
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value0 - 1);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 + 1);
    comm_spec.communities.push_back(value0 - 1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value1 + 1);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value1 - 1);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value1 + 1);
    comm_spec.communities.push_back(value1 - 1);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));
}

TEST_F(MatchCommunityTest, MatchAny2) {
    vector<string> communities = list_of("33:.*")("53:.*");
    MatchCommunity match(communities, false);

    uint32_t value0 = CommunityType::CommunityFromString("33:11");
    uint32_t value1 = CommunityType::CommunityFromString("53:11");
    CommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 65536);
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value0 + 65536);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 65536);
    comm_spec.communities.push_back(value0 + 65536);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value1 - 65536);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value1 + 65536);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value1 - 65536);
    comm_spec.communities.push_back(value1 + 65536);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));
}

TEST_F(MatchCommunityTest, MatchAny3) {
    vector<string> communities = list_of("23:11")("33:.*")("43:11")("53:.*");
    MatchCommunity match(communities, false);

    uint32_t value0 = CommunityType::CommunityFromString("43:11");
    uint32_t value1 = CommunityType::CommunityFromString("53:11");
    CommunitySpec comm_spec;
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 65536);
    comm_spec.communities.push_back(value0);
    comm_spec.communities.push_back(value0 + 65536);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value0 - 65536);
    comm_spec.communities.push_back(value0 + 65536);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value1 - 65536);
    comm_spec.communities.push_back(value1);
    comm_spec.communities.push_back(value1 + 65536);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, NULL, attr.get()));

    comm_spec.communities.clear();
    comm_spec.communities.push_back(value1 - 65536);
    comm_spec.communities.push_back(value1 + 65536);
    attr = attr_db_->Locate(spec);
    EXPECT_FALSE(match.Match(NULL, NULL, attr.get()));
}

// Parameterize match-all vs. match-any in MatchCommunity.
class MatchCommunityParamTest:
    public MatchCommunityTest,
    public ::testing::WithParamInterface<bool> {
};

//
// Fixed community values only.
//
TEST_P(MatchCommunityParamTest, Constructor1a) {
    vector<string> communities = list_of("23:11")("43:11");
    MatchCommunity match(communities, GetParam());
    EXPECT_EQ(GetParam(), match.match_all());
    EXPECT_EQ(2U, match.communities().size());
    EXPECT_EQ(0U, match.regex_strings().size());
    EXPECT_EQ(0U, match.regexs().size());

    uint32_t value0 = CommunityType::CommunityFromString(communities[0]);
    EXPECT_TRUE(match.communities().find(value0) != match.communities().end());
    uint32_t value1 = CommunityType::CommunityFromString(communities[1]);
    EXPECT_TRUE(match.communities().find(value1) != match.communities().end());
}

//
// Fixed community values only, including duplicates.
//
TEST_P(MatchCommunityParamTest, Constructor1b) {
    vector<string> communities = list_of("23:11")("43:11")("23:11")("43:11");
    MatchCommunity match(communities, GetParam());
    EXPECT_EQ(GetParam(), match.match_all());
    EXPECT_EQ(2U, match.communities().size());
    EXPECT_EQ(0U, match.regex_strings().size());
    EXPECT_EQ(0U, match.regexs().size());

    uint32_t value0 = CommunityType::CommunityFromString(communities[0]);
    EXPECT_TRUE(match.communities().find(value0) != match.communities().end());
    uint32_t value1 = CommunityType::CommunityFromString(communities[1]);
    EXPECT_TRUE(match.communities().find(value1) != match.communities().end());
}

//
// Community regular expressions only.
//
TEST_P(MatchCommunityParamTest, Constructor2a) {
    vector<string> communities = list_of("33:.*")("53:.*");
    MatchCommunity match(communities, GetParam());
    EXPECT_EQ(GetParam(), match.match_all());
    EXPECT_EQ(0U, match.communities().size());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[0]) != match.regex_strings().end());
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[1]) != match.regex_strings().end());
}

//
// Community regular expressions only, including duplicates.
//
TEST_P(MatchCommunityParamTest, Constructor2b) {
    vector<string> communities = list_of("33:.*")("53:.*")("33:.*")("53:.*");
    MatchCommunity match(communities, GetParam());
    EXPECT_EQ(GetParam(), match.match_all());
    EXPECT_EQ(0U, match.communities().size());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[0]) != match.regex_strings().end());
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[1]) != match.regex_strings().end());
}

//
// Fixed community values and regular expressions.
//
TEST_P(MatchCommunityParamTest, Constructor3a) {
    vector<string> communities =
        list_of("23:11")("33:.*")("43:11")("53:.*");
    MatchCommunity match(communities, GetParam());
    EXPECT_EQ(GetParam(), match.match_all());
    EXPECT_EQ(2U, match.communities().size());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());

    uint32_t value0 = CommunityType::CommunityFromString(communities[0]);
    EXPECT_TRUE(match.communities().find(value0) != match.communities().end());
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[1]) != match.regex_strings().end());
    uint32_t value2 = CommunityType::CommunityFromString(communities[2]);
    EXPECT_TRUE(match.communities().find(value2) != match.communities().end());
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[3]) != match.regex_strings().end());
}

//
// Fixed community values and regular expressions, including duplicates.
//
TEST_P(MatchCommunityParamTest, Constructor3b) {
    vector<string> communities =
        list_of("23:11")("33:.*")("43:11")("53:.*")("23:11")("33:.*");
    MatchCommunity match(communities, GetParam());
    EXPECT_EQ(GetParam(), match.match_all());
    EXPECT_EQ(2U, match.communities().size());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());

    uint32_t value0 = CommunityType::CommunityFromString(communities[0]);
    EXPECT_TRUE(match.communities().find(value0) != match.communities().end());
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[1]) != match.regex_strings().end());
    uint32_t value2 = CommunityType::CommunityFromString(communities[2]);
    EXPECT_TRUE(match.communities().find(value2) != match.communities().end());
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[3]) != match.regex_strings().end());
}

//
// Fixed community values only.
//
TEST_P(MatchCommunityParamTest, ToString1) {
    vector<string> communities = list_of("23:11")("43:11");
    MatchCommunity match(communities, GetParam());
    EXPECT_EQ(2U, match.communities().size());
    if (GetParam()) {
        EXPECT_EQ("community (all) [ 23:11,43:11 ]", match.ToString());
    } else {
        EXPECT_EQ("community (any) [ 23:11,43:11 ]", match.ToString());
    }
}

//
// Community regular expressions only.
//
TEST_P(MatchCommunityParamTest, ToString2) {
    vector<string> communities = list_of("33:.*")("53:.*");
    MatchCommunity match(communities, GetParam());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());
    if (GetParam()) {
        EXPECT_EQ("community (all) [ 33:.*,53:.* ]", match.ToString());
    } else {
        EXPECT_EQ("community (any) [ 33:.*,53:.* ]", match.ToString());
    }
}

//
// Fixed community values and regular expressions.
//
TEST_P(MatchCommunityParamTest, ToString3) {
    vector<string> communities = list_of("33:.*")("43:11")("53:.*");
    MatchCommunity match(communities, GetParam());
    EXPECT_EQ(1U, match.communities().size());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());
    if (GetParam()) {
        EXPECT_EQ("community (all) [ 43:11,33:.*,53:.* ]", match.ToString());
    } else {
        EXPECT_EQ("community (any) [ 43:11,33:.*,53:.* ]", match.ToString());
    }
}

//
// Invalid Community regular expressions.
//
TEST_P(MatchCommunityParamTest, InvalidRegex) {
    vector<string> communities = list_of("33:[.*")("53:.*]");
    MatchCommunity match(communities, GetParam());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());
    if (GetParam()) {
        EXPECT_NE("community (all) [ 33:.*,53:.* ]", match.ToString());
    } else {
        EXPECT_NE("community (any) [ 33:.*,53:.* ]", match.ToString());
    }
}

//
// Fixed community values only.
// One value is same and one is different.
//
TEST_P(MatchCommunityParamTest, IsEqual1a) {
    vector<string> communities1 = list_of("23:11")("43:11");
    vector<string> communities2 = list_of("23:11")("43:12");
    MatchCommunity match1(communities1, GetParam());
    MatchCommunity match2(communities2, GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Fixed community values only.
// One value is same and one is different.
//
TEST_P(MatchCommunityParamTest, IsEqual1b) {
    vector<string> communities1 = list_of("23:11")("43:11");
    vector<string> communities2 = list_of("23:12")("43:11");
    MatchCommunity match1(communities1, GetParam());
    MatchCommunity match2(communities2, GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Fixed community values only.
// One list is a subset of the other.
//
TEST_P(MatchCommunityParamTest, IsEqual1c) {
    vector<string> communities1 = list_of("23:11")("43:11")("63:11");
    vector<string> communities2 = list_of("23:11")("43:11");
    MatchCommunity match1(communities1, GetParam());
    MatchCommunity match2(communities2, GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Fixed community values only.
//
TEST_P(MatchCommunityParamTest, IsEqual1d) {
    vector<string> communities1 = list_of("23:11")("43:11");
    vector<string> communities2 = list_of("43:11")("23:11");
    MatchCommunity match1(communities1, GetParam());
    MatchCommunity match2(communities2, GetParam());
    EXPECT_TRUE(match1.IsEqual(match2));
    EXPECT_TRUE(match2.IsEqual(match1));
}

//
// Fixed community values only.
// Values of match-all are different.
//
TEST_P(MatchCommunityParamTest, IsEqual1e) {
    vector<string> communities1 = list_of("23:11")("43:11");
    vector<string> communities2 = list_of("43:11")("23:11");
    MatchCommunity match1(communities1, GetParam());
    MatchCommunity match2(communities2, !GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Community regular expressions only.
// One value is same and one is different.
//
TEST_P(MatchCommunityParamTest, IsEqual2a) {
    vector<string> communities1 = list_of("23:.*")("43:.*");
    vector<string> communities2 = list_of("23:.*")("44:.*");
    MatchCommunity match1(communities1, GetParam());
    MatchCommunity match2(communities2, GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Community regular expressions only.
// One value is same and one is different.
//
TEST_P(MatchCommunityParamTest, IsEqual2b) {
    vector<string> communities1 = list_of("23:.*")("43:.*");
    vector<string> communities2 = list_of("24:.*")("43:.*");
    MatchCommunity match1(communities1, GetParam());
    MatchCommunity match2(communities2, GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Community regular expressions only.
// One list is a subset of the other.
//
TEST_P(MatchCommunityParamTest, IsEqual2c) {
    vector<string> communities1 = list_of("23:.*")("43:.*")("63:.*");
    vector<string> communities2 = list_of("23:.*")("43:.*");
    MatchCommunity match1(communities1, GetParam());
    MatchCommunity match2(communities2, GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Community regular expressions only.
//
TEST_P(MatchCommunityParamTest, IsEqual2d) {
    vector<string> communities1 = list_of("23:.*")("43:.*");
    vector<string> communities2 = list_of("43:.*")("23:.*");
    MatchCommunity match1(communities1, GetParam());
    MatchCommunity match2(communities2, GetParam());
    EXPECT_TRUE(match1.IsEqual(match2));
    EXPECT_TRUE(match2.IsEqual(match1));
}

//
// Community regular expressions only.
// Values of match-all are different.
//
TEST_P(MatchCommunityParamTest, IsEqual2e) {
    vector<string> communities1 = list_of("23:.*")("43:.*");
    vector<string> communities2 = list_of("43:.*")("23:.*");
    MatchCommunity match1(communities1, GetParam());
    MatchCommunity match2(communities2, !GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

INSTANTIATE_TEST_CASE_P(Instance, MatchCommunityParamTest, ::testing::Bool());

// Parameterize match-all vs. match-any in MatchExtCommunity.
class MatchExtCommunityParamTest:
    public MatchExtCommunityTest,
    public ::testing::WithParamInterface<bool> {
};

//
// Fixed community values only.
//
TEST_P(MatchExtCommunityParamTest, Constructor1a) {
    vector<string> communities = list_of("target:23:11")("target:43:11");
    MatchExtCommunity match(communities, GetParam());
    EXPECT_EQ(GetParam(), match.match_all());
    EXPECT_EQ(2U, match.communities().size());
    EXPECT_EQ(0U, match.regex_strings().size());
    EXPECT_EQ(0U, match.regexs().size());

    RouteTarget val0 = RouteTarget::FromString(communities[0]);
    EXPECT_TRUE(match.Find(val0.GetExtCommunity()));
    RouteTarget val1 = RouteTarget::FromString(communities[1]);
    EXPECT_TRUE(match.Find(val1.GetExtCommunity()));
}

//
// Fixed community values only, including duplicates.
//
TEST_P(MatchExtCommunityParamTest, Constructor1b) {
    vector<string> communities =
        list_of("target:23:11")("target:43:11")("target:23:11")("target:43:11");
    MatchExtCommunity match(communities, GetParam());
    EXPECT_EQ(GetParam(), match.match_all());
    EXPECT_EQ(2U, match.communities().size());
    EXPECT_EQ(0U, match.regex_strings().size());
    EXPECT_EQ(0U, match.regexs().size());

    RouteTarget val0 = RouteTarget::FromString(communities[0]);
    EXPECT_TRUE(match.Find(val0.GetExtCommunity()));
    RouteTarget val1 = RouteTarget::FromString(communities[1]);
    EXPECT_TRUE(match.Find(val1.GetExtCommunity()));
}

//
// Community regular expressions only.
//
TEST_P(MatchExtCommunityParamTest, Constructor2a) {
    vector<string> communities = list_of("target:33:.*")("target:53:.*");
    MatchExtCommunity match(communities, GetParam());
    EXPECT_EQ(GetParam(), match.match_all());
    EXPECT_EQ(0U, match.communities().size());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[0]) != match.regex_strings().end());
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[1]) != match.regex_strings().end());
}

//
// Community regular expressions only, including duplicates.
//
TEST_P(MatchExtCommunityParamTest, Constructor2b) {
    vector<string> communities =
        list_of("target:33:.*")("target:53:.*")("target:33:.*")("target:53:.*");
    MatchExtCommunity match(communities, GetParam());
    EXPECT_EQ(GetParam(), match.match_all());
    EXPECT_EQ(0U, match.communities().size());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[0]) != match.regex_strings().end());
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[1]) != match.regex_strings().end());
}

//
// Fixed community values and regular expressions.
//
TEST_P(MatchExtCommunityParamTest, Constructor3a) {
    vector<string> communities =
        list_of("target:23:11")("target:33:.*")("target:43:11")("target:53:.*");
    MatchExtCommunity match(communities, GetParam());
    EXPECT_EQ(GetParam(), match.match_all());
    EXPECT_EQ(2U, match.communities().size());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());

    RouteTarget val0 = RouteTarget::FromString(communities[0]);
    EXPECT_TRUE(match.Find(val0.GetExtCommunity()));
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[1]) != match.regex_strings().end());
    RouteTarget val2 = RouteTarget::FromString(communities[2]);
    EXPECT_TRUE(match.Find(val2.GetExtCommunity()));
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[3]) != match.regex_strings().end());
}

//
// Fixed community values and regular expressions, including duplicates.
//
TEST_P(MatchExtCommunityParamTest, Constructor3b) {
    vector<string> communities = list_of("target:23:11")("target:33:.*")
                                        ("target:43:11")("target:53:.*")
                                        ("target:33:.*");
    MatchExtCommunity match(communities, GetParam());
    EXPECT_EQ(GetParam(), match.match_all());
    EXPECT_EQ(2U, match.communities().size());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());

    RouteTarget val0 = RouteTarget::FromString(communities[0]);
    EXPECT_TRUE(match.Find(val0.GetExtCommunity()));
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[1]) != match.regex_strings().end());
    RouteTarget val2 = RouteTarget::FromString(communities[2]);
    EXPECT_TRUE(match.Find(val2.GetExtCommunity()));
    EXPECT_TRUE(find(match.regex_strings().begin(), match.regex_strings().end(),
        communities[3]) != match.regex_strings().end());
}

//
// Fixed community values only.
//
TEST_P(MatchExtCommunityParamTest, ToString1) {
    vector<string> communities = list_of("target:23:11")("target:43:11");
    MatchExtCommunity match(communities, GetParam());
    EXPECT_EQ(2U, match.communities().size());
    if (GetParam()) {
        EXPECT_EQ("Extcommunity (all) [ target:23:11,target:43:11 ]",
                  match.ToString());
    } else {
        EXPECT_EQ("Extcommunity (any) [ target:23:11,target:43:11 ]",
                  match.ToString());
    }
}

//
// Community regular expressions only.
//
TEST_P(MatchExtCommunityParamTest, ToString2) {
    vector<string> communities = list_of("target:33:.*")("target:53:.*");
    MatchExtCommunity match(communities, GetParam());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());
    if (GetParam()) {
        EXPECT_EQ("Extcommunity (all) [ target:33:.*,target:53:.* ]",
                  match.ToString());
    } else {
        EXPECT_EQ("Extcommunity (any) [ target:33:.*,target:53:.* ]",
                  match.ToString());
    }
}

//
// Fixed community values and regular expressions.
//
TEST_P(MatchExtCommunityParamTest, ToString3) {
    vector<string> communities = list_of("target:33:.*")("target:43:11")
                                        ("target:53:.*");
    MatchExtCommunity match(communities, GetParam());
    EXPECT_EQ(1U, match.communities().size());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());
    if (GetParam()) {
        EXPECT_EQ("Extcommunity (all) [ target:43:11,"
                  "target:33:.*,target:53:.* ]", match.ToString());
    } else {
        EXPECT_EQ("Extcommunity (any) [ target:43:11,"
                  "target:33:.*,target:53:.* ]", match.ToString());
    }
}

//
// Invalid Community regular expressions.
//
TEST_P(MatchExtCommunityParamTest, InvalidRegex) {
    vector<string> communities = list_of("target:33:[.*")("target:53:.*]");
    MatchExtCommunity match(communities, GetParam());
    EXPECT_EQ(2U, match.regex_strings().size());
    EXPECT_EQ(2U, match.regexs().size());
    if (GetParam()) {
        EXPECT_NE("Extcommunity (all) [ target:33:.*,"
                  "target:53:.* ]", match.ToString());
    } else {
        EXPECT_NE("Extcommunity (all) [ target:33:.*,"
                  "target:53:.* ]", match.ToString());
    }
}

//
// Fixed community values only.
// One value is same and one is different.
//
TEST_P(MatchExtCommunityParamTest, IsEqual1a) {
    vector<string> communities1 = list_of("target:23:11")("target:43:11");
    vector<string> communities2 = list_of("target:23:11")("target:43:12");
    MatchExtCommunity match1(communities1, GetParam());
    MatchExtCommunity match2(communities2, GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Fixed community values only.
// One value is same and one is different.
//
TEST_P(MatchExtCommunityParamTest, IsEqual1b) {
    vector<string> communities1 = list_of("target:23:11")("target:43:11");
    vector<string> communities2 = list_of("target:23:12")("target:43:11");
    MatchExtCommunity match1(communities1, GetParam());
    MatchExtCommunity match2(communities2, GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Fixed community values only.
// One list is a subset of the other.
//
TEST_P(MatchExtCommunityParamTest, IsEqual1c) {
    vector<string> communities1 = list_of("target:23:11")("target:43:11")
                                         ("target:63:11");
    vector<string> communities2 = list_of("target:23:11")("target:43:11");
    MatchExtCommunity match1(communities1, GetParam());
    MatchExtCommunity match2(communities2, GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Fixed community values only.
//
TEST_P(MatchExtCommunityParamTest, IsEqual1d) {
    vector<string> communities1 = list_of("target:23:11")("target:43:11");
    vector<string> communities2 = list_of("target:43:11")("target:23:11");
    MatchExtCommunity match1(communities1, GetParam());
    MatchExtCommunity match2(communities2, GetParam());
    EXPECT_TRUE(match1.IsEqual(match2));
    EXPECT_TRUE(match2.IsEqual(match1));
}

//
// Fixed community values only.
// Values of match-all are different.
//
TEST_P(MatchExtCommunityParamTest, IsEqual1e) {
    vector<string> communities1 = list_of("target:23:11")("target:43:11");
    vector<string> communities2 = list_of("target:43:11")("target:23:11");
    MatchExtCommunity match1(communities1, GetParam());
    MatchExtCommunity match2(communities2, !GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Community regular expressions only.
// One value is same and one is different.
//
TEST_P(MatchExtCommunityParamTest, IsEqual2a) {
    vector<string> communities1 = list_of("target:23:.*")("target:43:.*");
    vector<string> communities2 = list_of("target:23:.*")("target:44:.*");
    MatchExtCommunity match1(communities1, GetParam());
    MatchExtCommunity match2(communities2, GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Community regular expressions only.
// One value is same and one is different.
//
TEST_P(MatchExtCommunityParamTest, IsEqual2b) {
    vector<string> communities1 = list_of("target:23:.*")("target:43:.*");
    vector<string> communities2 = list_of("target:24:.*")("target:43:.*");
    MatchExtCommunity match1(communities1, GetParam());
    MatchExtCommunity match2(communities2, GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Community regular expressions only.
// One list is a subset of the other.
//
TEST_P(MatchExtCommunityParamTest, IsEqual2c) {
    vector<string> communities1 = list_of("target:23:.*")("target:43:.*")
                                         ("target:63:.*");
    vector<string> communities2 = list_of("target:23:.*")("target:43:.*");
    MatchExtCommunity match1(communities1, GetParam());
    MatchExtCommunity match2(communities2, GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

//
// Community regular expressions only.
//
TEST_P(MatchExtCommunityParamTest, IsEqual2d) {
    vector<string> communities1 = list_of("target:23:.*")("target:43:.*");
    vector<string> communities2 = list_of("target:43:.*")("target:23:.*");
    MatchExtCommunity match1(communities1, GetParam());
    MatchExtCommunity match2(communities2, GetParam());
    EXPECT_TRUE(match1.IsEqual(match2));
    EXPECT_TRUE(match2.IsEqual(match1));
}

//
// Community regular expressions only.
// Values of match-all are different.
//
TEST_P(MatchExtCommunityParamTest, IsEqual2e) {
    vector<string> communities1 = list_of("target:23:.*")("target:43:.*");
    vector<string> communities2 = list_of("target:43:.*")("target:23:.*");
    MatchExtCommunity match1(communities1, GetParam());
    MatchExtCommunity match2(communities2, !GetParam());
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

INSTANTIATE_TEST_CASE_P(Instance, MatchExtCommunityParamTest,
                        ::testing::Bool());

class MatchProtocolTest : public ::testing::Test {
protected:
    MatchProtocolTest() : server_(&evm_), attr_db_(server_.attr_db()) {
    }

    void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    EventManager evm_;
    BgpServer server_;
    BgpAttrDB *attr_db_;
};

TEST_F(MatchProtocolTest, Constructor1) {
    vector<string> protocols = list_of("bgp")("xmpp")("static");
    MatchProtocol match(protocols);
    EXPECT_EQ(3U, match.protocols().size());

    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::BGP) != match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::XMPP) != match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::StaticRoute) != match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::ServiceChainRoute) == match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::AggregateRoute) == match.protocols().end());
}

TEST_F(MatchProtocolTest, Constructor2) {
    vector<string> protocols = list_of("service-chain")("aggregate")("static");
    MatchProtocol match(protocols);
    EXPECT_EQ(3U, match.protocols().size());

    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::BGP) == match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::XMPP) == match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::StaticRoute) != match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::ServiceChainRoute) != match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::AggregateRoute) != match.protocols().end());
}

TEST_F(MatchProtocolTest, Constructor3) {
    vector<string> protocols = list_of("bgp")("xmpp")("static")("xyz");
    MatchProtocol match(protocols);
    EXPECT_EQ(3U, match.protocols().size());

    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::BGP) != match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::XMPP) != match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::StaticRoute) != match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::ServiceChainRoute) == match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::AggregateRoute) == match.protocols().end());
}

TEST_F(MatchProtocolTest, Constructor4) {
    vector<string> protocols = list_of("interface")("interface-static")
                                       ("service-interface")("bgpaas");
    MatchProtocol match(protocols);
    EXPECT_EQ(4U, match.protocols().size());

    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::Interface) != match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::InterfaceStatic) != match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::ServiceInterface) != match.protocols().end());
    EXPECT_TRUE(find(match.protocols().begin(), match.protocols().end(),
        MatchProtocol::BGPaaS) != match.protocols().end());
}

TEST_F(MatchProtocolTest, ToString1a) {
    vector<string> protocols = list_of("bgp")("xmpp")("static");
    MatchProtocol match(protocols);
    EXPECT_EQ("protocol [ bgp,xmpp,static ]", match.ToString());
}

TEST_F(MatchProtocolTest, ToString1b) {
    vector<string> protocols = list_of("xmpp")("static")("bgp");
    MatchProtocol match(protocols);
    EXPECT_EQ("protocol [ bgp,xmpp,static ]", match.ToString());
}

TEST_F(MatchProtocolTest, ToString2a) {
    vector<string> protocols = list_of("static")("service-chain")("aggregate");
    MatchProtocol match(protocols);
    EXPECT_EQ("protocol [ static,service-chain,aggregate ]", match.ToString());
}

TEST_F(MatchProtocolTest, ToString2b) {
    vector<string> protocols = list_of("service-chain")("aggregate")("static");
    MatchProtocol match(protocols);
    EXPECT_EQ("protocol [ static,service-chain,aggregate ]", match.ToString());
}

TEST_F(MatchProtocolTest, ToString3a) {
    vector<string> protocols = list_of("interface")("interface-static");
    MatchProtocol match(protocols);
    EXPECT_EQ("protocol [ interface,interface-static ]", match.ToString());
}

TEST_F(MatchProtocolTest, ToString3b) {
    vector<string> protocols = list_of("service-interface")("bgpaas");
    MatchProtocol match(protocols);
    EXPECT_EQ("protocol [ service-interface,bgpaas ]", match.ToString());
}

TEST_F(MatchProtocolTest, IsEqual1) {
    vector<string> protocols1 = list_of("bgp")("xmpp")("static");
    vector<string> protocols2 = list_of("bgp")("xmpp")("static");
    MatchProtocol match1(protocols1);
    MatchProtocol match2(protocols2);
    EXPECT_TRUE(match1.IsEqual(match2));
    EXPECT_TRUE(match2.IsEqual(match1));
}

TEST_F(MatchProtocolTest, IsEqual2) {
    vector<string> protocols1 = list_of("bgp")("xmpp")("static");
    vector<string> protocols2 = list_of("bgp")("static")("xmpp");
    MatchProtocol match1(protocols1);
    MatchProtocol match2(protocols2);
    EXPECT_TRUE(match1.IsEqual(match2));
    EXPECT_TRUE(match2.IsEqual(match1));
}

TEST_F(MatchProtocolTest, IsEqual3) {
    vector<string> protocols1 = list_of("bgp")("xmpp")("static");
    vector<string> protocols2 = list_of("bgp")("xmpp")("static")("aggregate");
    MatchProtocol match1(protocols1);
    MatchProtocol match2(protocols2);
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

TEST_F(MatchProtocolTest, IsEqual4) {
    vector<string> protocols1 = list_of("xmpp")("static")("aggregate");
    vector<string> protocols2 = list_of("bgp")("xmpp")("static")("aggregate");
    MatchProtocol match1(protocols1);
    MatchProtocol match2(protocols2);
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

TEST_F(MatchProtocolTest, IsEqual5) {
    vector<string> protocols1 = list_of("interface")("interface-static")("aggregate");
    vector<string> protocols2 = list_of("interface-static")("interface")("aggregate");
    MatchProtocol match1(protocols1);
    MatchProtocol match2(protocols2);
    EXPECT_TRUE(match1.IsEqual(match2));
    EXPECT_TRUE(match2.IsEqual(match1));
}


TEST_F(MatchProtocolTest, Match1) {
    vector<string> protocols = list_of("bgp")("static");
    MatchProtocol match(protocols);
    BgpAttrPtr attr;

    PeerMock peer1(false, "10.1.1.1");
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr, 0, 0);
    EXPECT_TRUE(match.Match(NULL, &path1, NULL));

    PeerMock peer2(true, "20.1.1.1");
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr, 0, 0);
    EXPECT_FALSE(match.Match(NULL, &path2, NULL));

    BgpPath path3(BgpPath::StaticRoute, attr);
    EXPECT_FALSE(match.Match(NULL, &path3, attr.get()));

    BgpAttrSubProtocol sbp(MatchProtocolToString(MatchProtocol::StaticRoute));
    BgpAttrSpec spec;
    spec.push_back(&sbp);
    BgpAttrPtr static_attr = attr_db_->Locate(spec);
    BgpPath static_path3(BgpPath::StaticRoute, static_attr);
    EXPECT_TRUE(match.Match(NULL, &static_path3, static_attr.get()));

    BgpPath path4(BgpPath::ServiceChain, attr);
    EXPECT_FALSE(match.Match(NULL, &path4, NULL));

    BgpPath path5(BgpPath::Aggregate, attr);
    EXPECT_FALSE(match.Match(NULL, &path5, NULL));
}

TEST_F(MatchProtocolTest, Match2) {
    vector<string> protocols = list_of("service-chain")("xmpp")("aggregate");
    MatchProtocol match(protocols);
    BgpAttrPtr attr;

    PeerMock peer1(false, "10.1.1.1");
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr, 0, 0);
    EXPECT_FALSE(match.Match(NULL, &path1, NULL));

    PeerMock peer2(true, "20.1.1.1");
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr, 0, 0);
    EXPECT_TRUE(match.Match(NULL, &path2, NULL));

    BgpAttrSubProtocol sbp(MatchProtocolToString(MatchProtocol::StaticRoute));
    BgpAttrSpec spec;
    spec.push_back(&sbp);
    BgpAttrPtr static_attr = attr_db_->Locate(spec);
    BgpPath path3(BgpPath::StaticRoute, static_attr);
    EXPECT_FALSE(match.Match(NULL, &path3, static_attr.get()));

    BgpPath path4(BgpPath::ServiceChain, attr);
    EXPECT_TRUE(match.Match(NULL, &path4, NULL));

    BgpPath path5(BgpPath::Aggregate, attr);
    EXPECT_TRUE(match.Match(NULL, &path5, NULL));
}

TEST_F(MatchProtocolTest, Match2_WithSubprotocol) {
    vector<string> protocols = list_of("service-interface");
    MatchProtocol match(protocols);
    BgpAttrSubProtocol sbp(MatchProtocolToString(
                           MatchProtocol::ServiceInterface));
    BgpAttrSpec spec;
    spec.push_back(&sbp);
    BgpAttrPtr attr = attr_db_->Locate(spec);

    PeerMock peer1(false, "10.1.1.1");
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr, 0, 0);
    EXPECT_TRUE(match.Match(NULL, &path1, attr.get()));

    PeerMock peer2(true, "20.1.1.1");
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr, 0, 0);
    EXPECT_TRUE(match.Match(NULL, &path2, attr.get()));

    BgpPath path4(BgpPath::ServiceChain, attr);
    EXPECT_TRUE(match.Match(NULL, &path4, attr.get()));

    BgpPath path5(BgpPath::Aggregate, attr);
    EXPECT_TRUE(match.Match(NULL, &path5, attr.get()));
}

TEST_F(MatchProtocolTest, Match3) {
    vector<string> protocols = list_of("interface")("interface-static") \
                                      ("service-interface")("bgpaas");
    MatchProtocol match(protocols);

    BgpAttrSubProtocol sbp(MatchProtocolToString(
                           MatchProtocol::ServiceInterface));
    BgpAttrSpec spec;
    spec.push_back(&sbp);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    PeerMock peer1(true, "10.1.1.1");
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr, 0, 0);
    EXPECT_TRUE(match.Match(NULL, &path1, attr.get()));

    PeerMock peer2(false, "20.1.1.1");
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr, 0, 0);

    BgpAttrSubProtocol sbp2(MatchProtocolToString(MatchProtocol::BGPaaS));
    spec.clear();
    spec.push_back(&sbp2);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, &path2, attr.get()));

    BgpAttrSubProtocol sbp3(MatchProtocolToString(MatchProtocol::Interface));
    spec.clear();
    spec.push_back(&sbp3);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, &path1, attr.get()));

    BgpAttrSubProtocol sbp4(MatchProtocolToString(
                            MatchProtocol::InterfaceStatic));
    spec.clear();
    spec.push_back(&sbp4);
    attr = attr_db_->Locate(spec);
    EXPECT_TRUE(match.Match(NULL, &path1, attr.get()));
}


//
// Template structure to pass to fixture class template. Needed because
// gtest fixture class template can accept only one template parameter.
//
template <typename T1, typename T2, typename T3>
struct TypeDefinition {
  typedef T1 PrefixT;
  typedef T2 RouteT;
  typedef T3 MatchPrefixT;
};

// TypeDefinitions that we want to test.
typedef TypeDefinition<Ip4Prefix, InetRoute, MatchPrefixInet> InetDefinition;
typedef TypeDefinition<Inet6Prefix, Inet6Route, MatchPrefixInet6> Inet6Definition;

//
// Fixture class template - instantiated later for each TypeDefinition.
//
template <typename T>
class MatchPrefixTest : public ::testing::Test {
protected:
    typedef typename T::PrefixT PrefixT;
    typedef typename T::RouteT RouteT;
    typedef typename T::MatchPrefixT MatchPrefixT;

    MatchPrefixTest() : family_(GetFamily()), ipv6_prefix_("::ffff:") {
    }

    Address::Family GetFamily() const {
        assert(false);
        return Address::UNSPEC;
    }

    string BuildPrefix(const string &ipv4_prefix, uint8_t ipv4_plen) const {
        if (family_ == Address::INET) {
            return ipv4_prefix + "/" + integerToString(ipv4_plen);
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_prefix + "/" +
                integerToString(96 + ipv4_plen);
        }
        assert(false);
        return "";
    }

    void VerifyPrefixMatchListSize(const MatchPrefixT *match, size_t size) {
        EXPECT_EQ(size, match->match_list_.size());
    }

    void VerifyPrefixMatchExists(const MatchPrefixT *match_prefix,
        const string &match_prefix_str, const string &match_type_str) {
        boost::system::error_code ec;
        PrefixT prefix = PrefixT::FromString(match_prefix_str, &ec);
        EXPECT_EQ(0, ec.value());
        typename MatchPrefixT::MatchType match_type =
            MatchPrefixT::GetMatchType(match_type_str);
        typename MatchPrefixT::PrefixMatch prefix_match(prefix, match_type);
        EXPECT_TRUE(
            find(match_prefix->match_list_.begin(),
                match_prefix->match_list_.end(), prefix_match) !=
                match_prefix->match_list_.end());
    }

    Address::Family family_;
    string ipv6_prefix_;
};

// Specialization of GetFamily for INET.
template <>
Address::Family MatchPrefixTest<InetDefinition>::GetFamily() const {
    return Address::INET;
}

// Specialization of GetFamily for INET6.
template <>
Address::Family MatchPrefixTest<Inet6Definition>::GetFamily() const {
    return Address::INET6;
}

// Instantiate fixture class template for each TypeDefinition.
typedef ::testing::Types<InetDefinition, Inet6Definition> TypeDefinitionList;
TYPED_TEST_CASE(MatchPrefixTest, TypeDefinitionList);

// Verify that prefixes with same address and match type but different lengths
// are considered different.
TYPED_TEST(MatchPrefixTest, Constructor1) {
    PrefixMatchConfig cfg1(this->BuildPrefix("10.0.0.0", 8), "exact");
    PrefixMatchConfig cfg2(this->BuildPrefix("10.0.0.0", 16), "exact");
    PrefixMatchConfig cfg3(this->BuildPrefix("10.0.0.0", 24), "exact");
    PrefixMatchConfigList cfg_list;
    cfg_list.push_back(cfg1);
    cfg_list.push_back(cfg2);
    cfg_list.push_back(cfg3);
    typename TestFixture::MatchPrefixT match(cfg_list);
    this->VerifyPrefixMatchListSize(&match, 3);
    this->VerifyPrefixMatchExists(
        &match, this->BuildPrefix("10.0.0.0", 8), "exact");
    this->VerifyPrefixMatchExists(
        &match, this->BuildPrefix("10.0.0.0", 16), "exact");
    this->VerifyPrefixMatchExists(
        &match, this->BuildPrefix("10.0.0.0", 24), "exact");
}

// Verify that prefixes with same address and lengths but different match types
// are considered different.
TYPED_TEST(MatchPrefixTest, Constructor2) {
    PrefixMatchConfig cfg1(this->BuildPrefix("10.0.0.0", 16), "exact");
    PrefixMatchConfig cfg2(this->BuildPrefix("10.0.0.0", 16), "longer");
    PrefixMatchConfig cfg3(this->BuildPrefix("10.0.0.0", 16), "orlonger");
    PrefixMatchConfigList cfg_list;
    cfg_list.push_back(cfg1);
    cfg_list.push_back(cfg2);
    cfg_list.push_back(cfg3);
    typename TestFixture::MatchPrefixT match(cfg_list);
    this->VerifyPrefixMatchListSize(&match, 3);
    this->VerifyPrefixMatchExists(
        &match, this->BuildPrefix("10.0.0.0", 16), "exact");
    this->VerifyPrefixMatchExists(
        &match, this->BuildPrefix("10.0.0.0", 16), "longer");
    this->VerifyPrefixMatchExists(
        &match, this->BuildPrefix("10.0.0.0", 16), "orlonger");
}

// Verify that duplicate prefixes are dropped.
TYPED_TEST(MatchPrefixTest, Constructor3) {
    PrefixMatchConfig cfg1(this->BuildPrefix("10.0.0.0", 8), "exact");
    PrefixMatchConfig cfg2(this->BuildPrefix("10.0.0.0", 16), "exact");
    PrefixMatchConfig cfg3(this->BuildPrefix("10.0.0.0", 8), "exact");
    PrefixMatchConfig cfg4(this->BuildPrefix("10.0.0.0", 24), "exact");
    PrefixMatchConfigList cfg_list;
    cfg_list.push_back(cfg1);
    cfg_list.push_back(cfg2);
    cfg_list.push_back(cfg3);
    cfg_list.push_back(cfg4);
    typename TestFixture::MatchPrefixT match(cfg_list);
    this->VerifyPrefixMatchListSize(&match, 3);
    this->VerifyPrefixMatchExists(
        &match, this->BuildPrefix("10.0.0.0", 8), "exact");
    this->VerifyPrefixMatchExists(
        &match, this->BuildPrefix("10.0.0.0", 16), "exact");
    this->VerifyPrefixMatchExists(
        &match, this->BuildPrefix("10.0.0.0", 24), "exact");
}

TYPED_TEST(MatchPrefixTest, IsEqual1) {
    PrefixMatchConfig cfg1(this->BuildPrefix("10.0.0.0", 8), "exact");
    PrefixMatchConfig cfg2(this->BuildPrefix("10.0.0.0", 16), "exact");
    PrefixMatchConfig cfg3(this->BuildPrefix("10.0.0.0", 24), "exact");
    PrefixMatchConfigList cfg_list1;
    cfg_list1.push_back(cfg1);
    cfg_list1.push_back(cfg2);
    cfg_list1.push_back(cfg3);
    PrefixMatchConfigList cfg_list2;
    cfg_list2.push_back(cfg1);
    cfg_list2.push_back(cfg2);
    cfg_list2.push_back(cfg3);
    typename TestFixture::MatchPrefixT match1(cfg_list1);
    typename TestFixture::MatchPrefixT match2(cfg_list2);
    EXPECT_TRUE(match1.IsEqual(match2));
    EXPECT_TRUE(match2.IsEqual(match1));
}

TYPED_TEST(MatchPrefixTest, IsEqual2) {
    PrefixMatchConfig cfg1(this->BuildPrefix("10.0.0.0", 8), "exact");
    PrefixMatchConfig cfg2(this->BuildPrefix("10.0.0.0", 16), "exact");
    PrefixMatchConfig cfg3(this->BuildPrefix("10.0.0.0", 24), "exact");
    PrefixMatchConfigList cfg_list1;
    cfg_list1.push_back(cfg1);
    cfg_list1.push_back(cfg2);
    cfg_list1.push_back(cfg3);
    PrefixMatchConfigList cfg_list2;
    cfg_list2.push_back(cfg2);
    cfg_list2.push_back(cfg3);
    cfg_list2.push_back(cfg1);
    typename TestFixture::MatchPrefixT match1(cfg_list1);
    typename TestFixture::MatchPrefixT match2(cfg_list2);
    EXPECT_TRUE(match1.IsEqual(match2));
    EXPECT_TRUE(match2.IsEqual(match1));
}

TYPED_TEST(MatchPrefixTest, IsEqual3) {
    PrefixMatchConfig cfg1(this->BuildPrefix("10.0.0.0", 8), "exact");
    PrefixMatchConfig cfg2(this->BuildPrefix("10.0.0.0", 16), "exact");
    PrefixMatchConfig cfg3(this->BuildPrefix("10.0.0.0", 24), "exact");
    PrefixMatchConfigList cfg_list1;
    cfg_list1.push_back(cfg1);
    cfg_list1.push_back(cfg2);
    cfg_list1.push_back(cfg3);
    PrefixMatchConfigList cfg_list2;
    cfg_list2.push_back(cfg2);
    cfg_list2.push_back(cfg3);
    cfg_list2.push_back(cfg1);
    cfg_list2.push_back(cfg2);
    typename TestFixture::MatchPrefixT match1(cfg_list1);
    typename TestFixture::MatchPrefixT match2(cfg_list2);
    EXPECT_TRUE(match1.IsEqual(match2));
    EXPECT_TRUE(match2.IsEqual(match1));
}

TYPED_TEST(MatchPrefixTest, IsEqual4) {
    PrefixMatchConfig cfg1(this->BuildPrefix("10.0.0.0", 8), "exact");
    PrefixMatchConfig cfg2(this->BuildPrefix("10.0.0.0", 16), "exact");
    PrefixMatchConfig cfg3(this->BuildPrefix("10.0.0.0", 24), "exact");
    PrefixMatchConfig cfg4(this->BuildPrefix("10.0.0.0", 32), "exact");
    PrefixMatchConfigList cfg_list1;
    cfg_list1.push_back(cfg1);
    cfg_list1.push_back(cfg2);
    cfg_list1.push_back(cfg3);
    PrefixMatchConfigList cfg_list2;
    cfg_list2.push_back(cfg1);
    cfg_list2.push_back(cfg2);
    cfg_list2.push_back(cfg3);
    cfg_list2.push_back(cfg4);
    typename TestFixture::MatchPrefixT match1(cfg_list1);
    typename TestFixture::MatchPrefixT match2(cfg_list2);
    EXPECT_FALSE(match1.IsEqual(match2));
    EXPECT_FALSE(match2.IsEqual(match1));
}

TYPED_TEST(MatchPrefixTest, ToString1) {
    PrefixMatchConfig cfg1(this->BuildPrefix("10.0.0.0", 8), "exact");
    PrefixMatchConfig cfg2(this->BuildPrefix("10.0.0.0", 16), "exact");
    PrefixMatchConfig cfg3(this->BuildPrefix("10.0.0.0", 24), "exact");
    PrefixMatchConfigList cfg_list;
    cfg_list.push_back(cfg1);
    cfg_list.push_back(cfg2);
    cfg_list.push_back(cfg3);
    typename TestFixture::MatchPrefixT match(cfg_list);
    string result("prefix [ ");
    result += this->BuildPrefix("10.0.0.0", 8);
    result += ", ";
    result += this->BuildPrefix("10.0.0.0", 16);
    result += ", ";
    result += this->BuildPrefix("10.0.0.0", 24);
    result += " ]";
    EXPECT_EQ(result, match.ToString());
}

TYPED_TEST(MatchPrefixTest, ToString2) {
    PrefixMatchConfig cfg1(this->BuildPrefix("10.0.0.0", 8), "exact");
    PrefixMatchConfig cfg2(this->BuildPrefix("10.0.0.0", 16), "longer");
    PrefixMatchConfig cfg3(this->BuildPrefix("10.0.0.0", 24), "orlonger");
    PrefixMatchConfigList cfg_list;
    cfg_list.push_back(cfg1);
    cfg_list.push_back(cfg2);
    cfg_list.push_back(cfg3);
    typename TestFixture::MatchPrefixT match(cfg_list);
    string result("prefix [ ");
    result += this->BuildPrefix("10.0.0.0", 8);
    result += ", ";
    result += this->BuildPrefix("10.0.0.0", 16);
    result += " longer, ";
    result += this->BuildPrefix("10.0.0.0", 24);
    result += " orlonger";
    result += " ]";
    EXPECT_EQ(result, match.ToString());
}

TYPED_TEST(MatchPrefixTest, Match) {
    PrefixMatchConfig cfg1(this->BuildPrefix("10.1.1.0", 24), "exact");
    PrefixMatchConfig cfg2(this->BuildPrefix("10.2.0.0", 16), "longer");
    PrefixMatchConfig cfg3(this->BuildPrefix("10.3.0.0", 16), "orlonger");
    PrefixMatchConfigList cfg_list;
    cfg_list.push_back(cfg1);
    cfg_list.push_back(cfg2);
    cfg_list.push_back(cfg3);
    typename TestFixture::MatchPrefixT match(cfg_list);

    typename TestFixture::PrefixT prefix1 =
        TestFixture::PrefixT::FromString(this->BuildPrefix("10.1.1.0", 24));
    typename TestFixture::RouteT route1(prefix1);
    EXPECT_TRUE(match.Match(&route1, NULL, NULL));

    typename TestFixture::PrefixT prefix2 =
        TestFixture::PrefixT::FromString(this->BuildPrefix("10.1.1.1", 32));
    typename TestFixture::RouteT route2(prefix2);
    EXPECT_FALSE(match.Match(&route2, NULL, NULL));

    typename TestFixture::PrefixT prefix3 =
        TestFixture::PrefixT::FromString(this->BuildPrefix("10.2.0.0", 16));
    typename TestFixture::RouteT route3(prefix3);
    EXPECT_FALSE(match.Match(&route3, NULL, NULL));

    typename TestFixture::PrefixT prefix4 =
        TestFixture::PrefixT::FromString(this->BuildPrefix("10.2.1.0", 24));
    typename TestFixture::RouteT route4(prefix4);
    EXPECT_TRUE(match.Match(&route4, NULL, NULL));

    typename TestFixture::PrefixT prefix5 =
        TestFixture::PrefixT::FromString(this->BuildPrefix("10.3.0.0", 16));
    typename TestFixture::RouteT route5(prefix5);
    EXPECT_TRUE(match.Match(&route5, NULL, NULL));

    typename TestFixture::PrefixT prefix6 =
        TestFixture::PrefixT::FromString(this->BuildPrefix("10.3.1.0", 24));
    typename TestFixture::RouteT route6(prefix6);
    EXPECT_TRUE(match.Match(&route6, NULL, NULL));

    typename TestFixture::PrefixT prefix7 =
        TestFixture::PrefixT::FromString(this->BuildPrefix("10.0.0.0", 8));
    typename TestFixture::RouteT route7(prefix7);
    EXPECT_FALSE(match.Match(&route7, NULL, NULL));

    typename TestFixture::PrefixT prefix8 =
        TestFixture::PrefixT::FromString(this->BuildPrefix("10.1.0.0", 16));
    typename TestFixture::RouteT route8(prefix8);
    EXPECT_FALSE(match.Match(&route8, NULL, NULL));
}

static void SetUp() {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
