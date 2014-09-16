/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_control_interface_hpp
#define vnsw_agent_pkt_control_interface_hpp

#include <string>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>

#include <pkt/pkt_handler.h>

class PktHandler;

// ControlInterface is base class for control packet I/O between agent and
// dataplane. Control packets can be for ARP resolution, Diagnostics, Flow
// setup traps etc...
class ControlInterface {
public:
    static const uint32_t kMaxPacketSize = 9060;

    ControlInterface() { }
    virtual ~ControlInterface() { }

    void Init(PktHandler *pkt_handler) {
        pkt_handler_ = pkt_handler;
        InitControlInterface();
    }

    void Shutdown() { ShutdownControlInterface(); }
    void IoShutdown() { IoShutdownControlInterface(); }

    // Initialize the implementation of COntrolInterface
    virtual void InitControlInterface() = 0;

    // Shutdown control packet interface
    virtual void ShutdownControlInterface() = 0;

    // Stop I/O operations
    virtual void IoShutdownControlInterface() = 0;

    // Name of the control interface
    virtual const std::string &Name() const = 0;

    // Length of header added by implementation of ControlInterface. Buffer
    // passed in Send should reserve atleast EncapsulationLength() bytes
    // TBD : To go from here
    virtual uint32_t EncapsulationLength() const = 0;

    // Transmit packet on ControlInterface
    virtual int Send(const AgentHdr &hdr, const PacketBufferPtr &pkt) = 0;

    // Handle control packet. AgentHdr is already decoded
    bool Process(const AgentHdr &hdr, const PacketBufferPtr &pkt) {
        pkt_handler_->HandleRcvPkt(hdr, pkt);
        return true;
    }

    PktHandler *pkt_handler() const { return pkt_handler_; }
private:
    PktHandler *pkt_handler_;

    DISALLOW_COPY_AND_ASSIGN(ControlInterface);
};

#endif // vnsw_agent_pkt_control_interface_hpp
