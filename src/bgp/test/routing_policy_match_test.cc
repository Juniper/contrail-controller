/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */


#include <boost/assign/list_of.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_server.h"
#include "bgp/routing-policy/routing_policy_match.h"
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
    virtual void UpdatePrimaryPathCount(int count,
        Address::Family family) const { }
    virtual int GetPrimaryPathCount() const { return 0; }
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
    EXPECT_EQ(2, match.communities().size());
    EXPECT_EQ(0, match.regex_strings().size());
    EXPECT_EQ(0, match.regexs().size());

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
    EXPECT_EQ(2, match.communities().size());
    EXPECT_EQ(0, match.regex_strings().size());
    EXPECT_EQ(0, match.regexs().size());

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
    EXPECT_EQ(0, match.communities().size());
    EXPECT_EQ(2, match.regex_strings().size());
    EXPECT_EQ(2, match.regexs().size());
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
    EXPECT_EQ(0, match.communities().size());
    EXPECT_EQ(2, match.regex_strings().size());
    EXPECT_EQ(2, match.regexs().size());
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
    EXPECT_EQ(2, match.communities().size());
    EXPECT_EQ(2, match.regex_strings().size());
    EXPECT_EQ(2, match.regexs().size());

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
    EXPECT_EQ(2, match.communities().size());
    EXPECT_EQ(2, match.regex_strings().size());
    EXPECT_EQ(2, match.regexs().size());

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
    EXPECT_EQ(2, match.communities().size());
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
    EXPECT_EQ(2, match.regex_strings().size());
    EXPECT_EQ(2, match.regexs().size());
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
    EXPECT_EQ(1, match.communities().size());
    EXPECT_EQ(2, match.regex_strings().size());
    EXPECT_EQ(2, match.regexs().size());
    if (GetParam()) {
        EXPECT_EQ("community (all) [ 43:11,33:.*,53:.* ]", match.ToString());
    } else {
        EXPECT_EQ("community (any) [ 43:11,33:.*,53:.* ]", match.ToString());
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

class MatchProtocolTest : public ::testing::Test {
protected:
    MatchProtocolTest() { }
};

TEST_F(MatchProtocolTest, Constructor1) {
    vector<string> protocols = list_of("bgp")("xmpp")("static");
    MatchProtocol match(protocols);
    EXPECT_EQ(3, match.protocols().size());

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
    EXPECT_EQ(3, match.protocols().size());

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
    EXPECT_EQ(3, match.protocols().size());

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
    EXPECT_TRUE(match.Match(NULL, &path3, NULL));

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

    BgpPath path3(BgpPath::StaticRoute, attr);
    EXPECT_FALSE(match.Match(NULL, &path3, NULL));

    BgpPath path4(BgpPath::ServiceChain, attr);
    EXPECT_TRUE(match.Match(NULL, &path4, NULL));

    BgpPath path5(BgpPath::Aggregate, attr);
    EXPECT_TRUE(match.Match(NULL, &path5, NULL));
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
