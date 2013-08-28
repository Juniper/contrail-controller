/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "redis_processor_vizd.h"
#include "redis_connection.h"
#include <boost/assign/list_of.hpp>

#include "seqnum_lua.cpp"
#include "delrequest_lua.cpp"
#include "uveupdate_lua.cpp"
#include "uveupdate_st_lua.cpp"
#include "uvedelete_lua.cpp"
#include "expiredgens_lua.cpp"
#include "withdrawgen_lua.cpp"

using std::string;
using std::vector;
using std::pair;
using std::map;
using std::make_pair;
using boost::assign::list_of;

void 
RedisProcessorExec::RefreshGenerator(RedisAsyncConnection * rac,
            const std::string &source, const std::string &module,
            const std::string &coll, int timeout) {
    string gkey = string("GENERATOR:") + source + ":" + module;
    std::ostringstream tstr;
    tstr << timeout;

    assert(timeout);
    rac->RedisAsyncArgCmd(NULL,
            list_of(string("SET"))(gkey)(coll)(string("EX"))(tstr.str()));

}

void 
RedisProcessorExec::WithdrawGenerator(RedisAsyncConnection * rac,
            const std::string &source, const std::string &module,
            const std::string &coll) {

    string lua_scr(reinterpret_cast<char *>(withdrawgen_lua), withdrawgen_lua_len);
    rac->RedisAsyncArgCmd(NULL,
        list_of(string("EVAL"))(lua_scr)("0")(source)(module)(coll));
}

void
RedisProcessorExec::UVEUpdate(RedisAsyncConnection * rac, RedisProcessorIf *rpi,
                       const std::string &type, const std::string &attr,
                       const std::string &source, const std::string &module,
                       const std::string &key, const std::string &msg,
                       int32_t seq, const std::string &agg,
                       const std::string &hist, int64_t ts) {

    size_t sep = key.find(":");
    string table = key.substr(0, sep);
    std::ostringstream seqstr;
    seqstr << seq;
    std::ostringstream tsstr;
    tsstr << "{\"ts\":" << ts << "}";

    if (agg == "stats") {
        std::string sc,sp,ss;
        int64_t tsbin = (ts / 3600000000) * 3600000000;
        std::ostringstream tsbinstr;
        tsbinstr << tsbin;
        std::string lhist = hist;
        if (hist == "") {
            lhist = string("1");
        }
        ss = string("HISTORY-10:") + key + ":" + source + ":" + module + ":" + type + ":" + attr;  
        sc = string("S-3600-TOPVALS:") + key + ":" + source + ":" + module + ":" + type + ":" + attr + ":" + tsbinstr.str();
        sp = string("S-3600-SUMMARY:") + key + ":" + source + ":" + module + ":" + type + ":" + attr + ":" + tsbinstr.str();

        string lua_scr(reinterpret_cast<char *>(uveupdate_st_lua), uveupdate_st_lua_len);
        rac->RedisAsyncArgCmd(rpi,
            list_of(string("EVAL"))(lua_scr)("8")(
                string("TYPES:") + source + ":" + module)(
                string("ORIGINS:") + key)(
                string("TABLE:") + table)(
                string("UVES:") + source + ":" + module + ":" + type)(
                string("VALUES:") + key + ":" + source + ":" + module + ":" + type)(
                ss)(sc)(sp)(
                source)(module)(type)(attr)(key)(seqstr.str())(lhist)(tsstr.str())(msg));

    } else {

        string lua_scr(reinterpret_cast<char *>(uveupdate_lua), uveupdate_lua_len);
        rac->RedisAsyncArgCmd(rpi,
            list_of(string("EVAL"))(lua_scr)("5")(
                string("TYPES:") + source + ":" + module)(
                string("ORIGINS:") + key)(
                string("TABLE:") + table)(
                string("UVES:") + source + ":" + module + ":" + type)(
                string("VALUES:") + key + ":" + source + ":" + module + ":" + type)(
                source)(module)(type)(attr)(key)(seqstr.str())(msg));        
    }
}

void
RedisProcessorExec::UVEDelete(RedisAsyncConnection * rac, RedisProcessorIf *rpi,
        const std::string &type,
        const std::string &source, const std::string &module,
        const string &key, const int32_t seq) {

    size_t sep = key.find(":");
    string table = key.substr(0, sep);

    std::ostringstream seqstr;
    seqstr << seq;

    string lua_scr(reinterpret_cast<char *>(uvedelete_lua), uvedelete_lua_len);
    rac->RedisAsyncArgCmd(rpi,
        list_of(string("EVAL"))(lua_scr)("6")(
            string("DEL:") + key + ":" + source + ":" + module + ":" + type + ":" + seqstr.str())(
            string("VALUES:") + key + ":" + source + ":" + module + ":" + type)(
            string("UVES:") + source + ":" + module + ":" + type)(
            string("ORIGINS:") + key)(
            string("TABLE:") + table)(
            string("DELETED"))(
            source)(module)(type)(key));

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

DelRequest::DelRequest(RedisAsyncConnection * rac, finFn fn,
        const std::string& source, const std::string& module,
        const std::string& coll, int timeout) :
            rac_(rac), fin_(fn), source_(source), module_(module),
            coll_(coll), timeout_(timeout) {}

string DelRequest::Key() {
    string vkey = "TYPES:" + source_ + ":" + module_;
    return vkey;
}

bool DelRequest::RedisSend() {
    if (!rac_->IsConnUp())
        return false;

    std::ostringstream tstr;
    tstr << timeout_;
    string lua_scr(reinterpret_cast<char *>(delrequest_lua), delrequest_lua_len);
    rac_->RedisAsyncArgCmd(this,
        list_of(string("EVAL"))(lua_scr)("0")(source_)(module_)(coll_)(tstr.str()));
    return true;
}

void DelRequest::ProcessCallback(redisReply *reply) {
    if (reply->type == REDIS_REPLY_INTEGER) {
        res_ = true;
    } else {
        res_ = false;
    }
    FinalResult();
}

void DelRequest::FinalResult() {
    (fin_)(Key(), res_);
    delete this;
}

TypesQuery::TypesQuery(RedisAsyncConnection * rac, finFn fn,
        const std::string& source, const std::string& module,
        const std::string& coll, int timeout) :
            rac_(rac), fin_(fn), source_(source), module_(module),
            coll_(coll), timeout_(timeout) {}

string TypesQuery::Key() {
    string vkey = "TYPES:" + source_ + ":" + module_;
    return vkey;
}

bool TypesQuery::RedisSend() {
    if (!rac_->IsConnUp())
        return false;

    std::ostringstream tstr;
    tstr << timeout_;
    string lua_scr(reinterpret_cast<char *>(seqnum_lua), seqnum_lua_len);
    rac_->RedisAsyncArgCmd(this,
        list_of(string("EVAL"))(lua_scr)("0")(source_)(module_)(coll_)(tstr.str())); 
    return true;
}

void TypesQuery::ProcessCallback(redisReply *reply) {

    if (reply->type == REDIS_REPLY_ARRAY) {
        for (uint iter=0; iter < reply->elements; iter+=2) {
            LOG(INFO, "SeqQuery : " << reply->element[iter]->str <<
                    " seq " << reply->element[iter+1]->str);
            res_.insert(make_pair(reply->element[iter]->str,
                    atoi(reply->element[iter+1]->str)));
        }
    }
    FinalResult();
}

void TypesQuery::FinalResult() {
    (fin_)(Key(), res_);
    delete this;
}


struct GenClear : public RedisProcessorIf {
    typedef boost::function<void(const std::string &, bool)> finFn;
    // callback function to parent
    GenClear(RedisAsyncConnection * rac, finFn fn,
            const std::string& source, const std::string& module) : 
            rac_(rac), fin_(fn), source_(source), module_(module) {}

    string Key() {
        string vkey = "GENERATOR:" + source_ + ":" + module_ ;
        return vkey;
    }

    bool RedisSend() {

        if (!rac_->IsConnUp())
            return false;
        string lua_scr(reinterpret_cast<char *>(delrequest_lua), delrequest_lua_len);

        return rac_->RedisAsyncArgCmd(this,
            list_of(string("EVAL"))(lua_scr)("0")(source_)(module_)(
                "")("-1"));
    }

    void ProcessCallback(redisReply *reply) {
        res_ = false;

        if (reply->type == REDIS_REPLY_INTEGER) {
            LOG(INFO,"GenClear successful for " << Key());
            res_ = true;
        }
        FinalResult();
    }

    void FinalResult() {
        (fin_)(Key(), res_);
        delete this;
    }

private:
    RedisAsyncConnection * rac_;
    finFn fin_;
    std::string source_;
    std::string module_;
    bool res_;    
};


GenCleanupReq::GenCleanupReq (RedisAsyncConnection * rac,
        finFn fn) : rac_(rac), fin_(fn), res_(0) {}

string GenCleanupReq::Key() {
    string vkey = "GENERATOR_CLEANUP";
    return vkey;
}

bool GenCleanupReq::RedisSend() {
    if (!rac_->IsConnUp())
        return false;

    string lua_scr(reinterpret_cast<char *>(expiredgens_lua), expiredgens_lua_len);
    rac_->RedisAsyncArgCmd(this,
        list_of(string("EVAL"))(lua_scr)("0"));
    return true;
}

void GenCleanupReq::ProcessCallback(redisReply *reply) {

    vector<RedisProcessorIf *> vch;

    if (reply->type == REDIS_REPLY_ARRAY) {
        assert((reply->elements % 2) == 0);
        for (uint iter=0; iter < reply->elements; iter+=2) {
            GenClear * gc = new GenClear(rac_,
                boost::bind(&GenCleanupReq::CallbackFromChild, this, _1, _2),
                reply->element[iter]->str, reply->element[iter+1]->str);
            vch.push_back(gc);
            LOG(INFO, "Creating GenClear for " << gc->Key());
        }
    }
    rac_->GetEVM()->io_service()->post(boost::bind(&RedisProcessorIf::ChildSpawn, this, vch));
}

void GenCleanupReq::CallbackFromChild(const std::string & key, bool res) {
    bool  *p = new bool(res);
    ChildResult(key, (void *)p);
}

void GenCleanupReq::FinalResult() {

    for (map<string,void *>::iterator it = childMap_.begin();
            it != childMap_.end(); it++) {
        bool *p = (bool *)it->second;
        if  (*p) res_++;
        delete p;
    }
    if (res_) {
        LOG(INFO, "GenCleanupReq attempted " << childMap_.size() << 
                " succeeded " << res_);
    }
    (fin_)(Key(), res_);
    delete this;
}
