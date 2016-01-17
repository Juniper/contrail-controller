#include "base/os.h"
#include "test/test_xmpp.cc"


int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    Agent::GetInstance()->set_controller_ifmap_xmpp_server("127.0.0.1", 0);
    Agent::GetInstance()->controller()->SetAgentMcastLabelRange(0);

    LoggingInit();
    Sandesh::SetLocalLogging(true);
    Sandesh::SetLoggingLevel(SandeshLevel::UT_DEBUG); 

    Agent::GetInstance()->set_headless_agent_mode(false);
    int ret = RUN_ALL_TESTS();

    Agent::GetInstance()->event_manager()->Shutdown();
    TestShutdown();
    delete client;
    return ret;
}
