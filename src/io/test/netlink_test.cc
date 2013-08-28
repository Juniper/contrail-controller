/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <memory>

#include <boost/asio.hpp>
#include <boost/asio/netlink_protocol.hpp>

#include "testing/gunit.h"

#include "base/logging.h"
#include "io/event_manager.h"
#include "io/netlink.hpp"

using namespace std;

int
main()
{
    boost::asio::io_service ios;
    netlink_sock sock(ios, 0);
    ios.run();
}
