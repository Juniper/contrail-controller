/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __REDIS_PROCESSOR_VIZD_H__
#define __REDIS_PROCESSOR_VIZD_H__

#include <utility>
#include <string>
#include <vector>
#include <map>
#include <boost/function.hpp>
#include "hiredis/hiredis.h"

class RedisAsyncConnection; 
class RedisProcessorIf;

class RedisProcessorExec {
public:
    static bool
    UVEUpdate(RedisAsyncConnection * rac, RedisProcessorIf *rpi,
                       const std::string &type, const std::string &attr,
                       const std::string &source, const std::string &node_type,
                       const std::string &module, const std::string &instance_id,
                       const std::string &key, const std::string &message,
                       int32_t seq, const std::string &agg,
                       const std::string &atyp, int64_t ts);

    static bool
    UVEDelete(RedisAsyncConnection * rac, RedisProcessorIf *rpi,
            const std::string &type,
            const std::string &source, const std::string &node_type,
            const std::string &module, const std::string &instance_id,
            const std::string &key, int32_t seq);

    static bool
    SyncGetSeq(const std::string & redis_ip, unsigned short redis_port,  
            const std::string &source, const std::string &node_type,
            const std::string &module, const std::string &instance_id,
            std::map<std::string,int32_t> & seqReply);

    static bool 
    SyncDeleteUVEs(const std::string & redis_ip, unsigned short redis_port,  
            const std::string &source, const std::string &node_type,
            const std::string &module, const std::string &instance_id);

    static bool
    FlushUVEs(const std::string & redis_ip, unsigned short redis_port);
};

class RedisProcessorIf {
public:
    RedisProcessorIf() : replyCount_(-1) {}
    virtual ~RedisProcessorIf() {}

    // OpServerProxy's callback will call this
    // Derived class should provide an implementation
    // that returns a result to the parent, or spawns
    // children.
    virtual void ProcessCallback(redisReply *reply) = 0;

    // Used by clients to send the async command to Redis
    // Also used to send command to child nodes.
    // Derived class must provide an implementation
    virtual bool RedisSend() = 0;
    
    // Derived class constructs which child node it needs,
    // and this base class issues the calls to redis
    void ChildSpawn(const std::vector<RedisProcessorIf *> & vch);

    // Derived class calls this when it get a result from
    // one of the nodes' children
    void ChildResult(const std::string& key, void * childRes);

    // Derived class must implement this to report final result
    // to parent
    virtual void FinalResult() = 0;

    virtual std::string Key() = 0;
protected:
    int32_t replyCount_;
    std::map<std::string,void *> childMap_;
};

#endif

