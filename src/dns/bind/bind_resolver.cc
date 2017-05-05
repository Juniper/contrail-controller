/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <bind/bind_util.h>
#include <bind/bind_resolver.h>

BindResolver *BindResolver::resolver_;

void BindResolver::Init(boost::asio::io_service &io,
                        const std::vector<DnsServer> &dns_servers,
                        uint16_t client_port, Callback cb, uint8_t dscp) {
    assert(resolver_ == NULL);
    resolver_ = new BindResolver(io, dns_servers, client_port, cb, dscp);
}

void BindResolver::Shutdown() {
    if (resolver_) {
        delete resolver_;
        resolver_ = NULL;
    }
}

BindResolver::BindResolver(boost::asio::io_service &io,
                           const std::vector<DnsServer> &dns_servers,
                           uint16_t client_port, Callback cb,
                           uint8_t dscp)
               : pkt_buf_(NULL), cb_(cb), sock_(io), dscp_value_(dscp) {

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
                                            from_string("0.0.0.0", ec),
                                            client_port);
    sock_.open(boost::asio::ip::udp::v4(), ec);
    assert(ec.value() == 0);
    sock_.bind(local_ep, ec);
    if (ec.value() != 0) {
        local_ep.port(0);
        sock_.bind(local_ep, ec);
        assert(ec.value() == 0);
    }
    if (dscp_value_) {
        SetDscpSocketOption();
    }
    AsyncRead();
}

void BindResolver::SetDscpSocketOption() {
    /* The dscp_value_ field is expected to have DSCP value between 0 and 63 ie
     * in the lower order 6 bits of a byte. However, setsockopt expects DSCP
     * value in upper 6 bits of a byte. Hence left shift the value by 2 digits
     * before passing it to setsockopt */
    uint8_t value = dscp_value_ << 2;
    int retval = setsockopt(sock_.native_handle(), IPPROTO_IP, IP_TOS,
                            &value, sizeof(value));
    if (retval < 0) {
        DNS_BIND_TRACE(DnsBindError, "Setting DSCP bits on socket failed for "
                       << dscp_value_ << " with errno " << strerror(errno));
    }
}

void BindResolver::SetDscpValue(uint8_t val) {
    dscp_value_ = val;
    SetDscpSocketOption();
}

uint8_t BindResolver::GetDscpValue() {
    uint8_t dscp = 0;
    unsigned int optlen = sizeof(dscp);
    int retval = getsockopt(sock_.native_handle(), IPPROTO_IP, IP_TOS,
                            &dscp, &optlen);
    if (retval < 0) {
        DNS_BIND_TRACE(DnsBindError, "Getting DSCP bits on socket failed "
                       "with errno " << strerror(errno));
    }
    return dscp;
}

BindResolver::~BindResolver() {
    boost::system::error_code ec;
    sock_.cancel(ec);
    sock_.close(ec);
    for (unsigned int i = 0; i < dns_ep_.size(); ++i) {
        delete dns_ep_[i];
        dns_ep_[i] = NULL;
    }
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

bool BindResolver::DnsSend(uint8_t *pkt, boost::asio::ip::udp::endpoint ep,
                           std::size_t len) {
    if (len > 0) {
        sock_.async_send_to(
            boost::asio::buffer(pkt, len),ep,
            boost::bind(&BindResolver::DnsSendHandler, this,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred, pkt));
        return true;
    } else {
        DNS_BIND_TRACE(DnsBindError, "Invalid length of packet: " << len
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
            cb_(pkt_buf_, length);
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
