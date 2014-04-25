#include "test/test_xmppcs.cc"

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    Agent::GetInstance()->SetXmppServer("127.0.0.1", 0);
    Agent::GetInstance()->SetXmppServer("127.0.0.2", 1);

    Agent::GetInstance()->set_headless_agent_mode(false);
    int ret = RUN_ALL_TESTS();

    Agent::GetInstance()->GetEventManager()->Shutdown();
    AsioStop();
    return ret;
}

