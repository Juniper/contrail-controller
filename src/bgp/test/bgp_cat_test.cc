/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

#include <unistd.h>
#include "testing/gunit.h"

class BgpCatTest : public ::testing::Test {
};

TEST_F(BgpCatTest, Basic) {
    pid_t child = 0;
    char *const argv[3] = {
        (char *) "python",
        (char *) "controller/src/bgp/test/cat/tests/test_xmpp_peers.py",
        NULL
    };
    if (!(child = vfork()))
        execv("/usr/bin/python", argv);
    int status = 0;
    waitpid(child, &status, 0);
    if (WEXITSTATUS(status))
        std::cout << "CAT TESTS FAILED" << std::endl;
    EXPECT_EQ(0, WEXITSTATUS(status));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
