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

TEST_F(BgpCatTest, BasicPython) {
    int e;
    e = system("sudo pip install PyUnitReport 2>/dev/null");
    e = system("sudo pip install colorama 2>/dev/null");
    e = system("sudo apt-get -y install liblog4cplus-dev 2>/dev/null");
    e = system("sudo yum -y install liblog4cplus-dev 2>/dev/null");
    EXPECT_EQ(true, e == 0 || e != 0);

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
        cout << "CAT PYTHON TESTS FAILED" << endl;
    EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST_F(BgpCatTest, BasicGoLang) {
    pid_t child = 0;
    char *const argv[3] = {
        (char *) "go",
        (char *) "test",
        NULL
    };
    if (!(child = vfork())) {
        chdir("controller/src/bgp/test/cat/lib");
        execv("../../../../../../third_party/go/bin/go", argv);
    }
    int status = 0;
    waitpid(child, &status, 0);
    if (WEXITSTATUS(status))
        cout << "CAT GOLANG TESTS FAILED" << endl;
    EXPECT_EQ(0, WEXITSTATUS(status));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
