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
#include <vrf_ovsdb.h>

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
}

void AgentUtXmlOvsdbInit(AgentUtXmlTest *test) {
    test->AddValidateEntry("ovs-logical-switch", CreateOvsdbValidateNode);
    test->AddValidateEntry("ovs-uc-remote", CreateOvsdbValidateNode);
    test->AddValidateEntry("ovs-vrf", CreateOvsdbValidateNode);
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlLogicalSwitchValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlLogicalSwitchValidate::AgentUtXmlLogicalSwitchValidate
(const string &name, const uuid &id, const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id) {
}

AgentUtXmlLogicalSwitchValidate::~AgentUtXmlLogicalSwitchValidate() {
}

bool AgentUtXmlLogicalSwitchValidate::ReadXml() {
    if (AgentUtXmlValidationNode::ReadXml() == false)
        return false;
    return true;
}

bool AgentUtXmlLogicalSwitchValidate::Validate() {
    LogicalSwitchTable *table = ovs_test_session->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(table, UuidToString(id_));
    LogicalSwitchEntry *entry = static_cast<LogicalSwitchEntry *>
        (table->Find(&key));
    if (present()) {
        return (entry != NULL && entry->GetState() == KSyncEntry::IN_SYNC);
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
    if (vrf_entry == NULL) {
        if (present()) {
            return false;
        }
        return true;
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

