/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_TEST_TEST_XML_OVSDB_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_TEST_TEST_XML_OVSDB_H_

#include <test-xml/test_xml.h>
#include <test-xml/test_xml_validate.h>

void OvsdbTestSetSessionContext(OVSDB::OvsdbClientSession *sess);
void AgentUtXmlOvsdbInit(AgentUtXmlTest *test);

/////////////////////////////////////////////////////////////////////////////
// Validation entries
/////////////////////////////////////////////////////////////////////////////
class AgentUtXmlLogicalSwitchValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlLogicalSwitchValidate(const std::string &name,
                                    const boost::uuids::uuid &id,
                                    const pugi::xml_node &node);
    virtual ~AgentUtXmlLogicalSwitchValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
    virtual uint32_t wait_count() const { return 2000; }
    virtual uint32_t sleep_time() const { return 2000; }

private:
    const boost::uuids::uuid id_;
};

class AgentUtXmlUnicastRemoteValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlUnicastRemoteValidate(const std::string &name,
                                    const boost::uuids::uuid &id,
                                    const pugi::xml_node &node);
    virtual ~AgentUtXmlUnicastRemoteValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
    virtual uint32_t wait_count() const { return 2000; }
    virtual uint32_t sleep_time() const { return 2000; }

private:
    const boost::uuids::uuid id_;
    std::string mac_;
    boost::uuids::uuid vn_uuid_;
    std::string dest_ip_;
};

#endif  // SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_TEST_TEST_XML_OVSDB_H_
