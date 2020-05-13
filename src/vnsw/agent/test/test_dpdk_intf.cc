#include "base/os.h"
#include "test/test_cmn_util.h"
#include <cmn/agent_cmn.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/ksync/vnswif_listener_base.h>

void RouterIdDepInit(Agent *agent) {
}

class DpdkIntfTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        vnswif_ = agent_->ksync()->vnsw_interface_listner();
        client->WaitForIdle();
    }

    virtual void TearDown() {
        client->WaitForIdle();
    }

    Agent *agent_;
    VnswInterfaceListener *vnswif_;
};

TEST_F(DpdkIntfTest, dpdk_intf_status) {

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    InterfaceTable *table = agent_->interface_table();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortFind(1));
    VmInterface *vm_interface = static_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vm_interface->IsActive());
    VnswInterfaceListener::HostInterfaceEntry *host_intf = vnswif_->GetHostInterfaceEntry("vnet1");

    host_intf->link_up_= false;

    vm_interface->set_test_oper_state(false);
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, vm_interface->GetUuid(),
                                     vm_interface->name()));
    req.data.reset(new VmInterfaceOsOperStateData(false));
    table->Enqueue(&req);
    client->WaitForIdle();
    EXPECT_FALSE(vm_interface->IsActive());

    host_intf->link_up_= true;
    client->WaitForIdle();
    agent_->set_test_mode(false);
    EXPECT_TRUE(vm_interface->IsActive());
    agent_->set_test_mode(true);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
}

class SetupTask : public Task {
    public:
        SetupTask() :
            Task((TaskScheduler::GetInstance()->
                  GetTaskId("db::DBTable")), 0) {
        }

        virtual bool Run() {
            Agent::GetInstance()->ksync()->vnsw_interface_listner()->Shutdown();
            return true;
        }
    std::string Description() const { return "SetupTask"; }
};

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(DEFAULT_VNSW_DPDK_CONFIG_FILE,
                      ksync_init, true, true, true,
                      AgentParam::kAgentStatsInterval,
                      AgentParam::kFlowStatsInterval, true, false);

    Agent::GetInstance()->ksync()->VnswInterfaceListenerInit();
    Agent::GetInstance()->set_router_id(Ip4Address::from_string("10.1.1.1"));

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    SetupTask * task = new SetupTask();
    TaskScheduler::GetInstance()->Enqueue(task);
    client->WaitForIdle();
    delete client;
    return ret;
}
