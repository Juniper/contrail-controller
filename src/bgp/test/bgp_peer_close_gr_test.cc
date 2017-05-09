/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <set>
#include <string>
#include <vector>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"

#include "bgp/bgp_peer_close.h"
#include "bgp/bgp_proto.h"
#include "bgp/test/bgp_server_test_util.h"

#include "control-node/control_node.h"

using std::set;
using std::string;
using std::vector;
using ::testing::TestWithParam;
using ::testing::Bool;
using ::testing::ValuesIn;
using ::testing::Combine;

class BgpPeerCloseTest : public BgpPeerClose {
public:
    BgpPeerCloseTest() :
            BgpPeerClose(NULL),
            peer_deleted_(false),
            peer_admin_down_(false),
            server_deleted_(false),
            server_admin_down_(false),
            gr_helper_mode_enabled_(true),
            in_gr_timer_wait_state_(true),
            in_llgr_timer_wait_state_(true) {
    }

    virtual ~BgpPeerCloseTest() { }

    virtual bool IsInGRTimerWaitState() const {
        return in_gr_timer_wait_state_;
    }
    virtual bool IsInLlgrTimerWaitState() const {
        return in_llgr_timer_wait_state_;
    }
    virtual bool IsGRHelperModeEnabled() const {
        return gr_helper_mode_enabled_;
    }
    virtual bool IsPeerDeleted() const { return peer_deleted_; }
    virtual bool IsPeerAdminDown() const { return peer_admin_down_; }
    virtual bool IsServerDeleted() const { return server_deleted_; }
    virtual bool IsServerAdminDown() const { return server_admin_down_; }
    virtual const vector<string> &negotiated_families() const {
        return negotiated_families_;
    }
    virtual const vector<string> &PeerNegotiatedFamilies() const {
        return peer_negotiated_families_;
    }
    virtual const vector<BgpProto::OpenMessage::Capability *> &
        capabilities() const {
        return capabilities_;
    }

    void set_capabilities(const vector<BgpProto::OpenMessage::Capability *>
            &capabilities) {
        capabilities_ = capabilities;
    }

    void set_negotiated_families(const vector<string> &negotiated_families) {
        negotiated_families_ = negotiated_families;
        std::sort(negotiated_families_.begin(), negotiated_families_.end());
    }

    void set_peer_negotiated_families(
            const vector<string> &peer_negotiated_families) {
        peer_negotiated_families_ = peer_negotiated_families;
        std::sort(peer_negotiated_families_.begin(),
                  peer_negotiated_families_.end());
    }

    void set_server_deleted(bool deleted) { server_deleted_ = deleted; }
    void set_server_admin_down(bool down) { server_admin_down_ = down; }
    void set_peer_deleted(bool deleted) { peer_deleted_ = deleted; }
    void set_peer_admin_down(bool down) { peer_admin_down_ = down; }
    void set_gr_helper_mode_enabled(bool enabled) {
        gr_helper_mode_enabled_ = enabled;
    }
    void set_in_gr_timer_wait_state(bool in_wait_state) {
        in_gr_timer_wait_state_ = in_wait_state;
    }
    void set_in_llgr_timer_wait_state(bool in_wait_state) {
        in_llgr_timer_wait_state_ = in_wait_state;
    }

private:
    bool peer_deleted_;
    bool peer_admin_down_;
    bool server_deleted_;
    bool server_admin_down_;
    bool gr_helper_mode_enabled_;
    bool in_gr_timer_wait_state_;
    bool in_llgr_timer_wait_state_;
    vector<string> negotiated_families_;
    vector<string> peer_negotiated_families_;
    vector<BgpProto::OpenMessage::Capability *> capabilities_;
};

class BgpPeerCloseGrTest : public ::testing::Test {
protected:
    virtual void SetUp() {
        bgp_peer_close_test_.reset(new BgpPeerCloseTest());
    }

    virtual void TearDown() {
    }

    boost::scoped_ptr<BgpPeerCloseTest> bgp_peer_close_test_;
};

TEST_F(BgpPeerCloseGrTest, ServerDeleted) {
    bgp_peer_close_test_->set_server_deleted(true);
    EXPECT_FALSE(bgp_peer_close_test_->SetGRCapabilities(NULL));
}

TEST_F(BgpPeerCloseGrTest, ServerAdminDown) {
    bgp_peer_close_test_->set_server_admin_down(true);
    EXPECT_FALSE(bgp_peer_close_test_->SetGRCapabilities(NULL));
}

TEST_F(BgpPeerCloseGrTest, PeerDeleted) {
    bgp_peer_close_test_->set_peer_deleted(true);
    EXPECT_FALSE(bgp_peer_close_test_->SetGRCapabilities(NULL));
}

TEST_F(BgpPeerCloseGrTest, PeerAdminDown) {
    bgp_peer_close_test_->set_peer_admin_down(true);
    EXPECT_FALSE(bgp_peer_close_test_->SetGRCapabilities(NULL));
}

TEST_F(BgpPeerCloseGrTest, GRHelperModeDisabled) {
    bgp_peer_close_test_->set_gr_helper_mode_enabled(false);
    EXPECT_FALSE(bgp_peer_close_test_->SetGRCapabilities(NULL));
}

TEST_F(BgpPeerCloseGrTest, InGrTimerWaitingState) {
    bgp_peer_close_test_->set_in_gr_timer_wait_state(false);
    EXPECT_TRUE(bgp_peer_close_test_->SetGRCapabilities(NULL));
}

TEST_F(BgpPeerCloseGrTest, RestartFlagsTest1) {
    boost::scoped_ptr<BgpProto::OpenMessage::Capability> cap;
    uint16_t time = 0;
    cap.reset(BgpProto::OpenMessage::Capability::GR::Encode(time, false, false,
            vector<uint8_t>(), vector<Address::Family>()));
    std::vector<BgpProto::OpenMessage::Capability *> capabilities;
    capabilities.push_back(cap.get());
    bgp_peer_close_test_->set_capabilities(capabilities);
    bgp_peer_close_test_->set_in_gr_timer_wait_state(false);
    EXPECT_TRUE(bgp_peer_close_test_->SetGRCapabilities(NULL));
    EXPECT_EQ(false, bgp_peer_close_test_->gr_params().restarted());
    EXPECT_EQ(false, bgp_peer_close_test_->gr_params().notification());
    EXPECT_EQ(time, bgp_peer_close_test_->gr_params().time);
}

TEST_F(BgpPeerCloseGrTest, RestartFlagsTest2) {
    boost::scoped_ptr<BgpProto::OpenMessage::Capability> cap;
    uint16_t time = 0xFF;
    cap.reset(BgpProto::OpenMessage::Capability::GR::Encode(time, true, false,
            vector<uint8_t>(), vector<Address::Family>()));
    std::vector<BgpProto::OpenMessage::Capability *> capabilities;
    capabilities.push_back(cap.get());
    bgp_peer_close_test_->set_capabilities(capabilities);
    bgp_peer_close_test_->set_in_gr_timer_wait_state(false);
    EXPECT_TRUE(bgp_peer_close_test_->SetGRCapabilities(NULL));
    EXPECT_EQ(true, bgp_peer_close_test_->gr_params().restarted());
    EXPECT_EQ(false, bgp_peer_close_test_->gr_params().notification());
    EXPECT_EQ(time, bgp_peer_close_test_->gr_params().time);
}

TEST_F(BgpPeerCloseGrTest, RestartFlagsTest3) {
    boost::scoped_ptr<BgpProto::OpenMessage::Capability> cap;
    uint16_t time = BgpProto::OpenMessage::Capability::GR::RestartTimeMask;
    cap.reset(BgpProto::OpenMessage::Capability::GR::Encode(time, false, false,
            vector<uint8_t>(), vector<Address::Family>()));
    std::vector<BgpProto::OpenMessage::Capability *> capabilities;
    capabilities.push_back(cap.get());
    bgp_peer_close_test_->set_capabilities(capabilities);
    bgp_peer_close_test_->set_in_gr_timer_wait_state(false);
    EXPECT_TRUE(bgp_peer_close_test_->SetGRCapabilities(NULL));
    EXPECT_EQ(false, bgp_peer_close_test_->gr_params().restarted());
    EXPECT_EQ(false, bgp_peer_close_test_->gr_params().notification());
    EXPECT_EQ(time, bgp_peer_close_test_->gr_params().time);
}

TEST_F(BgpPeerCloseGrTest, RestartFlagsTest4) {
    boost::scoped_ptr<BgpProto::OpenMessage::Capability> cap;
    uint16_t time = BgpProto::OpenMessage::Capability::GR::RestartTimeMask/2;
    cap.reset(BgpProto::OpenMessage::Capability::GR::Encode(time, true, false,
            vector<uint8_t>(), vector<Address::Family>()));
    std::vector<BgpProto::OpenMessage::Capability *> capabilities;
    capabilities.push_back(cap.get());
    bgp_peer_close_test_->set_capabilities(capabilities);
    bgp_peer_close_test_->set_in_gr_timer_wait_state(false);
    EXPECT_TRUE(bgp_peer_close_test_->SetGRCapabilities(NULL));
    EXPECT_EQ(true, bgp_peer_close_test_->gr_params().restarted());
    EXPECT_EQ(false, bgp_peer_close_test_->gr_params().notification());
    EXPECT_EQ(time, bgp_peer_close_test_->gr_params().time);
}

TEST_F(BgpPeerCloseGrTest, RestartFlagsTest5) {
    boost::scoped_ptr<BgpProto::OpenMessage::Capability> cap;
    uint16_t time = 0;
    cap.reset(BgpProto::OpenMessage::Capability::GR::Encode(time, false, true,
            vector<uint8_t>(), vector<Address::Family>()));
    std::vector<BgpProto::OpenMessage::Capability *> capabilities;
    capabilities.push_back(cap.get());
    bgp_peer_close_test_->set_capabilities(capabilities);
    bgp_peer_close_test_->set_in_gr_timer_wait_state(false);
    EXPECT_TRUE(bgp_peer_close_test_->SetGRCapabilities(NULL));
    EXPECT_EQ(false, bgp_peer_close_test_->gr_params().restarted());
    EXPECT_EQ(true, bgp_peer_close_test_->gr_params().notification());
    EXPECT_EQ(time, bgp_peer_close_test_->gr_params().time);
}

TEST_F(BgpPeerCloseGrTest, RestartFlagsTest6) {
    boost::scoped_ptr<BgpProto::OpenMessage::Capability> cap;
    uint16_t time = 0xFF;
    cap.reset(BgpProto::OpenMessage::Capability::GR::Encode(time, true, true,
            vector<uint8_t>(), vector<Address::Family>()));
    std::vector<BgpProto::OpenMessage::Capability *> capabilities;
    capabilities.push_back(cap.get());
    bgp_peer_close_test_->set_capabilities(capabilities);
    bgp_peer_close_test_->set_in_gr_timer_wait_state(false);
    EXPECT_TRUE(bgp_peer_close_test_->SetGRCapabilities(NULL));
    EXPECT_EQ(true, bgp_peer_close_test_->gr_params().restarted());
    EXPECT_EQ(true, bgp_peer_close_test_->gr_params().notification());
    EXPECT_EQ(time, bgp_peer_close_test_->gr_params().time);
}

TEST_F(BgpPeerCloseGrTest, RestartFlagsTest7) {
    boost::scoped_ptr<BgpProto::OpenMessage::Capability> cap;
    uint16_t time = BgpProto::OpenMessage::Capability::GR::RestartTimeMask;
    cap.reset(BgpProto::OpenMessage::Capability::GR::Encode(time, false, true,
            vector<uint8_t>(), vector<Address::Family>()));
    std::vector<BgpProto::OpenMessage::Capability *> capabilities;
    capabilities.push_back(cap.get());
    bgp_peer_close_test_->set_capabilities(capabilities);
    bgp_peer_close_test_->set_in_gr_timer_wait_state(false);
    EXPECT_TRUE(bgp_peer_close_test_->SetGRCapabilities(NULL));
    EXPECT_EQ(false, bgp_peer_close_test_->gr_params().restarted());
    EXPECT_EQ(true, bgp_peer_close_test_->gr_params().notification());
    EXPECT_EQ(time, bgp_peer_close_test_->gr_params().time);
}

TEST_F(BgpPeerCloseGrTest, RestartFlagsTest8) {
    boost::scoped_ptr<BgpProto::OpenMessage::Capability> cap;
    uint16_t time = BgpProto::OpenMessage::Capability::GR::RestartTimeMask/2;
    cap.reset(BgpProto::OpenMessage::Capability::GR::Encode(time, true, true,
            vector<uint8_t>(), vector<Address::Family>()));
    std::vector<BgpProto::OpenMessage::Capability *> capabilities;
    capabilities.push_back(cap.get());
    bgp_peer_close_test_->set_capabilities(capabilities);
    bgp_peer_close_test_->set_in_gr_timer_wait_state(false);
    EXPECT_TRUE(bgp_peer_close_test_->SetGRCapabilities(NULL));
    EXPECT_EQ(true, bgp_peer_close_test_->gr_params().restarted());
    EXPECT_EQ(true, bgp_peer_close_test_->gr_params().notification());
    EXPECT_EQ(time, bgp_peer_close_test_->gr_params().time);
}

typedef std::tr1::tuple<bool, vector<string>, vector<string>,
        uint16_t, uint8_t, vector<Address::Family>,
        uint32_t, uint8_t, vector<Address::Family> > TestParams;

class BgpPeerCloseGrTestParam : public ::testing::TestWithParam<TestParams> {
protected:
    virtual void SetUp() {
        bgp_peer_close_test_.reset(new BgpPeerCloseTest());
        SetCapabilities();
    }

    virtual void TearDown() {
        STLDeleteValues(&capabilities_);
    }

    void Initialize() {
        bgp_peer_close_test_->set_in_llgr_timer_wait_state(
                ::std::tr1::get<0>(GetParam()));
        bgp_peer_close_test_->set_negotiated_families(
                ::std::tr1::get<1>(GetParam()));
        bgp_peer_close_test_->set_peer_negotiated_families(
                ::std::tr1::get<2>(GetParam()));
    }

    struct GRInfo {
        GRInfo(bool llgr, uint16_t time, std::vector<uint8_t> afi_flags,
               const vector<std::string> &families) :
            llgr(llgr), time(time), afi_flags(afi_flags), families(families) {
        }
        bool llgr;
        uint16_t time;
        std::vector<uint8_t> afi_flags;
        vector<std::string> families;
    };

    void SetCapabilities() {
        uint16_t gr_time = ::std::tr1::get<3>(GetParam());
        vector<Address::Family> gr_families = ::std::tr1::get<5>(GetParam());

        vector<uint8_t> afi_flags;
        for (size_t i = 0; i < gr_families.size(); i++) {
            uint8_t flags = 0;
            if (::std::tr1::get<4>(GetParam()) == 1 ||
                (i == gr_families.size() - 1 &&
                 ::std::tr1::get<4>(GetParam()) == 2)) {
                flags = BgpProto::OpenMessage::Capability::GR::
                            ForwardingStatePreservedFlag;
            }
            afi_flags.push_back(flags);
        }

        std::vector<std::string> families_str;
        for (size_t i = 0; i < gr_families.size(); i++)
            families_str.push_back(Address::FamilyToString(gr_families[i]));

        gr_info_.push_back(GRInfo(false, gr_time, afi_flags, families_str));
        families_str.clear();

        uint32_t llgr_time = ::std::tr1::get<6>(GetParam());
        uint8_t llgr_afi_flags = ::std::tr1::get<7>(GetParam());

        vector<Address::Family> llgr_families = ::std::tr1::get<8>(GetParam());

        BgpProto::OpenMessage::Capability *cap;
        cap = BgpProto::OpenMessage::Capability::GR::Encode(
                gr_time, false, true, afi_flags, gr_families);
        capabilities_.push_back(cap);

        afi_flags.clear();
        for (size_t i = 0; i < llgr_families.size(); i++) {
            afi_flags.push_back(llgr_afi_flags);
        }
        cap = BgpProto::OpenMessage::Capability::LLGR::Encode(
                llgr_time, llgr_afi_flags, llgr_families);
        for (size_t i = 0; i < llgr_families.size(); i++)
            families_str.push_back(Address::FamilyToString(llgr_families[i]));
        gr_info_.push_back(GRInfo(true, llgr_time, afi_flags, families_str));
        capabilities_.push_back(cap);
    }

    std::vector<BgpProto::OpenMessage::Capability *> capabilities_;
    std::vector<GRInfo> gr_info_;
    boost::scoped_ptr<BgpPeerCloseTest> bgp_peer_close_test_;
};

TEST_P(BgpPeerCloseGrTestParam, TestSetGRCapabilities) {
    std::vector<std::vector<GRInfo> > gr_infos = GetSubSets(gr_info_);
    std::vector<std::vector<BgpProto::OpenMessage::Capability *> >
        capabilities = GetSubSets(capabilities_);

    for (size_t i = 0; i < capabilities.size(); i++) {
        Initialize();
        bgp_peer_close_test_->set_capabilities(capabilities[i]);
        bool expected = true;
        int mismatch = 0;

        GRInfo *gr_info = NULL;
        GRInfo *llgr_info = NULL;

        // Find GR/LLGR capability info.
        for (size_t j = 0; expected && j < gr_infos[i].size(); j++) {
            if (gr_infos[i][j].llgr) {
                llgr_info = &gr_infos[i][j];
            } else {
                gr_info = &gr_infos[i][j];
            }
        }

        vector<string> gr_families;
        if (gr_info) {
            gr_families = gr_info->families;
            std::sort(gr_families.begin(), gr_families.end());
        }

        vector<string> llgr_families;
        if (llgr_info) {
            llgr_families = llgr_info->families;
            std::sort(llgr_families.begin(), llgr_families.end());
        }

        // If there is no GR Family negotiated, then GR must be aborted.
        if (expected && (!gr_info || gr_info->families.empty())) {
            mismatch = __LINE__;
            expected = false;
        }

        if (expected && !bgp_peer_close_test_->negotiated_families().empty() &&
                bgp_peer_close_test_->PeerNegotiatedFamilies() !=
                    bgp_peer_close_test_->negotiated_families()) {
            mismatch = __LINE__;
            expected = false;
        }

        if (expected && bgp_peer_close_test_->PeerNegotiatedFamilies() !=
                                                  gr_families) {
            mismatch = __LINE__;
            expected = false;
        }

        if (expected) {
            size_t i = 0;
            BOOST_FOREACH(uint8_t afi_flags, gr_info->afi_flags) {
                string fmly = gr_info->families[i++];
                if (!(afi_flags & BgpProto::OpenMessage::Capability::GR
                                      ::ForwardingStatePreservedFlag)) {
                    if (fmly != Address::FamilyToString(Address::EVPN) &&
                        fmly != Address::FamilyToString(Address::ERMVPN)) {
                        mismatch = __LINE__;
                        expected = false;
                        break;
                    }
                }
            }
        }

        // If there GR time is zero of if in LLGR timer state, check if LLGR
        // should take into effect.
        if (expected && (bgp_peer_close_test_->IsInLlgrTimerWaitState() ||
                         !gr_info->time)) {
            if (!llgr_info  || !llgr_info->time || llgr_families.empty()) {
                mismatch = __LINE__;
                expected = false;
            } else if (llgr_families != gr_families) {
                // Check if differing families are only evpn and/or ermvpn.
                vector<string> differing_families;
                std::set_symmetric_difference(gr_families.begin(),
                        gr_families.end(),
                        llgr_families.begin(), llgr_families.end(),
                        std::back_inserter(differing_families));

                if (differing_families.size() > 2) {
                    mismatch = __LINE__;
                    expected = false;
                } else if (differing_families.size() == 1) {
                    if (differing_families[0] != "e-vpn" &&
                            differing_families[0] != "erm-vpn") {
                        mismatch = __LINE__;
                        expected = false;
                    }
                } else {
                    if (differing_families[0] != "e-vpn" ||
                            differing_families[1] != "erm-vpn") {
                        mismatch = __LINE__;
                        expected = false;
                    }
                }
            }

            if (expected && llgr_info) {
                size_t i = 0;
                BOOST_FOREACH(uint8_t afi_flag, llgr_info->afi_flags) {
                    string fmly = llgr_info->families[i++];
                    if (!(afi_flag & BgpProto::OpenMessage::Capability::LLGR::
                                         ForwardingStatePreservedFlag)) {
                        if (fmly != Address::FamilyToString(Address::EVPN) &&
                            fmly != Address::FamilyToString(Address::ERMVPN)) {
                            mismatch = __LINE__;
                            expected = false;
                            break;
                        }
                    }
                }
            }
        }

        EXPECT_EQ(expected, bgp_peer_close_test_->SetGRCapabilities(NULL));
        if (expected)
            EXPECT_EQ(0, mismatch);
    }
}

static string af[] = { "e-vpn", "erm-vpn", "inet-vpn" };
static Address::Family afi[] = {
    Address::EVPN, Address::INETVPN, Address::ERMVPN
};

#define COMBINE_PARAMS                                                         \
    Combine(                                                                   \
        ::testing::Bool(),                                                     \
        ::testing::ValuesIn(GetSubSets(                                        \
            std::vector<std::string>(af, af + sizeof(af)/sizeof(af[0])))),     \
        ::testing::ValuesIn(GetSubSets(                                        \
            std::vector<std::string>(af, af + sizeof(af)/sizeof(af[0])))),     \
        ::testing::Values(0,                                                   \
            BgpProto::OpenMessage::Capability::GR::RestartTimeMask),           \
        ::testing::Values(0, 1, 2),                                            \
        ::testing::ValuesIn(GetSubSets(                                        \
           std::vector<Address::Family>(afi, afi+sizeof(afi)/sizeof(afi[0])))),\
        ::testing::Values(0,                                                   \
            BgpProto::OpenMessage::Capability::LLGR::RestartTimeMask),         \
        ::testing::Values(0,                                                   \
        BgpProto::OpenMessage::Capability::LLGR::ForwardingStatePreservedFlag),\
        ::testing::ValuesIn(GetSubSets(                                        \
           std::vector<Address::Family>(afi, afi+sizeof(afi)/sizeof(afi[0])))) \
    )

INSTANTIATE_TEST_CASE_P(BgpPeerCloseGrTestWithParams, BgpPeerCloseGrTestParam,
                        COMBINE_PARAMS);


class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
}

static void TearDown() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
