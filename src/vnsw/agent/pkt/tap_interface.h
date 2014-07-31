/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_tap_intf_hpp
#define vnsw_agent_pkt_tap_intf_hpp

#include <string>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <net/ethernet.h>

// Tap Interface handler to read or write to the "pkt0" interface.
// Packets reads from the tap are given to the registered callback.
// Write to the tap interface using AsyncWrite.
class TapInterface {
public:
    static const uint32_t kMaxPacketSize = 9060;
    typedef boost::function<void(uint8_t*, std::size_t, std::size_t)> 
        PktReadCallback;

    TapInterface(Agent *agent, const std::string &name,
                 boost::asio::io_service &io, PktReadCallback cb);
    virtual ~TapInterface();
    void Init();
    void IoShutdown();

    int tap_fd() const { return tap_fd_; }
    const unsigned char *mac_address() const { return mac_address_; }
    virtual void SetupTap();
    virtual void AsyncWrite(uint8_t *buf, std::size_t len);

protected:
    void SetupAsio();
    void SetupTap(const std::string& name);
    void AsyncRead();
    void ReadHandler(const boost::system::error_code &err, std::size_t length);
    void WriteHandler(const boost::system::error_code &err, std::size_t length,
		              uint8_t *buf);

    int tap_fd_;
    Agent *agent_;
    std::string name_;
    uint8_t *read_buf_;
    PktReadCallback pkt_handler_;
    unsigned char mac_address_[ETHER_ADDR_LEN];
    boost::asio::posix::stream_descriptor input_;
    DISALLOW_COPY_AND_ASSIGN(TapInterface);
};

#endif // vnsw_agent_pkt_tap_intf_hpp
