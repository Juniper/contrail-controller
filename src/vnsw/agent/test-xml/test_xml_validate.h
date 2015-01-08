/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_xml_test_xml_validate_h
#define vnsw_agent_test_xml_test_xml_validate_h

#include <iostream>
#include <vector>
#include <pugixml/pugixml.hpp>
#include <boost/uuid/uuid.hpp>

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
    virtual uint32_t wait_count() const { return 500; }
    virtual uint32_t sleep_time() const { return 500; }

private:
    std::string name_;
    pugi::xml_node node_;
    bool present_;
    bool delete_marked_;
    uint16_t id_;
};

#endif //vnsw_agent_test_xml_test_xml_validate_h
