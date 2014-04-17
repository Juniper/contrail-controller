//
// netlink_protocol.hpp
// ~~~~~~~~~~~~~~~~~~~~
//
// Created by Praveen K V
// Copyright (c) 2012 Contrail Systems. All rights reserved.
//
// Borrowed heavily from boost::asio::local socket code

#ifndef IO_NETLINK_PROTOCOL_HPP
#define IO_NETLINK_PROTOCOL_HPP

#include <boost/asio/detail/config.hpp>
#include <boost/asio/basic_datagram_socket.hpp>
#include <boost/asio/detail/socket_types.hpp>
#include <boost/asio/netlink_endpoint.hpp>

#include <boost/asio/detail/push_options.hpp>

namespace boost {
namespace asio {
namespace netlink {

//  asio netlink socket support
/**
 * The boost::asio::netlink::raw contains implementation of Netlink socket
 * in asio
 *
 * @par Thread Safety
 * @e Distinct @e objects: Safe.@n
 * @e Shared @e objects: Safe.
 *
 * @par Concepts:
 * Protocol.
 */
class raw {
public:
  raw() : proto(0) {};
  raw(int p) : proto(p) {};

  /// Obtain an identifier for the type of the protocol.
  int type() const
  {
#if defined(__linux__)
    return SOCK_RAW;
#elif defined(__FreeBSD__)
    return SOCK_DGRAM;
#else
#error "Unsupported platform"
#endif
  }

  /// Obtain an identifier for the protocol.
  int protocol() const
  {
    return proto;
  }

  /// Obtain an identifier for the protocol family.
  int family() const
  {
#if defined(__linux__)
    return AF_NETLINK;
#elif defined(__FreeBSD__)
    return AF_VENDOR00;
#else
#error "Unsupported platform"
#endif
  }

  /// The NETLINK domain socket type.
#if defined(__linux__)
  typedef basic_raw_socket<raw> socket;
#elif defined(__FreeBSD__)
  typedef basic_datagram_socket<raw> socket;
#else
#error "Unsupported platform"
#endif
  typedef basic_endpoint<raw> endpoint;

  int proto;
};

} // namespace netlink
} // namespace asio
} // namespace boost

#include <boost/asio/detail/pop_options.hpp>

#endif // IO_NETLINK_PROTOCOL_HPP
