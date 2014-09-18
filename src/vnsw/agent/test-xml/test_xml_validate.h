/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_xml_test_xml_validate_h
#define vnsw_agent_test_xml_test_xml_validate_h

#include <iostream>
#include <vector>
#include <pugixml/pugixml.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>

#include <base/util.h>

class AgentUtXmlTestCase;
class AgentUtXmlNode;

/////////////////////////////////////////////////////////////////////////////
//   Validate nodes
/////////////////////////////////////////////////////////////////////////////
class AgentUtXmlValidationNode;

class AgentUtXmlValidate : public AgentUtXmlNode {
private:
    typedef std::vector<AgentUtXmlValidationNode *> AgentUtXmlValidationList;
public:
    AgentUtXmlValidate(const std::string &name, const pugi::xml_node &node,
                       AgentUtXmlTestCase *test_case);
    ~AgentUtXmlValidate();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);
    virtual bool Run();

private:
    AgentUtXmlValidationList node_list_;
};

class AgentUtXmlValidationNode {
public:
    AgentUtXmlValidationNode(const std::string &name,
                             const pugi::xml_node &node);
    virtual ~AgentUtXmlValidationNode();

    virtual bool ReadXml() = 0;
    virtual bool Validate() = 0;
    virtual const std::string ToString() = 0;

    bool ReadCmnXml();
    const std::string &name() const { return name_; }
    uint16_t id() const { return id_; }
    bool delete_marked() const { return delete_marked_; }
    bool present() const { return present_; }
    const pugi::xml_node &node() const { return node_; }
private:
    std::string name_;
    pugi::xml_node node_;
    bool present_;
    bool delete_marked_;
    uint16_t id_;
};

class AgentUtXmlVnValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlVnValidate(const std::string &name,
                         const boost::uuids::uuid &id,
                         const pugi::xml_node &node);
    virtual ~AgentUtXmlVnValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
};

class AgentUtXmlVmValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlVmValidate(const std::string &name,
                         const boost::uuids::uuid &id,
                         const pugi::xml_node &node);
    virtual ~AgentUtXmlVmValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
};

class AgentUtXmlVmInterfaceValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlVmInterfaceValidate(const std::string &name,
                                  const boost::uuids::uuid &id,
                                  const pugi::xml_node &node);
    virtual ~AgentUtXmlVmInterfaceValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
};

class AgentUtXmlEthInterfaceValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlEthInterfaceValidate(const std::string &name,
                                   const boost::uuids::uuid &id,
                                   const pugi::xml_node &node);
    virtual ~AgentUtXmlEthInterfaceValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
};

class AgentUtXmlVrfValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlVrfValidate(const std::string &name,
                          const pugi::xml_node &node);
    virtual ~AgentUtXmlVrfValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
};

class AgentUtXmlAclValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlAclValidate(const std::string &name,
                          const pugi::xml_node &node);
    virtual ~AgentUtXmlAclValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
};

class AgentUtXmlFlowValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlFlowValidate(const std::string &name,
                           const pugi::xml_node &node);
    virtual ~AgentUtXmlFlowValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    uint16_t nh_id_;
    std::string sip_;
    std::string dip_;
    std::string proto_;
    uint16_t sport_;
    uint16_t dport_;
    uint16_t proto_id_;
};

#endif //vnsw_agent_test_xml_test_xml_validate_h
