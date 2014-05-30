/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "service_instance.h"

#include <boost/uuid/random_generator.hpp>
#include <pugixml/pugixml.hpp>
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

#include "base/test/task_test_util.h"
#include "cfg/cfg_init.h"
#include "oper/operdb_init.h"

using boost::uuids::uuid;

static void UuidTypeSet(const uuid &uuid, autogen::UuidType *idpair) {
    idpair->uuid_lslong = 0;
    idpair->uuid_mslong = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t value = uuid.data[16 - (i + 1)];
        idpair->uuid_lslong |= value << (8 * i);
    }
    for (int i = 0; i < 8; i++) {
        uint64_t value = uuid.data[8 - (i + 1)];
        idpair->uuid_mslong |= value << (8 * i);
    }
}

class ServiceInstanceIntegrationTest: public ::testing::Test {
protected:
    ServiceInstanceIntegrationTest() {
        agent_.reset(new Agent);
    }

    void SetUp() {
        agent_->set_cfg(new AgentConfig(agent_.get()));
        agent_->set_oper_db(new OperDB(agent_.get()));
        agent_->CreateDBTables();
        agent_->CreateDBClients();
        agent_->InitModules();
        config_ = doc_.append_child("config");
    }

    void TearDown() {
        agent_->oper_db()->Shutdown();
    }

    void EncodeNode(pugi::xml_node *parent, const std::string &obj_typename,
            const std::string &obj_name, const IFMapObject *object) {
        pugi::xml_node node = parent->append_child("node");
        node.append_attribute("type") = obj_typename.c_str();
        node.append_child("name").text().set(obj_name.c_str());
        object->EncodeUpdate(&node);
    }

    void EncodeServiceInstance(const uuid &uuid) {
        autogen::IdPermsType id;
        id.Clear();
        UuidTypeSet(uuid, &id.uuid);
        autogen::ServiceInstance svc_instance;
        svc_instance.SetProperty("id-perms", &id);
        std::stringstream name_gen;
        name_gen << "contrail:service-instance:" << uuid;
        pugi::xml_node update = config_.append_child("update");
        EncodeNode(&update, "service-instance", name_gen.str(), &svc_instance);
    }

protected:
    std::auto_ptr<Agent> agent_;
    pugi::xml_document doc_;
    pugi::xml_node config_;
};

TEST_F(ServiceInstanceIntegrationTest, Config) {
    boost::uuids::random_generator gen;
    uuid svc_id = gen();
    EncodeServiceInstance(svc_id);
    IFMapAgentParser *parser = agent_->GetIfMapAgentParser();
    parser->ConfigParse(config_, 1);
    task_util::WaitForIdle();

    ServiceInstanceTable *si_table = agent_->service_instance_table();
    EXPECT_EQ(1, si_table->Size());
    ServiceInstanceKey key(svc_id);
    ServiceInstance *svc_instance =
            static_cast<ServiceInstance *>(si_table->Find(&key, true));
    ASSERT_TRUE(svc_instance != NULL);
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
