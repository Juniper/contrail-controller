/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __REDIS_SENTINEL_CLIENT_H__
#define __REDIS_SENTINEL_CLIENT_H__

#include <string>
#include "io/event_manager.h"
#include "redis_connection.h"

class RedisSentinelClient {
public:
    typedef std::vector<std::string> RedisServices;
    typedef boost::function<void (const std::string& service, 
            const std::string& redis_ip,
            unsigned short redis_port)> RedisMasterUpdateCb;

    RedisSentinelClient(EventManager *evm, 
                        const std::string& sentinel_ip,
                        unsigned short sentinel_port,
                        RedisServices& services,
                        RedisMasterUpdateCb redis_master_update_cb);
    ~RedisSentinelClient();

private:
    struct RedisMasterInfo {
        RedisMasterInfo(const std::string& redis_ip, 
                        unsigned short redis_port) :
            ip(redis_ip), port(redis_port) { }
        bool operator==(const RedisMasterInfo& rhs) const {
            return ((ip == rhs.ip) && (port == rhs.port));
        }

        std::string ip;
        unsigned short port;
    };
    typedef std::map<std::string, RedisMasterInfo> RedisMasterMap;
    
    void Connect();
    void ConnUp();
    void ConnUpPostProcess();
    void ConnDown();
    void GetRedisMasters();
    void UpdateRedisMaster(const std::string& service, 
                           const std::string& redis_ip,
                           unsigned short redis_port);
    void ProcessSentinelMsg(const redisAsyncContext *c, 
                            void *msg, void *privdata);

    EventManager *evm_;
    std::string sentinel_ip_;
    unsigned short sentinel_port_;
    RedisServices services_;
    RedisMasterUpdateCb redis_master_update_cb_;
    uint64_t rx_msg_count_;
    uint64_t rx_msg_cnt_since_last_conn_; // message count since last connection
    RedisMasterMap redis_master_map_;
    boost::scoped_ptr<RedisAsyncConnection> sentinel_conn_;
    RedisAsyncConnection::ClientAsyncCmdCbFn sentinel_msg_cb_;
};

#endif //__REDIS_SENTINEL_CLIENT_H__
