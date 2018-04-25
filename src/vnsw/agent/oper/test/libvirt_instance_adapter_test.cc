#include <fstream>
#include <streambuf>
#include <unistd.h>
#include <libvirt/libvirt.h>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "testing/gunit.h"
#include "oper/libvirt_instance_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"
#include <boost/uuid/uuid.hpp>
#include <boost/lexical_cast.hpp>

/*
 *  ! Does not test networking.
 *  Those tests creates (and destroys) virtual machine using libvirt api.
 *  Developer is responsible for:
 *  a) providing .iso image conforming to data/libvirt_domain.xml
 *  b) installing libvirt service
 *  c) running test "by hand". Test is not run automatically using simple "scons test"
 *  vrouter-agent must be compiled with --with-libvirt option.
 */

class Agent;

class LibvirtAdapterTest : public ::testing::Test {
 protected:

  virtual void SetUp() {
      agent_ =  new Agent;
      libvirt_adapter_ = new LibvirtInstanceAdapter(agent_, "qemu:///system");

      properties_.virtualization_type = ServiceInstance::VRouterInstance;
      properties_.vrouter_instance_type = ServiceInstance::KVM;
      properties_.instance_id =
        boost::lexical_cast<boost::uuids::uuid>("ea294ce2-8182-11e4-ae5f-c32f93853088");

      properties_.vmi_inside =
        boost::lexical_cast<boost::uuids::uuid>("ea9b8b18-8182-11e4-8ad5-47899f2d45f1");
      properties_.vmi_outside =
        boost::lexical_cast<boost::uuids::uuid>("eb0e2d30-8182-11e4-b09d-533359975d6a");
      properties_.vmi_management =
        boost::lexical_cast<boost::uuids::uuid>("324d6de6-8183-11e4-8a75-13b524544c2f");

      properties_.mac_addr_inside = "00:50:56:00:00:01";
      properties_.mac_addr_outside = "00:50:56:00:00:02";
      properties_.mac_addr_management = "00:50:56:00:00:03";

      properties_.ip_addr_inside = "10.0.0.1";
      properties_.ip_addr_outside = "10.0.1.1";
      properties_.ip_addr_management = "10.0.2.1";

      properties_.image_name = "test";

      properties_.ip_prefix_len_inside = 24;
      properties_.ip_prefix_len_outside = 24;
      properties_.ip_prefix_len_management = 24;

      std::ifstream data("data/libvirt_domain.xml");
      properties_.instance_data = std::string((std::istreambuf_iterator<char>(data)),
                                              std::istreambuf_iterator<char>());
  }

  virtual void TearDown() {
      delete libvirt_adapter_;
      delete agent_;
  }

  ServiceInstance::Properties properties_;
  LibvirtInstanceAdapter *libvirt_adapter_;
  Agent* agent_;
};

TEST_F(LibvirtAdapterTest, DomainConfigFileExists) {
    ASSERT_NE(properties_.instance_data.size(), 0);
}

TEST_F(LibvirtAdapterTest, LibvirtConnection) {
    virConnectPtr conn = virConnectOpen("qemu:///system");
    ASSERT_NE(conn, (virConnectPtr)(NULL));
    virConnectClose(conn);
}

TEST_F(LibvirtAdapterTest, IsApplicable) {
    ASSERT_TRUE(libvirt_adapter_->isApplicable(properties_));
}

TEST_F(LibvirtAdapterTest, LibvirtStart) {
    InstanceTask* task = libvirt_adapter_->CreateStartTask(properties_ , false);
    ASSERT_NE(task, (InstanceTask *)(NULL));
    ASSERT_EQ(task->Run(), true);
    delete task;
}

TEST_F(LibvirtAdapterTest, LibvirtDomainCreated) {
    virConnectPtr conn = virConnectOpen("qemu:///system");
    ASSERT_NE(conn, (virConnectPtr)(NULL));
    std::string dom_uuid_str =
        boost::lexical_cast<std::string>(properties_.instance_id);
    virDomainPtr dom;
    for (int i = 0; i < 20; i++) {
        dom = virDomainLookupByUUIDString(conn,
            dom_uuid_str.c_str());
        if (dom != NULL)
            break;
        usleep(0.5 * 1e6);
    }
    ASSERT_NE(dom, (virDomainPtr)(NULL));
    virDomainFree(dom);
    virConnectClose(conn);
}

TEST_F(LibvirtAdapterTest, LibvirtStop) {
    InstanceTask* task = libvirt_adapter_->CreateStopTask(properties_);
    ASSERT_NE(task, (InstanceTask *)(NULL));
    ASSERT_EQ(task->Run(), true);
    delete task;
}

TEST_F(LibvirtAdapterTest, LibvirtDomainDestroyed) {
    virConnectPtr conn = virConnectOpen("qemu:///system");
    ASSERT_NE(conn, (virConnectPtr)(NULL));
    std::string dom_uuid_str =
        boost::lexical_cast<std::string>(properties_.instance_id);
    virDomainPtr dom;
    for (int i = 0; i < 20; i++) {
        dom = virDomainLookupByUUIDString(conn,
            dom_uuid_str.c_str());
        if (dom == NULL)
            break;
        usleep(0.5 * 1e6);
    }
    ASSERT_EQ(dom, (virDomainPtr)(NULL));
    virConnectClose(conn);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
