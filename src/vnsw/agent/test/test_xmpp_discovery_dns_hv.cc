#include "base/os.h"
#include "test/test_xmpp_discovery_dns.cc"


int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    LoggingInit();
    Sandesh::SetLocalLogging(true);
    Sandesh::SetLoggingLevel(SandeshLevel::UT_DEBUG);

    Agent::GetInstance()->set_headless_agent_mode(true);
    Agent::GetInstance()->set_xmpp_dns_test_mode(true);
    Agent::GetInstance()->controller()->unicast_cleanup_timer().set_stale_timer_interval(200);
    Agent::GetInstance()->controller()->multicast_cleanup_timer().set_stale_timer_interval(300);
    Agent::GetInstance()->controller()->config_cleanup_timer().set_stale_timer_interval(500);
    int ret = RUN_ALL_TESTS();

    TestShutdown();
    delete client;
    return ret;
}
