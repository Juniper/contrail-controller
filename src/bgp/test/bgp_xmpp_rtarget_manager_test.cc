/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_xmpp_rtarget_manager.h"
#include "routing-instance/routing_instance.h"

using std::string;

class RoutingInstanceTest : public RoutingInstance {
public:
    RoutingInstanceTest(string name) :
            RoutingInstance(name, NULL, NULL, NULL), gr_state_(0) {
    }

    bool IsStale() const { return (gr_state_ & BgpPath::Stale) != 0; }
    bool IsLlgrStale() const { return (gr_state_ & BgpPath::LlgrStale) != 0; }
    void set_gr_state(uint32_t gr_state) { gr_state_ = gr_state; }
    uint32_t gr_state() const { return gr_state_; }

private:
    uint32_t gr_state_;
};

class BgpXmppRTargetManagerTest : public BgpXmppRTargetManager {
public:
    BgpXmppRTargetManagerTest(RoutingInstanceTest *instance1,
        RoutingInstanceTest *instance2) :
            BgpXmppRTargetManager(NULL),
            instance1_(instance1), instance2_(instance2),
            empty_subscription_(false),
            delete_in_progress_(false), local_as_(0),
            enqueued_(false), enqueued_flags_(0), verify_(false) {
    }

    virtual bool IsSubscriptionEmpty() const { return empty_subscription_; }
    virtual void set_empty_subscription(bool empty_subscription) {
        empty_subscription_ = empty_subscription;
    }
    virtual bool delete_in_progress() const { return delete_in_progress_; }
    virtual const IPeer *Peer() const { return NULL; }
    virtual BgpAttrPtr GetRouteTargetRouteAttr() const { return NULL; }
    virtual int local_autonomous_system() const { return local_as_; }

    virtual bool IsSubscriptionGrStale(RoutingInstance *instance) const {
        return dynamic_cast<RoutingInstanceTest *>(instance)->IsStale();
    }

    virtual bool IsSubscriptionLlgrStale(RoutingInstance *instance) const {
        return dynamic_cast<RoutingInstanceTest *>(instance)->IsLlgrStale();
    }

    void VerifyEnqueued(uint32_t expected_flags, bool enqueued = true) {
        if (!enqueued || empty_subscription_) {
            EXPECT_TRUE(!enqueued_);
            return;
        }
        EXPECT_TRUE(enqueued_);
        EXPECT_EQ(expected_flags, enqueued_flags_);
        enqueued_ = false;
        enqueued_flags_ = 0;
    }

    void set_enqueued_flags(uint32_t flags) { enqueued_flags_ = flags; }
    void set_enqueued(bool enqueued) { enqueued_ = enqueued; }
    void set_verify(bool verify) { verify_ = verify; }

    virtual void Enqueue(DBRequest *req) const {
        RTargetTable::RequestData *data =
            dynamic_cast<RTargetTable::RequestData *>(req->data.get());
        if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
            const_cast<BgpXmppRTargetManagerTest *>(this)->
                set_enqueued_flags(data->nexthop().flags_);
        } else {
            EXPECT_EQ(NULL, data);
        }

        const_cast<BgpXmppRTargetManagerTest *>(this)->set_enqueued(true);
        req->key.reset();
        req->data.reset();
    }

    virtual const RouteTargetList &GetSubscribedRTargets(
            RoutingInstance *instance) const {
        return rtarget_list_;
    }

    RouteTargetList *rtarget_list() { return &rtarget_list_; }
    virtual void RTargetRouteOp(as4_t asn, const RouteTarget &rtarget,
                                BgpAttrPtr attr, bool add_change,
                                uint32_t flags) const {
        BgpXmppRTargetManager::RTargetRouteOp(asn, rtarget, attr, add_change,
                                              flags);

        if (!add_change || !verify_)
            return;
        uint32_t expected_flags = 0;
        if (instance1_->gr_state() & BgpPath::Stale &&
            instance2_->gr_state() & BgpPath::Stale)
            expected_flags |= BgpPath::Stale;
        if (instance1_->gr_state() & BgpPath::LlgrStale &&
            instance2_->gr_state() & BgpPath::LlgrStale)
            expected_flags |= BgpPath::LlgrStale;
            const_cast<BgpXmppRTargetManagerTest *>(this)->VerifyEnqueued(
                    expected_flags);
    }

private:
    RoutingInstanceTest *instance1_;
    RoutingInstanceTest *instance2_;
    bool empty_subscription_;
    bool delete_in_progress_;
    int local_as_;
    bool enqueued_;
    uint32_t enqueued_flags_;
    RouteTargetList rtarget_list_;
    bool verify_;
};

// GTest parameterize for
//     Stale/LlgrStale subscription state for the two routing-instances
//     SubscriptionList empty or not
typedef std::tr1::tuple<uint32_t, uint32_t, bool> TestParams;
class BgpXmppRTargetManagerTestRun :
    public ::testing::TestWithParam<TestParams> {
protected:
    BgpXmppRTargetManagerTestRun() :
            instance1_(new RoutingInstanceTest("instance1")),
            instance2_(new RoutingInstanceTest("instance2")),
            rtarget_manager_test_(
                    new BgpXmppRTargetManagerTest(instance1_.get(),
                                                  instance2_.get())) {
    }

    virtual void SetUp() {
        instance1_->set_gr_state(std::tr1::get<0>(GetParam()));
        instance2_->set_gr_state(std::tr1::get<1>(GetParam()));
        rtarget_manager_test_->set_empty_subscription(
                std::tr1::get<2>(GetParam()));
    }

    virtual void TearDown() { }

    boost::scoped_ptr<RoutingInstanceTest> instance1_;
    boost::scoped_ptr<RoutingInstanceTest> instance2_;
    boost::scoped_ptr<BgpXmppRTargetManagerTest> rtarget_manager_test_;
};

TEST_P(BgpXmppRTargetManagerTestRun, Test) {
    rtarget_manager_test_->rtarget_list()->insert(
            RouteTarget::FromString("target:1:1"));
    rtarget_manager_test_->PublishRTargetRoute(instance1_.get(), true);
    rtarget_manager_test_->VerifyEnqueued(0);

    rtarget_manager_test_->rtarget_list()->insert(
            RouteTarget::FromString("target:1:2"));
    rtarget_manager_test_->PublishRTargetRoute(instance1_.get(), true);
    rtarget_manager_test_->VerifyEnqueued(0);

    rtarget_manager_test_->rtarget_list()->clear();

    rtarget_manager_test_->rtarget_list()->insert(
            RouteTarget::FromString("target:2:1"));
    rtarget_manager_test_->PublishRTargetRoute(instance2_.get(), true);
    rtarget_manager_test_->VerifyEnqueued(0);

    rtarget_manager_test_->rtarget_list()->insert(
            RouteTarget::FromString("target:2:2"));
    rtarget_manager_test_->PublishRTargetRoute(instance2_.get(), true);
    rtarget_manager_test_->VerifyEnqueued(0);

    // Additional instances to already added target should not result in new
    // route enqueues.
    rtarget_manager_test_->rtarget_list()->insert(
            RouteTarget::FromString("target:2:1"));
    rtarget_manager_test_->PublishRTargetRoute(instance1_.get(), true);
    rtarget_manager_test_->VerifyEnqueued(0, false);

    rtarget_manager_test_->rtarget_list()->insert(
            RouteTarget::FromString("target:2:2"));
    rtarget_manager_test_->PublishRTargetRoute(instance1_.get(), true);
    rtarget_manager_test_->VerifyEnqueued(0, false);

    rtarget_manager_test_->rtarget_list()->clear();

    rtarget_manager_test_->rtarget_list()->insert(
            RouteTarget::FromString("target:1:1"));
    rtarget_manager_test_->PublishRTargetRoute(instance2_.get(), true);
    rtarget_manager_test_->VerifyEnqueued(0, false);

    rtarget_manager_test_->rtarget_list()->insert(
            RouteTarget::FromString("target:1:2"));
    rtarget_manager_test_->PublishRTargetRoute(instance2_.get(), true);
    rtarget_manager_test_->VerifyEnqueued(0, false);

    // Verify all rtarget routes update for new gr flags.
    rtarget_manager_test_->set_verify(true);

    // Trigger identifier change process to trigger GR flags update.
    rtarget_manager_test_->IdentifierUpdateCallback(Ip4Address());

    // Trigger asn change process to trigger GR flags update.
    rtarget_manager_test_->ASNUpdateCallback(0, 1);
}

INSTANTIATE_TEST_CASE_P(
    BgpXmppRTargetManagerTestRunWithParams,
    BgpXmppRTargetManagerTestRun,
    ::testing::Combine(
        ::testing::Values(
            0,
            BgpPath::Stale,
            BgpPath::Stale | BgpPath::LlgrStale
        ),
        ::testing::Values(
            0,
            BgpPath::Stale,
            BgpPath::Stale | BgpPath::LlgrStale
        ),
        ::testing::Bool()
    )
);

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
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
