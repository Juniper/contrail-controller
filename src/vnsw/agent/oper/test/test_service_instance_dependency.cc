/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "oper/ifmap_dependency_manager.h"
#include "oper/service_instance.h"

void RouterIdDepInit(Agent *agent) {
}

class DependencyMgrTest : public ::testing::Test {
public:
    void SetUp() {
        notify_count_ = 0;

        agent_ = Agent::GetInstance();
        mgr_ = agent_->oper_db()->dependency_manager();
        mgr_->Unregister("service-instance");
        mgr_->Register("service-instance",
                      boost::bind(&DependencyMgrTest::EventHandler, this, _1));
    }


    void EventHandler(DBEntry *entry) {
        notify_count_++;
    }

    ServiceInstance *Find(int id) {
        ServiceInstanceKey key(MakeUuid(id));
        return static_cast<ServiceInstance *>
            (agent_->service_instance_table()->FindActiveEntry(&key));
    }

    void TearDown() {
        DelLink("service-instance", "si1", "virtual-machine", "vm1");
        DelLink("virtual-network", "vn1", "virtual-machine", "vm1");
        DelLink("service-instance", "si1", "service-template", "st1");
        DelNode("virtual-machine", "vm1");
        DelNode("virtual-network", "vn1");
        DelNode("service-instance", "si1");
        DelNode("service-template", "st1");
        TestClient::WaitForIdle();
        mgr_->Unregister("service-instance");
        agent_->service_instance_table()->RegisterEventHandler();
    }

    int notify_count() const { return notify_count_; }
private:
    Agent *agent_;
    IFMapDependencyManager *mgr_;
    int notify_count_;
};

// Notification order. Nodes added before link
TEST_F(DependencyMgrTest, NotifyOrder_1) {
    char buff[4096];
    int len = 0;

    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "virtual-machine", "vm1", 1);
    AddNodeString(buff, len, "virtual-network", "vn1", 1); 
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    TestClient::WaitForIdle();

    AddXmlHdr(buff, len);
    AddLinkString(buff, len, "virtual-network", "vn1", "virtual-machine",
                  "vm1");

    AddNodeString(buff, len, "service-instance", "si1", 1);
    AddNodeString(buff, len, "service-template", "st1", 1);
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    TestClient::WaitForIdle();

    AddXmlHdr(buff, len);
    AddLinkString(buff, len, "service-instance", "si1", "service-template",
                  "st1", "service-instance-service-template");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    TestClient::WaitForIdle();

    WAIT_FOR(100, 1000, (notify_count() > 0));
}

// Re-ordered notification from DBTable
// "service-instance to service-template notified before notification of
// service-instance
TEST_F(DependencyMgrTest, NotifyOrder_2) {
    char buff[4096];
    int len = 0;

    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "virtual-machine", "vm1", 1);
    AddNodeString(buff, len, "virtual-network", "vn1", 1); 
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    TestClient::WaitForIdle();

    // Create a Link between vn and vm first. This will ensure 
    // service-instance to service-template link is notified before
    // nodes
    AddXmlHdr(buff, len);
    AddLinkString(buff, len, "virtual-network", "vn1", "virtual-machine",
                  "vm1");

    AddNodeString(buff, len, "service-instance", "si1", 1);
    AddNodeString(buff, len, "service-template", "st1", 1);
    AddLinkString(buff, len, "service-instance", "si1", "service-template",
                  "st1", "service-instance-service-template");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    TestClient::WaitForIdle();

    WAIT_FOR(100, 1000, (notify_count() > 0));
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
