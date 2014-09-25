/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_xml_test_xml_h
#define vnsw_agent_test_xml_test_xml_h

#include <iostream>
#include <vector>
#include <pugixml/pugixml.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>

#include <base/util.h>

class AgentUtXmlTestCase;
class AgentUtXmlNode;

class AgentUtXmlTest {
private:
    typedef std::vector<AgentUtXmlTestCase *> AgentUtXmlTestList;
public:
    AgentUtXmlTest(const std::string &file_name);
    virtual ~AgentUtXmlTest();

    bool Load();
    bool ReadXml();
    void ToString(std::string *str);
    bool Run();
private:
    std::string file_name_;
    std::string name_;
    pugi::xml_document doc_;
    AgentUtXmlTestList test_list_;
    DISALLOW_COPY_AND_ASSIGN(AgentUtXmlTest);
};

class AgentUtXmlTestCase {
private:
    typedef std::vector<AgentUtXmlNode *> AgentUtXmlNodeList;
public:
    AgentUtXmlTestCase(const std::string &name, const pugi::xml_node &node,
                       AgentUtXmlTest *test);
    ~AgentUtXmlTestCase();

    virtual bool ReadXml();
    virtual bool Run();
    virtual void ToString(std::string *str);
private:
    std::string name_;
    pugi::xml_node xml_node_;
    AgentUtXmlNodeList node_list_;
    pugi::xml_document gen_doc_;
    AgentUtXmlTest *test_;
    DISALLOW_COPY_AND_ASSIGN(AgentUtXmlTestCase);
};

class AgentUtXmlNode {
public:
    AgentUtXmlNode(const std::string &name, const pugi::xml_node &node,
                   AgentUtXmlTestCase *test_case);
    AgentUtXmlNode(const std::string &name, const pugi::xml_node &node,
                   bool gen_xml, AgentUtXmlTestCase *test_case);
    virtual ~AgentUtXmlNode();

    virtual bool ReadXml() = 0;
    virtual bool ToXml(pugi::xml_node *parent) = 0;
    virtual std::string NodeType() = 0;
    virtual void ToString(std::string *str);
    virtual bool Run() { assert(0); }

    void set_gen_xml(bool val) { gen_xml_ = val; }
    bool gen_xml() const { return gen_xml_; }
    bool op_delete() const { return op_delete_; }
    void set_op_delete(bool val) { op_delete_ = val; }
    const std::string &name() const { return name_; }
    const pugi::xml_node &node() const { return node_; }
protected:
    pugi::xml_node gen_node_;
private:
    pugi::xml_node node_;
    std::string name_;
    bool op_delete_;
    bool gen_xml_;
    AgentUtXmlTestCase *test_case_;
    DISALLOW_COPY_AND_ASSIGN(AgentUtXmlNode);
};

/////////////////////////////////////////////////////////////////////////////
//   Link nodes
/////////////////////////////////////////////////////////////////////////////
class AgentUtXmlLink : public AgentUtXmlNode {
public:
    AgentUtXmlLink(const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlLink();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
    pugi::xml_node xml_;
    std::string l_node_;
    std::string l_name_;
    std::string r_node_;
    std::string r_name_;
    DISALLOW_COPY_AND_ASSIGN(AgentUtXmlLink);
};

/////////////////////////////////////////////////////////////////////////////
//   Config nodes
/////////////////////////////////////////////////////////////////////////////
class AgentUtXmlConfig : public AgentUtXmlNode {
public:
    AgentUtXmlConfig(const std::string &name, const boost::uuids::uuid &uuid,
                     const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    AgentUtXmlConfig(const std::string &name, const boost::uuids::uuid &uuid,
                     const pugi::xml_node &node, bool gen_xml,
                     AgentUtXmlTestCase *test_case);
    virtual ~AgentUtXmlConfig();

    virtual bool ReadXml();
    virtual void ToString(std::string *str);
    const boost::uuids::uuid &id() const { return id_; }
protected:
    bool AddIdPerms(pugi::xml_node *parent);
private:
    boost::uuids::uuid id_;
    DISALLOW_COPY_AND_ASSIGN(AgentUtXmlConfig);
};

class AgentUtXmlVn : public AgentUtXmlConfig {
public:
    AgentUtXmlVn(const std::string &name, const boost::uuids::uuid &uuid,
                 const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlVn();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
};

class AgentUtXmlVm : public AgentUtXmlConfig {
public:
    AgentUtXmlVm(const std::string &name, const boost::uuids::uuid &uuid,
                 const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlVm();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
};

class AgentUtXmlVmInterface : public AgentUtXmlConfig {
public:
    AgentUtXmlVmInterface(const std::string &name,
                          const boost::uuids::uuid &uuid,
                          const pugi::xml_node &node,
                          AgentUtXmlTestCase *test_case);
    ~AgentUtXmlVmInterface();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
    std::string mac_;
    boost::uuids::uuid vn_uuid_;
    std::string vn_name_;
    boost::uuids::uuid vm_uuid_;
    std::string vm_name_;
    std::string vrf_;
    std::string ip_;
    bool add_nova_;
};

class AgentUtXmlEthInterface : public AgentUtXmlConfig {
public:
    AgentUtXmlEthInterface(const std::string &name,
                           const boost::uuids::uuid &uuid,
                           const pugi::xml_node &node,
                           AgentUtXmlTestCase *test_case);
    ~AgentUtXmlEthInterface();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);
    virtual bool Run();

private:
};

class AgentUtXmlVrf : public AgentUtXmlConfig {
public:
    AgentUtXmlVrf(const std::string &name, const boost::uuids::uuid &uuid,
                 const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlVrf();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
};

class AgentUtXmlVmiVrf : public AgentUtXmlConfig {
public:
    AgentUtXmlVmiVrf(const std::string &name, const boost::uuids::uuid &uuid,
                     const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlVmiVrf();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
};

class AgentUtXmlAcl : public AgentUtXmlConfig {
public:
    struct Ace {
        Ace() : 
            src_sg_(0), dst_sg_(0), src_vn_(""), dst_vn_(""), src_ip_(""),
            src_ip_plen_(0), dst_ip_(""), dst_ip_plen_(0), proto_(0),
            sport_begin_(-1), sport_end_(-1), dport_begin_(-1), dport_end_(-1) {
        }

        uint16_t src_sg_;
        uint16_t dst_sg_;
        std::string src_vn_;
        std::string dst_vn_;
        std::string src_ip_;
        uint16_t src_ip_plen_;
        std::string dst_ip_;
        uint16_t dst_ip_plen_;
        uint8_t proto_;
        int sport_begin_;
        int sport_end_;
        int dport_begin_;
        int dport_end_;
        std::string action_;
    };
    typedef std::vector<Ace> AceList;

    AgentUtXmlAcl(const std::string &name, const boost::uuids::uuid &uuid,
                  const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlAcl();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
    AceList ace_list_;
};

/////////////////////////////////////////////////////////////////////////////
//   Packet nodes
/////////////////////////////////////////////////////////////////////////////
class AgentUtXmlPacket : public AgentUtXmlNode {
public:
    AgentUtXmlPacket(const std::string &name, const pugi::xml_node &node,
                     AgentUtXmlTestCase *test_case);
    ~AgentUtXmlPacket();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);
    virtual bool Run();

private:
    uint16_t intf_id_;
    std::string intf_;
    std::string tunnel_sip_;
    std::string tunnel_dip_;
    uint32_t label_;
    std::string sip_;
    std::string dip_;
    std::string proto_;
    uint16_t sport_;
    uint16_t dport_;
    uint16_t proto_id_;
};

/////////////////////////////////////////////////////////////////////////////
//   Nova messages
/////////////////////////////////////////////////////////////////////////////
class AgentUtXmlNova : public AgentUtXmlConfig {
public:
    AgentUtXmlNova(const std::string &name, const boost::uuids::uuid &uuid,
                   const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlNova();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);
    virtual bool Run();

private:
    std::string mac_;
    std::string vm_name_;
    boost::uuids::uuid vn_uuid_;
    boost::uuids::uuid vm_uuid_;
    std::string ip_;
};

#endif //vnsw_agent_test_xml_test_xml_h
