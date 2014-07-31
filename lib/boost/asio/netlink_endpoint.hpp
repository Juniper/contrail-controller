//
// netlink_endpoint.hpp
// ~~~~~~~~~~~~~~~~~~~~
//
// Created by Praveen K V
// Copyright (c) 2012 Contrail Systems. All rights reserved.
//
// Borrowed heavily from boost::asio::local socket code

#ifndef IO_NETLINK_ENDPOINT_HPP
#define IO_NETLINK_ENDPOINT_HPP

#include <boost/asio/detail/config.hpp>
#include <boost/asio/detail/push_options.hpp>

namespace boost {
namespace asio {
namespace netlink {

/// Describes an endpoint for a NETLINK socket.
/**
 * The boost::asio::netlink::basic_endpoint class template describes an endpoint
 * that may be associated with a particular NETLINK socket.
 *
 * @par Thread Safety
 * @e Distinct @e objects: Safe.@n
 * @e Shared @e objects: Unsafe.
 *
 * @par Concepts:
 * Endpoint.
 */
template <typename Protocol>
class basic_endpoint
{
public:
  /// The protocol type associated with the endpoint.
  typedef Protocol protocol_type;

  /// underlying implementation of the socket layer.
  /// Dummy definition. Not used for netlink since we dont need "bind"
  typedef boost::asio::detail::socket_addr_type data_type;

  /// Default constructor.
  basic_endpoint() {
      memset(&impl_, 0, sizeof(impl_));
#if defined(__linux__)
      sa.nl_family = AF_NETLINK;
#elif defined(__FreeBSD__)
      sa.sa_family = AF_VENDOR00;
#endif
  }

  data_type *data() {
      return &impl_;
  }

  const data_type *data() const {
      return &impl_;
  }

  std::size_t size() const {
#if defined(__linux__)
      return sizeof(struct sockaddr_nl);
#else
      return sizeof(sa);
#endif
  }

  /// The protocol associated with the endpoint.
  protocol_type protocol() const {
    return protocol_type();
  }

private:
  // The underlying NETLINK domain endpoint.
  union {
      data_type impl_;
#if defined(__linux__)
      struct sockaddr_nl sa;
#elif defined(__FreeBSD__)
      struct sockaddr sa;
#endif
  };
};

} // namespace netlink
} // namespace asio
} // namespace boost

#include <boost/asio/detail/pop_options.hpp>

#endif // IO_NETLINK_ENDPOINT_HPP
