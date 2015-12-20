#include "base/os.h"
#include "base/logging.h"
#include <boost/filesystem.hpp>
#include "oper/loadbalancer_config.h"
#include "oper/operdb_init.h"
#include "oper/instance_manager.h"

#include <cstdlib>
#include <boost/uuid/random_generator.hpp>
#include "base/logging.h"
#include "testing/gunit.h"

#include "oper/loadbalancer_pool_info.h"
#include "cmn/agent.h"
#include "test/test_init.h"

using namespace std;
class Agent;
void RouterIdDepInit(Agent *agent) {
}


using boost::uuids::uuid;

class LoadbalancerConfigTest : public ::testing::Test {
  protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(LoadbalancerConfigTest, GenerateConfig) {
    boost::uuids::random_generator gen;
    uuid pool_id = gen();

    LoadBalancerPoolInfo props;
    props.set_vip_uuid(gen());

    autogen::LoadbalancerPoolType pool_attr;
    pool_attr.protocol = "HTTP";
    props.set_pool_properties(pool_attr);

    autogen::VirtualIpType vip_attr;
    vip_attr.address = "127.0.0.1";
    vip_attr.protocol = "HTTP";
    vip_attr.protocol_port = 80;
    vip_attr.connection_limit = 100;
    props.set_vip_properties(vip_attr);

    autogen::LoadbalancerMemberType member;
    member.address = "127.0.0.2";
    member.protocol_port = 80;
    member.weight = 10;
    props.members()->insert(std::make_pair(gen(), member));
    
    stringstream ss;
    boost::filesystem::path curr_dir(boost::filesystem::current_path());
    ss << curr_dir.string() << "/" << getpid() << ".conf";
    Agent::GetInstance()->oper_db()->instance_manager()->lb_config().GenerateConfig(ss.str(), pool_id, props);
    boost::system::error_code error;
    boost::filesystem::remove_all(ss.str(), error);
    if (error) {
        LOG(ERROR, "Error: " << error.message() << "in removing the file" );
    }
}

//Test to make sure we don't http-check to config file unless monitor_type is HTTP
TEST_F(LoadbalancerConfigTest, GenerateConfig_with_Monitor) {
    boost::uuids::random_generator gen;
    uuid pool_id = gen();

    LoadBalancerPoolInfo props;
    props.set_vip_uuid(gen());

    autogen::LoadbalancerPoolType pool_attr;
    pool_attr.protocol = "HTTP";
    props.set_pool_properties(pool_attr);

    autogen::VirtualIpType vip_attr;
    vip_attr.address = "127.0.0.1";
    vip_attr.protocol = "HTTP";
    vip_attr.protocol_port = 80;
    vip_attr.connection_limit = 100;
    props.set_vip_properties(vip_attr);

    autogen::LoadbalancerMemberType member;
    member.address = "127.0.0.2";
    member.protocol_port = 80;
    member.weight = 10;
    props.members()->insert(std::make_pair(gen(), member));

    autogen::LoadbalancerHealthmonitorType healthmonitor;
    healthmonitor.monitor_type = "PING";
    healthmonitor.timeout = 30;
    healthmonitor.max_retries = 3;
    healthmonitor.http_method = "GET";
    healthmonitor.expected_codes = "200";
    props.healthmonitors()->insert(std::make_pair(gen(), healthmonitor));

    stringstream ss;
    boost::filesystem::path curr_dir(boost::filesystem::current_path());
    ss << curr_dir.string() << "/" << getpid() << ".conf";
    Agent::GetInstance()->oper_db()->instance_manager()->lb_config().GenerateConfig(ss.str(), pool_id, props);

    ifstream file(ss.str().c_str());
    if (file) {
        string file_str((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
        string search_str = "http-check";
        size_t found = file_str.find(search_str);
        EXPECT_EQ(found, string::npos);
    }

    boost::system::error_code error;
    boost::filesystem::remove_all(ss.str(), error);
    if (error) {
        LOG(ERROR, "Error: " << error.message() << "in removing the file" );
    }
}

//Test to make sure HTTPS monitor adds SSL check
TEST_F(LoadbalancerConfigTest, GenerateConfig_with_SSL_Monitor) {
    boost::uuids::random_generator gen;
    uuid pool_id = gen();

    LoadBalancerPoolInfo props;
    props.set_vip_uuid(gen());

    autogen::LoadbalancerPoolType pool_attr;
    pool_attr.protocol = "TCP";
    props.set_pool_properties(pool_attr);

    autogen::VirtualIpType vip_attr;
    vip_attr.address = "127.0.0.1";
    vip_attr.protocol = "TCP";
    vip_attr.protocol_port = 443;
    vip_attr.connection_limit = 100;
    props.set_vip_properties(vip_attr);

    autogen::LoadbalancerMemberType member;
    member.address = "127.0.0.2";
    member.protocol_port = 443;
    member.weight = 10;
    props.members()->insert(std::make_pair(gen(), member));

    autogen::LoadbalancerHealthmonitorType healthmonitor;
    healthmonitor.monitor_type = "HTTPS";
    healthmonitor.timeout = 30;
    healthmonitor.max_retries = 3;
    healthmonitor.http_method = "GET";
    healthmonitor.expected_codes = "200";
    props.healthmonitors()->insert(std::make_pair(gen(), healthmonitor));

    stringstream ss;
    boost::filesystem::path curr_dir(boost::filesystem::current_path());
    ss << curr_dir.string() << "/" << getpid() << ".conf";
    Agent::GetInstance()->oper_db()->instance_manager()->lb_config().GenerateConfig(ss.str(), pool_id, props);

    ifstream file(ss.str().c_str());
    if (file) {
        string file_str((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
        string search_str1 = "HTTPS";

        size_t found = file_str.find(search_str1);
        EXPECT_NE(found, string::npos);
    }

    boost::system::error_code error;
    boost::filesystem::remove_all(ss.str(), error);
    if (error) {
        LOG(ERROR, "Error: " << error.message() << "in removing the file" );
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    LoggingInit();
    int result = RUN_ALL_TESTS();
    TestShutdown();
    client->WaitForIdle();
    delete client;

    return result;
}
