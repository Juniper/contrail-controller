/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "test_cmn_util.h"
#include "cmn/event_notifier.h"

void RouterIdDepInit(Agent *agent) {
}

typedef boost::shared_ptr<TaskTrigger> TaskTriggerPtr;

struct TestEventNotifyKey : public EventNotifyKey {
public:
    enum Type {
        A,
        B
    };
    TestEventNotifyKey(Type type) :
        EventNotifyKey(EventNotifyKey::GENERIC), type_(type) {
    }
    virtual ~TestEventNotifyKey() {
    }

    virtual bool IsLess(const EventNotifyKey &rhs) const {
        const TestEventNotifyKey *key =
            dynamic_cast<const TestEventNotifyKey *>(&rhs);
        if (key->type_ != type_) {
            return (key->type_ < type_);
        }
        return false;
    }
    Type type_;
};

class EventNotifierTest : public ::testing::Test {
public:
    EventNotifierTest() {
        agent_ = Agent::GetInstance();
        test_a_1_ = false;
        test_a_2_ = false;
        test_b_1_ = false;
    }
    virtual ~EventNotifierTest() { }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
    bool TestA_listener_1() {
        test_a_1_ = true;
        return true;
    }
    bool TestA_listener_2() {
        test_a_2_ = true;
        return true;
    }
    bool TestB_listener_1() {
        test_b_1_ = true;
        return true;
    }
    void Reset() {
        test_a_1_ = false;
        test_a_2_ = false;
        test_b_1_ = false;
    }
    bool test_a_1_;
    bool test_a_2_;
    bool test_b_1_;
    Agent *agent_;
};

TEST_F(EventNotifierTest, basic) {
}

TEST_F(EventNotifierTest, notify) {
    EventNotifyHandle::Ptr ptr1 = agent_->event_notifier()->RegisterSubscriber
        ((new TestEventNotifyKey(TestEventNotifyKey::A)),
         boost::bind(&EventNotifierTest::TestA_listener_1, this));
    EventNotifyHandle::Ptr ptr2 = agent_->event_notifier()->RegisterSubscriber
        ((new TestEventNotifyKey(TestEventNotifyKey::A)),
         boost::bind(&EventNotifierTest::TestA_listener_2, this));
    EventNotifyHandle::Ptr ptr3 = agent_->event_notifier()->RegisterSubscriber
        ((new TestEventNotifyKey(TestEventNotifyKey::B)),
         boost::bind(&EventNotifierTest::TestB_listener_1, this));

    agent_->event_notifier()->
        Notify(new TestEventNotifyKey(TestEventNotifyKey::A));
    WAIT_FOR(100, 100, (test_a_1_ == true));
    WAIT_FOR(100, 100, (test_a_2_ == true));
    EXPECT_TRUE(test_b_1_ == false);
    Reset();

    agent_->event_notifier()->
        Notify(new TestEventNotifyKey(TestEventNotifyKey::B));
    WAIT_FOR(100, 100, (test_b_1_ == true));
    EXPECT_TRUE(test_a_1_ == false);
    EXPECT_TRUE(test_a_2_ == false);
    Reset();

    agent_->event_notifier()->DeregisterSubscriber(ptr1);
    agent_->event_notifier()->
        Notify(new TestEventNotifyKey(TestEventNotifyKey::A));
    WAIT_FOR(100, 100, (test_a_2_ == true));
    EXPECT_TRUE(test_a_1_ == false);
    EXPECT_TRUE(test_b_1_ == false);
    Reset();

    agent_->event_notifier()->DeregisterSubscriber(ptr2);
    agent_->event_notifier()->DeregisterSubscriber(ptr3);
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false);
    client->WaitForIdle();
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
