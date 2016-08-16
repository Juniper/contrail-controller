/*
 * Copyright (c) 2015 Codilime
 */

#ifndef SRC_VNSW_AGENT_OPER_LIBVIRT_INSTANCE_ADAPTER_H_
#define SRC_VNSW_AGENT_OPER_LIBVIRT_INSTANCE_ADAPTER_H_
#include <tbb/tbb.h>
#include <libvirt/libvirt.h>
#include <boost/uuid/uuid_io.hpp>
#include <pugixml/pugixml.hpp>
#include <map>
#include <string>
#include "oper/instance_manager_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"

class InstanceTask;

class LibvirtInstanceAdapter : public InstanceManagerAdapter {
 public:
    LibvirtInstanceAdapter(Agent *agent, const std::string &libvirt_conn_addr)
    : agent_(agent), libvirt_conn_addr_(libvirt_conn_addr), conn_(NULL) {}
    ~LibvirtInstanceAdapter();

    InstanceTask *CreateStartTask(const ServiceInstance::Properties &props,
        bool update);
    InstanceTask *CreateStopTask(const ServiceInstance::Properties &props);
    bool isApplicable(const ServiceInstance::Properties &props);

    class DomainStartTask : public InstanceTaskMethod {
     public:
        DomainStartTask(LibvirtInstanceAdapter *parent_adapter,
            const ServiceInstance::Properties &props,
            bool update)
        : parent_adapter_(parent_adapter), si_properties_(props),
          update_(update) {}

        bool Run();
        void Stop() {}
        void Terminate() {}
        bool IsSetup() { return true; }

        const std::string &cmd() const {
            static const std::string cmdstr =
                "libvirt domain start task";
            return cmdstr;
        }

        int cmd_type() const {
            return START;
        }

     private:
        std::string XmlConf();
        static void DomainXMLAssignUUID(
            const std::string &libvirt_conf_str,
            const pugi::xml_document &libvirt_xml_conf);
        void DomainXMLSetInterfaceData(
            const pugi::xml_document &libvirt_xml_conf,
            const std::string &dom_uuid);
        static void DomainXMLAddInterface(
                pugi::xml_node *devices_node,
                const std::string &mac_addr,
                const std::string &intf_name);
        static bool CreateTAPInterfaces(const std::string &dom_uuid);

        LibvirtInstanceAdapter *parent_adapter_;
        const ServiceInstance::Properties &si_properties_;
        const bool update_;
    };

    class DomainStopTask : public InstanceTaskMethod {
     public:
        DomainStopTask(LibvirtInstanceAdapter *parent_adapter,
                        const ServiceInstance::Properties &props)
        : parent_adapter_(parent_adapter), si_properties_(props) {}

        bool Run();
        void Stop() {}
        void Terminate() {}
        bool IsSetup() { return true; }

        const std::string &cmd() const {
            static const std::string cmdstr =
                "libvirt domain stop task";
            return cmdstr;
        }

        int cmd_type() const {
            return STOP;
        }

     private:
        LibvirtInstanceAdapter *parent_adapter_;
        const ServiceInstance::Properties &si_properties_;
    };

 private:
    bool EnsureConnected();
    void CloseConnection();
    void EnsureDestroyed(const std::string &dom_uuid_str,
                         const ServiceInstance::Properties &si_properties);
    void UnregisterInterfaces(const ServiceInstance::Properties &si_properties);
    bool RegisterInterfaces(const ServiceInstance::Properties &si_properties);
    static std::string GenIntfName(const std::string &dom_uuid, char type);

    Agent *agent_;
    std::string libvirt_conn_addr_;
    virConnectPtr conn_;
    static tbb::mutex conn_mutex_;
};
#endif  // SRC_VNSW_AGENT_OPER_LIBVIRT_INSTANCE_ADAPTER_H_

