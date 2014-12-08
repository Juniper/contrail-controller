#ifndef __SRC_OPER_LIBVIRT_INSTANCE_ADAPTER_H
#define __SRC_OPER_LIBVIRT_INSTANCE_ADAPTER_H
#include <map>
#include <libvirt/libvirt.h>
#include <boost/uuid/uuid_io.hpp>
#include <pugixml/pugixml.hpp>
#include "oper/instance_manager_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"

class LibvirtInstanceAdapter : public InstanceManagerAdapter {
 public:
    LibvirtInstanceAdapter(Agent *agent, const std::string &libvirt_conn_addr)
    : agent_(agent), libvirt_conn_addr_(libvirt_conn_addr) {}
    ~LibvirtInstanceAdapter();

    InstanceTask* CreateStartTask(const ServiceInstance::Properties &props,
        bool update);
    InstanceTask* CreateStopTask(const ServiceInstance::Properties &props);
    bool isApplicable(const ServiceInstance::Properties &props);

 private:
    static void XMLConfAssignUUID(
        const std::string &libvirt_conf_str,
        pugi::xml_document &libvirt_xml_conf);
    static void XMLConfSetInterfaceData(
        const ServiceInstance::Properties &si_properties,
        pugi::xml_document &libvirt_xml_conf);
    static std::string gen_intf_name(
        const boost::uuids::uuid &dom_uuid, char type);
    static boost::uuids::uuid get_domain_uuid(virDomainPtr);
    static int DomainEventCallback(virConnectPtr conn,
                                    virDomainPtr dom,
                                    int event,
                                    int detail,
                                    void *opaque); 

    bool EnsureConnected();
    void RegisterTAPInterfaces(const ServiceInstance::Properties &si_properties);
    void DestroyDomain(const std::string &dom_uuid);
    void RegisterCallbacks();
    void DeregisterCallbacks();

    Agent *agent_;
    std::string libvirt_conn_addr_;
    virConnectPtr conn_;

    // TODO It probably shouldn't be stored here.
    std::map<boost::uuids::uuid, ServiceInstance::Properties> domain_properties_;
};
#endif
