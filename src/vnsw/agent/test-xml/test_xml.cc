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
#include "test_xml.h"
#include "test_xml_validate.h"

using namespace std;
using namespace pugi;
using namespace boost::uuids;

static bool GetStringAttribute(const xml_node &node, const string &name,
                               string *value) {
    xml_attribute attr = node.attribute(name.c_str());
    if (!attr) {
        return false;;
    }

    *value = attr.as_string();

    return true;
}

static bool GetUintAttribute(const xml_node &node, const string &name,
                             uint16_t *value) {
    xml_attribute attr = node.attribute(name.c_str());
    if (!attr) {
        return false;;
    }

    *value = attr.as_uint();

    return true;
}

static void NovaIntfAdd(bool op_delete, const uuid &id, const IpAddress &ip,
                        const uuid &vm_uuid, const uuid vn_uuid,
                        const string &name, const string &mac,
                        const string vm_name) {
    CfgIntKey *key = new CfgIntKey(id);
    DBRequest req;
    req.key.reset(key);

    if (op_delete) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        Agent::GetInstance()->interface_config_table()->Enqueue(&req);
        cout << "Nova Del Interface Message " << endl;
        return;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    CfgIntData *data = new CfgIntData();
    req.data.reset(data);
    data->Init(vm_uuid, vn_uuid, MakeUuid(0), name, ip,
               Ip6Address::v4_compatible(ip.to_v4()), mac, vm_name, 0,
               CfgIntEntry::CfgIntVMPort, 0);

    Agent::GetInstance()->interface_config_table()->Enqueue(&req);
    cout << "Nova Add Interface Message " << endl;
    return;
}

static void LinkXmlNode(xml_node *parent, const string &ltype,
                        const string lname, const string &rtype,
                        const string rname) {
    xml_node n = parent->append_child("link");

    xml_node n1 = n.append_child("node");
    n1.append_attribute("type") = ltype.c_str();
    
    xml_node n2 = n1.append_child("name");
    n2.append_child(pugi::node_pcdata).set_value(lname.c_str());

    n1 = n.append_child("node");
    n1.append_attribute("type") = rtype.c_str();
    
    n2 = n1.append_child("name");
    n2.append_child(pugi::node_pcdata).set_value(rname.c_str());

    return;
}

xml_node AddXmlNodeWithAttr(xml_node *parent, const char *attr) {
    xml_node n = parent->append_child("node");
    n.append_attribute("type") = attr;
    return n;
}

xml_node AddXmlNodeWithValue(xml_node *parent, const char *name,
                             const string &value) {
    xml_node n = parent->append_child(name);
    n.append_child(pugi::node_pcdata).set_value(value.c_str());
}

xml_node AddXmlNodeWithIntValue(xml_node *parent, const char *name,
                                int val) {
    stringstream s;
    s << val;
    xml_node n = parent->append_child(name);
    n.append_child(pugi::node_pcdata).set_value(s.str().c_str());
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlTest routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlTest::AgentUtXmlTest(const std::string &name) : file_name_(name) {
}

AgentUtXmlTest::~AgentUtXmlTest() {
    // TODO : Delete list_
}

bool AgentUtXmlTest::ReadXml() {
    xml_node list = doc_.child("test_suite");
    GetStringAttribute(list, "name", &name_);

    for (xml_node node = list.first_child(); node; node = node.next_sibling()) {
        if (strcmp(node.name(), "test") == 0) {
            xml_attribute attr = node.attribute("name");
            if (!attr) {
                cout << "Missing attribute \"name\". Skipping" << endl;
                continue;
            }
            AgentUtXmlTestCase *test = new AgentUtXmlTestCase(attr.value(),
                                                              node, this);
            test_list_.push_back(test);
            test->ReadXml();
        }
    }

    return true;
}

bool AgentUtXmlTest::Load() {
    struct stat s;
    if (stat(file_name_.c_str(), &s)) {
        cout << "Error <" << strerror(errno) << "> opening file "
            << file_name_ << endl;
        return false;
    }

    int fd = open(file_name_.c_str(), O_RDONLY);
    if (fd < 0) {
        cout << "Error <" << strerror(errno) << "> opening file "
            << file_name_ << endl;
        return false;
    }

    char *data = new char[s.st_size + 1];
    if (read(fd, data, s.st_size) < s.st_size) {
        cout << "Error <" << strerror(errno) << "> reading file "
            << file_name_ << endl;
        return false;
    }
    data[s.st_size] = '\0';

    xml_parse_result result = doc_.load(data);
    if (result) {
        cout << "Loaded data file successfully" << endl;
    } else {
        cout << "Error in XML string at offset <: " << result.offset
            << "> (error at [..." << (data + result.offset) << "])" << endl;
        return false;
    }

    return true;
}

void AgentUtXmlTest::ToString(string *str) {
    stringstream s;

    s << "Test Suite : " << name_ << endl;
    *str += s.str();
    for (AgentUtXmlTestList::iterator it = test_list_.begin();
         it != test_list_.end(); it++) {
        (*it)->ToString(str);
    }
    return;
}

bool AgentUtXmlTest::Run() {
    for (AgentUtXmlTestList::iterator it = test_list_.begin();
         it != test_list_.end(); it++) {
        (*it)->Run();
    }

    return true;
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlTestCase routines
/////////////////////////////////////////////////////////////////////////////
static bool CheckConfigNode(const string &node_name, const xml_node &node,
                            uuid *id, string *name) {
    if (strcmp(node.name(), node_name.c_str()) != 0) {
        return false;
    }

    xml_attribute attr = node.attribute("uuid");
    if (!attr) {
        cout << "Attribute \"uuid\" not found for " << node_name
            << ". Skipping..." << endl;
        return false;;
    }

    int x = attr.as_uint();
    if (x == 0) {
        cout << "Invalid \"uuid\" (0) for " << node_name << " Skipping" << endl;
        return false;
    }

    *id = MakeUuid(x);

    attr = node.attribute("name");
    if (!attr) {
        cout << "Attribute \"name\" not found for " << node_name
            << ". Skipping..." << endl;
        return false;;
    }

    *name = attr.as_string();
    if (*name == "") {
        cout << "Invalid \"name\" for " << node_name << " Skipping" << endl;
        return false;
    }

    return true;
}

AgentUtXmlTestCase:: AgentUtXmlTestCase(const std::string &name,
                                        const xml_node &node,
                                        AgentUtXmlTest *test)
    : name_(name), xml_node_(node), test_(test) {
    cout << "Creating test-case <" << name_ << ">" << endl;
}

AgentUtXmlTestCase::~AgentUtXmlTestCase() {
}

bool AgentUtXmlTestCase::ReadXml() {
    for (xml_node node = xml_node_.first_child(); node;
         node = node.next_sibling()) {

        string str;
        bool op_delete = false;
        if (GetStringAttribute(node, "delete", &str) == true ||
            GetStringAttribute(node, "del", &str) == true) {
            if (str != "false" && str != "0")
                op_delete = true;
        }

        uuid id;
        string name;
        AgentUtXmlNode *cfg = NULL;
        if (CheckConfigNode("virtual-network", node, &id, &name) == true ||
            CheckConfigNode("vn", node, &id, &name) == true) {
            cfg = new AgentUtXmlVn(name, id, node, this);
        }

        if (CheckConfigNode("virtual-machine", node, &id, &name) == true ||
            CheckConfigNode("vm", node, &id, &name) == true) {
            cfg = new AgentUtXmlVm(name, id, node, this);
        }

        if (CheckConfigNode("virtual-machine-interface", node, &id, &name) ||
            CheckConfigNode("vm-interface", node, &id, &name)) {
            cfg = new AgentUtXmlVmInterface(name, id, node, this);
        }

        if (CheckConfigNode("ethernet-interface", node, &id, &name) == true ||
            CheckConfigNode("eth-port", node, &id, &name) == true) {
            cfg = new AgentUtXmlEthInterface(name, id, node, this);
        }

        if (CheckConfigNode("routing-instance", node, &id, &name) == true ||
            CheckConfigNode("vrf", node, &id, &name) == true) {
            cfg = new AgentUtXmlVrf(name, id, node, this);
        }

        if (CheckConfigNode("acess-control-list", node, &id, &name) == true ||
            CheckConfigNode("acl", node, &id, &name) == true) {
            cfg = new AgentUtXmlAcl(name, id, node, this);
        }


        if (CheckConfigNode("virtual-machine-interface-routing-instance",
                            node, &id, &name) == true ||
            CheckConfigNode("vm-vrf", node, &id, &name) == true) {
            cfg = new AgentUtXmlVmiVrf(name, id, node, this);
        }

        if (strcmp(node.name(), "link") == 0) {
            cfg = new AgentUtXmlLink(node, this);
        }

        if (strcmp(node.name(), "packet") == 0) {
            if (GetStringAttribute(node, "name", &name) == false) {
                cout << "Attribute \"name\" not specified for Packet. Skipping"
                    << endl;
                continue;
            }
            cfg = new AgentUtXmlPacket(name, node, this);
        }

        if (strcmp(node.name(), "nova") == 0) {
            if (GetStringAttribute(node, "name", &name) == false) {
                cout << "Attribute \"name\" not specified for vm-interface."
                   " Skipping" << endl;
                continue;
            }
            cfg = new AgentUtXmlNova(name, id, node, this);
        }

        if (strcmp(node.name(), "validate") == 0) {
            if (GetStringAttribute(node, "name", &name) == false) {
                cout << "Attribute \"name\" not specified for validate."
                   " Skipping" << endl;
                continue;
            }
            cfg = new AgentUtXmlValidate(name, node, this);
        }

        if (cfg) {
            cfg->set_op_delete(op_delete);
        } else {
            cout << "Unknown node name <" << node.name() << ">. Ignoring" 
                << endl;
        }
        if (cfg) {
            bool ret = cfg->ReadXml();
            if (op_delete == false && ret == false) {
                delete cfg;
                cfg = NULL;
            }
        }

        if (cfg) {
            node_list_.push_back(cfg);
        }
    }

    return true;
}

bool AgentUtXmlTestCase::Run() {
    for (AgentUtXmlNodeList::iterator it = node_list_.begin();
         it != node_list_.end(); it++) {
        if ((*it)->gen_xml() == false) {
            (*it)->Run();
            TestClient::WaitForIdle();
            continue;
        }

        xml_document doc;

        xml_node decl = doc.prepend_child(pugi::node_declaration);
        decl.append_attribute("version") = "1.0";

        xml_node n = doc.append_child("config");
        xml_node n1;
        if ((*it)->op_delete()) {
            n1 = n.append_child("delete");
        } else {
            n1 = n.append_child("update");
        }
        (*it)->ToXml(&n1);
        doc.print(std::cout);
        Agent *agent = Agent::GetInstance();
        agent->ifmap_parser()->ConfigParse(n, 0);
        TestClient::WaitForIdle();
    }

    return true;
}

void AgentUtXmlTestCase::ToString(string *str) {
    stringstream s;

    s << "Test Case : " << name_ << endl;
    *str += s.str();
    for (AgentUtXmlNodeList::iterator it = node_list_.begin();
         it != node_list_.end(); it++) {
        (*it)->ToString(str);
    }
    return;
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlNode routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlNode::AgentUtXmlNode(const string &name, const xml_node &node,
                               AgentUtXmlTestCase *test_case) :
    node_(node), name_(name), op_delete_(false), gen_xml_(true),
    test_case_(test_case) {
}

AgentUtXmlNode::AgentUtXmlNode(const string &name, const xml_node &node,
                               bool gen_xml, AgentUtXmlTestCase *test_case) :
    node_(node), name_(name), op_delete_(false), gen_xml_(gen_xml),
    test_case_(test_case) {
}

AgentUtXmlNode::~AgentUtXmlNode() {
}

bool AgentUtXmlNode::ReadXml() {
    string str;
    op_delete_ = false;
    if (GetStringAttribute(node_, "delete", &str) == true ||
        GetStringAttribute(node_, "del", &str) == true) {
        if (str != "false" && str != "0")
            op_delete_ = true;
    }

    return true;
}

void AgentUtXmlNode::ToString(string *str) {
    stringstream s;

    if (op_delete_)
        s << "Delete ";
    else
        s << "Add ";

    s << NodeType() << " : " << name_ << " ";

    *str += s.str();
    return;
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlLink routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlLink::AgentUtXmlLink(const xml_node &node,
                               AgentUtXmlTestCase *test_case) :
    AgentUtXmlNode("link", node, test_case) {
}

AgentUtXmlLink::~AgentUtXmlLink() {
}

bool AgentUtXmlLink::ReadXml() {
    if (GetStringAttribute(node(), "left", &l_node_) == false) {
        cout << "Left node-type not specified for link. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "left-name", &l_name_) == false) {
        cout << "Right node-name not specified for link. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "right", &r_node_) == false) {
        cout << "Right node-type not specified for link. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "right-name", &r_name_) == false) {
        cout << "Right node-name not specified for link. Skipping" << endl;
        return false;
    }

    return true;
}

bool AgentUtXmlLink::ToXml(xml_node *parent) {
    LinkXmlNode(parent, l_node_, l_name_, r_node_, r_name_);
    return true;
}

void AgentUtXmlLink::ToString(string *str) {
    AgentUtXmlNode::ToString(str);
    stringstream s;

    s << "<" << l_node_ << " : " << l_name_ << "> <" << " right-node "
        << r_node_ << " : " << r_name_ << ">" << endl; 

    *str += s.str();
    return;
}

string AgentUtXmlLink::NodeType() {
    return "Link";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlConfig routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlConfig::AgentUtXmlConfig(const string &name, const uuid &id,
                                   const xml_node &node,
                                   AgentUtXmlTestCase *test_case) :
    AgentUtXmlNode(name, node, test_case), id_(id) {
}

AgentUtXmlConfig::AgentUtXmlConfig(const string &name, const uuid &id,
                                   const xml_node &node, bool gen_xml,
                                   AgentUtXmlTestCase *test_case) :
    AgentUtXmlNode(name, node, gen_xml, test_case), id_(id) {
}

AgentUtXmlConfig::~AgentUtXmlConfig() {
}

bool AgentUtXmlConfig::ReadXml() {
    return AgentUtXmlNode::ReadXml();
}

void AgentUtXmlConfig::ToString(std::string *str) {
    AgentUtXmlNode::ToString(str);
    stringstream s;
    s << " UUID : " << id_;
    *str += s.str();
}

static void AddPermissions(xml_node *parent) {
    xml_node n = parent->append_child("permissions");

    AddXmlNodeWithValue(&n, "owner", "cloud-admin");
    AddXmlNodeWithValue(&n, "owner-access", "7");
    AddXmlNodeWithValue(&n, "group", "cloud-admin-group");
    AddXmlNodeWithValue(&n, "group-access", "7");
    AddXmlNodeWithValue(&n, "other-access", "7");
}

static void AddUuid(xml_node *parent, const uuid &id) {
    xml_node n = parent->append_child("uuid");

    std::vector<uint8_t> v1(id.size());
    std::vector<uint64_t> v(id.size());
    std::copy(id.begin(), id.end(), v.begin());

    uint64_t ms_val = v[7] + (v[6] << 8) + (v[5] << 16) + (v[4] << 24) + 
                   (v[3] << 32) + (v[2] << 40) + (v[1] << 48) + (v[0] << 56);
    uint64_t ls_val = v[15] + (v[14] << 8) + (v[13] << 16) + (v[12] << 24) + 
                   (v[11] << 32) + (v[10] << 40) + (v[9] << 48) + (v[8] << 56);
    stringstream s;
    s << ms_val;
    AddXmlNodeWithValue(&n, "uuid-mslong", s.str());

    stringstream s1;
    s1 << ls_val;
    AddXmlNodeWithValue(&n, "uuid-lslong", s1.str());
}

bool AgentUtXmlConfig::AddIdPerms(xml_node *parent) {

    if (op_delete())
        return true;
    xml_node n = parent->append_child("id-perms");
    AddPermissions(&n);
    AddUuid(&n, id());
    AddXmlNodeWithValue(&n, "enable", "true");
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVn routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVn::AgentUtXmlVn(const string &name, const uuid &id,
                           const xml_node &node,
                           AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlVn::~AgentUtXmlVn() {
}


bool AgentUtXmlVn::ReadXml() {
    return AgentUtXmlConfig::ReadXml();
}

bool AgentUtXmlVn::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());
    AddIdPerms(&n);
    return true;
}

void AgentUtXmlVn::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlVn::NodeType() {
    return "virtual-network";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVm routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVm::AgentUtXmlVm(const string &name, const uuid &id,
                           const xml_node &node,
                           AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlVm::~AgentUtXmlVm() {
}


bool AgentUtXmlVm::ReadXml() {
    return AgentUtXmlConfig::ReadXml();
}

bool AgentUtXmlVm::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());
    AddIdPerms(&n);
    return true;
}

void AgentUtXmlVm::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlVm::NodeType() {
    return "virtual-machine";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVmInterface routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVmInterface::AgentUtXmlVmInterface(const string &name,
                                             const uuid &id,
                                             const xml_node &node,
                                             AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlVmInterface::~AgentUtXmlVmInterface() {
}

bool AgentUtXmlVmInterface::ReadXml() {
    if (AgentUtXmlConfig::ReadXml() == false)
        return false;

    GetStringAttribute(node(), "mac", &mac_);

    vm_name_ = "";
    vn_uuid_ = nil_uuid();
    vm_uuid_ = nil_uuid();
    vrf_ = "";
    add_nova_ = false;
    ip_ = "0.0.0.0";

    GetStringAttribute(node(), "vn-name", &vn_name_);
    uint16_t id = 0;
    GetUintAttribute(node(), "vn-uuid", &id);
    if (id) {
        vn_uuid_ = MakeUuid(id);
    }

    GetStringAttribute(node(), "vm-name", &vm_name_);
    id = 0;
    GetUintAttribute(node(), "vm-uuid", &id);
    if (id) {
        vm_uuid_ = MakeUuid(id);
    }

    GetStringAttribute(node(), "vrf", &vrf_);
    GetStringAttribute(node(), "ip", &ip_);

    string str;
    if (GetStringAttribute(node(), "nova", &str) ||
        GetUintAttribute(node(), "nova", &id)) {
        add_nova_ = true;
    }

    return true;
}

bool AgentUtXmlVmInterface::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());

    if (op_delete() == false) {
        xml_node n1 = n.append_child("virtual-machine-interface-mac-address");
        AddXmlNodeWithValue(&n1, "mac-address", mac_);
        AddIdPerms(&n);
    }

    if (add_nova_) {
        boost::system::error_code ec;
        IpAddress ip = Ip4Address::from_string(ip_, ec);
        NovaIntfAdd(op_delete(), id(), ip, vm_uuid_, vm_uuid_, name(), mac_,
                    vm_name_);
    }

    if (vm_name_ != "") {
        LinkXmlNode(parent, NodeType(), name(), "virtual-network", vn_name_);
    }

    if (vm_name_ != "") {
        LinkXmlNode(parent, NodeType(), name(), "virtual-machine", vm_name_);
    }

    if (vrf_ != "") {
        string str = name() + "-" + vm_name_;
        LinkXmlNode(parent, "virtual-machine-interface-routing-instance",
                    str, "routing-instance", vrf_);

        LinkXmlNode(parent, NodeType(), name(),
                    "virtual-machine-interface-routing-instance", str);
    }

    return true;
}

void AgentUtXmlVmInterface::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlVmInterface::NodeType() {
    return "virtual-machine-interface";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlEthInterface routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlEthInterface::AgentUtXmlEthInterface(const string &name,
                                               const uuid &id,
                                               const xml_node &node,
                                               AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, false, test_case) {
}

AgentUtXmlEthInterface::~AgentUtXmlEthInterface() {
}

bool AgentUtXmlEthInterface::ReadXml() {
    return AgentUtXmlConfig::ReadXml();
}

bool AgentUtXmlEthInterface::ToXml(xml_node *parent) {
    assert(0);
    return true;
}

void AgentUtXmlEthInterface::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlEthInterface::NodeType() {
    return "physical-interface";
}

bool AgentUtXmlEthInterface::Run() {
    cout << "Create Ethernet Interface" << endl;
    return true;
}
/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVrf routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVrf::AgentUtXmlVrf(const string &name, const uuid &id,
                             const xml_node &node,
                             AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlVrf::~AgentUtXmlVrf() {
}


bool AgentUtXmlVrf::ReadXml() {
    return AgentUtXmlConfig::ReadXml();
}

bool AgentUtXmlVrf::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());
    AddIdPerms(&n);
    return true;
}

void AgentUtXmlVrf::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlVrf::NodeType() {
    return "routing-instance";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVmiVrf routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVmiVrf::AgentUtXmlVmiVrf(const string &name, const uuid &id,
                                   const xml_node &node,
                                   AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlVmiVrf::~AgentUtXmlVmiVrf() {
}


bool AgentUtXmlVmiVrf::ReadXml() {
    return AgentUtXmlConfig::ReadXml();
}

bool AgentUtXmlVmiVrf::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());

    xml_node n1 = n.append_child("value");
    AddXmlNodeWithValue(&n1, "direction", "both");
    AddXmlNodeWithValue(&n1, "vlan-tag", "0");

    AddIdPerms(&n);
    return true;
}

void AgentUtXmlVmiVrf::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlVmiVrf::NodeType() {
    return "virtual-machine-interface-routing-instance";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlAcl routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlAcl::AgentUtXmlAcl(const string &name, const uuid &id,
                             const xml_node &node,
                             AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlAcl::~AgentUtXmlAcl() {
}

bool AgentUtXmlAcl::ReadXml() {
    if (AgentUtXmlConfig::ReadXml() == false)
        return false;

    for (xml_node n = node().first_child(); n; n = n.next_sibling()) {
        if (strcmp(n.name(), "ace") != 0) {
            continue;
        }

        Ace ace;

        GetUintAttribute(n, "src-sg", (uint16_t *)&ace.src_sg_);
        GetUintAttribute(n, "dst-sg", (uint16_t *)&ace.dst_sg_);

        GetStringAttribute(n, "src-vn", &ace.src_vn_);
        GetStringAttribute(n, "dst-vn", &ace.dst_vn_);

        GetStringAttribute(n, "src-ip", &ace.src_ip_);
        GetUintAttribute(n, "src-plen", (uint16_t *)&ace.src_ip_plen_);
        GetStringAttribute(n, "dst-ip", &ace.dst_ip_);
        GetUintAttribute(n, "dst-plen", (uint16_t *)&ace.dst_ip_plen_);

        std::string str;
        GetStringAttribute(n, "sport", &str);
        sscanf(str.c_str(), "%d:%d", &ace.sport_begin_, &ace.sport_end_);

        GetStringAttribute(n, "dport", &str);
        sscanf(str.c_str(), "%d:%d", &ace.dport_begin_, &ace.dport_end_);

        GetStringAttribute(n, "dport", &ace.action_);

        ace_list_.push_back(ace);
    }

    return true;
}

bool AgentUtXmlAcl::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());

    xml_node n1 = n.append_child("access-control-list-entries");
    AddXmlNodeWithValue(&n1, "dynamic", "false");

    for (AceList::iterator it = ace_list_.begin(); it != ace_list_.end();
         it++) {
        n1 = n1.append_child("acl-rule");

        xml_node n2 = n1.append_child("match-condition");
        AddXmlNodeWithValue(&n2, "protocol", "any");

        xml_node n3 = n2.append_child("src-address");

        Ace ace = (*it);
        if (ace.src_sg_) {
            AddXmlNodeWithIntValue(&n3, "security-group", ace.src_sg_);
        }

        if (ace.src_vn_ != "") {
            AddXmlNodeWithValue(&n3, "virtual-network", ace.src_vn_);
        }

        if (ace.src_ip_ != "") {
            xml_node n4 = n3.append_child("subnet");
            AddXmlNodeWithValue(&n4, "ip-prefix", ace.src_ip_);
            AddXmlNodeWithIntValue(&n4, "ip-prefix-len", ace.src_ip_plen_);
        }

        n3 = n2.append_child("dst-address");
        if (ace.dst_sg_) {
            AddXmlNodeWithIntValue(&n3, "security-group", ace.dst_sg_);
        }

        if (ace.dst_vn_ != "") {
            AddXmlNodeWithValue(&n3, "virtual-network", ace.dst_vn_);
        }

        if (ace.dst_ip_ != "") {
            xml_node n4 = n3.append_child("subnet");
            AddXmlNodeWithValue(&n4, "ip-prefix", ace.dst_ip_);
            AddXmlNodeWithIntValue(&n4, "ip-prefix-len", ace.dst_ip_plen_);
        }

        n2 = n1.append_child("action_list");
        AddXmlNodeWithValue(&n2, "simple-action", ace.action_);
    }

    AddIdPerms(&n);
    return true;
}

void AgentUtXmlAcl::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlAcl::NodeType() {
    return "access-control-list";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlPacket routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlPacket::AgentUtXmlPacket(const string &name, const xml_node &node,
                                   AgentUtXmlTestCase *test_case) :
    AgentUtXmlNode(name, node, false, test_case), intf_id_(0xFFFF) {
}

AgentUtXmlPacket::~AgentUtXmlPacket() {
}

bool AgentUtXmlPacket::ReadXml() {
    AgentUtXmlNode::ReadXml();
    GetStringAttribute(node(), "tunnel_sip", &tunnel_sip_);
    GetStringAttribute(node(), "tunnel_dip", &tunnel_dip_);
    GetUintAttribute(node(), "label", (uint16_t *)&label_);

    uint16_t id = 0;
    if (GetUintAttribute(node(), "id", &intf_id_) == false) {
        cout << "Attribute \"id\" not specified for Packet. Skipping"
            << endl;
        return false;
    }

    if (GetStringAttribute(node(), "interface", &intf_) == false) {
        cout << "Attribute \"interface\" not specified for Packet. Skipping"
            << endl;
        return false;
    }

    if (GetStringAttribute(node(), "sip", &sip_) == false) {
        cout << "Attribute \"sip\" not specified for Packet. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "dip", &dip_) == false) {
        cout << "Attribute \"dip\" not specified for Packet. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "proto", &proto_) == false &&
        GetUintAttribute(node(), "proto", &proto_id_) == false) {
        cout << "Attribute \"proto\" not specified for Packet. Skipping" 
            << endl;
        return false;
    }

    if (proto_ == "tcp" || proto_ == "udp") {
        if (proto_ == "tcp")
            proto_id_ = 6;
        else
            proto_id_ = 17;
        if (GetUintAttribute(node(), "sport", &sport_) == false) {
            cout << "Attribute \"sport\" not specified for Packet. Skipping"
                << endl;
            return false;
        }

        if (GetUintAttribute(node(), "dport", &dport_) == false) {
            cout << "Attribute \"dport\" not specified for Packet. Skipping"
                << endl;
            return false;
        }
    }

    return true;
}

bool AgentUtXmlPacket::ToXml(xml_node *parent) {
    assert(0);
    return true;
}

void AgentUtXmlPacket::ToString(string *str) {
    AgentUtXmlNode::ToString(str);

    stringstream s;
    if (label_ == 0) {
        s << "Interface <" << intf_ << "> ";
    } else {
        s << "Tunnel <" << intf_ << " : " << label_ << " : " << tunnel_sip_
            << " : " << tunnel_dip_ << "> ";
    }
    s << "<" << sip_ << " : " << dip_ << " : " << proto_ << " : " << sport_
        << " : " << dport_ << ">" << endl;
    *str += s.str();
    return;
}

string AgentUtXmlPacket::NodeType() {
    return "packet";
}

bool AgentUtXmlPacket::Run() {
    cout << "Generate packet" << endl;
    boost::system::error_code ec;
    IpAddress ip = IpAddress::from_string(sip_, ec);
    if (ip.is_v4()) {
        if (proto_ == "udp") {
            TxUdpPacket(intf_id_, sip_.c_str(), dip_.c_str(), sport_, dport_);
        } else if (proto_ == "udp") {
            TxTcpPacket(intf_id_, sip_.c_str(), dip_.c_str(), sport_, dport_,
                        false);
        } else {
            TxIpPacket(intf_id_, sip_.c_str(), dip_.c_str(), proto_id_);
        }
    } else {
        if (proto_ == "udp") {
            TxUdp6Packet(intf_id_, sip_.c_str(), dip_.c_str(), sport_, dport_);
        } else if (proto_ == "udp") {
            TxTcp6Packet(intf_id_, sip_.c_str(), dip_.c_str(), sport_, dport_,
                         false);
        } else {
            TxIp6Packet(intf_id_, sip_.c_str(), dip_.c_str(), proto_id_);
        }
    }
    
    return true;
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlNova routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlNova::AgentUtXmlNova(const string &name, const uuid &id,
                             const xml_node &node,
                             AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, false, test_case) {
}

AgentUtXmlNova::~AgentUtXmlNova() {
}

bool AgentUtXmlNova::ReadXml() {
    if (AgentUtXmlNode::ReadXml() == false)
        return false;

    if (GetStringAttribute(node(), "mac", &mac_) == false) {
        cout << "Attribute \"mac\" not specified for nova. Skipping"
            << endl;
        return false;
    }

    uint16_t num;
    if (GetUintAttribute(node(), "vm-uuid", &num) == false) {
        cout << "Attribute \"vm-uuid\" not specified for nova. Skipping"
            << endl;
        return false;
    }
    vm_uuid_ = MakeUuid(num);

    if (GetUintAttribute(node(), "vn-uuid", &num) == false) {
        cout << "Attribute \"vn-uuid\" not specified for nova. Skipping"
            << endl;
        return false;
    }
    vn_uuid_ = MakeUuid(num);

    if (GetStringAttribute(node(), "vm-name", &vm_name_) == false) {
        cout << "Attribute \"vm_name\" not specified for nova. Skipping"
            << endl;
        return false;
    }

    if (GetStringAttribute(node(), "ip", &ip_) == false) {
        cout << "Attribute \"ip\" not specified for nova. Skipping"
            << endl;
        return false;
    }

    return true;
}

bool AgentUtXmlNova::ToXml(xml_node *parent) {
    assert(0);
}

void AgentUtXmlNova::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlNova::NodeType() {
    return "Nova";
}

bool AgentUtXmlNova::Run() {
    boost::system::error_code ec;
    IpAddress ip = Ip4Address::from_string(ip_, ec);
    NovaIntfAdd(op_delete(), id(), ip, vm_uuid_, vn_uuid_, name(), mac_,
                vm_name_);
    return true;
}
