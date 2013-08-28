/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#pragma once

#include <boost/asio/io_service.hpp>

#include "base/util.h"

class EventManager {
public:
    EventManager();

    // Run until shutdown.
    void Run();

    // Run at most once.
    size_t RunOnce();

    // Run all ready handlers, without blocking.
    size_t Poll();

    void Shutdown();

    // Accept connection on the specified port.
    void RegisterTCPAcceptHandler();

    boost::asio::io_service *io_service() { return &io_service_; }

private:
    boost::asio::io_service io_service_;
    bool shutdown_;
    DISALLOW_COPY_AND_ASSIGN(EventManager);
};
