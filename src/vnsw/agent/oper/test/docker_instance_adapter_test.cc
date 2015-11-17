#include "testing/gunit.h"
#include "oper/docker_instance_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

class Agent;

class DockerAdapterTest : public ::testing::Test {
    protected:

    virtual void SetUp() {
        boost::uuids::random_generator gen;
        agent_ =  new Agent;
        docker_adapter = new DockerInstanceAdapter("/bin/true", agent_);
        
        properties_.virtualization_type = ServiceInstance::VRouterInstance;
        properties_.vrouter_instance_type = ServiceInstance::Docker;
        properties_.instance_id = gen();

        ServiceInstance::InterfaceData vmi_data_inside;
        ServiceInstance::InterfaceData vmi_data_outside;
        ServiceInstance::InterfaceData vmi_data_management;

        vmi_data_inside.vmi_uuid = gen();
        vmi_data_outside.vmi_uuid = gen();
        vmi_data_management.vmi_uuid = gen();

        vmi_data_inside.intf_type = "left";
        vmi_data_outside.intf_type = "right";
        vmi_data_management.intf_type = "management";

        vmi_data_inside.mac_addr = "01:00:00:00:00:00";
        vmi_data_outside.mac_addr = "02:00:00:00:00:00";
        vmi_data_management.mac_addr = "03:00:00:00:00:00";

        vmi_data_inside.ip_addr = "10.0.0.1";
        vmi_data_outside.ip_addr = "10.0.1.1";
        vmi_data_management.ip_addr = "10.0.2.1";

        properties_.image_name = "test";

        vmi_data_inside.ip_prefix_len = 24;
        vmi_data_outside.ip_prefix_len = 24;
        vmi_data_management.ip_prefix_len = 24;

        properties_.interfaces.push_back(vmi_data_inside);
        properties_.interfaces.push_back(vmi_data_outside);
        properties_.interfaces.push_back(vmi_data_management);
    }

    virtual void TearDown() {
        delete docker_adapter;
        delete agent_;
    }

    const ServiceInstance::Properties &properties() const { return properties_; }

    ServiceInstance::Properties properties_;
    DockerInstanceAdapter *docker_adapter;
    Agent* agent_;
};

TEST_F(DockerAdapterTest, IsApplicable) {
    ASSERT_TRUE(docker_adapter->isApplicable(properties()));

}

TEST_F(DockerAdapterTest, DockerStart) {
    InstanceTask* task = docker_adapter->CreateStartTask(properties(), false);
    std::string cmd = task->cmd();
    //s1.find(s2)
    ASSERT_NE(std::string::npos, cmd.find("create"));
    ASSERT_NE(std::string::npos, cmd.find("--image test"));
    ASSERT_NE(std::string::npos, cmd.find("--vmi-left-ip 10.0.0.1/24"));
    ASSERT_NE(std::string::npos, cmd.find("--vmi-left-mac 01:00:00:00:00:00"));
    ASSERT_NE(std::string::npos, cmd.find("--vmi-right-ip 10.0.1.1/24"));
    ASSERT_NE(std::string::npos, cmd.find("--vmi-right-mac 02:00:00:00:00:00"));
    ASSERT_NE(std::string::npos, cmd.find("--vmi-management-ip 10.0.2.1/24"));
    ASSERT_NE(std::string::npos, cmd.find("--vmi-management-mac 03:00:00:00:00:00"));
    delete task;
}

TEST_F(DockerAdapterTest, DockerStop) {
    InstanceTask* task = docker_adapter->CreateStopTask(properties());
    std::string cmd = task->cmd();

    ASSERT_NE(std::string::npos, cmd.find("destroy"));
    ASSERT_NE(std::string::npos, cmd.find("--vmi-left-id"));;
    ASSERT_NE(std::string::npos, cmd.find("--vmi-right-id"));
    ASSERT_NE(std::string::npos, cmd.find("--vmi-management-id"));;
    delete task;
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    int result = RUN_ALL_TESTS();
    return result;
}
