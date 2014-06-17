/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "redis_connection.h"

#include <tbb/mutex.h>
#include <boost/bind.hpp>
#include "base/util.h"
#include "base/logging.h"
#include "base/parse_object.h"
#include <cstdlib>
#include "hiredis/hiredis.h"
#include "hiredis/base64.h"
#include "hiredis/boostasio.hpp"

using std::string;
using std::vector;

RedisAsyncConnection::RAC_CbFnsMap RedisAsyncConnection::rac_cb_fns_map_;
tbb::mutex RedisAsyncConnection::rac_cb_fns_map_mutex_;

RedisAsyncConnection::RedisAsyncConnection(EventManager *evm, const std::string & redis_ip,
        unsigned short redis_port, ClientConnectCbFn client_connect_cb, ClientDisconnectCbFn client_disconnect_cb) :
    evm_(evm),
    hostname_(redis_ip),
    port_(redis_port),
    callDisconnected_(0),
    callFailed_(0),
    callSucceeded_(0),
    callbackNull_(0),
    callbackFailed_(0),
    callbackSucceeded_(0),
    context_(NULL),
    state_(REDIS_ASYNC_CONNECTION_INIT),
    reconnect_timer_(*evm->io_service()),
    client_connect_cb_(client_connect_cb),
    client_disconnect_cb_(client_disconnect_cb) {
    boost::system::error_code ec;
    boost::asio::ip::address redis_addr(
        boost::asio::ip::address::from_string(hostname_, ec));
    endpoint_ = boost::asio::ip::tcp::endpoint(redis_addr, redis_port);
}

RedisAsyncConnection::~RedisAsyncConnection() {
    boost::system::error_code ec;

    if (client_connect_cb_)
        client_connect_cb_ = NULL;

    if (client_disconnect_cb_)
        client_disconnect_cb_ = NULL;

    if (context_) {
      {
        tbb::mutex::scoped_lock lock(rac_cb_fns_map_mutex_);

        RedisAsyncConnection::RAC_CbFnsMap& fns_map = 
            RedisAsyncConnection::rac_cb_fns_map();
        RedisAsyncConnection::RAC_CbFnsMap::iterator it = fns_map.find(context_);
        assert(it != fns_map.end());
        fns_map.erase(it);
      }
      redisAsyncFree(context_);
    }    
    reconnect_timer_.cancel(ec);
}

void RedisAsyncConnection::RAC_Reconnect(const boost::system::error_code &error) {
    if (error) {
        LOG(INFO, "RAC_Reconnect error: " << error.message());
        if (error.value() != boost::system::errc::operation_canceled) {
            LOG(INFO, "OpServerProxy::OpServerImpl::RAC_Reconnect error: "
                    << error.category().name()
                    << " " << error.message());
        } else {
            return;
        }
    } else {
        LOG(INFO, "RedisAsyncConnection::RAC_Reconnect initiated " << this);
    }

    if (!RAC_Connect()) {
        assert(0);
    }
}

void RedisAsyncConnection::RAC_ConnectCallbackProcess(const struct redisAsyncContext *c, int status) {
    LOG(DEBUG, "RAC_Connect status: " << status << " " << this);
    if (status != REDIS_OK) {
        if (context_) {
            tbb::mutex::scoped_lock lock(rac_cb_fns_map_mutex_);

            RedisAsyncConnection::RAC_CbFnsMap& fns_map = 
                RedisAsyncConnection::rac_cb_fns_map();
            RedisAsyncConnection::RAC_CbFnsMap::iterator it = fns_map.find(context_);
            if (it != fns_map.end()) {
                fns_map.erase(it);
            } 
            context_ = NULL;
            client_.reset();
        }

        boost::system::error_code ec;
        reconnect_timer_.expires_from_now(boost::posix_time::seconds(RedisAsyncConnection::RedisReconnectTime), ec);
        reconnect_timer_.async_wait(boost::bind(&RedisAsyncConnection::RAC_Reconnect, this, boost::asio::placeholders::error));
        return;
    }
    state_ = REDIS_ASYNC_CONNECTION_CONNECTED;
    LOG(DEBUG, "Connected to REDIS...\n");

    if (client_connect_cb_)
        client_connect_cb_();
}

void RedisAsyncConnection::RAC_StatUpdate(const redisReply *reply) {
    if (reply == NULL) {
        callbackNull_++;
        return;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
        callbackFailed_++;
    }  else {
        callbackSucceeded_++;
    }
}

void RedisAsyncConnection::RAC_ConnectCallback(const struct redisAsyncContext *c, int status) {
    RedisAsyncConnection::RAC_ConnectCbFn fn;

      {
        tbb::mutex::scoped_lock lock(rac_cb_fns_map_mutex_);
        RedisAsyncConnection::RAC_CbFnsMap& fns_map = RedisAsyncConnection::rac_cb_fns_map();
        RedisAsyncConnection::RAC_CbFnsMap::iterator it = fns_map.find(c);
        if (it == fns_map.end())
            assert(0);

        RedisAsyncConnection::RAC_CbFns *fns = it->second;
        fn = fns->connect_cbfn_;
        fns->connect_cbfn_ = NULL;
      }

    if (!fn) {
        assert(0);
    }
    (fn)(c, status);
}

void RedisAsyncConnection::RAC_DisconnectCallbackProcess(const struct redisAsyncContext *c, int status) {
      LOG(DEBUG, "RAC_Disconnect status: " << status << " " << this);
      {
        tbb::mutex::scoped_lock lock(rac_cb_fns_map_mutex_);
        RedisAsyncConnection::RAC_CbFnsMap& fns_map = RedisAsyncConnection::rac_cb_fns_map();
        RedisAsyncConnection::RAC_CbFnsMap::iterator it = fns_map.find(context_);
        if (it != fns_map.end())
            fns_map.erase(it);
        state_ = REDIS_ASYNC_CONNECTION_DISCONNECTED;
        context_ = NULL;
        client_.reset();
      }

    if (client_disconnect_cb_)
        client_disconnect_cb_();
}

void RedisAsyncConnection::RAC_DisconnectCallback(const struct redisAsyncContext *c, int status) {
    RedisAsyncConnection::RAC_DisconnectCbFn fn;

      {
        tbb::mutex::scoped_lock lock(rac_cb_fns_map_mutex_);
        RedisAsyncConnection::RAC_CbFnsMap& fns_map = RedisAsyncConnection::rac_cb_fns_map();
        RedisAsyncConnection::RAC_CbFnsMap::iterator it = fns_map.find(c);
        if (it == fns_map.end()) {
            return;
        }

        RedisAsyncConnection::RAC_CbFns *fns = it->second;
        fn = fns->disconnect_cbfn_;
        fns->disconnect_cbfn_ = NULL;
      }

    if (!fn) {
        assert(0);
    }

    (fn)(c, status);
}

bool RedisAsyncConnection::RAC_Connect(void) {
    tbb::mutex::scoped_lock lock(mutex_);

    assert(!context_);
    context_ = redisAsyncConnect(hostname_.c_str(), port_);
    if (context_->err) {
        LOG(DEBUG, "RAC_Connect: redisAsyncConnect() failed:" << context_->errstr);
        boost::system::error_code ec;
        reconnect_timer_.expires_from_now(boost::posix_time::seconds(RedisAsyncConnection::RedisReconnectTime), ec);
        reconnect_timer_.async_wait(boost::bind(&RedisAsyncConnection::RAC_Reconnect, this, boost::asio::placeholders::error));
        context_ = NULL;
        return true;
    }
    client_.reset(new redisBoostClient(*evm_->io_service(), context_, mutex_));

    tbb::mutex::scoped_lock fns_lock(rac_cb_fns_map_mutex_);

    assert(redisAsyncSetConnectCallback(context_, RedisAsyncConnection::RAC_ConnectCallback) == REDIS_OK);
    RedisAsyncConnection::RAC_CbFnsMap& fns_map = RedisAsyncConnection::rac_cb_fns_map();
    RedisAsyncConnection::RAC_CbFnsMap::iterator it = fns_map.find(context_);
    if (it == fns_map.end()) {
        const redisAsyncContext *c_context = context_;
        it = (fns_map.insert(c_context, new RAC_CbFns)).first;
    } else {
        assert(0);
    }
    it->second->stat_cbfn_ = boost::bind(&RedisAsyncConnection::RAC_StatUpdate, this, _1);
    it->second->connect_cbfn_ = boost::bind(&RedisAsyncConnection::RAC_ConnectCallbackProcess, this, _1, _2);

    assert(redisAsyncSetDisconnectCallback(context_, RedisAsyncConnection::RAC_DisconnectCallback) == REDIS_OK);
    it->second->disconnect_cbfn_ = boost::bind(&RedisAsyncConnection::RAC_DisconnectCallbackProcess, this, _1, _2);

    state_ = REDIS_ASYNC_CONNECTION_PENDING;
    return true;
}

void RedisAsyncConnection::RAC_AsyncCmdCallback(redisAsyncContext *c, void *r, void *privdata) {
    ClientAsyncCmdCbFn cbfn;
    RAC_StatCbFn stat_fn;
    {
        tbb::mutex::scoped_lock lock(rac_cb_fns_map_mutex_);
        RedisAsyncConnection::RAC_CbFnsMap& fns_map = RedisAsyncConnection::rac_cb_fns_map();
        RedisAsyncConnection::RAC_CbFnsMap::iterator it = fns_map.find(c);
        if (it == fns_map.end()) {
            return;
        }
        cbfn = it->second->client_async_cmd_cbfn_;
        stat_fn = it->second->stat_cbfn_;
    }
    if (cbfn) {
        (cbfn)(c, r, privdata);
        return;
    }

    redisReply *reply = (redisReply*)r;
    assert(stat_fn);
    (stat_fn)(reply);

#if 0

    if (reply->type == REDIS_REPLY_ARRAY) {
        LOG(DEBUG, __func__ << "REDIS_REPLY_ARRAY == " << reply->elements);
        int i;
        for (i = 0; i < (int)reply->elements; i++) {
            if (reply->element[i]->type == REDIS_REPLY_STRING) {
                LOG(DEBUG, __func__ << "Element" << i << "== " << reply->element[i]->str);
            } else {
                LOG(DEBUG, __func__ << "Element" << i << " type == " << reply->element[i]->type);
            }
        }
    } else if (reply->type == REDIS_REPLY_STRING) {
        LOG(DEBUG, __func__ << "REDIS_REPLY_STRING == " << reply->str);
    } else if (reply->type == REDIS_REPLY_INTEGER) {
        LOG(DEBUG, __func__ << "REDIS_REPLY_INTEGER == " << reply->type);
    } else {
        LOG(DEBUG, __func__ << "reply->type == " << reply->type);
    }
#endif
}

bool RedisAsyncConnection::SetClientAsyncCmdCb(ClientAsyncCmdCbFn cb_fn) {
    tbb::mutex::scoped_lock lock(rac_cb_fns_map_mutex_);
    RedisAsyncConnection::RAC_CbFnsMap& fns_map = rac_cb_fns_map();
    RedisAsyncConnection::RAC_CbFnsMap::iterator it = fns_map.find(context_);

    if (it == fns_map.end()) {
        assert(0);
    } else {
        if ((!it->second) || (it->second->client_async_cmd_cbfn_ != NULL))
            assert(0);
        it->second->client_async_cmd_cbfn_ = cb_fn;
    }
    return true;
}



bool RedisAsyncConnection::RedisAsyncArgCmd(void *rpi,
        const vector<string> &args) {

    tbb::mutex::scoped_lock lock(mutex_);

    if (state_ != REDIS_ASYNC_CONNECTION_CONNECTED) {
        callDisconnected_++;
        return false;
    }

    int argc = args.size();
    const char** argv = new const char* [argc];
    for (uint i=0; i < args.size(); i++) {
        argv[i] = args[i].c_str();
    }
    bool status = false;
    int ret;

    ret = redisAsyncCommandArgv(context_,
            RedisAsyncConnection::RAC_AsyncCmdCallback,
            rpi,
            argc,
            argv,
            NULL);

    delete[] argv;

    if (REDIS_ERR == ret) {
        LOG(INFO, "Could NOT apply " << args[0] << " to Redis : ");
        callFailed_++;
    } else {
        status = true;
        callSucceeded_++;
    }
    return status;
}


bool RedisAsyncConnection::RedisAsyncCommand(void *rpi, const char *format, ...) {
    tbb::mutex::scoped_lock lock(mutex_);

    if (state_ != REDIS_ASYNC_CONNECTION_CONNECTED) {
        callDisconnected_++;
        return false;
    }

    bool status = false;
    int ret;
    va_list ap; 
    va_start(ap,format);

    ret = redisvAsyncCommand(context_,
            RedisAsyncConnection::RAC_AsyncCmdCallback,
            rpi,
            format,
            ap);

    if (REDIS_ERR == ret) {
        LOG(INFO, "Could NOT apply " << format << " to Redis : ");
        callFailed_++;
    } else {
        status = true;
        callSucceeded_++;
    }
    va_end(ap);
    return status;
}
