#include "base/os.h"
#include "test/test_xmppcs_bcast.cc"

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    Agent::GetInstance()->set_controller_ifmap_xmpp_server("127.0.0.2", 0);
    Agent::GetInstance()->set_controller_ifmap_xmpp_server("127.0.0.1", 1);
    Agent::GetInstance()->SetAgentMcastLabelRange(0);
    Agent::GetInstance()->SetAgentMcastLabelRange(1);
    Agent::GetInstance()->set_headless_agent_mode(true);

    int ret = RUN_ALL_TESTS();
    Agent::GetInstance()->event_manager()->Shutdown();
    //AsioStop();
    //TaskScheduler::GetInstance()->Terminate();
    TestShutdown();
    delete client;
    return ret;
}
