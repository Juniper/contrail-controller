/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_tap_intf_hpp
#define vnsw_agent_pkt_tap_intf_hpp

#include <string>
#include <net/ethernet.h>
#include <linux/if_ether.h>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <net/ethernet.h>

#include <cmn/agent.h>
#include <pkt/packet_buffer.h>
#include "vr_defs.h"

// Tap Interface handler to read or write to the "pkt0" interface.
// Packets reads from the tap are given to the registered callback.
// Write to the tap interface using Send().
class TapInterface {
public:
    static const uint32_t kAgentHdrLen =
        (sizeof(ether_header) + sizeof(struct agent_hdr));
    static const uint32_t kMaxPacketSize = 9060;
    typedef boost::function<void(uint8_t*, std::size_t, std::size_t)>
        PktReadCallback;

    TapInterface(Agent *agent, const std::string &name,
                 boost::asio::io_service &io, PktReadCallback cb);
    virtual ~TapInterface();
    void Init();
    void IoShutdown();

    int tap_fd() const { return tap_fd_; }
    const MacAddress &mac_address() const { return mac_address_; }
    virtual void SetupTap();

    void WritePacketBufferHandler
        (const boost::system::error_code &error, std::size_t length,
         PacketBufferPtr pkt, uint8_t *buff);
    virtual void Send(const AgentHdr &hdr, PacketBufferPtr pkt);

    void Encode(uint8_t *buff, const AgentHdr &hdr);
    virtual uint32_t EncapHeaderLen() const {
        return kAgentHdrLen;
    }

protected:
    void SetupAsio();
    void SetupTap(const std::string& name);
    void AsyncRead();
    void ReadHandler(const boost::system::error_code &err, std::size_t length);
    virtual void Send(const std::vector<boost::asio::const_buffer> &buff_list,
                      PacketBufferPtr pkt, uint8_t *agent_hdr_buff);

    int tap_fd_;
    Agent *agent_;
    std::string name_;
    uint8_t *read_buf_;
    PktReadCallback pkt_handler_;
    MacAddress mac_address_;
    boost::asio::posix::stream_descriptor input_;
    DISALLOW_COPY_AND_ASSIGN(TapInterface);
};

#endif // vnsw_agent_pkt_tap_intf_hpp
