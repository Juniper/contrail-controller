/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#pragma once

#include <boost/asio/io_service.hpp>
#include <tbb/spin_mutex.h>

#include "base/util.h"

//
// Wrapper around boost::io_service.
//
// The mutex and the related assertions in Run, RunOnce and Poll are used
// to detect the case where multiple threads are running the EventManager.
// This typically happens in unit tests that inadvertently call RunOnce or
// Poll directly or indirectly after having started a ServerThread (which
// calls Run).
//
class EventManager {
public:
    EventManager();

    // Run until shutdown.
    void Run();
    void RunWithExceptionHandling();

    // Run at most once.
    size_t RunOnce();

    // Run all ready handlers, without blocking.
    size_t Poll();

    void Shutdown();

    boost::asio::io_service *io_service() { return &io_service_; }

private:
    boost::asio::io_service io_service_;
    bool shutdown_;
    tbb::spin_mutex mutex_;

    DISALLOW_COPY_AND_ASSIGN(EventManager);
};
