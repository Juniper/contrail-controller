/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "base/os.h"
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
using namespace AgentUtXmlUtils;

namespace AgentUtXmlUtils {
bool GetStringAttribute(const xml_node &node, const string &name,
                        string *value) {
    xml_attribute attr = node.attribute(name.c_str());
    if (!attr) {
        return false;;
    }

    *value = attr.as_string();

    return true;
}

bool GetUintAttribute(const xml_node &node, const string &name,
                      uint16_t *value) {
    xml_attribute attr = node.attribute(name.c_str());
    if (!attr) {
        return false;;
    }

    *value = attr.as_uint();

    return true;
}

void NovaIntfAdd(bool op_delete, const uuid &id, const IpAddress &ip,
                 const uuid &vm_uuid, const uuid vn_uuid, const string &name,
                 const string &mac, const string vm_name) {
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
               Ip6Address::v4_compatible(ip.to_v4()), mac, vm_name,
               VmInterface::kInvalidVlanId, VmInterface::kInvalidVlanId,
               CfgIntEntry::CfgIntVMPort, 0);

    Agent::GetInstance()->interface_config_table()->Enqueue(&req);
    cout << "Nova Add Interface Message " << endl;
    return;
}

void LinkXmlNode(xml_node *parent, const string &ltype, const string lname,
                 const string &rtype, const string rname) {
    xml_node n = parent->append_child("link");

    xml_node n1 = n.append_child("node");
    n1.append_attribute("type") = ltype.c_str();
    
    xml_node n2 = n1.append_child("name");
    n2.append_child(pugi::node_pcdata).set_value(lname.c_str());

    n1 = n.append_child("node");
    n1.append_attribute("type") = rtype.c_str();
    
    n2 = n1.append_child("name");
    n2.append_child(pugi::node_pcdata).set_value(rname.c_str());

    string mdata = ltype + "-" + rtype;
    xml_node n3 = n.append_child("metadata");
    n3.append_attribute("type") = mdata.c_str();
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
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlTest routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlTest::AgentUtXmlTest(const std::string &name) : file_name_(name) {
}

AgentUtXmlTest::~AgentUtXmlTest() {

    for (AgentUtXmlTestList::iterator i = test_list_.begin();
         i != test_list_.end(); i++) {
        delete *i;
    }
}

void AgentUtXmlTest::AddConfigEntry(const std::string &name,
                                    AgentUtXmlTestConfigCreateFn fn) {
    config_factory_[name] = fn;
}

void AgentUtXmlTest::AddValidateEntry(const std::string &name,
                                      AgentUtXmlTestValidateCreateFn fn) {
    validate_factory_[name] = fn;
}

AgentUtXmlTest::AgentUtXmlTestConfigCreateFn
AgentUtXmlTest::GetConfigCreateFn(const std::string &name) {
    AgentUtXmlTestConfigFactory::iterator iter = config_factory_.find(name);
    if (iter == config_factory_.end()) {
        return AgentUtXmlTest::AgentUtXmlTestConfigCreateFn();
    }

    return iter->second;
}

AgentUtXmlTest::AgentUtXmlTestValidateCreateFn
AgentUtXmlTest::GetValidateCreateFn(const std::string &name) {
    AgentUtXmlTestValidateFactory::iterator iter = validate_factory_.find(name);
    if (iter == validate_factory_.end()) {
        assert(0);
    }
    return iter->second;
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

    char data[s.st_size + 1];
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

    xml_attribute attr = node.attribute("name");
    if (!attr) {
        cout << "Attribute \"name\" not found for " << node_name
            << ". Skipping..." << endl;
        return false;
    }

    *name = attr.as_string();
    if (*name == "") {
        cout << "Invalid \"name\" for " << node_name << " Skipping" << endl;
        return false;
    }

    if (node_name == "validate")
        return false;

    attr = node.attribute("uuid");
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

    return true;
}

AgentUtXmlTestCase:: AgentUtXmlTestCase(const std::string &name,
                                        const xml_node &node,
                                        AgentUtXmlTest *test)
    : name_(name), xml_node_(node), test_(test) {
    cout << "Creating test-case <" << name_ << ">" << endl;
}

AgentUtXmlTestCase::~AgentUtXmlTestCase() {
    for (AgentUtXmlNodeList::iterator i =  node_list_.begin();
         i != node_list_.end(); i++) {
        delete *i;
    }
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

        AgentUtXmlTest::AgentUtXmlTestConfigCreateFn fn =
            test_->GetConfigCreateFn(node.name());
        if (CheckConfigNode(node.name(), node, &id, &name) == true) {
            if (fn.empty() == false)
                cfg = fn(node.name(), name, id, node, this);
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

