/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "io/event_manager.h"
#include "base/logging.h"
#include "io/io_log.h"

using namespace boost::asio;

SandeshTraceBufferPtr IOTraceBuf(SandeshTraceBufferCreate(IO_TRACE_BUF, 1000));

EventManager::EventManager() {
    shutdown_ = false;
    //SandeshTraceBufferCreate(IO_TRACE_BUF, 1000);
}

void EventManager::Shutdown() {
    shutdown_ = true;

    // TODO: make sure that are no users of this event manager.
    io_service_.stop();
}

void EventManager::Run() {
    io_service::work work(io_service_);
    do {
        if (shutdown_) break;
        boost::system::error_code ec;
        io_service_.run(ec);
        if (ec) {
            EVENT_MANAGER_LOG_ERROR("io_service run failed: " << ec.message());
            continue;
        }
    } while(0);
}

size_t EventManager::RunOnce() {
    if (shutdown_) return 0;
    boost::system::error_code err;
    size_t res = io_service_.run_one(err);
    if (res == 0)
        io_service_.reset();
    return res;
}

size_t EventManager::Poll() {
    if (shutdown_) return 0;
    boost::system::error_code err;
    size_t res = io_service_.poll(err);
    if (res == 0)
        io_service_.reset();
    return res;
}
