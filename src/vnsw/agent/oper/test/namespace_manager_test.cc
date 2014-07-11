/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "oper/namespace_manager.h"

#include <boost/uuid/random_generator.hpp>

#include "base/test/task_test_util.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_agent_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/test/ifmap_test_util.h"
#include "oper/ifmap_dependency_manager.h"
#include "oper/service_instance.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

static boost::uuids::uuid IdPermsGetUuid(const autogen::IdPermsType &id) {
    boost::uuids::uuid uuid;
    CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
    return uuid;
}

class NamespaceManagerTest : public ::testing::Test {
public:
    bool IsExpectedStatusType(NamespaceState *state, int expected) {
        return state->status_type() == expected;
    }

    bool IsUpdateCommand(NamespaceState *state) {
        return std::string::npos != state->cmd().find("--update");
    }

    bool WaitForAWhile(time_t target) {
        time_t now = time(NULL);
        return now >= target;
    }

protected:
    static const int kTimeoutSeconds = 15;

    NamespaceManagerTest() :
            dependency_manager_(
                new IFMapDependencyManager(&database_, &graph_)),
            ns_manager_(new NamespaceManager(&evm_)),
            agent_signal_(new AgentSignal(&evm_)),
            si_table_(NULL) {
    }

    ~NamespaceManagerTest() {
        delete agent_signal_;
    }

    virtual void SetUp() {
        IFMapAgentLinkTable_Init(&database_, &graph_);
        vnc_cfg_Agent_ModuleInit(&database_, &graph_);
        dependency_manager_->Initialize();
        DB::RegisterFactory("db.service-instance.0",
                            &ServiceInstanceTable::CreateTable);
        si_table_ = static_cast<ServiceInstanceTable *>(
            database_.CreateTable("db.service-instance.0"));
        si_table_->Initialize(&graph_, dependency_manager_.get());
        agent_signal_->Initialize();

        boost::uuids::random_generator gen;
        instance_id_ = gen();
    }

    virtual void TearDown() {
        ns_manager_->Terminate();
        agent_signal_->Terminate();

        dependency_manager_->Terminate();
        IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
            database_.FindTable(IFMAP_AGENT_LINK_DB_NAME));
        assert(link_table);
        link_table->Clear();
        db_util::Clear(&database_);
    }

    boost::uuids::uuid AddServiceInstance(const std::string &name) {
        ifmap_test_util::IFMapMsgNodeAdd(&database_, "service-instance", name);
        task_util::WaitForIdle();
        IFMapTable *table = IFMapTable::FindTable(&database_,
                                                  "service-instance");
        IFMapNode *node = table->FindNode(name);
        if (node == NULL) {
            return boost::uuids::nil_uuid();
        }
        DBRequest request;
        si_table_->IFNodeToReq(node, request);
        si_table_->Enqueue(&request);
        task_util::WaitForIdle();

        autogen::ServiceInstance *si_object =
                static_cast<autogen::ServiceInstance *>(node->GetObject());
        const autogen::IdPermsType &id = si_object->id_perms();
        boost::uuids::uuid instance_id = IdPermsGetUuid(id);
        ServiceInstanceKey key(instance_id);
        ServiceInstance *svc_instance =
                static_cast<ServiceInstance *>(si_table_->Find(&key, true));
        if (svc_instance == NULL) {
            return boost::uuids::nil_uuid();
        }

        /*
         * Set non-nil uuids
         */
        UpdateProperties(svc_instance, true);
        return instance_id;
    }

    void DeleteServiceInstance(const std::string &name) {
        ifmap_test_util::IFMapMsgNodeDelete(&database_, "service-instance", name);
    }

    void MarkServiceInstanceAsDeleted(boost::uuids::uuid id) {
        ServiceInstanceKey key(id);
        ServiceInstance *svc_instance =
                static_cast<ServiceInstance *>(si_table_->Find(&key, true));
        if (svc_instance != NULL) {
            svc_instance->MarkDelete();
            si_table_->Change(svc_instance);
        }
    }

    bool UpdateProperties(boost::uuids::uuid id, bool usable) {
        ServiceInstanceKey key(id);
        ServiceInstance *svc_instance =
                static_cast<ServiceInstance *>(si_table_->Find(&key, true));
        if (svc_instance == NULL) {
            return false;
        }
        UpdateProperties(svc_instance, usable);

        return true;
    }

    ServiceInstance *GetServiceInstance(boost::uuids::uuid id) {
        ServiceInstanceKey key(id);
        ServiceInstance *svc_instance =
                static_cast<ServiceInstance *>(si_table_->Find(&key, true));
        if (svc_instance == NULL) {
            return NULL;
        }
        return svc_instance;
    }

    NamespaceState *ServiceInstanceState(boost::uuids::uuid id) {
        ServiceInstance *svc_instance = GetServiceInstance(id);
        if (svc_instance == NULL) {
            return NULL;
        }
        return ns_manager_->GetState(svc_instance);
    }

    NamespaceTaskQueue *GetTaskQueue(boost::uuids::uuid id) {
        ServiceInstance *svc_instance = GetServiceInstance(id);
        if (svc_instance == NULL) {
            return NULL;
        }

        std::stringstream ss;
        ss << svc_instance->properties().instance_id;
        return ns_manager_->GetTaskQueue(ss.str());
    }

    void TriggerSigChild(pid_t pid, int status) {
        boost::system::error_code ec;
        ns_manager_->HandleSigChild(ec, SIGCHLD, pid, status);
    }

    void NotifyChange(boost::uuids::uuid id) {
        ServiceInstance *svc_instance = GetServiceInstance(id);
        if (svc_instance == NULL) {
            return;
        }
        si_table_->Change(svc_instance);
    }

    void UpdateProperties(ServiceInstance* svc_instance, bool usable) {
        /*
         * Set non-nil uuids
         */
        ServiceInstance::Properties prop;
        prop.Clear();
        prop.virtualization_type = ServiceInstance::NetworkNamespace;
        boost::uuids::random_generator gen;
        prop.instance_id = instance_id_;
        prop.vmi_inside = gen();
        prop.vmi_outside = gen();
        prop.ip_addr_inside = "10.0.0.1";
        prop.ip_addr_outside = "10.0.0.2";
        prop.ip_prefix_len_inside = 24;
        if (usable) {
            prop.ip_prefix_len_outside = 24;
        }
        svc_instance->set_properties(prop);
        if (usable) {
            EXPECT_TRUE(svc_instance->IsUsable());
        } else {
            EXPECT_FALSE(svc_instance->IsUsable());
        }
        si_table_->Change(svc_instance);
    }

    DB database_;
    DBGraph graph_;
    EventManager evm_;
    std::auto_ptr<IFMapDependencyManager> dependency_manager_;
    std::auto_ptr<NamespaceManager> ns_manager_;
    AgentSignal *agent_signal_;
    ServiceInstanceTable *si_table_;
    boost::uuids::uuid instance_id_;
};

TEST_F(NamespaceManagerTest, ExecTrue) {
    ns_manager_->Initialize(&database_, agent_signal_, "/bin/true", 1, 10);
    boost::uuids::uuid id = AddServiceInstance("exec-true");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(NamespaceState::Starting, ns_state->status_type());
    EXPECT_NE(0, ns_state->pid());

    task_util::WaitForCondition(&evm_,
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Started),
            kTimeoutSeconds);

    EXPECT_EQ(NamespaceState::Started, ns_state->status_type());
    EXPECT_EQ(0, ns_state->status());

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}

TEST_F(NamespaceManagerTest, ExecFalse) {
    ns_manager_->Initialize(&database_, agent_signal_, "/bin/false", 1, 10);
    boost::uuids::uuid id = AddServiceInstance("exec-false");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(NamespaceState::Starting, ns_state->status_type());

    task_util::WaitForCondition(&evm_,
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Error),
            kTimeoutSeconds);

    EXPECT_EQ(NamespaceState::Error, ns_state->status_type());
    EXPECT_NE(0, ns_state->status());

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}

TEST_F(NamespaceManagerTest, Update) {
    ns_manager_->Initialize(&database_, agent_signal_, "/bin/true", 1, 10);
    boost::uuids::uuid id = AddServiceInstance("exec-update");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(NamespaceState::Starting, ns_state->status_type());

    task_util::WaitForCondition(&evm_,
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Started),
            kTimeoutSeconds);

    EXPECT_EQ(NamespaceState::Started, ns_state->status_type());
    EXPECT_EQ(0, ns_state->status());

    bool updated = UpdateProperties(id, true);
    EXPECT_EQ(true, updated);

    task_util::WaitForCondition(&evm_,
            boost::bind(&NamespaceManagerTest::IsUpdateCommand, this, ns_state),
            kTimeoutSeconds);

    EXPECT_TRUE(IsUpdateCommand(ns_state));

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}

TEST_F(NamespaceManagerTest, UpdateProperties) {
    ns_manager_->Initialize(&database_, agent_signal_, "/bin/true", 1, 10);
    boost::uuids::uuid id = AddServiceInstance("exec-update");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(NamespaceState::Starting, ns_state->status_type());

    task_util::WaitForCondition(&evm_,
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Started),
            kTimeoutSeconds);

    EXPECT_EQ(NamespaceState::Started, ns_state->status_type());
    EXPECT_EQ(0, ns_state->status());

    NotifyChange(id);
    task_util::WaitForIdle();

    EXPECT_NE(NamespaceState::Starting, ns_state->status_type());

    bool updated = UpdateProperties(id, true);
    EXPECT_EQ(true, updated);

    task_util::WaitForCondition(&evm_,
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Starting),
            kTimeoutSeconds);

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}

TEST_F(NamespaceManagerTest, Timeout) {
    ns_manager_->Initialize(&database_, NULL, "/bin/true", 1, 1);
    boost::uuids::uuid id = AddServiceInstance("exec-timeout");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(NamespaceState::Starting, ns_state->status_type());

    time_t now = time(NULL);
    task_util::WaitForCondition(&evm_,
            boost::bind(&NamespaceManagerTest::WaitForAWhile, this, now + 2),
            kTimeoutSeconds);

    EXPECT_EQ(NamespaceState::Timeout, ns_state->status_type());

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}

TEST_F(NamespaceManagerTest, TaskQueue) {
    static const int kNumUpdate = 5;
    ns_manager_->Initialize(&database_, NULL, "/bin/true", 10, 1);
    boost::uuids::uuid id = AddServiceInstance("exec-queue");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(NamespaceState::Starting, ns_state->status_type());
    NamespaceTaskQueue *queue = GetTaskQueue(id);

    EXPECT_EQ(1, queue->Size());

    for (int i = 0; i != kNumUpdate; i++) {
        bool updated = UpdateProperties(id, true);
        EXPECT_EQ(true, updated);
        task_util::WaitForIdle();

        EXPECT_EQ(i + 2, queue->Size());
    }

    for (int i = 0; i != kNumUpdate; i++) {
        TriggerSigChild(ns_state->pid(), 0);
        task_util::WaitForIdle();

        EXPECT_EQ(kNumUpdate - i, queue->Size());
    }

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}

TEST_F(NamespaceManagerTest, Usable) {
    ns_manager_->Initialize(&database_, agent_signal_, "/bin/true", 1, 10);
    boost::uuids::uuid id = AddServiceInstance("exec-usable");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(NamespaceState::Starting, ns_state->status_type());

    task_util::WaitForCondition(&evm_,
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Started),
            kTimeoutSeconds);

    EXPECT_EQ(NamespaceState::Started, ns_state->status_type());
    EXPECT_EQ(0, ns_state->status());

    NotifyChange(id);
    task_util::WaitForIdle();

    EXPECT_NE(NamespaceState::Starting, ns_state->status_type());

    bool updated = UpdateProperties(id, false);
    EXPECT_EQ(true, updated);

    task_util::WaitForCondition(&evm_,
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Stopped),
            kTimeoutSeconds);

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}

static void SetUp() {
}

static void TearDown() {
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
