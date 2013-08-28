/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "../analytics/redis_connection.h"
#ifndef __APPLE__
#include "gperftools/heap-checker.h"
#endif

RedisAsyncConnection * rac_alloc(EventManager *evm, const std::string & redis_ip,
            unsigned short redis_port,
            RedisAsyncConnection::ClientConnectCbFn client_connect_cb,
            RedisAsyncConnection::ClientDisconnectCbFn client_disconnect_cb) {
    RedisAsyncConnection * rac =
            new RedisAsyncConnection( evm, redis_ip, redis_port,
        client_connect_cb, client_disconnect_cb);
    rac->RAC_Connect();
    return rac;
}
RedisAsyncConnection * rac_alloc_nocheck(EventManager *evm, const std::string & redis_ip,
            unsigned short redis_port,
            RedisAsyncConnection::ClientConnectCbFn client_connect_cb,
            RedisAsyncConnection::ClientDisconnectCbFn client_disconnect_cb) {
#ifndef __APPLE__
    HeapLeakChecker::Disabler disabler;
#endif
    RedisAsyncConnection * rac =
            new RedisAsyncConnection( evm, redis_ip, redis_port,
        client_connect_cb, client_disconnect_cb);
    rac->RAC_Connect();
    return rac;
}
