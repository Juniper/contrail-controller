#include "testing/gunit.h"
#include "oper/instance_manager_adapter.h"
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

        properties_.vmi_inside = gen();
        properties_.vmi_outside = gen();
        properties_.vmi_management = gen();

        properties_.mac_addr_inside = "01:00:00:00:00:00";
        properties_.mac_addr_outside = "02:00:00:00:00:00";
        properties_.mac_addr_management = "03:00:00:00:00:00";

        properties_.ip_addr_inside = "10.0.0.1";
        properties_.ip_addr_outside = "10.0.1.1";
        properties_.ip_addr_management = "10.0.2.1";

        properties_.image_name = "test";

        properties_.ip_prefix_len_inside = 24;
        properties_.ip_prefix_len_outside = 24;
        properties_.ip_prefix_len_management = 24;

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
