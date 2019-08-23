/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/filesystem.hpp>
#include <unistd.h>

#include "base/test/task_test_util.h"
#include "bgp/bgp_log.h"
#include "testing/gunit.h"

using std::cout;
using std::endl;
using std::string;

class BgpCatTest : public ::testing::Test {
};

TEST_F(BgpCatTest, BasicGoLang) {
    pid_t child = 0;
    char *const argv[3] = {
        (char *) "go",
        (char *) "test",
        NULL
    };
    boost::filesystem::path cwd(boost::filesystem::current_path());
    if (!(child = vfork())) {
        setenv("GOPATH", cwd.string().c_str(), true);
        chdir("controller/src/cat/test");
        execv("../../../../third_party/go/bin/go", argv);
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
