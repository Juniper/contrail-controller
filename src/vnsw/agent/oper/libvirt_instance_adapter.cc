#include <libvirt/libvirt.h>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <pugixml/pugixml.hpp>
#include "oper/libvirt_instance_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"
#include "oper/interface_common.h"
#include "net/address.h"

LibvirtInstanceAdapter::~LibvirtInstanceAdapter() {
    if (conn_ != NULL) {
        DeregisterCallbacks();
        virConnectClose(conn_);
        conn_ = NULL;
    }
}

InstanceTask* LibvirtInstanceAdapter::CreateStartTask(
    const ServiceInstance::Properties &si_properties, bool update) {
    if (!EnsureConnected())
        return NULL;
    RegisterCallbacks();

    std::string dom_uuid_str =
        boost::lexical_cast<std::string>(si_properties.instance_id);

    // make a pugixml config document out of si's instance data 
    pugi::xml_document domain_xml_conf;
    pugi::xml_parse_result parse_result =
        domain_xml_conf.load(si_properties.instance_data.c_str());
    if (!parse_result || !domain_xml_conf.child("domain"))
        return NULL;

    // modify and save as a string for libvirt
    XMLConfAssignUUID(dom_uuid_str, domain_xml_conf);
    // interfaces defined by Contrail (left, right, management)
    XMLConfSetInterfaceData(si_properties, domain_xml_conf);
    std::stringstream domain_conf;
    domain_xml_conf.save(domain_conf);

    // Domain configuration can't be updated w/o restarting anyway.
    DestroyDomain(dom_uuid_str);
    // create a transient domain
    virDomainPtr dom = virDomainCreateXML(conn_, domain_conf.str().c_str(), 0);
    if (dom == NULL) {
        // TODO ERR
        return NULL;
    }

    // lastly, store the configuration for callbacks
    domain_properties_[si_properties.instance_id] = si_properties;
    virDomainFree(dom);
    // TODO do libvirt business in another task. 
    return new InstanceTask("true", START, agent_->event_manager());
}

InstanceTask* LibvirtInstanceAdapter::CreateStopTask(
    const ServiceInstance::Properties &si_properties) {
    if (!EnsureConnected())
        return NULL;

    std::string dom_uuid_str =
        boost::lexical_cast<std::string>(si_properties.instance_id);
    DestroyDomain(dom_uuid_str);

    domain_properties_.erase(si_properties.instance_id);
    return new InstanceTask("true", STOP, agent_->event_manager());
}

bool LibvirtInstanceAdapter::isApplicable(
    const ServiceInstance::Properties &props) {
    return (props.virtualization_type == ServiceInstance::VRouterInstance
            && props.vrouter_instance_type == ServiceInstance::KVM);
}

void LibvirtInstanceAdapter::XMLConfAssignUUID(
            const std::string &dom_uuid_str,
            pugi::xml_document &libvirt_xml_conf) {
    pugi::xml_node uuid_node = libvirt_xml_conf.child("domain").child("uuid");
    if (!uuid_node)
        uuid_node = libvirt_xml_conf.child("domain").append_child("uuid");
    uuid_node.set_value(dom_uuid_str.c_str());    
}

void LibvirtInstanceAdapter::XMLConfSetInterfaceData(
        const ServiceInstance::Properties &si_properties,
        pugi::xml_document &libvirt_xml_conf) {
    using namespace pugi;
    // to be deprecated. Agent code currently supports only 3 interfaces/dom.
    xml_node left_intf_node =
        libvirt_xml_conf.child("domain").append_child("interface");
    left_intf_node.append_attribute("type").set_value("network"); // TAP
    xml_node left_mac_node = left_intf_node.append_child("mac");
    left_mac_node.append_attribute("mac").set_value(
        si_properties.mac_addr_inside.c_str());
    xml_node left_dev_node = left_intf_node.append_child("target");
    left_dev_node.append_attribute("dev").set_value(
        gen_intf_name(si_properties.vmi_inside, 'l').c_str());

    xml_node right_intf_node =
        libvirt_xml_conf.child("domain").append_child("interface");
    right_intf_node.append_attribute("type").set_value("network"); // TAP
    xml_node right_mac_node = right_intf_node.append_child("mac");
    right_mac_node.append_attribute("mac").set_value(
        si_properties.mac_addr_outside.c_str());
    xml_node right_dev_node = right_intf_node.append_child("target");
    right_dev_node.append_attribute("dev").set_value(
        gen_intf_name(si_properties.vmi_outside, 'r').c_str());

    xml_node management_intf_node =
        libvirt_xml_conf.child("domain").append_child("interface");
    management_intf_node.append_attribute("type").set_value("network"); // TAP
    xml_node management_mac_node = management_intf_node.append_child("mac");
    management_mac_node.append_attribute("mac").set_value(
        si_properties.mac_addr_management.c_str());
    xml_node management_dev_node = management_intf_node.append_child("target");
    management_dev_node.append_attribute("dev").set_value(
        gen_intf_name(si_properties.vmi_management, 'm').c_str());
}

void LibvirtInstanceAdapter::RegisterTAPInterfaces(
    const ServiceInstance::Properties &si_properties) {
    // left interface
    VmInterface::Add(agent_->interface_table(),
                     si_properties.vmi_inside,
                     "",
                     Ip4Address::from_string(si_properties.ip_addr_inside),
                     si_properties.mac_addr_inside,
                     gen_intf_name(si_properties.vmi_inside, 'l'),
                     si_properties.instance_id,
                     0,
                     0,
                     "",
                     Ip6Address());
    // right interface
    VmInterface::Add(agent_->interface_table(),
                     si_properties.vmi_outside,
                     "",
                     Ip4Address::from_string(si_properties.ip_addr_outside),
                     si_properties.mac_addr_outside,
                     gen_intf_name(si_properties.vmi_outside, 'r'),
                     si_properties.instance_id,
                     0,
                     0,
                     "",
                     Ip6Address());
    // management interface
    VmInterface::Add(agent_->interface_table(),
                     si_properties.vmi_management,
                     "",
                     Ip4Address::from_string(si_properties.ip_addr_management),
                     si_properties.mac_addr_management,
                     gen_intf_name(si_properties.vmi_management, 'm'),
                     si_properties.instance_id,
                     0,
                     0,
                     "",
                     Ip6Address());
}

std::string LibvirtInstanceAdapter::gen_intf_name(
            const boost::uuids::uuid &dom_uuid, char type) {
    std::string dom_uuid_str =
        boost::lexical_cast<std::string>(dom_uuid);
    std::stringstream intf_name;
    intf_name << "si" << dom_uuid_str.substr(0, 5) << type;
    return intf_name.str();
}

boost::uuids::uuid LibvirtInstanceAdapter::get_domain_uuid(virDomainPtr dom) {
    char domain_uuid[VIR_UUID_STRING_BUFLEN];
    virDomainGetUUIDString(dom, domain_uuid);
    boost::uuids::string_generator uuid_gen;
    boost::uuids::uuid rv = uuid_gen(domain_uuid);
    return rv;
}

bool LibvirtInstanceAdapter::EnsureConnected() {
    if (conn_ == NULL) {
        conn_ = virConnectOpen(libvirt_conn_addr_.c_str());
        if (conn_ == NULL) 
            return false;
    }
    return true;
}

void LibvirtInstanceAdapter::DestroyDomain(const std::string &dom_uuid) {
    virDomainPtr dom = virDomainLookupByUUID(conn_,
        (const unsigned char *)(dom_uuid.c_str()));
    if (dom != NULL) {
        virDomainDestroy(dom);
        virDomainFree(dom);
    }
}

int LibvirtInstanceAdapter::DomainEventCallback(virConnectPtr conn,
                        virDomainPtr dom,
                        int event,
                        int detail,
                        void *opaque) {
    boost::uuids::uuid dom_uuid = get_domain_uuid(dom);
    LibvirtInstanceAdapter *adapter = (LibvirtInstanceAdapter *)(opaque);

    if (opaque == NULL ||
        adapter->domain_properties_.find(dom_uuid) ==
            adapter->domain_properties_.end()) {
        // not our domain
        return -1;
    }
    switch (event) {
        case VIR_DOMAIN_EVENT_STARTED:
             // pass tap interfaces to vrouter, only after the domain is
             // started
             adapter->RegisterTAPInterfaces(adapter->domain_properties_[dom_uuid]);
        break;
        default:
        break;
    }
    return 0; // "currently ignored by libvirt"
} 

void LibvirtInstanceAdapter::RegisterCallbacks() {
    virConnectDomainEventRegister(conn_,
        &LibvirtInstanceAdapter::DomainEventCallback,
        this,
        NULL);
}

void LibvirtInstanceAdapter::DeregisterCallbacks() {
    virConnectDomainEventDeregister(conn_, 
        &LibvirtInstanceAdapter::DomainEventCallback);
}
