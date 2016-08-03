/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "Thrift.h"

#include <string>

#include "io/event_manager.h"
#include "base/logging.h"
#include "io/io_log.h"

using boost::asio::io_service;

SandeshTraceBufferPtr IOTraceBuf(SandeshTraceBufferCreate(IO_TRACE_BUF, 1000));

EventManager::EventManager() {
    shutdown_ = false;
}

void EventManager::Shutdown() {
    shutdown_ = true;
    io_service_.stop();
}

void EventManager::Run() {
    using apache::thrift::TException;

    assert(mutex_.try_lock());
    io_service::work work(io_service_);
    do {
        if (shutdown_) break;
        boost::system::error_code ec;
        try {
            io_service_.run(ec);
            if (ec) {
                EVENT_MANAGER_LOG_ERROR("io_service run failed: " <<
                                        ec.message());
                break;
            }
        } catch (const TException &except) {
            // ignore thrift exceptions
            EVENT_MANAGER_LOG_ERROR("Thrift exception caught : " <<
                                    except.what() << "; ignoring");
            continue;
        } catch (std::exception &except) {
            static std::string what = except.what();
            EVENT_MANAGER_LOG_ERROR("Exception caught in io_service run : " <<
                                    what);
            assert(false);
        } catch (...) {
            EVENT_MANAGER_LOG_ERROR("Exception caught in io_service run : "
                                    "bailing out");
            assert(false);
        }
    } while (true);
    mutex_.unlock();
}

size_t EventManager::RunOnce() {
    assert(mutex_.try_lock());
    if (shutdown_) return 0;
    boost::system::error_code err;
    size_t res = io_service_.run_one(err);
    if (res == 0)
        io_service_.reset();
    mutex_.unlock();
    return res;
}

size_t EventManager::Poll() {
    assert(mutex_.try_lock());
    if (shutdown_) return 0;
    boost::system::error_code err;
    size_t res = io_service_.poll(err);
    if (res == 0)
        io_service_.reset();
    mutex_.unlock();
    return res;
}
