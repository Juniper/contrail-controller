#include <base/os.h>
#include <iostream>
#include <boost/program_options.hpp>
#include <testing/gunit.h>
#include <test/test_cmn_util.h>
#include "test-xml/test_xml.h"
#include "test-xml/test_xml_oper.h"
#include "oper/test/test_xml_physical_device.h"
#include "test_xml_flow_agent_init.h"

using namespace std;
namespace opt = boost::program_options;

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

class TestPkt : public ::testing::Test {
};

TEST_F(TestPkt, parse_1) {
    AgentUtXmlTest test("controller/src/vnsw/agent/pkt/test/pkt-parse.xml");
    AgentUtXmlOperInit(&test);
    if (test.Load() == true) {
        test.ReadXml();

        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
}

TEST_F(TestPkt, ingress_flow_1) {
    AgentUtXmlTest test("controller/src/vnsw/agent/pkt/test/ingress-flow.xml");
    AgentUtXmlOperInit(&test);
    if (test.Load() == true) {
        test.ReadXml();

        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
}

TEST_F(TestPkt, egress_flow_1) {
    AgentUtXmlTest test("controller/src/vnsw/agent/pkt/test/egress-flow.xml");
    AgentUtXmlOperInit(&test);
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

    client = XmlPktParseTestInit(init_file, ksync_init);
    client->agent()->flow_stats_collector()->set_expiry_time(1000*1000);
    client->agent()->flow_stats_collector()->set_delete_short_flow(false);
    usleep(1000);
    client->WaitForIdle();
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
