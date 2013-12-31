/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_tap_intf_hpp
#define vnsw_agent_pkt_tap_intf_hpp

#include <string>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>

// Tap Interface handler to read or write to the "pkt0" interface.
// Packets reads from the tap are given to the registered callback.
// Write to the tap interface using AsyncWrite.
class TapInterface {
public:
    static const uint32_t kMaxPacketSize = 9060;
    typedef boost::function<void(uint8_t*, std::size_t)> PktReadCallback;

    TapInterface(const std::string &name, boost::asio::io_service &io, 
                 PktReadCallback cb);
    virtual ~TapInterface();

    int TapFd() const;
    const unsigned char *MacAddress() const;
    virtual void AsyncWrite(uint8_t *buf, std::size_t len);

protected:
    class TapDescriptor;

    void SetupAsio();
    void SetupTap(const std::string& name);
    void AsyncRead();
    void ReadHandler(const boost::system::error_code &err, std::size_t length);
    void WriteHandler(const boost::system::error_code &err, std::size_t length,
		              uint8_t *buf);

    uint8_t *read_buf_;
    PktReadCallback pkt_handler_;
    boost::scoped_ptr<TapDescriptor> tap_;
    boost::asio::posix::stream_descriptor input_;
    DISALLOW_COPY_AND_ASSIGN(TapInterface);
};

#endif // vnsw_agent_pkt_tap_intf_hpp
