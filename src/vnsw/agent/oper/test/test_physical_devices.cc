#include <base/os.h>
#include <iostream>
#include <boost/program_options.hpp>
#include <testing/gunit.h>
#include <test/test_cmn_util.h>
#include "test-xml/test_xml.h"
#include "test-xml/test_xml_oper.h"
#include "test_xml_physical_device.h"
#include "test_xml_agent_init.h"

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

class TestPhysicalDevice : public ::testing::Test {
};

TEST_F(TestPhysicalDevice, device_1) {
    AgentUtXmlTest test("controller/src/vnsw/agent/oper/test/physical-device.xml");
    AgentUtXmlOperInit(&test);
    AgentUtXmlPhysicalDeviceInit(&test);
    if (test.Load() == true) {
        test.ReadXml();

        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
}

TEST_F(TestPhysicalDevice, physical_port_1) {
    AgentUtXmlTest test("controller/src/vnsw/agent/oper/test/physical-interface.xml");
    AgentUtXmlOperInit(&test);
    AgentUtXmlPhysicalDeviceInit(&test);
    if (test.Load() == true) {
        test.ReadXml();

        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
}

TEST_F(TestPhysicalDevice, remote_physical_port_1) {
    AgentUtXmlTest test("controller/src/vnsw/agent/oper/test/remote-physical-interface.xml");
    AgentUtXmlOperInit(&test);
    AgentUtXmlPhysicalDeviceInit(&test);
    if (test.Load() == true) {
        test.ReadXml();

        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
}

TEST_F(TestPhysicalDevice, logical_port_1) {
    AgentUtXmlTest test("controller/src/vnsw/agent/oper/test/logical-interface.xml");
    AgentUtXmlOperInit(&test);
    AgentUtXmlPhysicalDeviceInit(&test);
    if (test.Load() == true) {
        test.ReadXml();

        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
}

TEST_F(TestPhysicalDevice, physical_device_vn_1) {
    AgentUtXmlTest test("controller/src/vnsw/agent/oper/test/physical-device-vn.xml");
    AgentUtXmlOperInit(&test);
    AgentUtXmlPhysicalDeviceInit(&test);
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

    client = PhysicalDeviceTestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
