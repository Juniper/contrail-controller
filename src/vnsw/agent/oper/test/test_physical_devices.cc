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
public:
    TestPhysicalDevice() { }
    ~TestPhysicalDevice() { }
    void SetUp() {
        agent_ = Agent::GetInstance();
        table_ = agent_->physical_device_table();
    }

    Agent *agent_;
    PhysicalDeviceTable *table_;
};

TEST_F(TestPhysicalDevice, device_master_1) {
    PhysicalDeviceTable *table = agent_->physical_device_table();
    boost::uuids::uuid u = MakeUuid(1);

    // When mastership for a ToR changes, multicast sets master_ field in
    // physical-device table. Ensure, that multicast uses RESYNC operation
    // for this.
    // Check that calling EnqueueDeviceChange does not create DBEntry
    table->EnqueueDeviceChange(u, true);
    TestClient::WaitForIdle();
    EXPECT_TRUE(PhysicalDeviceGet(1) == NULL);

    AddPhysicalDevice("dev-1", 1);
    TestClient::WaitForIdle();

    PhysicalDevice *dev = PhysicalDeviceGet(1);
    EXPECT_TRUE(dev != NULL);
    EXPECT_FALSE(dev->master());

    table->EnqueueDeviceChange(dev->uuid(), true);
    TestClient::WaitForIdle();
    EXPECT_TRUE(dev->master());

    table->EnqueueDeviceChange(dev->uuid(), false);
    TestClient::WaitForIdle();
    EXPECT_FALSE(dev->master());

    DeletePhysicalDevice("dev-1");
    TestClient::WaitForIdle();
    WAIT_FOR(1000, 100, (PhysicalDeviceGet(1) == NULL));
}

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

//Adds two TOR, one VM and then delete one TOR.
//Makes sure db state for that TOR is released on
//physical_device_vn.
//https://bugs.launchpad.net/juniperopenstack/+bug/1418192
TEST_F(TestPhysicalDevice, multicast_tor_1) {
    AgentUtXmlTest test("controller/src/vnsw/agent/oper/test/tor-multicast.xml");
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
