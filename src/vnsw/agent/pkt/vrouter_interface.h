/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_pkt_vrouter_pkt_io_hpp
#define vnsw_agent_pkt_vrouter_pkt_io_hpp

#include "control_interface.h"
#include "cmn/agent_stats.h"
#include "vr_types.h"
#include "vr_defs.h"
#include "vr_mpls.h"

// VrouterControlInterface is implementation of ControlInterface for platforms
// using vrouter. This class assumes agent_hdr defined in
// vrouter/include/vr_defs.h is prepended with control information
class VrouterControlInterface : public ControlInterface {
public:
    static const uint32_t kAgentHdrLen =
        (sizeof(ether_header) + sizeof(struct agent_hdr));

    VrouterControlInterface() : ControlInterface() { }
    virtual ~VrouterControlInterface() {
        vr_cmd_list_.clear();
        vr_cmd_params_list_.clear();
        agent_cmd_list_.clear();
    }

    virtual void InitControlInterface() {
        // Init and populate vector for translating command types from vrouter
        // to agent
        vr_cmd_list_.insert(vr_cmd_list_.begin(), MAX_AGENT_HDR_COMMANDS,
                            AgentHdr::INVALID);
        vr_cmd_list_[AGENT_TRAP_ARP] = AgentHdr::TRAP_ARP;
        vr_cmd_list_[AGENT_TRAP_L2_PROTOCOLS] = AgentHdr::TRAP_L2_PROTOCOL;
        vr_cmd_list_[AGENT_TRAP_NEXTHOP] = AgentHdr::TRAP_NEXTHOP;
        vr_cmd_list_[AGENT_TRAP_RESOLVE] = AgentHdr::TRAP_RESOLVE;
        vr_cmd_list_[AGENT_TRAP_FLOW_MISS] = AgentHdr::TRAP_FLOW_MISS;
        vr_cmd_list_[AGENT_TRAP_L3_PROTOCOLS] = AgentHdr::TRAP_L3_PROTOCOLS;
        vr_cmd_list_[AGENT_TRAP_DIAG] = AgentHdr::TRAP_DIAG;
        vr_cmd_list_[AGENT_TRAP_SOURCE_MISMATCH] =
            AgentHdr::TRAP_SOURCE_MISMATCH;
        vr_cmd_list_[AGENT_TRAP_HANDLE_DF] = AgentHdr::TRAP_HANDLE_DF;
        vr_cmd_list_[AGENT_TRAP_ZERO_TTL] = AgentHdr::TRAP_ZERO_TTL;
        vr_cmd_list_[AGENT_TRAP_ICMP_ERROR] = AgentHdr::TRAP_ICMP_ERROR;
        vr_cmd_list_[AGENT_TRAP_FLOW_ACTION_HOLD] = AgentHdr::TRAP_FLOW_ACTION_HOLD;
        vr_cmd_list_[AGENT_TRAP_TOR_CONTROL_PKT] = AgentHdr::TRAP_TOR_CONTROL_PKT;
        vr_cmd_list_[AGENT_TRAP_FLOW_ACTION_HOLD] = AgentHdr::TRAP_FLOW_ACTION_HOLD;
        vr_cmd_list_[AGENT_TRAP_ROUTER_ALERT] = AgentHdr::TRAP_ROUTER_ALERT;
        vr_cmd_list_[AGENT_TRAP_MAC_LEARN] = AgentHdr::TRAP_MAC_LEARN;
        vr_cmd_list_[AGENT_TRAP_MAC_MOVE] = AgentHdr::TRAP_MAC_MOVE;
        // Init and populate vector for translating command params from vrouter
        // to agent
        vr_cmd_params_list_.insert(vr_cmd_params_list_.begin(), MAX_CMD_PARAMS,
                                   AgentHdr::MAX_PACKET_CMD_PARAM);
        vr_cmd_params_list_[CMD_PARAM_PACKET_CTRL] = AgentHdr::PACKET_CMD_PARAM_CTRL;
        vr_cmd_params_list_[CMD_PARAM_1_DIAG] = AgentHdr::PACKET_CMD_PARAM_DIAG;

        // Init and populate vector for translating command types from agent
        // to vrouter
        agent_cmd_list_.insert(agent_cmd_list_.begin(), AgentHdr::INVALID,
                               MAX_AGENT_HDR_COMMANDS);
        agent_cmd_list_[AgentHdr::TX_SWITCH] = AGENT_CMD_SWITCH;
        agent_cmd_list_[AgentHdr::TX_ROUTE] = AGENT_CMD_ROUTE;
    }

    // Length of header added by implementation of VrouterControlInterface.
    // Buffer passed in Send should reserve atleast EncapsulationLength() bytes
    virtual uint32_t EncapsulationLength() const {
        return kAgentHdrLen;
    }

    int DecodeAgentHdr(AgentHdr *hdr, uint8_t *buff, uint32_t len) {
        // Enusure sanity of the packet
        if (len <= kAgentHdrLen) {
            pkt_handler()->agent()->stats()->incr_pkt_invalid_agent_hdr();
            pkt_handler()->agent()->stats()->incr_pkt_exceptions();
            pkt_handler()->agent()->stats()->incr_pkt_dropped();
            return 0;
        }

        // Decode agent_hdr
        agent_hdr *vr_agent_hdr =
            (agent_hdr *) (buff + sizeof(ether_header));

        hdr->ifindex = ntohs(vr_agent_hdr->hdr_ifindex);
        hdr->vrf = ntohs(vr_agent_hdr->hdr_vrf);
        hdr->cmd = VrCmdToAgentCmd(ntohs(vr_agent_hdr->hdr_cmd));
        hdr->cmd_param = ntohl(vr_agent_hdr->hdr_cmd_param);
        hdr->nh = ntohl(vr_agent_hdr->hdr_cmd_param_1);
        hdr->cmd_param_2 = ntohl(vr_agent_hdr->hdr_cmd_param_2);
        hdr->cmd_param_3 = ntohl(vr_agent_hdr->hdr_cmd_param_3);
        hdr->cmd_param_4 = ntohl(vr_agent_hdr->hdr_cmd_param_4);
        hdr->cmd_param_5 = vr_agent_hdr->hdr_cmd_param_5;
        if (hdr->cmd == AGENT_TRAP_HANDLE_DF) {
            hdr->mtu = ntohl(vr_agent_hdr->hdr_cmd_param);
            hdr->flow_index = ntohl(vr_agent_hdr->hdr_cmd_param_1);
        }

        return kAgentHdrLen;
    }

    // Handle packet received by VrouterControlInterface
    // Format of packet trapped is OUTER_ETH - AGENT_HDR - PAYLOAD
    bool Process(const PacketBufferPtr &pkt) {
        AgentHdr hdr;
        int agent_hdr_len = 0;

        agent_hdr_len = DecodeAgentHdr(&hdr, pkt->data(), pkt->data_len());
        if (agent_hdr_len <= 0) {
            return false;
        }

        pkt->SetOffset(agent_hdr_len);
        return ControlInterface::Process(hdr, pkt);
    }

    int EncodeAgentHdr(uint8_t *buff, const AgentHdr &hdr) {
        bzero(buff, sizeof(agent_hdr));

        // Add outer ethernet header
        struct ether_header *eth = (struct ether_header *)buff;
        eth->ether_shost[ETHER_ADDR_LEN - 1] = 1;
        eth->ether_dhost[ETHER_ADDR_LEN - 1] = 2;
        eth->ether_type = htons(ETHERTYPE_IP);

        // Fill agent_hdr
        agent_hdr *vr_agent_hdr = (agent_hdr *) (eth + 1);
        vr_agent_hdr->hdr_ifindex = htons(hdr.ifindex);
        vr_agent_hdr->hdr_vrf = htons(hdr.vrf);
        vr_agent_hdr->hdr_cmd = htons(hdr.cmd);
        vr_agent_hdr->hdr_cmd_param = htonl(hdr.cmd_param);
        vr_agent_hdr->hdr_cmd_param_1 = htonl(hdr.cmd_param_1);
        return 0;
    }

    // Transmit packet on VrouterControlInterface.
    // Format of packet after encapsulation is OUTER_ETH - AGENT_HDR - PAYLOAD
    virtual int Send(const AgentHdr &hdr, const PacketBufferPtr &pkt) {
        uint16_t agent_hdr_len = kAgentHdrLen;
        uint8_t *agent_hdr_buff = new uint8_t [agent_hdr_len];
        EncodeAgentHdr(agent_hdr_buff, hdr);

        int ret = Send(agent_hdr_buff, agent_hdr_len, pkt);
        if (ret <= 0)
            return ret;

        return ret - sizeof(agent_hdr);
    }

    virtual int Send(uint8_t *buff, uint16_t buf_len,
                     const PacketBufferPtr &pkt) = 0;
private:
    AgentHdr::PktCommand VrCmdToAgentCmd(uint16_t vr_cmd) {
        AgentHdr::PktCommand cmd = AgentHdr::INVALID;
        if (vr_cmd < vr_cmd_list_.size()) {
            cmd = vr_cmd_list_[vr_cmd];
        }

        return cmd;
    }

    AgentHdr::PktCommandParams VrCmdParamtoAgentCmdParam(uint16_t param) {
        AgentHdr::PktCommandParams cmd = AgentHdr::MAX_PACKET_CMD_PARAM;
        if (param < vr_cmd_params_list_.size()) {
            cmd = vr_cmd_params_list_[param];
        }

        return cmd;
    }

    uint16_t AgentCmdToVrCmd(AgentHdr::PktCommand agent_cmd) {
        assert((uint32_t)agent_cmd < agent_cmd_list_.size());
        return agent_cmd_list_[agent_cmd];
    }

    std::vector<AgentHdr::PktCommand> vr_cmd_list_;
    std::vector<AgentHdr::PktCommandParams> vr_cmd_params_list_;
    std::vector<uint16_t> agent_cmd_list_;

    DISALLOW_COPY_AND_ASSIGN(VrouterControlInterface);
};
#endif // vnsw_agent_pkt_vrouter_pkt_io_hpp
