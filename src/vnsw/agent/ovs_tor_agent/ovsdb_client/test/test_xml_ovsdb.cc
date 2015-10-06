/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
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

#include <ovs_tor_agent/tor_agent_init.h>
#include <ovsdb_client.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>
#include <physical_switch_ovsdb.h>
#include <logical_switch_ovsdb.h>
#include <unicast_mac_remote_ovsdb.h>
#include <vlan_port_binding_ovsdb.h>
#include <vrf_ovsdb.h>
#include <vn_ovsdb.h>
#include <multicast_mac_local_ovsdb.h>

#include <test-xml/test_xml.h>
#include <test-xml/test_xml_oper.h>
#include <test-xml/test_xml_validate.h>
#include "test_xml_ovsdb.h"

using namespace std;
using namespace pugi;
using namespace boost::uuids;
using namespace AgentUtXmlUtils;
using namespace OVSDB;

static OvsdbClientSession *ovs_test_session = NULL;

void OvsdbTestSetSessionContext(OvsdbClientSession *sess) {
    ovs_test_session = sess;
}

AgentUtXmlValidationNode *
CreateOvsdbValidateNode(const string &type, const string &name,
                        const uuid &id, const xml_node &node) {
    if (type == "ovs-logical-switch")
        return new AgentUtXmlLogicalSwitchValidate(name, id, node);
    if (type == "ovs-uc-remote")
        return new AgentUtXmlUnicastRemoteValidate(name, id, node);
    if (type == "ovs-vrf")
        return new AgentUtXmlOvsdbVrfValidate(name, id, node);
    if (type == "ovs-vlan-port-binding")
        return new AgentUtXmlVlanPortBindingValidate(name, id, node);
}

void AgentUtXmlOvsdbInit(AgentUtXmlTest *test) {
    test->AddValidateEntry("ovs-logical-switch", CreateOvsdbValidateNode);
    test->AddValidateEntry("ovs-uc-remote", CreateOvsdbValidateNode);
    test->AddValidateEntry("ovs-vrf", CreateOvsdbValidateNode);
    test->AddValidateEntry("ovs-vlan-port-binding", CreateOvsdbValidateNode);
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlLogicalSwitchValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlLogicalSwitchValidate::AgentUtXmlLogicalSwitchValidate
(const string &name, const uuid &id, const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id), vxlan_id_(0),
    match_vxlan_(false) {
}

AgentUtXmlLogicalSwitchValidate::~AgentUtXmlLogicalSwitchValidate() {
}

bool AgentUtXmlLogicalSwitchValidate::ReadXml() {
    if (AgentUtXmlValidationNode::ReadXml() == false)
        return false;
    match_vxlan_ = GetUintAttribute(node(), "vxlan-id", &vxlan_id_);
    return true;
}

bool AgentUtXmlLogicalSwitchValidate::Validate() {
    LogicalSwitchTable *table = ovs_test_session->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(table, UuidToString(id_));
    LogicalSwitchEntry *entry = static_cast<LogicalSwitchEntry *>
        (table->Find(&key));
    if (present()) {
        bool ret = (entry != NULL && entry->GetState() == KSyncEntry::IN_SYNC);
        if (ret == true && match_vxlan_) {
            ret = ((uint16_t)entry->vxlan_id() == vxlan_id_);
        }
        return ret;
    } else {
        return (entry == NULL);
    }
}

const string AgentUtXmlLogicalSwitchValidate::ToString() {
    return "ovs-logical-switch";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlOvsdbVrfValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlOvsdbVrfValidate::AgentUtXmlOvsdbVrfValidate
(const string &name, const uuid &id, const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id) {
}

AgentUtXmlOvsdbVrfValidate::~AgentUtXmlOvsdbVrfValidate() {
}

bool AgentUtXmlOvsdbVrfValidate::ReadXml() {
    if (AgentUtXmlValidationNode::ReadXml() == false)
        return false;

    uint16_t id = 0;
    if (GetUintAttribute(node(), "vn-uuid", &id) == false) {
        cout << "Attribute Parsing failed " << endl;
        return false;
    }

    vn_uuid_ = MakeUuid(id);

    return true;
}

bool AgentUtXmlOvsdbVrfValidate::Validate() {
    VrfOvsdbObject *table = ovs_test_session->client_idl()->vrf_ovsdb();
    VrfOvsdbEntry vrf_key(table, UuidToString(vn_uuid_));
    VrfOvsdbEntry *vrf_entry =
        static_cast<VrfOvsdbEntry *>(table->Find(&vrf_key));
    if (present()) {
        if (vrf_entry == NULL) {
            return false;
        }
    } else {
        return (vrf_entry == NULL);
    }

    return true;
}

const string AgentUtXmlOvsdbVrfValidate::ToString() {
    return "ovs-vrf";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlUnicastRemoteValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlUnicastRemoteValidate::AgentUtXmlUnicastRemoteValidate
(const string &name, const uuid &id, const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id) {
}

AgentUtXmlUnicastRemoteValidate::~AgentUtXmlUnicastRemoteValidate() {
}

bool AgentUtXmlUnicastRemoteValidate::ReadXml() {
    if (AgentUtXmlValidationNode::ReadXml() == false)
        return false;
    if (GetStringAttribute(node(), "mac", &mac_) == false) {
        cout << "Attribute Parsing failed " << endl;
        return false;
    }

    uint16_t id = 0;
    if (GetUintAttribute(node(), "vn-uuid", &id) == false) {
        cout << "Attribute Parsing failed " << endl;
        return false;
    }

    vn_uuid_ = MakeUuid(id);

    return true;
}

bool AgentUtXmlUnicastRemoteValidate::Validate() {
    VrfOvsdbObject *table = ovs_test_session->client_idl()->vrf_ovsdb();
    VrfOvsdbEntry vrf_key(table, UuidToString(vn_uuid_));
    VrfOvsdbEntry *vrf_entry =
        static_cast<VrfOvsdbEntry *>(table->Find(&vrf_key));
    if (vrf_entry == NULL) {
        if (present()) {
            return false;
        }
        return true;
    }

    UnicastMacRemoteTable *u_table = vrf_entry->route_table();
    UnicastMacRemoteEntry key(u_table, mac_);
    UnicastMacRemoteEntry *entry = static_cast<UnicastMacRemoteEntry *>
        (u_table->Find(&key));
    if (present()) {
        return (entry != NULL && entry->GetState() == KSyncEntry::IN_SYNC);
    } else {
        return entry == NULL;
    }
}

const string AgentUtXmlUnicastRemoteValidate::ToString() {
    return "ovs-unicast-remote";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVlanPortBindingValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVlanPortBindingValidate::AgentUtXmlVlanPortBindingValidate
(const string &name, const uuid &id, const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id), stale_entry_(false) {
}

AgentUtXmlVlanPortBindingValidate::~AgentUtXmlVlanPortBindingValidate() {
}

bool AgentUtXmlVlanPortBindingValidate::ReadXml() {
    if (AgentUtXmlValidationNode::ReadXml() == false)
        return false;
    if (GetStringAttribute(node(), "physical-device",
                           &physical_device_name_) == false) {
        cout << "Attribute Parsing failed " << endl;
        return false;
    }

    if (GetStringAttribute(node(), "physical-interface",
                           &physical_port_name_) == false) {
        cout << "Attribute Parsing failed " << endl;
        return false;
    }

    uint16_t id = 0;
    if (GetUintAttribute(node(), "ls-uuid", &id) == false) {
        cout << "Attribute Parsing failed " << endl;
        return false;
    }
    logical_switch_name_ = UuidToString(MakeUuid(id));

    std::string str;
    if (GetStringAttribute(node(), "stale", &str)) {
        if (str == "1" || str == "yes")
            stale_entry_ = true;
        else
            stale_entry_ = false;
    }

    if (GetUintAttribute(node(), "vlan", &vlan_id_) == false) {
        cout << "Attribute Parsing failed " << endl;
        return false;
    }
    return true;
}

bool AgentUtXmlVlanPortBindingValidate::Validate() {
    VlanPortBindingTable *table =
        ovs_test_session->client_idl()->vlan_port_table();
    VlanPortBindingEntry vlan_port_key(table, physical_device_name_,
                                       physical_port_name_, vlan_id_,
                                       "");
    VlanPortBindingEntry *vlan_port_entry =
        static_cast<VlanPortBindingEntry *>(table->Find(&vlan_port_key));
    if (present()) {
        if (vlan_port_entry == NULL) {
            return false;
        }
    } else {
        return (vlan_port_entry == NULL);
    }

    if (stale_entry_ != vlan_port_entry->stale()) {
        return false;
    }

    if (logical_switch_name_ != vlan_port_entry->logical_switch_name()) {
        return false;
    }

    if (vlan_id_ != vlan_port_entry->vlan()) {
        return false;
    }

    return true;
}

const string AgentUtXmlVlanPortBindingValidate::ToString() {
    return "ovs-vlan-port-binding";
}
