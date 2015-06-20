/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_xml_test_xml_packet_h
#define vnsw_agent_test_xml_test_xmk_packet_h

#include <agent_cmn.h>
#include <agent.h>
#include <oper/nexthop.h>
#include <pkt/pkt_handler.h>

/////////////////////////////////////////////////////////////////////////////
// Generic class to describe packets. Can be used for packet parse tests,
// flow tests etc...
/////////////////////////////////////////////////////////////////////////////
class AgentUtXmlPacketUtils {
public:
    AgentUtXmlPacketUtils();
    virtual ~AgentUtXmlPacketUtils();

    bool ReadXml(const pugi::xml_node &node);
    const string ToString();

    bool Inet6Packet(Interface *intf, Agent *agent, PktGen *pkt);
    bool InetPacket(Interface *intf, Agent *agent, PktGen *pkt);
    bool MakePacket(Agent *agent, PktGen *pkt);

    TunnelType::Type GetTunnelType() const;
    uint8_t GetIpProto() const;
    uint8_t GetTrapCode() const;
    PktHandler::PktModuleName GetPacketModule()const;
    bool IsL2Mode() const;
    bool IsL3Mode() const;

public:
    std::string name_;
    uint32_t len_;
    uint32_t vrf_id_;
    std::string vrf_str_;
    bool ingress_;
    std::string fwd_mode_;
    std::string pkt_module_;

    uint32_t intf_id_;
    std::string intf_;

    std::string tunnel_type_;
    std::string tunnel_sip_;
    std::string tunnel_dip_;
    uint32_t label_;
    uint32_t vxlan_id_;

    std::string smac_;
    std::string dmac_;
    std::string sip_;
    std::string dip_;
    uint16_t proto_id_;
    std::string proto_;
    uint16_t sport_;
    uint16_t dport_;
    bool tcp_ack_;

    std::string trap_code_;
    uint32_t hash_id_;
};

/////////////////////////////////////////////////////////////////////////////
//   Packet nodes
/////////////////////////////////////////////////////////////////////////////
class AgentUtXmlPacket : public AgentUtXmlNode {
public:
    AgentUtXmlPacket(const std::string &name, const pugi::xml_node &node,
                     AgentUtXmlTestCase *test_case);
    virtual ~AgentUtXmlPacket();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);
    virtual bool Run();

    bool TxInet6Packet();
    bool TxInetPacket();
private:
    AgentUtXmlPacketUtils pkt_;
};

/////////////////////////////////////////////////////////////////////////////
// Packet Parser Validation entries
/////////////////////////////////////////////////////////////////////////////
class AgentUtXmlPktParseValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlPktParseValidate(const std::string &name,
                               const pugi::xml_node &node);
    virtual ~AgentUtXmlPktParseValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
    virtual uint32_t wait_count() { return 2; }

    bool Validate(PktHandler::PktModuleName type, PktInfo *info);
private:
    AgentUtXmlPacketUtils pkt_;
};

#endif //  vnsw_agent_test_xml_test_xml_packet_h
