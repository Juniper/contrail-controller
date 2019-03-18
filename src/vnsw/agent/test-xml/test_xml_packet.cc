/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include <base/os.h>
#include <iostream>
#include <fstream>
#include <pugixml/pugixml.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>

#include <test/test_cmn_util.h>
#include <pkt/test/test_pkt_util.h>
#include <oper/physical_device.h>
#include <oper/physical_device_vn.h>

#include <test-xml/test_xml.h>
#include <test-xml/test_xml_oper.h>
#include <test-xml/test_xml_validate.h>
#include <test-xml/test_xml_packet.h>

using namespace std;
using namespace pugi;
using namespace boost::uuids;
using namespace AgentUtXmlUtils;
int hash_id = 1;

AgentUtXmlPacketUtils::AgentUtXmlPacketUtils() {
    name_ = "pkt";
    len_ = 64;
    vrf_id_ = -1;
    vrf_str_ = "";
    ingress_ = false;
    fwd_mode_ = "l3";
    pkt_module_ = "flow";

    intf_id_ = 0;
    intf_ = "";

    tunnel_type_ = "";
    tunnel_sip_ = "0.0.0.0";
    tunnel_dip_ = "0.0.0.0";
    label_ = -1;
    vxlan_id_ = 0;

    smac_ = "00:00:00:00:00:01";
    dmac_ = "00:00:00:00:00:02";
    sip_ = "1.1.1.1";
    dip_ = "1.1.1.2";
    proto_id_ = 1;
    proto_ = "icmp";
    sport_ = 1;
    dport_ = 2;
    tcp_ack_ = false;

    trap_code_ = "flow";
    hash_id_ = 0;
}

AgentUtXmlPacketUtils::~AgentUtXmlPacketUtils() {
}

TunnelType::Type AgentUtXmlPacketUtils::GetTunnelType()
    const {
    if (tunnel_type_ == "gre" || tunnel_type_ == "GRE")
        return TunnelType::MPLS_GRE;
    if (tunnel_type_ == "udp" || tunnel_type_ == "UDP")
        return TunnelType::MPLS_UDP;
    if (tunnel_type_ == "vxlan" || tunnel_type_ == "VXLAN")
        return TunnelType::VXLAN;
    return TunnelType::INVALID;
}

uint8_t AgentUtXmlPacketUtils::GetIpProto() const {
    if (proto_ == "tcp")
        return 6;
    if (proto_ == "udp")
        return 17;
    if (proto_ == "icmp")
        return 1;

    return atoi(proto_.c_str());
}

uint8_t AgentUtXmlPacketUtils::GetTrapCode() const {
    if (trap_code_ == "flow")
        return AgentHdr::TRAP_FLOW_MISS;

    assert(0);
    return AgentHdr::INVALID;
}

PktHandler::PktModuleName AgentUtXmlPacketUtils::GetPacketModule() const {
    if (pkt_module_ == "flow" || pkt_module_ == "FLOW")
        return PktHandler::FLOW;
    if (pkt_module_ == "invalid" || pkt_module_ == "INVALID")
        return PktHandler::INVALID;

    assert(0);
    return PktHandler::INVALID;
}

bool AgentUtXmlPacketUtils::IsL2Mode() const {
    if (fwd_mode_ == "l2" || fwd_mode_ == "L2")
        return true;
    return false;
}

bool AgentUtXmlPacketUtils::IsL3Mode() const {
    if (fwd_mode_ == "l3" || fwd_mode_ == "L3")
        return true;
    return false;
}

bool AgentUtXmlPacketUtils::ReadXml(const pugi::xml_node &node) {
    GetStringAttribute(node, "fwd_mode", &fwd_mode_);
    GetStringAttribute(node, "pkt-module", &pkt_module_);

    GetStringAttribute(node, "tunnel_type", &tunnel_type_);
    if (tunnel_type_ != "") {
        GetStringAttribute(node, "tunnel_sip", &tunnel_sip_);
        GetStringAttribute(node, "tunnel_dip", &tunnel_dip_);
        GetUintAttribute(node, "label", (uint16_t *)&label_);
        GetUintAttribute(node, "vxlan-id", (uint16_t *)&vxlan_id_);
    }

    GetStringAttribute(node, "smac", &smac_);
    GetStringAttribute(node, "dmac", &dmac_);

    GetUintAttribute(node, "intf-id", (uint16_t *)&intf_id_);
    GetStringAttribute(node, "intf", &intf_);
    GetStringAttribute(node, "interface", &intf_);

    GetStringAttribute(node, "sip", &sip_);
    GetStringAttribute(node, "dip", &dip_);
    GetStringAttribute(node, "proto", &proto_);
    GetUintAttribute(node, "proto", &proto_id_);
    GetUintAttribute(node, "sport", &sport_);
    GetUintAttribute(node, "dport", &dport_);
    GetUintAttribute(node, "hash_id", (uint16_t *)&hash_id_);

    if (hash_id_ ==  0) {
        hash_id_ = hash_id++;
    }
    return true;
}

const string AgentUtXmlPacketUtils::ToString() {
    stringstream s;
    if (tunnel_type_ == "") {
        s << "Packet < " << name_ << "> Interface <" << intf_ << "> ";
    } else {
        s << "Packet < " << name_ << "> Tunnel <" << tunnel_type_ << " : "
            << intf_ << " : " << label_ << " : " << tunnel_sip_ << " : "
            << tunnel_dip_ << "> ";
    }
    s << "<" << sip_ << " : " << dip_ << " : " << proto_ << " : " << sport_
        << " : " << dport_ << ">" << endl;
    return s.str();
}

static int GetVxlan(Interface *intf) {
    int vxlan = 0;
    if (intf) {
        VmInterface *vmi = static_cast<VmInterface *>(intf);
        if (vmi->vn()) {
            vxlan = vmi->vn()->GetVxLanId();
        }
    }
    return vxlan;
}

bool AgentUtXmlPacketUtils::InetPacket(Interface *intf, Agent *agent,
                                       PktGen *pkt) {
    TunnelType::Type tun_type = GetTunnelType();
    if (tun_type != TunnelType::INVALID) {
        pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
        const VmInterface *vhost = static_cast<const VmInterface *>
            (agent->vhost_interface());
        string dip = tunnel_dip_;
        if (dip == "0.0.0.0") {
            dip = vhost->primary_ip_addr().to_string();
        }

        uint8_t proto = IPPROTO_UDP;
        if (tun_type == TunnelType::MPLS_GRE)
            proto = IPPROTO_GRE;

        pkt->AddIpHdr(tunnel_sip_.c_str(), dip.c_str(), proto);
        if (tun_type == TunnelType::MPLS_GRE) {
            pkt->AddGreHdr();
            pkt->AddMplsHdr(label_, true);
        } else if (tun_type == TunnelType::MPLS_UDP) {
            pkt->AddUdpHdr(VR_MPLS_OVER_UDP_SRC_PORT, VR_MPLS_OVER_UDP_DST_PORT,
                           (len_ + 100));
            pkt->AddMplsHdr(label_, true);
        } else if (tun_type == TunnelType::VXLAN) {
            pkt->AddUdpHdr(VR_VXLAN_UDP_SRC_PORT, VR_VXLAN_UDP_DST_PORT, 0);
            int vxlan = GetVxlan(intf);
            pkt->AddVxlanHdr(vxlan);
        }

        if (fwd_mode_ == "l2") {
            pkt->AddEthHdr(dmac_.c_str(), smac_.c_str(), 0x800);
        }
    } else {
        pkt->AddEthHdr(dmac_.c_str(), smac_.c_str(), 0x800);
    }

    pkt->AddIpHdr(sip_.c_str(), dip_.c_str(), proto_id_);
    if (proto_id_ == 17) {
        pkt->AddUdpHdr(sport_, dport_, len_);
    } else if (proto_id_ == 6) {
        pkt->AddTcpHdr(sport_, dport_, false, false, tcp_ack_, len_);
    } else if (proto_id_ == 1) {
        pkt->AddIcmpHdr();
    } else {
        assert(0);
    }
    return true;
}

bool AgentUtXmlPacketUtils::Inet6Packet(Interface *intf, Agent *agent,
                                        PktGen *pkt) {
    return true;
}

bool AgentUtXmlPacketUtils::MakePacket(Agent *agent, PktGen *pkt) {
    bool ret = false;

    boost::system::error_code ec;
    IpAddress ip = IpAddress::from_string(sip_, ec);

    bool ingress_flow = true;
    TunnelType::Type tunnel_type = GetTunnelType();
    if (tunnel_type != TunnelType::INVALID) {
        ingress_flow = false;
    }

    VmInterface *intf = NULL;
    if (atoi(intf_.c_str())) {
        intf = static_cast<VmInterface *>(VmPortGet(atoi(intf_.c_str())));
        if (intf) {
            intf_id_ = intf->id();
        }
    }

    if (tunnel_type == TunnelType::MPLS_GRE) {
        if (IsL2Mode()) {
            label_ = intf->l2_label();
        } else {
            label_ = intf->label();
        }
    }

    if (fwd_mode_ != "l2") {
        if (ingress_flow)
            dmac_ = agent->vrrp_mac().ToString();
        else
            dmac_ = intf->mac().ToString();
    }

    if (proto_id_ == 0) {
        proto_id_ = GetIpProto();
    }

    uint16_t if_id = intf_id_;
    if (tunnel_type != TunnelType::INVALID) {
        const VmInterface *vhost = static_cast<const VmInterface *>
            (agent->vhost_interface());
        if_id = vhost->parent()->id();
    }

    int vxlan_id = 0;
    if ((tunnel_type == TunnelType::VXLAN) && (ingress_flow == false)) {
        vxlan_id = GetVxlan(intf);
    }

    if (intf->vmi_type() == VmInterface::BAREMETAL) {
        vxlan_id = GetVxlan(intf);
    }

    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(if_id, GetTrapCode(), hash_id_, vrf_id_, label_, vxlan_id);
    if (ip.is_v4()) {
        ret = InetPacket(intf, agent, pkt);
    } else {
        ret = Inet6Packet(intf, agent, pkt);
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlPacket routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlPacket::AgentUtXmlPacket(const string &name, const xml_node &node,
                                   AgentUtXmlTestCase *test_case) :
    AgentUtXmlNode(name, node, false, test_case), pkt_() {
}

AgentUtXmlPacket::~AgentUtXmlPacket() {
}

bool AgentUtXmlPacket::ReadXml() {
    AgentUtXmlNode::ReadXml();
    return pkt_.ReadXml(node());
}

bool AgentUtXmlPacket::ToXml(xml_node *parent) {
    assert(0);
    return true;
}

void AgentUtXmlPacket::ToString(string *str) {
    *str = pkt_.ToString();
    return;
}

string AgentUtXmlPacket::NodeType() {
    return "packet";
}

bool AgentUtXmlPacket::Run() {
    cout << "Generate packet" << endl;
    Agent *agent = Agent::GetInstance();
    PktGen pkt;
    pkt_.MakePacket(agent, &pkt);

    uint8_t *ptr(new uint8_t[pkt.GetBuffLen()]);
    memcpy(ptr, pkt.GetBuff(), pkt.GetBuffLen());

    TestPkt0Interface *pkt0 = static_cast<TestPkt0Interface *>
        (agent->pkt()->control_interface());
    pkt0->ProcessFlowPacket(ptr, pkt.GetBuffLen(), pkt.GetBuffLen());
    return true;
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlPktParseValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlPktParseValidate::AgentUtXmlPktParseValidate(const string &name,
                                                       const xml_node &node) :
    AgentUtXmlValidationNode(name, node), pkt_() {
}

AgentUtXmlPktParseValidate::~AgentUtXmlPktParseValidate() {
}

bool AgentUtXmlPktParseValidate::ReadXml() {
    return pkt_.ReadXml(node());
}

const string AgentUtXmlPktParseValidate::ToString() {
    return pkt_.ToString();
}

bool AgentUtXmlPktParseValidate::Validate(PktHandler::PktModuleName type,
                                          PktInfo *info) {
    bool ret = true;

    if (type != pkt_.GetPacketModule())
        return false;

    if (type == PktHandler::INVALID)
        return true;

    if (info->ip_saddr != Ip4Address::from_string(pkt_.sip_) &&
        (info->ip_saddr != Ip6Address::from_string(pkt_.sip_)))
        return false;

    if (info->ip_daddr != Ip4Address::from_string(pkt_.dip_) &&
        (info->ip_daddr != Ip6Address::from_string(pkt_.dip_)))
        return false;

    if (pkt_.IsL3Mode() == false) {
        if (info->smac != MacAddress::FromString(pkt_.smac_))
            return false;
        if (info->dmac != MacAddress::FromString(pkt_.dmac_))
            return false;
    }

    if (pkt_.proto_id_ != info->ip_proto) {
        return false;
    }

    if (pkt_.proto_id_ == 6 || pkt_.proto_id_ == 17) {
        if (pkt_.sport_ != info->sport)
            return false;
        if (pkt_.dport_ != info->dport)
            return false;
    }

    return ret;
}

bool AgentUtXmlPktParseValidate::Validate() {
    Agent *agent = Agent::GetInstance();
    cout << "Generating packet" << endl;

    PktGen pkt;
    pkt_.MakePacket(agent, &pkt);

    uint32_t buff_len = pkt.GetBuffLen();
    uint32_t payload_len = pkt.GetBuffLen();
    uint8_t *ptr(new uint8_t[buff_len]);
    memcpy(ptr, pkt.GetBuff(), pkt.GetBuffLen());

    PacketBufferPtr buff(agent->pkt()->packet_buffer_manager()->Allocate
        (PktHandler::RX_PACKET, ptr, buff_len, buff_len - payload_len,
         payload_len, 0));

    PktInfo pkt_info(buff);
    VrouterControlInterface *pkt0 = static_cast<VrouterControlInterface *>
        (agent->pkt()->control_interface());

    AgentHdr agent_hdr;
    pkt0->DecodeAgentHdr(&agent_hdr, (uint8_t *)(ptr), payload_len);

    PktHandler::PktModuleName type;
    type = agent->pkt()->pkt_handler()->ParsePacket(agent_hdr, &pkt_info,
                                                    (ptr + ETH_HLEN +
                                                    sizeof(struct agent_hdr)));
    return Validate(type, &pkt_info);
}
