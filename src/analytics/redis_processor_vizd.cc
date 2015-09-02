/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/contrail-globals.h"
#include "base/string_util.h"
#include "redis_processor_vizd.h"
#include "redis_connection.h"
#include <boost/assign/list_of.hpp>
#include "hiredis/hiredis.h"
#include "hiredis/boostasio.hpp"

#include "seqnum_lua.cpp"
#include "delrequest_lua.cpp"
#include "uveupdate_lua.cpp"
#include "uvedelete_lua.cpp"
#include "flushuves_lua.cpp"

using std::string;
using std::vector;
using std::pair;
using std::map;
using std::make_pair;
using boost::assign::list_of;

bool
RedisProcessorExec::UVEUpdate(RedisAsyncConnection * rac, RedisProcessorIf *rpi,
                       const std::string &type, const std::string &attr,
                       const std::string &source, const std::string &node_type,
                       const std::string &module, 
                       const std::string &instance_id,
                       const std::string &key, const std::string &msg,
                       int32_t seq, const std::string &agg,
                       int64_t ts, unsigned int part,
                       bool is_alarm) {
    
    bool ret = false;
    size_t sep = key.find(":");
    string table = key.substr(0, sep);
    std::ostringstream seqstr;
    seqstr << seq;
    std::ostringstream tsstr;
    tsstr << "{\"ts\":" << ts << "}";
    const std::string table_index(is_alarm ? "ALARM_TABLE:" : "TABLE:");
    const std::string origin_index(is_alarm ? "ALARM_ORIGINS:" : "ORIGINS:");

    {
        string lua_scr(reinterpret_cast<char *>(uveupdate_lua), uveupdate_lua_len);
        ret = rac->RedisAsyncArgCmd(rpi,
            list_of(string("EVAL"))(lua_scr)("5")(
                string("TYPES:") + source + ":" + node_type + ":" + module + ":" + instance_id)(
                origin_index + key)(
                table_index + table)(
                string("UVES:") + source + ":" + node_type + ":" + module +
                ":" + instance_id + ":" + type)(
                string("VALUES:") + key + ":" + source + ":" + node_type + 
                ":" + module + ":" + instance_id + ":" + type)(
                source)(node_type)(module)(instance_id)(type)(attr)(key)
                (seqstr.str())(msg)(integerToString(REDIS_DB_UVE))
                (integerToString(part))(integerToString(is_alarm)));
    }
    return ret;
}

bool
RedisProcessorExec::UVEDelete(RedisAsyncConnection * rac, RedisProcessorIf *rpi,
        const std::string &type,
        const std::string &source, const std::string &node_type,
        const std::string &module, const std::string &instance_id,
        const string &key, const int32_t seq, bool is_alarm) {

    size_t sep = key.find(":");
    string table = key.substr(0, sep);

    std::ostringstream seqstr;
    seqstr << seq;
    const std::string table_index(is_alarm ? "ALARM_TABLE:" : "TABLE:");
    const std::string origin_index(is_alarm ? "ALARM_ORIGINS:" : "ORIGINS:");

    string lua_scr(reinterpret_cast<char *>(uvedelete_lua), uvedelete_lua_len);
    return rac->RedisAsyncArgCmd(rpi,
        list_of(string("EVAL"))(lua_scr)("6")(
            string("DEL:") + key + ":" + source + ":" + node_type + ":" +
            module + ":" + instance_id + ":" + type + ":" + seqstr.str())(
            string("VALUES:") + key + ":" + source + ":" + node_type + ":" + 
            module + ":" + instance_id + ":" + type)(
            string("UVES:") + source + ":" + node_type + ":" + module + ":" +
            instance_id + ":" + type)(
            origin_index + key)(
            table_index + table)(
            string("DELETED"))(
            source)(node_type)(module)(instance_id)(type)(key)(
            integerToString(REDIS_DB_UVE))(integerToString(is_alarm)));

}


bool
RedisProcessorExec::SyncGetSeq(const std::string & redis_ip, unsigned short redis_port,
        const std::string & redis_password, const std::string &source,
        const std::string &node_type,
        const std::string &module, const std::string &instance_id,
        std::map<std::string,int32_t> & seqReply) {

    redisContext *c = redisConnect(redis_ip.c_str(), redis_port);

    if (c->err) {
        LOG(ERROR, "No connection for SyncGetSeq " << source << ":" << node_type
            << ":" << module << ":" << instance_id);
        redisFree(c); 
        return false;
    }

    //Authenticate the context with password
    if (!redis_password.empty()) {
        redisReply * reply = (redisReply *) redisCommand(c, "AUTH %s",
						     redis_password.c_str());
        if (reply->type == REDIS_REPLY_ERROR) {
	    LOG(ERROR, "Authentication to redis error");
            freeReplyObject(reply);
            redisFree(c);
            return false;
        }
        freeReplyObject(reply);
    }
 

    string lua_scr(reinterpret_cast<char *>(seqnum_lua), seqnum_lua_len);

    redisReply * reply = (redisReply *) redisCommand(c, 
            "EVAL %s 0 %s %s %s %s %s",
            lua_scr.c_str(), source.c_str(), node_type.c_str(),
            module.c_str(), instance_id.c_str(), integerToString(REDIS_DB_UVE).c_str());

    if (!reply) {
        LOG(INFO, "SeqQuery Error : " << c->errstr);
        redisFree(c);
        return false;
    }

    if (reply->type == REDIS_REPLY_ARRAY) {
        for (uint iter=0; iter < reply->elements; iter+=2) {
            LOG(INFO, "SeqQuery <" << source << ":" << node_type << 
                    ":" << module << ":" << instance_id << 
                    "> : " << reply->element[iter]->str <<
                    " seq " << reply->element[iter+1]->str);
            seqReply.insert(make_pair(reply->element[iter]->str,
                    atoi(reply->element[iter+1]->str)));
        }
        freeReplyObject(reply);
        redisFree(c);
        return true;
    }
    LOG(ERROR, "Unrecognized reponse of type " << reply->type <<
            " for SyncGetSeq " << source << ":" << node_type << ":" << 
            module << ":" << instance_id);
    freeReplyObject(reply);
    redisFree(c); 
    return false;
}


bool 
RedisProcessorExec::SyncDeleteUVEs(const std::string & redis_ip, unsigned short redis_port,
        const std::string &redis_password,  
        const std::string &source, const std::string &node_type,
        const std::string &module, const std::string &instance_id) {

    redisContext *c = redisConnect(redis_ip.c_str(), redis_port);
    std::string generator(source + ":" + node_type + ":" + module +
                          ":" + instance_id);

    if (c->err) {
        LOG(ERROR, "No connection for SyncDeleteUVEs : " << generator);
        redisFree(c); 
        return false;
    }

    //Authenticate the context with password
    if (!redis_password.empty()) {
        std::string & local_password = const_cast<std::string &>(redis_password);
        redisReply * reply = (redisReply *) redisCommand(c, "AUTH %s",
                                                     local_password.c_str());
        if (reply->type == REDIS_REPLY_ERROR) {
            LOG(ERROR, "Authentication to redis error");
            freeReplyObject(reply);
            redisFree(c);
            return false;
        }
        freeReplyObject(reply);
    }
 
    string lua_scr(reinterpret_cast<char *>(delrequest_lua), delrequest_lua_len);

    redisReply * reply = (redisReply *) redisCommand(c, 
            "EVAL %s 0 %s %s %s %s %s",
            lua_scr.c_str(), source.c_str(), node_type.c_str(),
            module.c_str(), instance_id.c_str(), integerToString(REDIS_DB_UVE).c_str());

    if (!reply) {
        LOG(ERROR, "SyncDeleteUVEs failed for " << generator << " : " <<
            c->errstr);
        redisFree(c);
        return false;
    }

    if (reply->type == REDIS_REPLY_INTEGER) {
        freeReplyObject(reply);
        redisFree(c);
        return true;
    }
    LOG(ERROR, "Unrecognized response of type " << reply->type <<
        " for SyncDeleteUVEs : " << generator);
    freeReplyObject(reply);
    redisFree(c);
    // Redis returns error if the time taken to execute the script is
    // more than the lua-time-limit configured in redis.conf
    // It is not easy to handle this case gracefully, hence assert.
    assert(0);
    return false;
}

bool
RedisProcessorExec::FlushUVEs(const std::string & redis_ip,
                              unsigned short redis_port, 
                              const std::string & redis_password) {
    redisContext *c = redisConnect(redis_ip.c_str(), redis_port);

    if (c->err) {
        LOG(ERROR, "No connection for FlushUVEs");
        redisFree(c);
        return false;
    }

    //Authenticate the context with password
    if (!redis_password.empty()) {
        std::string & local_password = const_cast<std::string &>(redis_password);
        redisReply * reply = (redisReply *) redisCommand(c, "AUTH %s",
                                                     local_password.c_str());
        if (reply->type == REDIS_REPLY_ERROR) {
            LOG(ERROR, "Authentication to redis error");
            freeReplyObject(reply);
            redisFree(c);
            return false;
        }
        freeReplyObject(reply);
    }

    string lua_scr(reinterpret_cast<char *>(flushuves_lua), flushuves_lua_len);

    redisReply * reply = (redisReply *) redisCommand(c, "EVAL %s 0 %s",
            lua_scr.c_str(), integerToString(REDIS_DB_UVE).c_str());

    if (!reply) {
        LOG(INFO, "FlushUVEs Error : " << c->errstr);
        redisFree(c);
        return false;
    }

    if (reply->type == REDIS_REPLY_INTEGER) {
        freeReplyObject(reply);
        redisFree(c);
        return true;
    }
    LOG(ERROR, "Unrecognized reponse of type " << reply->type
            << " for FlushUVEs");
    freeReplyObject(reply);
    redisFree(c);
    return false;
}

void RedisProcessorIf::ChildSpawn(const vector<RedisProcessorIf *> & vch) {
    for (vector<RedisProcessorIf *>::const_iterator it = vch.begin();
            it != vch.end(); it++) {
        if ((*it)->RedisSend()) {
            childMap_.insert(std::make_pair((*it)->Key(), (void *)NULL));
        }
    }
    replyCount_ = 0;
    if (vch.size() == 0) FinalResult();
}

void RedisProcessorIf::ChildResult(const string& key, void * childRes) {
    map<string,void *>::iterator mapentry = childMap_.find(key);
    assert(mapentry != childMap_.end());

    mapentry->second = childRes;
    if ((size_t)(++replyCount_) == childMap_.size()) {
        FinalResult(); 
    }
}
