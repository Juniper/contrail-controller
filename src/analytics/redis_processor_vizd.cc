/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/contrail-globals.h"
#include "redis_processor_vizd.h"
#include "redis_connection.h"
#include <boost/assign/list_of.hpp>
#include "hiredis/hiredis.h"
#include "hiredis/boostasio.hpp"

#include "seqnum_lua.cpp"
#include "delrequest_lua.cpp"
#include "uveupdate_lua.cpp"
#include "uveupdate_st_lua.cpp"
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
                       const std::string &hist, int64_t ts) {
    
    bool ret = false;
    size_t sep = key.find(":");
    string table = key.substr(0, sep);
    std::ostringstream seqstr;
    seqstr << seq;
    std::ostringstream tsstr;
    tsstr << "{\"ts\":" << ts << "}";

    if (agg == "stats") {
        std::string sc,sp,ss;
        int64_t tsbin = (ts / 3600000000ULL) * 3600000000ULL;
        std::ostringstream tsbinstr;
        tsbinstr << tsbin;
        std::string lhist = hist;
        if (hist == "") {
            lhist = string("1");
        }
        ss = string("HISTORY-10:") + key + ":" + source + ":" + node_type + ":" + 
             module + ":" + instance_id + ":" + type + ":" + attr;  
        sc = string("S-3600-TOPVALS:") + key + ":" + source + ":" + node_type + 
             ":" + module + ":" + instance_id + ":" + type + ":" + attr + 
             ":" + tsbinstr.str();
        sp = string("S-3600-SUMMARY:") + key + ":" + source + ":" + node_type + 
             ":" + module + ":" + instance_id + ":" + type + ":" + attr + 
             ":" + tsbinstr.str();

        string lua_scr(reinterpret_cast<char *>(uveupdate_st_lua), uveupdate_st_lua_len);
        ret = rac->RedisAsyncArgCmd(rpi,
            list_of(string("EVAL"))(lua_scr)("8")(
                string("TYPES:") + source + ":" + node_type + ":" + module + ":" + instance_id)(
                string("ORIGINS:") + key)(
                string("TABLE:") + table)(
                string("UVES:") + source + ":" + node_type + ":" + module + 
                ":" + instance_id + ":" + type)(
                string("VALUES:") + key + ":" + source + ":" + node_type +
                ":" + module + ":" + instance_id + ":" + type)(
                ss)(sc)(sp)(
                source)(node_type)(module)(instance_id)(type)(attr)(key)
                (seqstr.str())(lhist)(tsstr.str())(msg)
                (integerToString(REDIS_DB_UVE)));

    } else {

        string lua_scr(reinterpret_cast<char *>(uveupdate_lua), uveupdate_lua_len);
        ret = rac->RedisAsyncArgCmd(rpi,
            list_of(string("EVAL"))(lua_scr)("5")(
                string("TYPES:") + source + ":" + node_type + ":" + module + ":" + instance_id)(
                string("ORIGINS:") + key)(
                string("TABLE:") + table)(
                string("UVES:") + source + ":" + node_type + ":" + module +
                ":" + instance_id + ":" + type)(
                string("VALUES:") + key + ":" + source + ":" + node_type + 
                ":" + module + ":" + instance_id + ":" + type)(
                source)(node_type)(module)(instance_id)(type)(attr)(key)
                (seqstr.str())(msg)(integerToString(REDIS_DB_UVE)));        
    }
    return ret;
}

bool
RedisProcessorExec::UVEDelete(RedisAsyncConnection * rac, RedisProcessorIf *rpi,
        const std::string &type,
        const std::string &source, const std::string &node_type,
        const std::string &module, const std::string &instance_id,
        const string &key, const int32_t seq) {

    size_t sep = key.find(":");
    string table = key.substr(0, sep);

    std::ostringstream seqstr;
    seqstr << seq;

    string lua_scr(reinterpret_cast<char *>(uvedelete_lua), uvedelete_lua_len);
    return rac->RedisAsyncArgCmd(rpi,
        list_of(string("EVAL"))(lua_scr)("6")(
            string("DEL:") + key + ":" + source + ":" + node_type + ":" +
            module + ":" + instance_id + ":" + type + ":" + seqstr.str())(
            string("VALUES:") + key + ":" + source + ":" + node_type + ":" + 
            module + ":" + instance_id + ":" + type)(
            string("UVES:") + source + ":" + node_type + ":" + module + ":" +
            instance_id + ":" + type)(
            string("ORIGINS:") + key)(
            string("TABLE:") + table)(
            string("DELETED"))(
            source)(node_type)(module)(instance_id)(type)(key)(integerToString(REDIS_DB_UVE)));

}


bool
RedisProcessorExec::SyncGetSeq(const std::string & redis_ip, unsigned short redis_port,  
        const std::string &source, const std::string &node_type,
        const std::string &module, const std::string &instance_id,
        std::map<std::string,int32_t> & seqReply) {

    redisContext *c = redisConnect(redis_ip.c_str(), redis_port);

    if (c->err) {
        LOG(ERROR, "No connection for SyncGetSeq " << source << ":" << node_type
            << ":" << module << ":" << instance_id);
        redisFree(c); 
        return false;
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
        const std::string &source, const std::string &node_type,
        const std::string &module, const std::string &instance_id) {

    redisContext *c = redisConnect(redis_ip.c_str(), redis_port);

    if (c->err) {
        LOG(ERROR, "No connection for SyncDeleteUVEs " << source << ":" << 
            node_type << ":" << module << ":" << instance_id);
        redisFree(c); 
        return false;
    } 

    string lua_scr(reinterpret_cast<char *>(delrequest_lua), delrequest_lua_len);

    redisReply * reply = (redisReply *) redisCommand(c, 
            "EVAL %s 0 %s %s %s %s %s",
            lua_scr.c_str(), source.c_str(), node_type.c_str(),
            module.c_str(), instance_id.c_str(), integerToString(REDIS_DB_UVE).c_str());

    if (!reply) {
        LOG(INFO, "SyncDeleteUVEs Error : " << c->errstr);
        redisFree(c);
        return false;
    }

    if (reply->type == REDIS_REPLY_INTEGER) {
        freeReplyObject(reply);
        redisFree(c);
        return true;
    }
    LOG(ERROR, "Unrecognized reponse of type " << reply->type <<
            " for SyncDeleteUVEs " << source << ":" << node_type << ":" <<
            module << ":" << instance_id);
    freeReplyObject(reply);
    redisFree(c); 
    return false;
}

bool
RedisProcessorExec::FlushUVEs(const std::string & redis_ip,
                              unsigned short redis_port) {
    redisContext *c = redisConnect(redis_ip.c_str(), redis_port);

    if (c->err) {
        LOG(ERROR, "No connection for FlushUVEs");
        redisFree(c);
        return false;
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
