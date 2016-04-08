/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */


#include "base/os.h"
#include "oper/instance_manager.h"

#include <cstdlib>
#include <boost/filesystem.hpp>
#include <boost/uuid/random_generator.hpp>

#include "base/logging.h"
#include "testing/gunit.h"
#include <test/test_cmn_util.h>
#include "base/test/task_test_util.h"
#include "cfg/cfg_init.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_agent_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/test/ifmap_test_util.h"
#include "oper/ifmap_dependency_manager.h"
#include "oper/instance_task.h"
#include "oper/operdb_init.h"
#include "oper/service_instance.h"
#include "schema/vnc_cfg_types.h"

using namespace std;
class Agent;
void RouterIdDepInit(Agent *agent) {
}


static boost::uuids::uuid IdPermsGetUuid(const autogen::IdPermsType &id) {
    boost::uuids::uuid uuid;
    CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
    return uuid;
}

class InstanceManagerTest : public ::testing::Test {
public:
    bool IsExpectedStatusType(InstanceState *state, int expected) {
        return state->status_type() == expected;
    }

    bool IsUpdateCommand(InstanceState *state) {
        return string::npos != state->cmd().find("--update");
    }

    bool WaitForAWhile(time_t target) {
        time_t now = time(NULL);
        return now >= target;
    }

protected:
    static const int kTimeoutSeconds = 15;

    InstanceManagerTest() {
    }

    ~InstanceManagerTest() {
    }

    virtual void TearDown() {
        IFMapAgentStaleCleaner *cl = new IFMapAgentStaleCleaner(agent_->db(),
                    agent_->cfg()->cfg_graph());
        cl->StaleTimeout(1);
        task_util::WaitForIdle();
        delete cl;
    }


    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        stringstream ss;
        boost::filesystem::path curr_dir(boost::filesystem::current_path());
        ss << curr_dir.string() << "/" << getpid() << "/";
        agent_->oper_db()-> instance_manager()->loadbalancer_config_path_ = ss.str();

    }


    boost::uuids::uuid AddServiceInstance(const string &name) {
        ifmap_test_util::IFMapMsgNodeAdd(agent_->db(), "service-instance", name);
        task_util::WaitForIdle();
        IFMapTable *table = IFMapTable::FindTable(agent_->db(),
                                                  "service-instance");
        ServiceInstanceTable *si_table = agent_->service_instance_table();
        IFMapNode *node = table->FindNode(name);
        if (node == NULL) {
            return boost::uuids::nil_uuid();
        }
        DBRequest request;
        boost::uuids::uuid si_uuid;
        si_table->IFNodeToUuid(node, si_uuid);
        si_table->IFNodeToReq(node, request, si_uuid);
        si_table->Enqueue(&request);
        task_util::WaitForIdle();

        autogen::ServiceInstance *si_object =
                static_cast<autogen::ServiceInstance *>(node->GetObject());
        const autogen::IdPermsType &id = si_object->id_perms();
        boost::uuids::uuid instance_id = IdPermsGetUuid(id);
        ServiceInstanceKey key(instance_id);
        ServiceInstance *svc_instance =
                static_cast<ServiceInstance *>(si_table->Find(&key, true));
        if (svc_instance == NULL) {
            return boost::uuids::nil_uuid();
        }

        /*
         * Set non-nil uuids
         */
        UpdateProperties(svc_instance, true);
        return instance_id;
    }

    void DeleteServiceInstance(const string &name) {
        ifmap_test_util::IFMapMsgNodeDelete(agent_->db(), "service-instance", name);
    }

    void MarkServiceInstanceAsDeleted(boost::uuids::uuid id) {
        ServiceInstanceKey key(id);
        ServiceInstance *svc_instance =
                static_cast<ServiceInstance
                *>(agent_->service_instance_table()->Find(&key, true));
        if (svc_instance != NULL) {
            svc_instance->MarkDelete();
            agent_->service_instance_table()->Change(svc_instance);
        }
    }

    bool UpdateProperties(boost::uuids::uuid id, bool usable) {
        ServiceInstanceKey key(id);
        ServiceInstance *svc_instance = static_cast<ServiceInstance *>
                (agent_->service_instance_table()->Find(&key, true));
        if (svc_instance == NULL) {
            return false;
        }
        UpdateProperties(svc_instance, usable);

        return true;
    }

    ServiceInstance *GetServiceInstance(boost::uuids::uuid id) {
        ServiceInstanceKey key(id);
        ServiceInstance *svc_instance = static_cast<ServiceInstance *>
            (agent_->service_instance_table()->Find(&key, true));
        if (svc_instance == NULL) {
            return NULL;
        }
        return svc_instance;
    }

    InstanceState *ServiceInstanceState(boost::uuids::uuid id) {
        ServiceInstance *svc_instance = GetServiceInstance(id);
        if (svc_instance == NULL) {
            return NULL;
        }
        return agent_->oper_db()->instance_manager()->GetState(svc_instance);
    }

    InstanceTaskQueue *GetTaskQueue(boost::uuids::uuid id) {
        ServiceInstance *svc_instance = GetServiceInstance(id);
        if (svc_instance == NULL) {
            return NULL;
        }

        stringstream ss;
        ss << svc_instance->properties().instance_id;
        return agent_->oper_db()->instance_manager()->GetTaskQueue(ss.str());
    }

    void NotifyChange(boost::uuids::uuid id) {
        ServiceInstance *svc_instance = GetServiceInstance(id);
        if (svc_instance == NULL) {
            return;
        }
        agent_->service_instance_table()->Change(svc_instance);
    }

    void UpdateProperties(ServiceInstance* svc_instance, bool usable) {
        /*
         * Set non-nil uuids
         */
        ServiceInstance::Properties prop;
        prop.Clear();
        prop.virtualization_type = ServiceInstance::NetworkNamespace;
        boost::uuids::random_generator gen;
        prop.instance_id = svc_instance->uuid();
        prop.vmi_inside = gen();
        prop.vmi_outside = gen();
        prop.ip_addr_inside = "10.0.0.1";
        prop.ip_addr_outside = "10.0.0.2";
        prop.ip_prefix_len_inside = 24;
        prop.gw_ip = "10.0.0.254";
        if (usable) {
            prop.ip_prefix_len_outside = 24;
        }
        svc_instance->set_properties(prop);
        if (usable) {
            EXPECT_TRUE(svc_instance->IsUsable());
        } else {
            EXPECT_FALSE(svc_instance->IsUsable());
        }
        agent_->service_instance_table()->Change(svc_instance);
    }

    const std::string &loadbalancer_config_path() const {
        return agent_->oper_db()->instance_manager()->loadbalancer_config_path_;
    }
protected:
    Agent *agent_;
};

TEST_F(InstanceManagerTest, ExecTrue) {
    agent_->oper_db()->instance_manager()->SetNetNSCmd("/bin/true");
    boost::uuids::uuid id = AddServiceInstance("exec-true");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    InstanceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(InstanceState::Starting, ns_state->status_type());
    EXPECT_NE(0, ns_state->pid());

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Started),
            kTimeoutSeconds);

    EXPECT_EQ(InstanceState::Started, ns_state->status_type());
    EXPECT_EQ(0, ns_state->status());
    ASSERT_TRUE(ns_state->errors().empty() == true);

    DeleteServiceInstance("exec-true");
    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Stopped),
            kTimeoutSeconds);
    task_util::WaitForIdle();
    IFMapTable *table = IFMapTable::FindTable(agent_->db(),
                                                  "service-instance");
    IFMapNode *node = table->FindNode("exec-true");
    ASSERT_TRUE(node == NULL);
}

TEST_F(InstanceManagerTest, ExecNotExisting) {
    agent_->oper_db()->instance_manager()->SetNetNSCmd("/bin/junk");
    boost::uuids::uuid id = AddServiceInstance("exec-false");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    InstanceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(InstanceState::Starting, ns_state->status_type());
    ASSERT_TRUE(ns_state->errors().empty() == true);

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Error),
            kTimeoutSeconds);

    EXPECT_EQ(InstanceState::Error, ns_state->status_type());
    EXPECT_NE(0, ns_state->status());
    ASSERT_TRUE(ns_state->errors().empty() == false);

    DeleteServiceInstance("exec-false");
    task_util::WaitForIdle();
    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Stopped),
            kTimeoutSeconds);
    task_util::WaitForIdle();
    IFMapTable *table = IFMapTable::FindTable(agent_->db(),
                                                  "service-instance");
    IFMapNode *node = table->FindNode("exec-false");
    ASSERT_TRUE(node == NULL);
}

TEST_F(InstanceManagerTest, Update) {
    agent_->oper_db()->instance_manager()->SetNetNSCmd("/bin/true");
    boost::uuids::uuid id = AddServiceInstance("exec-update");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    InstanceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(InstanceState::Starting, ns_state->status_type());

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Started),
            kTimeoutSeconds);

    EXPECT_EQ(InstanceState::Started, ns_state->status_type());
    EXPECT_EQ(0, ns_state->status());

    bool updated = UpdateProperties(id, true);
    EXPECT_EQ(true, updated);

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsUpdateCommand, this, ns_state),
            kTimeoutSeconds);

    EXPECT_TRUE(IsUpdateCommand(ns_state));

    DeleteServiceInstance("exec-update");
    task_util::WaitForIdle();

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Stopped),
            kTimeoutSeconds);
    task_util::WaitForIdle();
    IFMapTable *table = IFMapTable::FindTable(agent_->db(),
                                                  "service-instance");
    IFMapNode *node = table->FindNode("exec-update");
    ASSERT_TRUE(node == NULL);

}

TEST_F(InstanceManagerTest, UpdateProperties) {
    agent_->oper_db()->instance_manager()->SetNetNSCmd("/bin/true");
    boost::uuids::uuid id = AddServiceInstance("exec-update");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    InstanceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(InstanceState::Starting, ns_state->status_type());

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Started),
            kTimeoutSeconds);

    EXPECT_EQ(InstanceState::Started, ns_state->status_type());
    EXPECT_EQ(0, ns_state->status());

    NotifyChange(id);
    task_util::WaitForIdle();

    EXPECT_NE(InstanceState::Starting, ns_state->status_type());

    bool updated = UpdateProperties(id, true);
    EXPECT_EQ(true, updated);

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Starting),
            kTimeoutSeconds);

    DeleteServiceInstance("exec-update");
    task_util::WaitForIdle();

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Stopped),
            kTimeoutSeconds);
    task_util::WaitForIdle();
    IFMapTable *table = IFMapTable::FindTable(agent_->db(),
                                                  "service-instance");
    IFMapNode *node = table->FindNode("exec-update");
    ASSERT_TRUE(node == NULL);
}

/**
 * Timeout test works by not plugin in the signal manager and receiving
 * SIGCHLD rather than by executing a sleep.
 */

#if 0
TEST_F(InstanceManagerTest, Timeout) {

    agent_->oper_db()->instance_manager()->SetNetNSCmd("/bin/tr");
    boost::uuids::uuid id = AddServiceInstance("exec-timeout");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    InstanceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(InstanceState::Starting, ns_state->status_type());

    time_t now = time(NULL);
    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::WaitForAWhile, this, now + 5),
            kTimeoutSeconds);

    EXPECT_EQ(InstanceState::Timeout, ns_state->status_type());

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}
#endif

TEST_F(InstanceManagerTest, TaskQueue) {
    static const int kNumUpdate = 5;
    agent_->oper_db()->instance_manager()->SetNetNSCmd("/bin/true");
    boost::uuids::uuid id = AddServiceInstance("exec-queue");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    InstanceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(InstanceState::Starting, ns_state->status_type());
    InstanceTaskQueue *queue = GetTaskQueue(id);

    EXPECT_EQ(1, queue->Size());

    for (int i = 0; i != kNumUpdate; i++) {
        bool updated = UpdateProperties(id, true);
        EXPECT_EQ(true, updated);
        task_util::WaitForIdle();

        EXPECT_EQ(i + 2, queue->Size());
    }

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Started),
            kTimeoutSeconds);

    EXPECT_EQ(InstanceState::Started, ns_state->status_type());
    EXPECT_EQ(0, ns_state->status());

    DeleteServiceInstance("exec-queue");
    task_util::WaitForIdle();

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Stopped),
            kTimeoutSeconds);
    task_util::WaitForIdle();
    IFMapTable *table = IFMapTable::FindTable(agent_->db(),
                                                  "service-instance");
    IFMapNode *node = table->FindNode("exec-queue");
    ASSERT_TRUE(node == NULL);
}
TEST_F(InstanceManagerTest, Usable) {
    agent_->oper_db()->instance_manager()->SetNetNSCmd("/bin/true");
    boost::uuids::uuid id = AddServiceInstance("exec-usable");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    InstanceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(InstanceState::Starting, ns_state->status_type());

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Started),
            kTimeoutSeconds);

    EXPECT_EQ(InstanceState::Started, ns_state->status_type());
    EXPECT_EQ(0, ns_state->status());

    NotifyChange(id);
    task_util::WaitForIdle();

    EXPECT_NE(InstanceState::Starting, ns_state->status_type());

    bool updated = UpdateProperties(id, false);
    EXPECT_EQ(true, updated);

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Stopped),
            kTimeoutSeconds);

    DeleteServiceInstance("exec-usable");
    task_util::WaitForIdle();

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&InstanceManagerTest::IsExpectedStatusType, this, ns_state, InstanceState::Stopped),
            kTimeoutSeconds);
    task_util::WaitForIdle();
    IFMapTable *table = IFMapTable::FindTable(agent_->db(),
                                                  "service-instance");
    IFMapNode *node = table->FindNode("exec-usable");
    ASSERT_TRUE(node == NULL);
}

TEST_F(InstanceManagerTest, InstanceStaleCleanup) {

    agent_->oper_db()->instance_manager()->SetNetNSCmd("/bin/true");
    boost::uuids::random_generator gen;
    std::string vm_uuid = UuidToString(gen());
    std::string lb_uuid = UuidToString(gen());

    boost::filesystem::path curr_dir (boost::filesystem::current_path());
    stringstream ss;
    ss << curr_dir.string() << "/" << getpid() << "/";
    string store_path = ss.str();

    agent_->oper_db()->instance_manager()->SetNamespaceStorePath(store_path.c_str());
    store_path +=  ("vrouter-" + vm_uuid + ":" + lb_uuid);
    boost::system::error_code error;
    if (!boost::filesystem::exists(store_path, error)) {
        boost::filesystem::create_directories(store_path, error);
        if (error) {
            LOG(ERROR, "Error : " << error.message() << "in creating directory");
        }
    }
    store_path  = loadbalancer_config_path();
    store_path += (lb_uuid + ".conf");
    if (!boost::filesystem::exists(store_path, error)) {
        std::ofstream fs(store_path.c_str());
        fs << "Test Config for " << lb_uuid;
        fs.close();
        if (error) {
            LOG(ERROR, "Error : " << error.message() << "in creating conf file");
        }
    }
    EXPECT_EQ(1, boost::filesystem::exists(store_path));

    agent_->oper_db()->instance_manager()->StaleTimeout();
    task_util::WaitForIdle();
    EXPECT_EQ(0, boost::filesystem::exists(store_path));

    boost::filesystem::remove_all(ss.str(), error);
    if (error) {
        LOG(ERROR, "Error : " << error.message() << "in removing directory");
    }
}
int main(int argc, char **argv) {
   GETUSERARGS();

   //Asio is disabled below to take control of event manager in the
   //tests
    client = TestInit(init_file, ksync_init, false, false, false, 30000,
            1000, false);
    usleep(100000);
    client->WaitForIdle();

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    client->WaitForIdle();
    delete client;
    return ret;

}
