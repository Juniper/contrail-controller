/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include <boost/scoped_ptr.hpp>
#include <testing/gunit.h>
#include <base/logging.h>

#include <bfd/bfd_state_machine.h>

using namespace BFD;

class StateMachineTest : public ::testing::Test {
 public:
    StateMachineTest() : sm(CreateStateMachine()) {}
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

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
