/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_xml_test_xml_h
#define vnsw_agent_test_xml_test_xml_h

#include <iostream>
#include <vector>
#include <pugixml/pugixml.hpp>
#include <boost/uuid/uuid.hpp>

#include <base/util.h>

class AgentUtXmlTestCase;
class AgentUtXmlNode;
class AgentUtXmlValidationNode;

namespace AgentUtXmlUtils {
bool GetStringAttribute(const pugi::xml_node &node, const std::string &name,
                        std::string *value);
bool GetUintAttribute(const pugi::xml_node &node, const std::string &name,
                      uint16_t *value);
bool GetIntAttribute(const pugi::xml_node &node, const std::string &name,
                     int *value);
bool GetBoolAttribute(const pugi::xml_node &node, const std::string &name,
                      bool *value);
void NovaIntfAdd(bool op_delete, const boost::uuids::uuid &id,
                 const Ip4Address &ip, const boost::uuids::uuid &vm_uuid,
                 const boost::uuids::uuid vn_uuid, const std::string &name,
                 const std::string &mac, const std::string vm_name);
void LinkXmlNode(pugi::xml_node *parent, const std::string &ltype,
                 const std::string lname, const std::string &rtype,
                 const std::string rname);
pugi::xml_node AddXmlNodeWithAttr(pugi::xml_node *parent, const char *attr);
pugi::xml_node AddXmlNodeWithValue(pugi::xml_node *parent, const char *name,
                             const std::string &value);
pugi::xml_node AddXmlNodeWithIntValue(pugi::xml_node *parent, const char *name,
                                      int val);
}

class AgentUtXmlTest {
private:
    typedef std::vector<AgentUtXmlTestCase *> AgentUtXmlTestList;
public:
    typedef boost::function<AgentUtXmlNode *(const std::string type,
                                             const std::string &name,
                                             const boost::uuids::uuid &id,
                                             const pugi::xml_node &node,
                                             AgentUtXmlTestCase *test_case)>
        AgentUtXmlTestConfigCreateFn;

    typedef boost::function<AgentUtXmlValidationNode *
        (const std::string &type, const std::string &name,
         const boost::uuids::uuid &id, const pugi::xml_node &node)>
        AgentUtXmlTestValidateCreateFn;

    typedef std::map<std::string, AgentUtXmlTestConfigCreateFn>
        AgentUtXmlTestConfigFactory;
    typedef std::map<std::string, AgentUtXmlTestValidateCreateFn>
        AgentUtXmlTestValidateFactory;

    AgentUtXmlTest(const std::string &file_name);
    virtual ~AgentUtXmlTest();

    bool Load();
    bool ReadXml();
    void ToString(std::string *str);
    bool Run();
    bool Run(std::string test_case);
    void AddConfigEntry(const std::string &name,
                        AgentUtXmlTestConfigCreateFn fn);
    void AddValidateEntry(const std::string &name,
                          AgentUtXmlTestValidateCreateFn fn);
    AgentUtXmlTestConfigCreateFn GetConfigCreateFn(const std::string &name);
    AgentUtXmlTestValidateCreateFn GetValidateCreateFn(const std::string &name);
private:
    std::string file_name_;
    std::string name_;
    pugi::xml_document doc_;
    AgentUtXmlTestList test_list_;
    AgentUtXmlTestConfigFactory config_factory_;
    AgentUtXmlTestValidateFactory validate_factory_;
    DISALLOW_COPY_AND_ASSIGN(AgentUtXmlTest);
};

class AgentUtXmlTestCase {
private:
    typedef std::vector<AgentUtXmlNode *> AgentUtXmlNodeList;
public:
    AgentUtXmlTestCase(const std::string &name, const pugi::xml_node &node,
                       AgentUtXmlTest *test);
    virtual ~AgentUtXmlTestCase();
    AgentUtXmlTest *test() { return test_; }

    virtual bool ReadXml();
    virtual bool Run();
    virtual void ToString(std::string *str);
    void set_verbose(bool val) { verbose_ = val; }
    bool verbose() const { return verbose_; }
    const std::string &name() const { return name_; }
private:
    std::string name_;
    pugi::xml_node xml_node_;
    AgentUtXmlNodeList node_list_;
    pugi::xml_document gen_doc_;
    AgentUtXmlTest *test_;
    bool verbose_;
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
    AgentUtXmlTestCase *test_case() { return test_case_; }
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
//   Task control nodes
/////////////////////////////////////////////////////////////////////////////
class AgentUtXmlTask : public AgentUtXmlNode {
public:
    AgentUtXmlTask(const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    virtual ~AgentUtXmlTask();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);
    virtual bool Run();

private:
    pugi::xml_node xml_;
    std::string stop_;
    DISALLOW_COPY_AND_ASSIGN(AgentUtXmlTask);
};

/////////////////////////////////////////////////////////////////////////////
//   Link nodes
/////////////////////////////////////////////////////////////////////////////
class AgentUtXmlLink : public AgentUtXmlNode {
public:
    AgentUtXmlLink(const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    virtual ~AgentUtXmlLink();

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

class AgentUtXmlFlowExport : public AgentUtXmlNode {
public:
    AgentUtXmlFlowExport(const pugi::xml_node &node,
                         AgentUtXmlTestCase *test_case);
    ~AgentUtXmlFlowExport();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);
    virtual bool Run();

private:
    void EnqueueFlowExport(FlowEntry *fe, uint64_t bytes, uint64_t pkts);
    uint16_t nh_id_;
    std::string sip_;
    std::string dip_;
    std::string proto_;
    uint16_t proto_id_;
    uint16_t sport_;
    uint16_t dport_;
    uint32_t bytes_;
    uint32_t pkts_;
};

class AgentUtXmlFlowThreshold : public AgentUtXmlNode {
public:
    AgentUtXmlFlowThreshold(const pugi::xml_node &node,
                            AgentUtXmlTestCase *test_case);
    ~AgentUtXmlFlowThreshold();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);
    virtual bool Run();

private:
    uint32_t flow_export_count_;
    uint32_t configured_flow_export_rate_;
    uint32_t threshold_;
};

#endif //vnsw_agent_test_xml_test_xml_h
