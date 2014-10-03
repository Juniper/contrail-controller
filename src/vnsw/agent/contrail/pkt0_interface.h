/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_contrail_pkt0_interface_hpp
#define vnsw_agent_contrail_pkt0_interface_hpp

#include <string>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>

#include <pkt/vrouter_interface.h>

// pkt0 interface implementation of VrouterControlInterface
class Pkt0Interface: public VrouterControlInterface {
public:
    Pkt0Interface(const std::string &name, boost::asio::io_service *io);
    virtual ~Pkt0Interface();

    void InitControlInterface();
    void IoShutdownControlInterface();
    void ShutdownControlInterface();

    const std::string &Name() const { return name_; }
    int Send(uint8_t *buff, uint16_t buff_len, const PacketBufferPtr &pkt);

    const unsigned char *mac_address() const { return mac_address_; }
protected:
    void AsyncRead();
    void ReadHandler(const boost::system::error_code &err, std::size_t length);
    void WriteHandler(const boost::system::error_code &error,
                      std::size_t length, PacketBufferPtr pkt, uint8_t *buff);

    std::string name_;
    int tap_fd_;
    unsigned char mac_address_[ETHER_ADDR_LEN];
    boost::asio::posix::stream_descriptor input_;

    uint8_t *read_buff_;
    PktHandler *pkt_handler_;
    DISALLOW_COPY_AND_ASSIGN(Pkt0Interface);
};

#endif // vnsw_agent_pkt_contrail_pkt0_interface_hpp
