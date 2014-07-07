/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include <boost/scoped_ptr.hpp>
#include <boost/optional.hpp>
#include <testing/gunit.h>
#include <base/logging.h>

#include <bfd/bfd_state_machine.h>
#include "bfd/test/bfd_test_utils.h"

using namespace BFD;

class StateMachineTest : public ::testing::Test {
  public:
    StateMachineTest() : sm(CreateStateMachine(&evm)) {}

    EventManager evm;
    boost::scoped_ptr<StateMachine> sm;
};

TEST_F(StateMachineTest, Test1) {
    EXPECT_EQ(kInit, sm->GetState());
    sm->ProcessRemoteState(kInit);
    EXPECT_EQ(kUp, sm->GetState());
    sm->ProcessRemoteState(kInit);
    EXPECT_EQ(kUp, sm->GetState());
    sm->ProcessRemoteState(kUp);
    EXPECT_EQ(kUp, sm->GetState());
    sm->ProcessRemoteState(kInit);
    EXPECT_EQ(kUp, sm->GetState());
    sm->ProcessRemoteState(kDown);
    EXPECT_EQ(kDown, sm->GetState());
    sm->ProcessRemoteState(kUp);
    EXPECT_EQ(kDown, sm->GetState());
    sm->ProcessRemoteState(kDown);
    EXPECT_EQ(kInit, sm->GetState());
    sm->ProcessRemoteState(kDown);
    EXPECT_EQ(kInit, sm->GetState());
    sm->ProcessRemoteState(kUp);
    EXPECT_EQ(kUp, sm->GetState());
}

TEST_F(StateMachineTest, Test2) {
    EXPECT_EQ(kInit, sm->GetState());
    sm->ProcessTimeout();
    EXPECT_EQ(kDown, sm->GetState());
    sm->ProcessTimeout();
    EXPECT_EQ(kDown, sm->GetState());
    sm->ProcessRemoteState(kDown);
    EXPECT_EQ(kInit, sm->GetState());
    sm->ProcessRemoteState(kInit);
    EXPECT_EQ(kUp, sm->GetState());
    sm->ProcessTimeout();
    EXPECT_EQ(kDown, sm->GetState());
}

static void ChangeCallback(boost::optional<BFDState> *output, BFDState input) {
    *output = input;
}

TEST_F(StateMachineTest, Test_Callback) {
    EventManagerThread evt(&evm);

    boost::optional<BFDState> state;
    boost::optional<StateMachine::ChangeCb> cb(boost::bind(&ChangeCallback, &state, _1));
    sm->SetCallback(cb);

    EXPECT_EQ(kInit, sm->GetState());

    sm->ProcessTimeout();
    TASK_UTIL_EXPECT_TRUE(state.is_initialized());
    EXPECT_EQ(kDown, state.get());
    EXPECT_EQ(kDown, sm->GetState());
    state.reset();

    sm->ProcessTimeout();
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    EXPECT_FALSE(state.is_initialized());
    EXPECT_EQ(kDown, sm->GetState());

    sm->ProcessRemoteState(kDown);
    TASK_UTIL_EXPECT_TRUE(state.is_initialized());
    EXPECT_EQ(kInit, state.get());
    EXPECT_EQ(kInit, sm->GetState());
    state.reset();

    sm->ProcessRemoteState(kInit);
    TASK_UTIL_EXPECT_TRUE(state.is_initialized());
    EXPECT_EQ(kUp, state.get());
    EXPECT_EQ(kUp, sm->GetState());
    state.reset();

    sm->ProcessRemoteState(kUp);
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    EXPECT_FALSE(state.is_initialized());
    EXPECT_EQ(kUp, sm->GetState());

    sm->ProcessTimeout();
    TASK_UTIL_EXPECT_TRUE(state.is_initialized());
    EXPECT_EQ(kDown, state.get());
    EXPECT_EQ(kDown, sm->GetState());
    state.reset();
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
