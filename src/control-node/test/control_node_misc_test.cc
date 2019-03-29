/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

#include "base/test/task_test_util.h"
#include "control-node/control_node.h"

using process::ProcessState;

class ControlNodeMiscTest : public ::testing::Test {
protected:
};

TEST_F(ControlNodeMiscTest, StateMessage) {
    ProcessState::type state = ProcessState::FUNCTIONAL;
    string message;
    EXPECT_EQ("IFMap Server End-Of-RIB not computed, "
        "No BGP configuration for self",
        ControlNode::GetProcessState(false, false, false, &state, &message));
    EXPECT_EQ(ProcessState::NON_FUNCTIONAL, state);

    state = ProcessState::FUNCTIONAL;
    message = "Rabbit Connection down";
    EXPECT_EQ("Rabbit Connection down, IFMap Server End-Of-RIB not computed, "
        "No BGP configuration for self, BGP is administratively down",
        ControlNode::GetProcessState(false, true, false, &state, &message));
    EXPECT_EQ(ProcessState::NON_FUNCTIONAL, state);

    state = ProcessState::FUNCTIONAL;
    message = "Rabbit Connection down";
    EXPECT_EQ("Rabbit Connection down, IFMap Server End-Of-RIB not computed, "
        "No BGP configuration for self",
        ControlNode::GetProcessState(false, false, false, &state, &message));
    EXPECT_EQ(ProcessState::NON_FUNCTIONAL, state);

    state = ProcessState::FUNCTIONAL;
    message = "Rabbit Connection down, Cassandra Connection down";
    EXPECT_EQ("Rabbit Connection down, Cassandra Connection down, "
        "IFMap Server End-Of-RIB not computed, "
        "No BGP configuration for self",
        ControlNode::GetProcessState(false, false, false, &state, &message));
    EXPECT_EQ(ProcessState::NON_FUNCTIONAL, state);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
