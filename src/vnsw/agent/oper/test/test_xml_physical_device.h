/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_xml_test_xml_physical_device_h
#define vnsw_agent_test_xml_test_xml_physical_device_h

#include <test-xml/test_xml.h>
#include <test-xml/test_xml_validate.h>

void AgentUtXmlPhysicalDeviceInit(AgentUtXmlTest *test);

class AgentUtXmlPhysicalDevice : public AgentUtXmlConfig {
public:
    AgentUtXmlPhysicalDevice(const std::string &name,
                             const boost::uuids::uuid &uuid,
                             const pugi::xml_node &node,
                             AgentUtXmlTestCase *test_case);
    ~AgentUtXmlPhysicalDevice();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
};

class AgentUtXmlPhysicalInterface : public AgentUtXmlConfig {
public:
    AgentUtXmlPhysicalInterface(const std::string &name,
                                const boost::uuids::uuid &uuid,
                                const pugi::xml_node &node,
                                AgentUtXmlTestCase *test_case);
    ~AgentUtXmlPhysicalInterface();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
    std::string device_name_;
};

class AgentUtXmlRemotePhysicalInterface : public AgentUtXmlConfig {
public:
    AgentUtXmlRemotePhysicalInterface(const std::string &name,
                                      const boost::uuids::uuid &uuid,
                                      const pugi::xml_node &node,
                                      AgentUtXmlTestCase *test_case);
    ~AgentUtXmlRemotePhysicalInterface();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
    std::string device_name_;
};

class AgentUtXmlLogicalInterface : public AgentUtXmlConfig {
public:
    AgentUtXmlLogicalInterface(const std::string &name,
                               const boost::uuids::uuid &uuid,
                               const pugi::xml_node &node,
                               AgentUtXmlTestCase *test_case);
    ~AgentUtXmlLogicalInterface();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
    std::string port_name_;
    std::string vmi_name_;
};

/////////////////////////////////////////////////////////////////////////////
// Validation entries
/////////////////////////////////////////////////////////////////////////////
class AgentUtXmlPhysicalDeviceValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlPhysicalDeviceValidate(const std::string &name,
                                     const boost::uuids::uuid &id,
                                     const pugi::xml_node &node);
    virtual ~AgentUtXmlPhysicalDeviceValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
};

class AgentUtXmlPhysicalInterfaceValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlPhysicalInterfaceValidate(const std::string &name,
                                        const boost::uuids::uuid &id,
                                        const pugi::xml_node &node);
    virtual ~AgentUtXmlPhysicalInterfaceValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
};

class AgentUtXmlRemotePhysicalInterfaceValidate :
    public AgentUtXmlValidationNode {
public:
    AgentUtXmlRemotePhysicalInterfaceValidate(const std::string &name,
                                              const boost::uuids::uuid &id,
                                              const pugi::xml_node &node);
    virtual ~AgentUtXmlRemotePhysicalInterfaceValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    std::string fqdn_;
    const boost::uuids::uuid id_;
    boost::uuids::uuid device_uuid_;
};

class AgentUtXmlLogicalInterfaceValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlLogicalInterfaceValidate(const std::string &name,
                                  const boost::uuids::uuid &id,
                                  const pugi::xml_node &node);
    virtual ~AgentUtXmlLogicalInterfaceValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
    std::string physical_port_;
    boost::uuids::uuid device_uuid_;
    boost::uuids::uuid vmi_uuid_;
    uint16_t vlan_;
};

class AgentUtXmlPhysicalDeviceVnValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlPhysicalDeviceVnValidate(const std::string &name,
                                      const boost::uuids::uuid &id,
                                      const pugi::xml_node &node);
    virtual ~AgentUtXmlPhysicalDeviceVnValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
    boost::uuids::uuid device_uuid_;
    boost::uuids::uuid vn_uuid_;
};

#endif //vnsw_agent_test_xml_test_xml_physical_device_h
