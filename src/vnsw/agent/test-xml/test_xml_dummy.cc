#include <iostream>
#include <boost/program_options.hpp>
#include <testing/gunit.h>
#include <test/test_cmn_util.h>
#include "test_xml.h"

using namespace std;
namespace opt = boost::program_options;

void RouterIdDepInit(Agent *agent) {
}

static void GetArgs(char *test_file, int argc, char *argv[]) {
    test_file[0] = '\0';
    opt::options_description desc("Options");
    opt::variables_map vars;
    desc.add_options()
        ("help", "Print help message")
        ("test-data", opt::value<string>(), "Specify test data file");

    opt::store(opt::parse_command_line(argc, argv, desc), vars);
    opt::notify(vars);
    if (vars.count("test-data")) {
        strcpy(test_file, vars["test-data"].as<string>().c_str());
    }
    return;
}

class TestDummy : public ::testing::Test {
};

TEST_F(TestDummy, test_1) {
    char test_file[1024];

    AgentUtXmlTest test("controller/src/vnsw/agent/test-xml/dummy.xml");
    if (test.Load() == true) {
        test.ReadXml();

        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
