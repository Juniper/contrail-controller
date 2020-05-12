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
protected:
    void run(const string &path);
};


void BgpCatTest::run(const string &path) {
    pid_t child = 0;
    char *const argv[5] = {
        (char *) "go",
        (char *) "test",
        (char *) "--timeout 20m",
        (char *) "-cover",
        NULL
    };

    if (!(child = vfork())) {
        chdir(path.c_str());
        execv("../../../../third_party/go/bin/go", argv);
    }
    int status = 0;
    waitpid(child, &status, 0);
    if (WEXITSTATUS(status))
        cout << path << " failed" << endl;
    EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST_F(BgpCatTest, BasicGoLang) {
    run("controller/src/cat/config");
    run("controller/src/cat/controlnode");
    run("controller/src/cat/agent");
    run("controller/src/cat/test");

}

static void SetUp() {
    system("./src/contrail-api-client/generateds/generateDS.py -f -o "
           "controller/src/cat/types -g golang-api "
           "src/contrail-api-client/schema/ietf-l3vpn-schema.xsd");
    system("./src/contrail-api-client/generateds/generateDS.py -f -o "
           "controller/src/cat/types -g golang-api "
           "src/contrail-api-client/schema/bgp_schema.xsd");
    system("./src/contrail-api-client/generateds/generateDS.py -f -o "
           "controller/src/cat/types -g golang-api "
           "src/contrail-api-client/schema/vnc_cfg.xsd");

    boost::filesystem::path cwd(boost::filesystem::current_path());
    setenv("GOPATH", cwd.string().c_str(), true);
}

static void TearDown() {
    system("rm -rf controller/src/cat/types");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
