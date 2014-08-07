#include "oper/loadbalancer_haproxy.h"

#include <cstdlib>
#include <boost/uuid/random_generator.hpp>
#include "base/logging.h"
#include "testing/gunit.h"

#include "oper/loadbalancer_properties.h"

using boost::uuids::uuid;

class LoadbalancerHaproxyTest : public ::testing::Test {
  protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    LoadbalancerHaproxy haproxy_;
};

TEST_F(LoadbalancerHaproxyTest, GenerateConfig) {
    boost::uuids::random_generator gen;
    uuid pool_id = gen();

    LoadbalancerProperties props;
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
    
    std::stringstream ss;
    ss << "/tmp/" << getpid() << ".conf";
    haproxy_.GenerateConfig(ss.str(), pool_id, props);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    int result = RUN_ALL_TESTS();
    return result;
}
