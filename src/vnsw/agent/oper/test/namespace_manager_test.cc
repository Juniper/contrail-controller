/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "oper/namespace_manager.h"

#include <cstdlib>
#include <boost/filesystem.hpp>
#include <boost/uuid/random_generator.hpp>

#include "base/test/task_test_util.h"
#include "base/logging.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_agent_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/test/ifmap_test_util.h"
#include "oper/ifmap_dependency_manager.h"
#include "oper/service_instance.h"
#include "oper/loadbalancer.h"
#include "oper/operdb_init.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"
#include "test/test_init.h"

using namespace std;
class Agent;
void RouterIdDepInit(Agent *agent) {
}


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
        return string::npos != state->cmd().find("--update");
    }

    bool WaitForAWhile(time_t target) {
        time_t now = time(NULL);
        return now >= target;
    }

protected:
    static const int kTimeoutSeconds = 15;

    NamespaceManagerTest()
          : agent_(new Agent),
            agent_config_(new AgentConfig(agent_.get())) {
        agent_->set_cfg(agent_config_.get());
        oper_db_.reset(new OperDB(agent_.get()));
        agent_->set_oper_db(oper_db_.get());
        stringstream ss;
        ss << "/tmp/" << getpid() << "/";
        agent_->oper_db()-> namespace_manager()->loadbalancer_config_path_ = ss.str();
    }

    ~NamespaceManagerTest() {
    }

    virtual void TearDown() {
        task_util::WaitForIdle();

        agent_->oper_db()->Shutdown();
        agent_->cfg()->Shutdown();
        task_util::WaitForIdle();

        DB *database = agent_->db();
        db_util::Clear(database);
        task_util::WaitForIdle();

        /**
         * The factory create method for ifmap link table takes the
         * graph as a boost::bind() argument; failure to cleanup the registry
         * implies creating the table using a stale graph pointer.
         */
        DB::ClearFactoryRegistry();
    }


    virtual void SetUp() {
        DB *db = agent_->db();
        agent_config_->CreateDBTables(db);
        oper_db_->CreateDBTables(db);
        agent_config_->RegisterDBClients(db);
        oper_db_->RegisterDBClients();
        agent_config_->Init();
        oper_db_->Init();
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
        si_table->IFNodeToReq(node, request);
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

    void AddLoadbalancerVip(const string &pool_name) {
        string vip_name("vip" + pool_name);
        autogen::VirtualIpType vip_attr;
        vip_attr.protocol = "HTTP";
        vip_attr.protocol_port = 80;
        vip_attr.connection_limit = 100;
        map<string, AutogenProperty *> pmap;
        pmap.insert(make_pair("virtual-ip-properties", &vip_attr));
        ifmap_test_util::IFMapMsgPropertySet(
            agent_->db(), "virtual-ip", vip_name, pmap, 0);

        ifmap_test_util::IFMapMsgLink(
            agent_->db(), "loadbalancer-pool", pool_name,
            "virtual-ip", vip_name,
            "virtual-ip-loadbalancer-pool");
    }


   Loadbalancer *GetLoadbalancer(boost::uuids::uuid pool_id) {
        LoadbalancerKey key(pool_id);
        return static_cast<Loadbalancer *>(
            agent_->loadbalancer_table()->Find(&key, true));
    }

    void AddLoadbalancerMember(std::string &pool_name,
                               const char *ip_address, int port) {
        boost::uuids::random_generator gen;
        boost::uuids::uuid member_id = gen();
        map<string, AutogenProperty *> pmap;
        autogen::LoadbalancerMemberType attr;
        attr.address = ip_address;
        attr.protocol_port = port;
        pmap.insert(make_pair("loadbalancer-member-properties", &attr));
        ifmap_test_util::IFMapMsgPropertySet(
            agent_->db(), "loadbalancer-member", UuidToString(member_id),
            pmap, 0);

        ifmap_test_util::IFMapMsgLink(
            agent_->db(), "loadbalancer-pool", pool_name,
            "loadbalancer-member", UuidToString(member_id),
            "loadbalancer-pool-loadbalancer-member");
    }


    boost::uuids::uuid AddLoadbalancer(std::string pool_name) {

        map<string, AutogenProperty *> pmap;
        autogen::LoadbalancerPoolType pool_attr;
        pool_attr.protocol = "HTTP";
        pool_attr.loadbalancer_method = "ROUND_ROBIN";
        pmap.insert(make_pair("loadbalancer-pool-properties", &pool_attr));
        ifmap_test_util::IFMapMsgPropertySet(
            agent_->db(), "loadbalancer-pool", pool_name, pmap, 0);

        AddLoadbalancerVip(pool_name);

        task_util::WaitForIdle();
        IFMapTable *table = IFMapTable::FindTable(agent_->db(),
                                                  "loadbalancer-pool");
        IFMapNode *node = table->FindNode(pool_name);
        if (node == NULL) {
            return boost::uuids::nil_uuid();
        }

       AddLoadbalancerMember(pool_name, "127.0.0.1", 80);
       AddLoadbalancerMember(pool_name, "127.0.0.2", 80);

        DBRequest request;
        agent_->loadbalancer_table()->IFNodeToReq(node, request);
        agent_->loadbalancer_table()->Enqueue(&request);

       task_util::WaitForIdle();
        autogen::LoadbalancerPool *lb_object =
                static_cast<autogen::LoadbalancerPool *>(node->GetObject());
        const autogen::IdPermsType &id = lb_object->id_perms();
        boost::uuids::uuid instance_id = IdPermsGetUuid(id);

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

    NamespaceState *ServiceInstanceState(boost::uuids::uuid id) {
        ServiceInstance *svc_instance = GetServiceInstance(id);
        if (svc_instance == NULL) {
            return NULL;
        }
        return agent_->oper_db()->namespace_manager()->GetState(svc_instance);
    }

    NamespaceTaskQueue *GetTaskQueue(boost::uuids::uuid id) {
        ServiceInstance *svc_instance = GetServiceInstance(id);
        if (svc_instance == NULL) {
            return NULL;
        }

        stringstream ss;
        ss << svc_instance->properties().instance_id;
        return agent_->oper_db()->namespace_manager()->GetTaskQueue(ss.str());
    }

    void TriggerSigChild(pid_t pid, int status) {
        boost::system::error_code ec;
        agent_->oper_db()->namespace_manager()->HandleSigChild(ec, SIGCHLD, pid, status);
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
        return agent_->oper_db()->namespace_manager()->loadbalancer_config_path_;
    }
protected:
    std::auto_ptr<Agent> agent_;
    std::auto_ptr<AgentConfig> agent_config_;
    std::auto_ptr<OperDB> oper_db_;
};

TEST_F(NamespaceManagerTest, ExecTrue) {
    agent_->oper_db()->namespace_manager()->Initialize(agent_->db(),
            agent_->agent_signal(), "/bin/true", 1, 10);
    boost::uuids::uuid id = AddServiceInstance("exec-true");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(NamespaceState::Starting, ns_state->status_type());
    EXPECT_NE(0, ns_state->pid());

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Started),
            kTimeoutSeconds);

    EXPECT_EQ(NamespaceState::Started, ns_state->status_type());
    EXPECT_EQ(0, ns_state->status());

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}

TEST_F(NamespaceManagerTest, ExecFalse) {
    agent_->oper_db()->namespace_manager()->Initialize(agent_->db(), agent_->agent_signal(), "/bin/false", 1, 10);
    boost::uuids::uuid id = AddServiceInstance("exec-false");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(NamespaceState::Starting, ns_state->status_type());

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Error),
            kTimeoutSeconds);

    EXPECT_EQ(NamespaceState::Error, ns_state->status_type());
    EXPECT_NE(0, ns_state->status());

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}

TEST_F(NamespaceManagerTest, Update) {
    agent_->oper_db()->namespace_manager()->Initialize(agent_->db(), agent_->agent_signal(), "/bin/true", 1, 10);
    boost::uuids::uuid id = AddServiceInstance("exec-update");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(NamespaceState::Starting, ns_state->status_type());

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Started),
            kTimeoutSeconds);

    EXPECT_EQ(NamespaceState::Started, ns_state->status_type());
    EXPECT_EQ(0, ns_state->status());

    bool updated = UpdateProperties(id, true);
    EXPECT_EQ(true, updated);

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&NamespaceManagerTest::IsUpdateCommand, this, ns_state),
            kTimeoutSeconds);

    EXPECT_TRUE(IsUpdateCommand(ns_state));

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}

TEST_F(NamespaceManagerTest, UpdateProperties) {
    agent_->oper_db()->namespace_manager()->Initialize(agent_->db(), agent_->agent_signal(), "/bin/true", 1, 10);
    boost::uuids::uuid id = AddServiceInstance("exec-update");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(NamespaceState::Starting, ns_state->status_type());

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Started),
            kTimeoutSeconds);

    EXPECT_EQ(NamespaceState::Started, ns_state->status_type());
    EXPECT_EQ(0, ns_state->status());

    NotifyChange(id);
    task_util::WaitForIdle();

    EXPECT_NE(NamespaceState::Starting, ns_state->status_type());

    bool updated = UpdateProperties(id, true);
    EXPECT_EQ(true, updated);

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Starting),
            kTimeoutSeconds);

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}

TEST_F(NamespaceManagerTest, Timeout) {

    agent_->oper_db()->namespace_manager()->Initialize(agent_->db(), NULL, "/bin/true", 1, 1);
    boost::uuids::uuid id = AddServiceInstance("exec-timeout");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(NamespaceState::Starting, ns_state->status_type());

    time_t now = time(NULL);
    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&NamespaceManagerTest::WaitForAWhile, this, now + 5),
            kTimeoutSeconds);

    EXPECT_EQ(NamespaceState::Timeout, ns_state->status_type());

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}
TEST_F(NamespaceManagerTest, TaskQueue) {
    static const int kNumUpdate = 5;
    agent_->oper_db()->namespace_manager()->Initialize(agent_->db(), NULL, "/bin/true", 10, 1);
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
    agent_->oper_db()->namespace_manager()->Initialize(agent_->db(), agent_->agent_signal(), "/bin/true", 1, 10);
    boost::uuids::uuid id = AddServiceInstance("exec-usable");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);

    EXPECT_EQ(0, ns_state->status());
    EXPECT_EQ(NamespaceState::Starting, ns_state->status_type());

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Started),
            kTimeoutSeconds);

    EXPECT_EQ(NamespaceState::Started, ns_state->status_type());
    EXPECT_EQ(0, ns_state->status());

    NotifyChange(id);
    task_util::WaitForIdle();

    EXPECT_NE(NamespaceState::Starting, ns_state->status_type());

    bool updated = UpdateProperties(id, false);
    EXPECT_EQ(true, updated);

    task_util::WaitForCondition(agent_->event_manager(),
            boost::bind(&NamespaceManagerTest::IsExpectedStatusType, this, ns_state, NamespaceState::Stopped),
            kTimeoutSeconds);

    MarkServiceInstanceAsDeleted(id);
    task_util::WaitForIdle();
}

TEST_F(NamespaceManagerTest, LoadbalancerConfig) {
    agent_->oper_db()->namespace_manager()->Initialize(agent_->db(), agent_->agent_signal(), "/bin/true", 1, 10);

    boost::uuids::random_generator gen;
    std::string pool_name(UuidToString(gen()));

    boost::uuids::uuid lbid = AddLoadbalancer(pool_name);
    task_util::WaitForIdle();

    AddServiceInstance("/bin/true");
    ifmap_test_util::IFMapMsgLink(
            agent_->db(), "loadbalancer-pool", pool_name,
            "service-instance", "/bin/true",
            "loadbalancer-pool-service-instance");
    task_util::WaitForIdle();

    stringstream pathgen;
    pathgen << loadbalancer_config_path() << lbid
            << "/etc/haproxy/haproxy.cfg";
    boost::filesystem::path config(pathgen.str());
    std::time_t old_time =
        boost::filesystem::last_write_time(pathgen.str());
    EXPECT_TRUE(boost::filesystem::exists(config));

    lbid = AddLoadbalancer(UuidToString(gen()));
    pathgen.str("");
    pathgen.clear();
    pathgen << loadbalancer_config_path() << lbid
            << "/etc/haproxy/haproxy.cfg";
    boost::filesystem::path config1(pathgen.str());

    //Make sure that both files exists
    EXPECT_TRUE(boost::filesystem::exists(config1));
    EXPECT_TRUE(boost::filesystem::exists(config));
    task_util::WaitForIdle();

    //Lets add a new member to Pool and verify that config file is
    //updated
    AddLoadbalancerMember(pool_name, "127.0.0.3", 80);
    task_util::WaitForIdle();
    std::time_t new_time =
        boost::filesystem::last_write_time(pathgen.str());
    EXPECT_TRUE(old_time <= new_time);
}

TEST_F(NamespaceManagerTest, NamespaceStaleCleanup) {
    agent_->oper_db()->namespace_manager()->Initialize(agent_->db(),
            agent_->agent_signal(), "/bin/true", 1, 10);

    boost::uuids::random_generator gen;
    std::string vm_uuid = UuidToString(gen());
    std::string lb_uuid = UuidToString(gen());
    std::string store_path = "/tmp/lb/";

    agent_->oper_db()->namespace_manager()->SetNamespaceStorePath(store_path);
    store_path +=  ("vrouter-" + vm_uuid + ":" + lb_uuid);
    boost::system::error_code error;
    if (!boost::filesystem::exists(store_path, error)) {
        boost::filesystem::create_directories(store_path, error);
    }
    store_path  = loadbalancer_config_path() + lb_uuid;

    if (!boost::filesystem::exists(store_path, error)) {
        boost::filesystem::create_directories(store_path, error);
    }
    EXPECT_EQ(1, boost::filesystem::exists(store_path));

    agent_->oper_db()->namespace_manager()->StaleTimeout();
    task_util::WaitForIdle();
    EXPECT_EQ(0, boost::filesystem::exists(store_path));


    store_path = "/tmp/lb/" + ("vrouter-" + vm_uuid + ":" + lb_uuid);
    boost::filesystem::remove_all(store_path.c_str(), error);
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    int result = RUN_ALL_TESTS();
    return result;
}
