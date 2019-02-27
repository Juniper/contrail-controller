/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include <base/os.h>
#include <iostream>
#include <fstream>
#include <pugixml/pugixml.hpp>
#include <boost/uuid/uuid.hpp>

#include <test/test_cmn_util.h>
#include <pkt/test/test_pkt_util.h>
#include <oper/physical_device.h>
#include <oper/physical_device_vn.h>

#include <test-xml/test_xml.h>
#include <test-xml/test_xml_oper.h>
#include <test-xml/test_xml_validate.h>
#include "test_xml_physical_device.h"

using namespace std;
using namespace pugi;
using namespace boost::uuids;
using namespace AgentUtXmlUtils;

AgentUtXmlValidationNode *
CreatePhysicalDeviceValidateNode(const string &type, const string &name,
                                 const uuid &id, const xml_node &node) {
    if (type == "physical-router" || type == "device")
        return new AgentUtXmlPhysicalDeviceValidate(name, id, node);
    if (type == "physical-interface" || type == "physical-port")
        return new AgentUtXmlPhysicalInterfaceValidate(name, id, node);
    if (type == "remote-physical-interface" || type == "remote-physical-port")
        return new AgentUtXmlRemotePhysicalInterfaceValidate(name, id, node);
    if (type == "logical-interface" || type == "logical-port")
        return new AgentUtXmlLogicalInterfaceValidate(name, id, node);
    if (type == "physical-router-vn" || type == "device-vn")
        return new AgentUtXmlPhysicalDeviceVnValidate(name, id, node);
    if (type == "multicast-tor")
        return new AgentUtXmlMulticastTorValidate(name, node);
    return NULL;
}

AgentUtXmlNode *
CreatePhysicalDeviceNode(const string &type, const string &name,const uuid &id,
                         const xml_node &node, AgentUtXmlTestCase *test_case) {
    if (type == "physical-router" || type == "device")
        return new AgentUtXmlPhysicalDevice(name, id, node, test_case);
    if (type == "physical-interface" || type == "physical-port")
        return new AgentUtXmlPhysicalInterface(name, id, node, test_case);
    if (type == "remote-physical-interface" || type == "remote-physical-port")
        return new AgentUtXmlRemotePhysicalInterface(name, id, node, test_case);
    if (type == "logical-interface" || type == "logical-port")
        return new AgentUtXmlLogicalInterface(name, id, node, test_case);
    return NULL;
}

void AgentUtXmlPhysicalDeviceInit(AgentUtXmlTest *test) {
    test->AddConfigEntry("physical-router", CreatePhysicalDeviceNode);
    test->AddConfigEntry("device", CreatePhysicalDeviceNode);
    test->AddConfigEntry("physical-interface", CreatePhysicalDeviceNode);
    test->AddConfigEntry("physical-port", CreatePhysicalDeviceNode);
    test->AddConfigEntry("remote-physical-interface", CreatePhysicalDeviceNode);
    test->AddConfigEntry("logical-interface", CreatePhysicalDeviceNode);
    test->AddConfigEntry("logical-port", CreatePhysicalDeviceNode);

    test->AddValidateEntry("physical-router", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("device", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("physical-interface", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("physical-port", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("remote-physical-interface",
                           CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("logical-interface", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("logical-port", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("physical-router-vn", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("device-vn", CreatePhysicalDeviceValidateNode);
    test->AddValidateEntry("multicast-tor", CreatePhysicalDeviceValidateNode);
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
    string display_name;
    if (GetStringAttribute(node(), "display", &display_name)) {
        AddXmlNodeWithValue(&n, "display-name", display_name);
    } else {
        AddXmlNodeWithValue(&n, "display-name", name());
    }
    string device_ip;
    if (GetStringAttribute(node(), "dataplane-ip", &device_ip)) {
        AddXmlNodeWithValue(&n, "physical-router-dataplane-ip", device_ip);
    } else {
        AddXmlNodeWithValue(&n, "physical-router-dataplane-ip", "111.111.111.111");
    }
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
    AddXmlNodeWithValue(&n, "display-name", name());
    AddIdPerms(&n);

    if (device_name_ != "") {
        LinkXmlNode(parent, "physical-router", device_name_,
                    NodeType(), name());
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
//  AgentUtXmlRemotePhysicalInterface routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlRemotePhysicalInterface::AgentUtXmlRemotePhysicalInterface
    (const string &name, const uuid &id, const xml_node &node,
     AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlRemotePhysicalInterface::~AgentUtXmlRemotePhysicalInterface() {
}


bool AgentUtXmlRemotePhysicalInterface::ReadXml() {
    if (AgentUtXmlConfig::ReadXml() == false)
        return false;

    GetStringAttribute(node(), "device", &device_name_);
    return true;
}

bool AgentUtXmlRemotePhysicalInterface::ToXml(xml_node *parent) {
    Agent *agent = Agent::GetInstance();
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    string fqdn;
    if (device_name_ != "") {
        fqdn = "dummy:" + device_name_ + ":" + name();
    } else {
        fqdn = "dummy:" + agent->agent_name() + ":" + name();
    }
    AddXmlNodeWithValue(&n, "name", fqdn);
    string display_name;
    if (GetStringAttribute(node(), "display", &display_name)) {
        AddXmlNodeWithValue(&n, "display-name", display_name);
    } else {
        AddXmlNodeWithValue(&n, "display-name", name());
    }
    AddIdPerms(&n);

    if (device_name_ != "") {
        LinkXmlNode(parent, "physical-router", device_name_,
                    NodeType(), fqdn);
    }

    return true;
}

void AgentUtXmlRemotePhysicalInterface::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlRemotePhysicalInterface::NodeType() {
    return "physical-interface";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlLogicalInterface routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlLogicalInterface::AgentUtXmlLogicalInterface
    (const string &name, const uuid &id, const xml_node &node,
     AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case), vlan_(0xFFFF) {
}

AgentUtXmlLogicalInterface::~AgentUtXmlLogicalInterface() {
}

bool AgentUtXmlLogicalInterface::ReadXml() {
    if (AgentUtXmlConfig::ReadXml() == false)
        return false;

    GetStringAttribute(node(), "port", &port_name_);
    GetStringAttribute(node(), "vmi", &vmi_name_);
    GetUintAttribute(node(), "vlan", &vlan_);
    return true;
}

bool AgentUtXmlLogicalInterface::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());
    AddXmlNodeWithValue(&n, "display-name", name());
    if (vlan_ >= 0 && vlan_ <= 0x4096)
        AddXmlNodeWithIntValue(&n, "logical-interface-vlan-tag", vlan_);
    AddIdPerms(&n);

    if (port_name_ != "") {
        LinkXmlNode(parent, NodeType(), name(), "physical-interface",
                    port_name_);
    }

    if (vmi_name_ != "") {
        LinkXmlNode(parent, NodeType(), name(), "virtual-machine-interface",
                    vmi_name_);
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
    AgentUtXmlValidationNode(name, node), id_(id), match_display_name_(false) {
}

AgentUtXmlPhysicalDeviceValidate::~AgentUtXmlPhysicalDeviceValidate() {
}

bool AgentUtXmlPhysicalDeviceValidate::ReadXml() {
    if (AgentUtXmlValidationNode::ReadXml() == false)
        return false;
    match_display_name_ = GetStringAttribute(node(), "display", &display_name_);
    return true;
}

bool AgentUtXmlPhysicalDeviceValidate::Validate() {
    PhysicalDevice *dev;
    PhysicalDeviceKey key(id_);
    dev = static_cast<PhysicalDevice *>
        (Agent::GetInstance()->physical_device_table()->FindActiveEntry(&key));
    if (present()) {
        if (dev != NULL) {
            if (match_display_name_ && display_name_ != dev->name()) {
                return false;
            }
            return true;
        }
        return false;
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
    AgentUtXmlValidationNode(name, node), id_(id) {
}

AgentUtXmlPhysicalInterfaceValidate::~AgentUtXmlPhysicalInterfaceValidate() {
}

bool AgentUtXmlPhysicalInterfaceValidate::ReadXml() {

    if (AgentUtXmlValidationNode::ReadXml() == false)
        return false;
    return true;
}

bool AgentUtXmlPhysicalInterfaceValidate::Validate() {
    Agent *agent = Agent::GetInstance();

    PhysicalInterface *port;
    PhysicalInterfaceKey key(name());
    port = static_cast<PhysicalInterface *>
        (agent->interface_table()->FindActiveEntry(&key));

    if (present() == false) {
        if (port != NULL)
            return false;
        return true;
    }

    if (port == NULL)
        return false;

    return true;
}

const string AgentUtXmlPhysicalInterfaceValidate::ToString() {
    return "physical-interface";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlRemotePhysicalInterfaceValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlRemotePhysicalInterfaceValidate::AgentUtXmlRemotePhysicalInterfaceValidate
    (const string &name, const uuid &id, const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id), device_uuid_() {
}

AgentUtXmlRemotePhysicalInterfaceValidate::~AgentUtXmlRemotePhysicalInterfaceValidate() {
}

bool AgentUtXmlRemotePhysicalInterfaceValidate::ReadXml() {

    if (AgentUtXmlValidationNode::ReadXml() == false)
        return false;

    uint16_t id = 0;
    GetUintAttribute(node(), "device", &id);
    if (id) {
        device_uuid_ = MakeUuid(id);
    }

    return true;
}

bool AgentUtXmlRemotePhysicalInterfaceValidate::Validate() {
    Agent *agent = Agent::GetInstance();

    RemotePhysicalInterface *port;
    RemotePhysicalInterfaceKey key(name());
    port = static_cast<RemotePhysicalInterface *>
        (agent->interface_table()->FindActiveEntry(&key));

    if (present() == false) {
        if (port != NULL)
            return false;
        return true;
    }

    if (port == NULL)
        return false;

    if (device_uuid_ != nil_uuid()) {
        PhysicalDevice *dev = port->physical_device();
        if (dev->uuid() != device_uuid_)
            return false;
    }

    return true;
}

const string AgentUtXmlRemotePhysicalInterfaceValidate::ToString() {
    return "physical-interface";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlLogicalInterfaceValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlLogicalInterfaceValidate::AgentUtXmlLogicalInterfaceValidate
    (const string &name, const uuid &id, const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id), physical_port_(),
    device_uuid_(), vmi_uuid_(), vlan_(0xFFFF) {
}

AgentUtXmlLogicalInterfaceValidate::~AgentUtXmlLogicalInterfaceValidate() {
}

bool AgentUtXmlLogicalInterfaceValidate::ReadXml() {
    if (AgentUtXmlValidationNode::ReadXml() == false)
        return false;

    GetStringAttribute(node(), "port", &physical_port_);

    uint16_t id = 0;
    GetUintAttribute(node(), "vmi", &id);
    if (id) {
        vmi_uuid_ = MakeUuid(id);
    }

    GetUintAttribute(node(), "vlan", &vlan_);
    return true;
}

bool AgentUtXmlLogicalInterfaceValidate::Validate() {
    Agent *agent = Agent::GetInstance();

    VlanLogicalInterface *port;

    VlanLogicalInterfaceKey key(id_, name());
    port = static_cast<VlanLogicalInterface *>
        (agent->interface_table()->FindActiveEntry(&key));

    if (present() == false) {
        if (port != NULL)
            return false;
        return true;
    }

    if (port == NULL)
        return false;

    if (physical_port_ != "") {
        PhysicalInterface *physical_port = static_cast<PhysicalInterface *>
            (port->physical_interface());
        if (physical_port == NULL)
            return false;
        if (physical_port->name() != physical_port_)
            return false;
    }

    if (vmi_uuid_ != nil_uuid()) {
        Interface *vmi = port->vm_interface();
        if (vmi == NULL)
            return false;
        if (vmi->GetUuid() != vmi_uuid_)
            return false;
    }

    if (vlan_ >= 0 && vlan_ < 4096) {
        if (vlan_ != port->vlan())
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
    AgentUtXmlValidationNode(name, node), id_(id), vxlan_id_(0xFFFF) {
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

    GetUintAttribute(node(), "vxlan-id", &vxlan_id_);
    return true;
}

bool AgentUtXmlPhysicalDeviceVnValidate::Validate() {
    Agent *agent = Agent::GetInstance();

    PhysicalDeviceVn *entry;
    PhysicalDeviceVnKey key(device_uuid_, vn_uuid_);
    entry = static_cast<PhysicalDeviceVn *>
        (agent->physical_device_vn_table()->FindActiveEntry(&key));

    if (present() == false) {
        if (entry != NULL)
            return false;
        return true;
    }

    if (entry == NULL)
        return false;

    if (vxlan_id_ != 0xFFFF) {
        if (entry->vxlan_id() != vxlan_id_)
            return false;
        VnEntry *vn = entry->vn();
        if (vn == NULL)
            return false;
        if (vn->GetVxLanId() != vxlan_id_)
            return false;
    }

    return true;
}

const string AgentUtXmlPhysicalDeviceVnValidate::ToString() {
    return "physical-device-vn";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlMulticastTorValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlMulticastTorValidate::AgentUtXmlMulticastTorValidate
(const string &name, const xml_node &node) :
    AgentUtXmlValidationNode(name, node) {
}

AgentUtXmlMulticastTorValidate::~AgentUtXmlMulticastTorValidate() {
}

bool AgentUtXmlMulticastTorValidate::ReadXml() {
    if (AgentUtXmlValidationNode::ReadXml() == false)
        return false;

    GetStringAttribute(node(), "name", &test_name_);
    return true;
}

bool AgentUtXmlMulticastTorValidate::Validate() {
    Agent *agent = Agent::GetInstance();
    if (test_name_ == "force-change-vxlan-network-id-mode") {
        agent->set_vxlan_network_identifier_mode(Agent::CONFIGURED);
    }
    static BgpPeer *peer = NULL;
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent->
                                                                   oper_db()->multicast());
    VrfEntry *vrf =
        Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf1");

    if (test_name_ == "add_tor_olist") {
        BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
            (vrf->GetBridgeRouteTable());
        if (peer == NULL) {
            peer = CreateBgpPeer("127.0.0.1", "multicast-tor-test");
        }
        //Add multicast tor olist
        TunnelOlist olist;
        olist.push_back(OlistTunnelEntry(nil_uuid(), 10,
                                         IpAddress::from_string("8.8.8.8").to_v4(),
                                         TunnelType::VxlanType()));
        AgentPath *path = NULL;
        BridgeRouteEntry *entry = table->FindRoute(MacAddress::BroadcastMac());
        if (entry != NULL)
            path = entry->FindPath(peer);
        if (path == NULL) {
            mc_handler->ModifyTorMembers(peer,
                                         vrf->GetName(),
                                         olist,
                                         10,
                                         1);
        }
        //Verify CNH
        entry = table->FindRoute(MacAddress::BroadcastMac());
        if (entry == NULL) {
            return false;
        }
        path = entry->FindPath(agent->multicast_peer());
        if (path == NULL) {
            return false;
        }
        path = entry->FindPath(peer);
        if (path == NULL) {
            return false;
        }
    }

    if (test_name_ == "del_tor_olist") {
        BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
            (vrf->GetBridgeRouteTable());
        TunnelOlist olist;
        //Delete route
        mc_handler->ModifyTorMembers(peer,
                                     vrf->GetName(),
                                     olist,
                                     10,
                                     ControllerPeerPath::kInvalidPeerIdentifier);
        BridgeRouteEntry *entry = table->FindRoute(MacAddress::BroadcastMac());
        if (entry) {
            const AgentPath *path = entry->FindPath(peer);
            if (path != NULL) {
                return false;
            }
        }
        if (peer)
            DeleteBgpPeer(peer);
    }

    return true;
}

const string AgentUtXmlMulticastTorValidate::ToString() {
    return "multicast-tor";
}
