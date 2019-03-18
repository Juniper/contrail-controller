/*
 * Copyright (c) 2015 Codilime
 */

#include <arpa/inet.h>
#include <errno.h>
#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <pugixml/pugixml.hpp>
#include <fstream>
#include "cfg/cfg_init.h"
#include "oper/libvirt_instance_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"
#include "oper/interface_common.h"
#include "base/address.h"
#include "base/logging.h"

using pugi::xml_document;
using pugi::xml_node;
using pugi::xml_attribute;
using pugi::xml_parse_result;

tbb::mutex LibvirtInstanceAdapter::conn_mutex_;

static bool close_descriptor(int fd) {
    while (close(fd) < 0) {
        if (errno != EINTR) {
            LOG(ERROR, "Could not close descriptor, errno: " << errno);
            return false;
        }
    }
    return true;
}

static bool alloc_tap_interface(const char *devname, int flags) {
    struct ifreq ifr;
    int fd;
    LOG(DEBUG, "Allocating TAP device " << devname);

    while ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
        if (errno != EINTR) {
            LOG(ERROR, "Cannot open /dev/net/tun");
            return false;
        }
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = flags;
    strncpy(ifr.ifr_name, devname, IFNAMSIZ);

    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
        close_descriptor(fd);
        LOG(ERROR, "Error creating tap interface "
            << devname << ", errno: " << errno);
        return false;
    }
    if (ioctl(fd, TUNSETPERSIST, 1) < 0) {
        close_descriptor(fd);
        LOG(ERROR, "Error setting persistent tap interface "
            << devname << ", errno: " << errno);
        return false;
    }
    close_descriptor(fd);
    LOG(DEBUG, "Created device: " << ifr.ifr_name);

    ifr.ifr_flags = IFF_UP;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOG(ERROR, "Could not open AF_INET socket, errno: " << errno);
        return false;
    }
    ioctl(fd, SIOCSIFFLAGS, &ifr);
    close_descriptor(fd);
    return true;
}

static bool destroy_tap_interface(const char *devname) {
    struct ifreq ifr;
    int fd;

    while ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
        if (errno != EINTR) {
            LOG(ERROR, "Cannot open /dev/net/tun");
            return false;
        }
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP;
    strncpy(ifr.ifr_name, devname, IFNAMSIZ);

    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
        close_descriptor(fd);
        LOG(ERROR, "Error opening tap interface "
            << devname << ", errno: " << errno);
        return false;
    }
    if (ioctl(fd, TUNSETPERSIST, 0) < 0) {
        close_descriptor(fd);
        LOG(ERROR, "Error destroying tap interface "
            << devname << ", errno: " << errno);
        return false;
    }
    close_descriptor(fd);
    return true;
}

static xml_node get_or_create_node(xml_node *parent,
                                   const std::string &child_name) {
    xml_node child = parent->child(child_name.c_str());
    if (child)
        return child;
    return parent->append_child(child_name.c_str());
}

LibvirtInstanceAdapter::~LibvirtInstanceAdapter() {
    CloseConnection();
}

InstanceTask* LibvirtInstanceAdapter::CreateStartTask(
    const ServiceInstance::Properties &si_properties, bool update) {
    LOG(DEBUG, "creating libvirt instance start task");
    if (!EnsureConnected())
        return NULL;

    return new LibvirtInstanceAdapter::DomainStartTask(this, si_properties,
        update);
}

InstanceTask* LibvirtInstanceAdapter::CreateStopTask(
    const ServiceInstance::Properties &si_properties) {
    LOG(DEBUG, "creating libvirt instance stop task");
    if (!EnsureConnected())
        return NULL;

    return new LibvirtInstanceAdapter::DomainStopTask(this, si_properties);
}

bool LibvirtInstanceAdapter::isApplicable(
    const ServiceInstance::Properties &props) {
    LOG(DEBUG, "checked whether libvirt is "
        "applicable for chosen service instance");
    return (props.virtualization_type == ServiceInstance::VRouterInstance
            && props.vrouter_instance_type == ServiceInstance::KVM);
}

bool LibvirtInstanceAdapter::DomainStartTask::Run() {
    is_running_ = true;
    std::string dom_uuid_str =
        boost::lexical_cast<std::string>(si_properties_.instance_id);

    // Domain configuration can't be updated w/o restarting anyway.
    parent_adapter_->EnsureDestroyed(dom_uuid_str, si_properties_);

    // create a transient domain
    std::string xml = XmlConf();
    if (xml.empty()) {
        LOG(ERROR, "Could not parse domain XML");
        return false;
    }
    LOG(DEBUG, "Creating domain: " << xml);

    if (!CreateTAPInterfaces(dom_uuid_str)) {
        LOG(ERROR, "Error creating TAP interfaces");
        return false;
    }

    virDomainPtr dom = virDomainCreateXML(parent_adapter_->conn_,
        xml.c_str(), 0);
    if (dom == NULL) {
        LOG(ERROR, "Error creating domain: " << virGetLastErrorMessage());
        return false;
    }
    parent_adapter_->RegisterInterfaces(si_properties_);

    virDomainFree(dom);
    LOG(DEBUG, "Domain created: " << dom_uuid_str);
    return true;
}

bool LibvirtInstanceAdapter::DomainStopTask::Run() {
    is_running_ = true;
    std::string dom_uuid_str =
        boost::lexical_cast<std::string>(si_properties_.instance_id);
    parent_adapter_->EnsureDestroyed(dom_uuid_str, si_properties_);
    return true;
}

std::string LibvirtInstanceAdapter::DomainStartTask::XmlConf() {
    std::string dom_uuid_str =
        boost::lexical_cast<std::string>(si_properties_.instance_id);
    // make a pugixml config document out of si's instance data
    xml_document domain_xml_conf;
    xml_parse_result parse_result =
        domain_xml_conf.load(si_properties_.instance_data.c_str());
    if (!parse_result || !domain_xml_conf.child("domain")) {
        LOG(ERROR, "Error parsing XML data or domain is missing");
        return "";
    }

    // make sure devices node exists
    xml_node devices_node = domain_xml_conf.child("domain").child("devices");
    if (!devices_node)
        domain_xml_conf.child("domain").append_child("devices");

    // modify and save as a string for libvirt
    DomainXMLAssignUUID(dom_uuid_str, domain_xml_conf);

    // interfaces defined by Contrail (left, right, management)
    DomainXMLSetInterfaceData(domain_xml_conf, dom_uuid_str);

    std::stringstream domain_conf;
    domain_xml_conf.save(domain_conf);
    return domain_conf.str();
}

void LibvirtInstanceAdapter::DomainStartTask::DomainXMLAssignUUID(
            const std::string &dom_uuid_str,
            const xml_document &libvirt_xml_conf) {
    xml_node domain_node = libvirt_xml_conf.child("domain");
    xml_node uuid_node = get_or_create_node(&domain_node, "uuid");
    xml_node name_node = get_or_create_node(&domain_node, "name");
    if (name_node.text().empty())
        name_node.text().set("contrail_si-");
    uuid_node.text().set(dom_uuid_str.c_str());
    name_node.text().set((std::string(name_node.text().get()) + "-" +
        dom_uuid_str.substr(0, 5)).c_str());
    LOG(DEBUG, dom_uuid_str.c_str());
}

bool LibvirtInstanceAdapter::DomainStartTask::CreateTAPInterfaces(
    const std::string &dom_uuid) {
    // to be deprecated. Agent code currently supports only 3 interfaces/dom.
    std::string left = LibvirtInstanceAdapter::GenIntfName(dom_uuid, 'l');
    std::string right = LibvirtInstanceAdapter::GenIntfName(dom_uuid, 'r');
    std::string mgmt = LibvirtInstanceAdapter::GenIntfName(dom_uuid, 'm');

    return alloc_tap_interface(left.c_str(), IFF_TAP) &&
        alloc_tap_interface(right.c_str(), IFF_TAP) &&
        alloc_tap_interface(mgmt.c_str(), IFF_TAP);
}

void LibvirtInstanceAdapter::DomainStartTask::DomainXMLSetInterfaceData(
        const xml_document &libvirt_xml_conf, const std::string &dom_uuid) {
    LOG(DEBUG, "adding vrouter interface "
        "data to libvirt instance configuration");
    // to be deprecated. Agent code currently supports only 3 interfaces/dom.
    xml_node devices_node = libvirt_xml_conf.child("domain").child("devices");

    DomainXMLAddInterface(&devices_node,
                          si_properties_.mac_addr_inside,
                          LibvirtInstanceAdapter::GenIntfName(dom_uuid, 'l'));
    DomainXMLAddInterface(&devices_node,
                          si_properties_.mac_addr_outside,
                          LibvirtInstanceAdapter::GenIntfName(dom_uuid, 'r'));
    DomainXMLAddInterface(&devices_node,
                          si_properties_.mac_addr_management,
                          LibvirtInstanceAdapter::GenIntfName(dom_uuid, 'm'));
}

void LibvirtInstanceAdapter::DomainStartTask::DomainXMLAddInterface(
        xml_node *devices_node, const std::string &mac_addr,
        const std::string &intf_name) {
    xml_node intf_node = devices_node->append_child("interface");
    intf_node.append_attribute("type").set_value("ethernet");

    xml_node mac_node = intf_node.append_child("mac");
    mac_node.append_attribute("address").set_value(mac_addr.c_str());

    xml_node target_node = intf_node.append_child("target");
    target_node.append_attribute("dev").set_value(intf_name.c_str());
}

std::string LibvirtInstanceAdapter::GenIntfName(
        const std::string &dom_uuid, char type) {
    return std::string("tap_" + dom_uuid.substr(0, 8) + type);
}

bool LibvirtInstanceAdapter::EnsureConnected() {
    // gets called from InstanceTask threads
    tbb::mutex::scoped_lock lock(conn_mutex_);

    LOG(DEBUG, "ensuring we have a libvirt connection");
    if (conn_ == NULL) {
        conn_ = virConnectOpen(libvirt_conn_addr_.c_str());
        if (conn_ == NULL)
            return false;
    }
    return true;
}

void LibvirtInstanceAdapter::EnsureDestroyed(
        const std::string &dom_uuid_str,
        const ServiceInstance::Properties &si_properties) {
    virDomainPtr dom = virDomainLookupByUUIDString(conn_, dom_uuid_str.c_str());
    if (dom != NULL) {
        std::string domain_name = std::string(virDomainGetName(dom));
        virDomainDestroy(dom);
        virDomainFree(dom);
    }
    std::string left = LibvirtInstanceAdapter::GenIntfName(dom_uuid_str, 'l');
    std::string right = LibvirtInstanceAdapter::GenIntfName(dom_uuid_str, 'r');
    std::string mgmt = LibvirtInstanceAdapter::GenIntfName(dom_uuid_str, 'm');
    destroy_tap_interface(left.c_str());
    destroy_tap_interface(right.c_str());
    destroy_tap_interface(mgmt.c_str());
    UnregisterInterfaces(si_properties);
}

bool LibvirtInstanceAdapter::RegisterInterfaces(
    const ServiceInstance::Properties &si_properties) {
    LOG(DEBUG, "registering TAP interfaces to vrouter");
    std::string dom_uuid_str =
        boost::lexical_cast<std::string>(si_properties.instance_id);
    switch (si_properties.interface_count) {
        case 3:  // management interface
        VmInterface::NovaAdd(agent_->interface_table(),
                         si_properties.vmi_management,
                         GenIntfName(dom_uuid_str, 'm'),
                         Ip4Address::from_string(
                             si_properties.ip_addr_management),
                         si_properties.mac_addr_management,
                         GenIntfName(dom_uuid_str, 'm'),
                         si_properties.instance_id,
                         VmInterface::kInvalidVlanId,
                         VmInterface::kInvalidVlanId,
                         Agent::NullString(),
                         Ip6Address(),
                         VmInterface::vHostUserClient,
                         Interface::TRANSPORT_ETHERNET);
        agent_->cfg()->cfg_interface_client()->FetchInterfaceData(
            si_properties.vmi_management);
        // fallover
        case 2:  // right interface
        VmInterface::NovaAdd(agent_->interface_table(),
                         si_properties.vmi_outside,
                         GenIntfName(dom_uuid_str, 'r'),
                         Ip4Address::from_string(si_properties.ip_addr_outside),
                         si_properties.mac_addr_outside,
                         GenIntfName(dom_uuid_str, 'r'),
                         si_properties.instance_id,
                         VmInterface::kInvalidVlanId,
                         VmInterface::kInvalidVlanId,
                         Agent::NullString(),
                         Ip6Address(),
                         VmInterface::vHostUserClient,
                         Interface::TRANSPORT_ETHERNET);
        agent_->cfg()->cfg_interface_client()->FetchInterfaceData(
            si_properties.vmi_outside);
        // fallover
        case 1:  // left interface
        VmInterface::NovaAdd(agent_->interface_table(),
                         si_properties.vmi_inside,
                         GenIntfName(dom_uuid_str, 'l'),
                         Ip4Address::from_string(si_properties.ip_addr_inside),
                         si_properties.mac_addr_inside,
                         GenIntfName(dom_uuid_str, 'l'),
                         si_properties.instance_id,
                         VmInterface::kInvalidVlanId,
                         VmInterface::kInvalidVlanId,
                         Agent::NullString(),
                         Ip6Address(),
                         VmInterface::vHostUserClient,
                         Interface::TRANSPORT_ETHERNET);
        agent_->cfg()->cfg_interface_client()->FetchInterfaceData(
            si_properties.vmi_inside);
        break;
        default:
            return false;
    }
    return true;
}

void LibvirtInstanceAdapter::UnregisterInterfaces(
        const ServiceInstance::Properties &si_properties) {
    switch (si_properties.interface_count) {
        case 3:  // management interface
        VmInterface::Delete(agent_->interface_table(),
                           si_properties.vmi_inside,
                           VmInterface::INSTANCE_MSG);
        // fallover
        case 2:  // right interface
        VmInterface::Delete(agent_->interface_table(),
                           si_properties.vmi_outside,
                           VmInterface::INSTANCE_MSG);
        // fallover
        case 1:  // left interface
        VmInterface::Delete(agent_->interface_table(),
                           si_properties.vmi_management,
                           VmInterface::INSTANCE_MSG);
        break;
    }
}

void LibvirtInstanceAdapter::CloseConnection() {
    tbb::mutex::scoped_lock lock(conn_mutex_);
    LOG(DEBUG, "closing libvirt connection");
    if (conn_ != NULL) {
        virConnectClose(conn_);
        conn_ = NULL;
    }
}
