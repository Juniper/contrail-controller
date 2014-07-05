/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <bind/bind_util.h>
#include <bind/bind_resolver.h>

BindResolver *BindResolver::resolver_;

void BindResolver::Init(boost::asio::io_service &io,
                        const std::vector<DnsServer> &dns_servers,
                        Callback cb) {
    assert(resolver_ == NULL);
    resolver_ = new BindResolver(io, dns_servers, cb);
}

void BindResolver::Shutdown() {
    if (resolver_) {
        delete resolver_;
        resolver_ = NULL;
    }
}

BindResolver::BindResolver(boost::asio::io_service &io,
                           const std::vector<DnsServer> &dns_servers,
                           Callback cb)
               : pkt_buf_(NULL), cb_(cb), sock_(io) {

    boost::system::error_code ec;
    uint8_t size = (dns_servers.size() > max_dns_servers) ? 
                    dns_servers.size() : max_dns_servers;
    dns_ep_.resize(size);
    for (unsigned int i = 0; i < dns_servers.size(); ++i) {
        boost::asio::ip::udp::endpoint *ep = 
            new boost::asio::ip::udp::endpoint(
                     boost::asio::ip::address::from_string(
                     dns_servers[i].ip_, ec), dns_servers[i].port_);
        assert (ec.value() == 0);
        dns_ep_[i] = ep;
    }

    boost::asio::ip::udp::endpoint local_ep(boost::asio::ip::address::
                                            from_string("0.0.0.0", ec), 0);
    sock_.open(boost::asio::ip::udp::v4(), ec);
    assert(ec.value() == 0);
    sock_.bind(local_ep, ec);
    assert(ec.value() == 0);
    AsyncRead();
}

BindResolver::~BindResolver() {
    boost::system::error_code ec;
    sock_.cancel(ec);
    sock_.close(ec);
    for (unsigned int i = 0; i < dns_ep_.size(); ++i)
        delete dns_ep_[i];
    resolver_ = NULL;
    if (pkt_buf_) delete [] pkt_buf_;
}

void BindResolver::SetupResolver(const DnsServer &server, uint8_t idx) {
    if (idx >= max_dns_servers) {
        DNS_BIND_TRACE(DnsBindError, "BindResolver doesnt support more than " <<
                       max_dns_servers << " servers; ignoring request for " <<
                       idx);
        return;
    }

    if (idx < dns_ep_.size() && dns_ep_[idx]) {
        delete dns_ep_[idx];
        dns_ep_[idx] = NULL;
    }

    boost::system::error_code ec;
    boost::asio::ip::udp::endpoint *ep = new boost::asio::ip::udp::endpoint(
        boost::asio::ip::address::from_string(server.ip_, ec), server.port_);
    assert (ec.value() == 0);
    dns_ep_[idx] = ep;
}

bool BindResolver::DnsSend(uint8_t *pkt, unsigned int dns_srv_index, 
                           std::size_t len) {
    if (dns_srv_index < dns_ep_.size() && dns_ep_[dns_srv_index] && len > 0) {
        sock_.async_send_to(
              boost::asio::buffer(pkt, len), *dns_ep_[dns_srv_index],
              boost::bind(&BindResolver::DnsSendHandler, this,
                          boost::asio::placeholders::error,
                          boost::asio::placeholders::bytes_transferred, pkt));
        return true;
    } else {
        DNS_BIND_TRACE(DnsBindError, "Invalid server index: " << dns_srv_index
                       << ";");
        delete [] pkt;
        return false;
    }
}

void BindResolver::DnsSendHandler(const boost::system::error_code &error,
                                  std::size_t length, uint8_t *pkt) {
    if (error)
        DNS_BIND_TRACE(DnsBindError, "Error sending packet to DNS server : " <<
                       boost::system::system_error(error).what() << ";");
    delete [] pkt;
}

void BindResolver::AsyncRead() {
    pkt_buf_ = new uint8_t[max_pkt_size];
    sock_.async_receive(boost::asio::buffer(pkt_buf_, max_pkt_size),
                        boost::bind(&BindResolver::DnsRcvHandler, this,
                                    boost::asio::placeholders::error,
                                    boost::asio::placeholders::bytes_transferred));
}

void BindResolver::DnsRcvHandler(const boost::system::error_code &error,
                                 std::size_t length) {
    bool del = false;
    if (!error) {
        if (cb_)
            cb_(pkt_buf_);
        else
            del = true;
    } else {
        DNS_BIND_TRACE(DnsBindError, "Error receiving DNS response : " <<
                       boost::system::system_error(error).what() << ";");
        if (error.value() == boost::asio::error::operation_aborted) {
            return;
        }
        del = true;
    }
    if (del) delete [] pkt_buf_;
    pkt_buf_ = NULL;
    AsyncRead();
}
