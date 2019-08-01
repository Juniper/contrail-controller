/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

#include <unistd.h>
#include "base/test/task_test_util.h"
#include "bgp/bgp_log.h"
#include "testing/gunit.h"

using std::cout;
using std::endl;
using std::string;

class BgpCatTest : public ::testing::Test {
};

TEST_F(BgpCatTest, Basic) {
    system("sudo pip install PyUnitReport 2>/dev/null");
    system("sudo apt-get -y install liblog4cplus-dev 2>/dev/null");
    system("sudo yum -y install liblog4cplus-dev 2>/dev/null");

    EXPECT_EQ(0, access("/usr/bin/python", X_OK));

    // Wait for the mock agent and control-node binaries availability.
    string build_dir = getenv("CONTRAIL_BUILD_DIR") ?: "build/debug";
    TASK_UTIL_WAIT_EQ(0, access((build_dir +
        "/bgp/test/bgp_ifmap_xmpp_integration_test").c_str(), X_OK), 5000000,
            120, "Waiting for bgp_ifmap_xmpp_integration_test to get built");
    TASK_UTIL_WAIT_EQ(0, access((build_dir +
        "/vnsw/agent/contrail/contrail-vrouter-agent").c_str(), X_OK), 5000000,
            120, "Waiting for contrail-vrouter-agent to get built");

    // Wait for some time to ensure that linked excutables are ready.
    sleep(5);
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
        cout << "CAT TESTS FAILED" << endl;
    EXPECT_EQ(0, WEXITSTATUS(status));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
