/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __bind_resolver_h__
#define __bind_resolver_h__

#include <iostream>
#include <stdint.h>
#include <vector>
#include <boost/asio.hpp>
#include <boost/function.hpp>

class BindResolver {
public:
    typedef boost::function<void(uint8_t *, std::size_t)> Callback;
    static const int max_pkt_size = 1024;
    static const uint8_t max_dns_servers = 2;

    struct DnsServer {
        DnsServer (const std::string &ip, uint16_t port)
            : ip_(ip), port_(port) {}

        std::string ip_;
        uint16_t port_;
    };

    BindResolver(boost::asio::io_service &io, 
                 const std::vector<DnsServer> &dns_servers,
                 uint16_t client_port, Callback cb, uint8_t dscp);
    virtual ~BindResolver();
    void SetupResolver(const DnsServer &server, uint8_t idx);
    bool DnsSend(uint8_t *pkt, unsigned int dns_srv_index, std::size_t len);
    bool DnsSend(uint8_t *pkt, boost::asio::ip::udp::endpoint ep,
                 std::size_t len);
    void SetDscpValue(uint8_t val);

    static void Init(boost::asio::io_service &io,
                     const std::vector<DnsServer> &dns_servers,
                     uint16_t client_port, Callback cb, uint8_t dscp);
    static void Shutdown();
    static BindResolver *Resolver() { return resolver_; }

private:
    void SetDscpSocketOption();
    void AsyncRead();
    void DnsSendHandler(const boost::system::error_code &error,
                        std::size_t length, uint8_t *pkt);
    void DnsRcvHandler(const boost::system::error_code &error,
                       std::size_t length);

    uint8_t *pkt_buf_;
    Callback cb_;
    boost::asio::ip::udp::socket sock_;
    std::vector<boost::asio::ip::udp::endpoint *> dns_ep_;
    uint8_t dscp_value_;
    static BindResolver *resolver_;

    DISALLOW_COPY_AND_ASSIGN(BindResolver);
};

#endif // __bind_resolver_h__
