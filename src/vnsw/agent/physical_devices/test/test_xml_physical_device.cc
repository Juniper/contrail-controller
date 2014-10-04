/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include <iostream>
#include <fstream>
#include <pugixml/pugixml.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>

#include <test/test_cmn_util.h>
#include <pkt/test/test_pkt_util.h>
#include <test-xml/test_xml.h>
#include <test-xml/test_xml_oper.h>
#include <test-xml/test_xml_validate.h>
#include "test_xml_physical_device.h"

#include <physical_devices/tables/physical_devices_types.h>
#include <physical_devices/tables/device_manager.h>
#include <physical_devices/tables/physical_device.h>
#include <physical_devices/tables/physical_port.h>
#include <physical_devices/tables/logical_port.h>
#include <physical_devices/tables/physical_device_vn.h>

using namespace std;
using namespace pugi;
using namespace boost::uuids;
using namespace AgentUtXmlUtils;

using AGENT::PhysicalDeviceEntry;
using AGENT::PhysicalDeviceTable;
using AGENT::PhysicalDeviceKey;

using AGENT::PhysicalPortEntry;
using AGENT::PhysicalPortTable;
using AGENT::PhysicalPortKey;

using AGENT::LogicalPortEntry;
using AGENT::LogicalPortTable;
using AGENT::VlanLogicalPortKey;
using AGENT::DefaultLogicalPortKey;

using AGENT::PhysicalDeviceVnEntry;
using AGENT::PhysicalDeviceVnTable;
using AGENT::PhysicalDeviceVnKey;

AgentUtXmlValidationNode *
CreatePhysicalDeviceValidateNode(const string &type, const string &name,
                                 const uuid &id, const xml_node &node) {
    if (type == "physical-router" || type == "device")
        return new AgentUtXmlPhysicalDeviceValidate(name, id, node);
    if (type == "physical-interface" || type == "physical-port")
        return new AgentUtXmlPhysicalInterfaceValidate(name, id, node);
    if (type == "logical-interface" || type == "logical-port")
        return new AgentUtXmlLogicalInterfaceValidate(name, id, node);
    if (type == "physical-router-vn" || type == "device-vn")
        return new AgentUtXmlPhysicalDeviceVnValidate(name, id, node);
}

AgentUtXmlNode *
CreatePhysicalDeviceNode(const string &type, const string &name,const uuid &id,
                         const xml_node &node, AgentUtXmlTestCase *test_case) {
    if (type == "physical-router" || type == "device")
        return new AgentUtXmlPhysicalDevice(name, id, node, test_case);
    if (type == "physical-interface" || type == "physical-port")
        return new AgentUtXmlPhysicalInterface(name, id, node, test_case);
    if (type == "logical-interface" || type == "logical-port")
        return new AgentUtXmlLogicalInterface(name, id, node, test_case);
}

void AgentUtXmlPhysicalDeviceInit(AgentUtXmlTest *test) {
    test->AddConfigEntry("physical-router", CreatePhysicalDeviceNode);
    test->AddConfigEntry("device", CreatePhysicalDeviceNode);
    test->AddConfigEntry("physical-interface", CreatePhysicalDeviceNode);
    test->AddConfigEntry("physical-port", CreatePhysicalDeviceNode);
    test->AddConfigEntry("logical-interface", CreatePhysicalDeviceNode);
    test->AddConfigEntry("logical-port", CreatePhysicalDeviceNode);

    test->AddValidateEntry("physical-router", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("device", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("physical-interface", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("physical-port", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("logical-interface", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("logical-port", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("physical-router-vn", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("device-vn", CreatePhysicalDeviceValidateNode);
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlPhysicalDevice routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlPhysicalDevice::AgentUtXmlPhysicalDevice(const string &name,
                                                   const uuid &id,
                                                   const xml_node &node,
                                                   AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlPhysicalDevice::~AgentUtXmlPhysicalDevice() {
}


bool AgentUtXmlPhysicalDevice::ReadXml() {
    return AgentUtXmlConfig::ReadXml();
}

bool AgentUtXmlPhysicalDevice::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());
    AddIdPerms(&n);
    return true;
}

void AgentUtXmlPhysicalDevice::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlPhysicalDevice::NodeType() {
    return "physical-router";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlPhysicalInterface routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlPhysicalInterface::AgentUtXmlPhysicalInterface
    (const string &name, const uuid &id, const xml_node &node,
     AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlPhysicalInterface::~AgentUtXmlPhysicalInterface() {
}


bool AgentUtXmlPhysicalInterface::ReadXml() {
    if (AgentUtXmlConfig::ReadXml() == false)
        return false;

    GetStringAttribute(node(), "device", &device_name_);
    return true;
}

bool AgentUtXmlPhysicalInterface::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());
    AddIdPerms(&n);

    if (device_name_ != "") {
        LinkXmlNode(parent, NodeType(), name(), "physical-router",
                    device_name_);
    }

    return true;
}

void AgentUtXmlPhysicalInterface::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlPhysicalInterface::NodeType() {
    return "physical-interface";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlLogicalInterface routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlLogicalInterface::AgentUtXmlLogicalInterface
    (const string &name, const uuid &id, const xml_node &node,
     AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlLogicalInterface::~AgentUtXmlLogicalInterface() {
}

bool AgentUtXmlLogicalInterface::ReadXml() {
    if (AgentUtXmlConfig::ReadXml() == false)
        return false;

    GetStringAttribute(node(), "port", &port_name_);
    return true;
}

bool AgentUtXmlLogicalInterface::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());
    AddIdPerms(&n);

    if (port_name_ != "") {
        LinkXmlNode(parent, NodeType(), name(), "physical-interface",
                    port_name_);
    }

    return true;
}

void AgentUtXmlLogicalInterface::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlLogicalInterface::NodeType() {
    return "logical-interface";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlPhysicalDeviceValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlPhysicalDeviceValidate::AgentUtXmlPhysicalDeviceValidate
(const string &name, const uuid &id, const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id) {
}

AgentUtXmlPhysicalDeviceValidate::~AgentUtXmlPhysicalDeviceValidate() {
}

bool AgentUtXmlPhysicalDeviceValidate::ReadXml() {
    return true;
}

bool AgentUtXmlPhysicalDeviceValidate::Validate() {
    PhysicalDeviceEntry *dev;
    PhysicalDeviceKey key(id_);
    dev = static_cast<PhysicalDeviceEntry *>
        (Agent::GetInstance()->device_manager()->device_table()->FindActiveEntry(&key));
    if (present()) {
        return dev != NULL;
    } else {
        return dev == NULL;
    }
}

const string AgentUtXmlPhysicalDeviceValidate::ToString() {
    return "physical-router";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlPhysicalInterfaceValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlPhysicalInterfaceValidate::AgentUtXmlPhysicalInterfaceValidate
    (const string &name, const uuid &id, const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id), device_uuid_() {
}

AgentUtXmlPhysicalInterfaceValidate::~AgentUtXmlPhysicalInterfaceValidate() {
}

bool AgentUtXmlPhysicalInterfaceValidate::ReadXml() {

    if (AgentUtXmlValidationNode::ReadXml() == false)
        return false;

    uint16_t id = 0;
    GetUintAttribute(node(), "device", &id);
    if (id) {
        device_uuid_ = MakeUuid(id);
    }

    return true;
}

bool AgentUtXmlPhysicalInterfaceValidate::Validate() {
    PhysicalDeviceManager *mgr = Agent::GetInstance()->device_manager();

    PhysicalPortEntry *port;
    PhysicalPortKey key(id_);
    port = static_cast<PhysicalPortEntry *>
        (mgr->physical_port_table()->FindActiveEntry(&key));

    if (present() == false) {
        if (port != NULL)
            return false;
        return true;
    }

    if (port == NULL)
        return false;

    if (device_uuid_ != nil_uuid()) {
        PhysicalDeviceEntry *dev = port->device();
        if (dev->uuid() != device_uuid_)
            return false;
    }

    return true;
}

const string AgentUtXmlPhysicalInterfaceValidate::ToString() {
    return "physical-interface";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlLogicalInterfaceValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlLogicalInterfaceValidate::AgentUtXmlLogicalInterfaceValidate
    (const string &name, const uuid &id, const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id), physical_port_uuid_(),
    device_uuid_(), vlan_() {
}

AgentUtXmlLogicalInterfaceValidate::~AgentUtXmlLogicalInterfaceValidate() {
}

bool AgentUtXmlLogicalInterfaceValidate::ReadXml() {
    if (AgentUtXmlValidationNode::ReadXml() == false)
        return false;

    uint16_t id = 0;
    GetUintAttribute(node(), "port", &id);
    if (id) {
        physical_port_uuid_ = MakeUuid(id);
    }

    GetUintAttribute(node(), "vlan", &vlan_);
    return true;
}

bool AgentUtXmlLogicalInterfaceValidate::Validate() {
    PhysicalDeviceManager *mgr = Agent::GetInstance()->device_manager();

    LogicalPortEntry *port;

    if (vlan_ == 0) {
        DefaultLogicalPortKey key(id_);
        port = static_cast<LogicalPortEntry *>
            (mgr->logical_port_table()->FindActiveEntry(&key));
    } else {
        VlanLogicalPortKey key(id_);
        port = static_cast<LogicalPortEntry *>
            (mgr->logical_port_table()->FindActiveEntry(&key));
    }

    if (present() == false) {
        if (port != NULL)
            return false;
        return true;
    }

    if (port == NULL)
        return false;

    if (physical_port_uuid_ != nil_uuid()) {
        PhysicalPortEntry *physical_port = port->physical_port();
        if (physical_port->uuid() != physical_port_uuid_)
            return false;
    }

    return true;
}

const string AgentUtXmlLogicalInterfaceValidate::ToString() {
    return "logical-interface";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlPhysicalDeviceVnValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlPhysicalDeviceVnValidate::AgentUtXmlPhysicalDeviceVnValidate
(const string &name, const uuid &id, const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id) {
}

AgentUtXmlPhysicalDeviceVnValidate::~AgentUtXmlPhysicalDeviceVnValidate() {
}

bool AgentUtXmlPhysicalDeviceVnValidate::ReadXml() {
    if (AgentUtXmlValidationNode::ReadXml() == false)
        return false;

    uint16_t id = 0;
    GetUintAttribute(node(), "device", &id);
    if (id) {
        device_uuid_ = MakeUuid(id);
    }

    GetUintAttribute(node(), "vn", &id);
    if (id) {
        vn_uuid_ = MakeUuid(id);
    }

    return true;
}

bool AgentUtXmlPhysicalDeviceVnValidate::Validate() {
    PhysicalDeviceManager *mgr = Agent::GetInstance()->device_manager();

    PhysicalDeviceVnEntry *entry;
    PhysicalDeviceVnKey key(device_uuid_, vn_uuid_);
    entry = static_cast<PhysicalDeviceVnEntry *>
        (mgr->physical_device_vn_table()->FindActiveEntry(&key));

    if (present() == false) {
        if (entry != NULL)
            return false;
        return true;
    }

    if (entry == NULL)
        return false;

    return true;
}

const string AgentUtXmlPhysicalDeviceVnValidate::ToString() {
    return "physical-device-vn";
}
