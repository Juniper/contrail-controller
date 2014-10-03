/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __REDIS_CONNECTION__H__
#define __REDIS_CONNECTION__H__

#include <string>
#include <boost/asio.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <tbb/mutex.h>
#include "hiredis/hiredis.h"
#include "hiredis/async.h"
#include "hiredis/boostasio.hpp"
#include "io/event_manager.h"

/*
 * Class for maintaining an async connection to Redis, aka RAC - redis async connection
 */
class RedisAsyncConnection {
public:
    static const int RedisReconnectTime = 5;

    typedef boost::function<void (void)> ClientConnectCbFn;
    typedef boost::function<void (void)> ClientDisconnectCbFn;
    typedef boost::function<void (const redisAsyncContext *c, void *r, void *privdata)> ClientAsyncCmdCbFn;
    typedef boost::function<void (const redisReply *reply)> RAC_StatCbFn;
    typedef boost::function<void (const struct redisAsyncContext*, int)> RAC_ConnectCbFn;
    typedef boost::function<void (const struct redisAsyncContext*, int)> RAC_DisconnectCbFn;

    struct RAC_CbFns {
        RAC_CbFns() :
            stat_cbfn_(NULL),
            connect_cbfn_(NULL),
            disconnect_cbfn_(NULL),
            client_async_cmd_cbfn_(NULL) { }
        RAC_StatCbFn stat_cbfn_;
        RAC_ConnectCbFn connect_cbfn_;
        RAC_DisconnectCbFn disconnect_cbfn_;
        ClientAsyncCmdCbFn client_async_cmd_cbfn_;
    };

    typedef boost::ptr_map<const redisAsyncContext *, RAC_CbFns> RAC_CbFnsMap;

    RedisAsyncConnection(EventManager *evm, const std::string & redis_ip,
            unsigned short redis_port, ClientConnectCbFn client_connect_cb = NULL,
            ClientDisconnectCbFn client_disconnect_cb = NULL);
    virtual ~RedisAsyncConnection();

    bool RAC_Connect(void);

    bool IsConnUp() {
        return (state_ == REDIS_ASYNC_CONNECTION_CONNECTED);
    }
    bool SetClientAsyncCmdCb(ClientAsyncCmdCbFn cb_fn);
    bool RedisAsyncCommand(void *rpi, const char *format, ...);
    bool RedisAsyncArgCmd(void *rpi, const std::vector<std::string> &args);
    void RAC_StatUpdate(const redisReply *reply);

    static RAC_CbFnsMap& rac_cb_fns_map() {
        return rac_cb_fns_map_;
    }
    EventManager * GetEVM() { return evm_; } 

    uint64_t CallDisconnected() { return callDisconnected_; }
    uint64_t CallFailed() { return callFailed_; } 
    uint64_t CallSucceeded() { return callSucceeded_; } 
    uint64_t CallbackNull() { return callbackNull_; }
    uint64_t CallbackFailed() { return callbackFailed_; }
    uint64_t CallbackSucceeded() { return callbackSucceeded_; }

    boost::asio::ip::tcp::endpoint Endpoint() const { return endpoint_; }
private:
    enum RedisState {
        REDIS_ASYNC_CONNECTION_INIT      = 0,
        REDIS_ASYNC_CONNECTION_PENDING   = 1,
        REDIS_ASYNC_CONNECTION_CONNECTED = 2,
        REDIS_ASYNC_CONNECTION_DISCONNECTED = 3,
    };

    EventManager *evm_;
    const std::string hostname_;
    const unsigned short port_;

    uint64_t callDisconnected_;
    uint64_t callFailed_;
    uint64_t callSucceeded_;
    uint64_t callbackNull_;
    uint64_t callbackFailed_;
    uint64_t callbackSucceeded_;

    redisAsyncContext *context_;
    //boost::scoped_ptr<redisBoostClient> client_;
    boost::shared_ptr<redisBoostClient> client_;
    tbb::mutex mutex_;
    RedisState state_;
    boost::asio::deadline_timer reconnect_timer_;

    void RAC_Reconnect(const boost::system::error_code &error);

    /* the flow for connect callback is
     * 1. RAC_ConnectCallback gets called from hiredis lib
     * 2. A lookup against redisAsyncContext in rac_cb_fns_map_ yields
     * RAC_ConnectCallbackProcess_ptr, which is same as
     * RAC_ConnectCallbackProcess
     * 3. From RAC_ConnectCallbackProcess client's client_connect_cb_ gets called
     *
     * Flow for other callbacks is similar...
     */

    /* connect callback related fields */
    void RAC_ConnectCallbackProcess(const struct redisAsyncContext *c, int status);
    static void RAC_ConnectCallback(const struct redisAsyncContext *c, int status);
    //RAC_ConnectCbFn RAC_ConnectCallbackProcess_ptr;
    ClientConnectCbFn client_connect_cb_;

    /* disconnect callback related fields */
    void RAC_DisconnectCallbackProcess(const struct redisAsyncContext *c, int status);
    static void RAC_DisconnectCallback(const struct redisAsyncContext *c, int status);
    //RAC_DisconnectCbFn RAC_DisconnectCallbackProcess_ptr;
    ClientDisconnectCbFn client_disconnect_cb_;

    /* async command callback related fields */
    static void RAC_AsyncCmdCallback(redisAsyncContext *c, void *r, void *privdata);

    static RAC_CbFnsMap rac_cb_fns_map_;
    static tbb::mutex rac_cb_fns_map_mutex_;

    boost::asio::ip::tcp::endpoint endpoint_;
};
#endif
