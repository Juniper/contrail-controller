/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <tbb/mutex.h>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/tuple/tuple.hpp>
#include "base/util.h"
#include "base/logging.h"
#include <tbb/atomic.h>
#include <cstdlib>
#include <utility>
#include "hiredis/hiredis.h"
#include "hiredis/boostasio.hpp"
#include <list>
#include "../analytics/redis_connection.h"
#include "base/work_pipeline.h"
#include "QEOpServerProxy.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "query.h"
#include "analytics_types.h"
#include "stats_select.h"

using std::list;
using std::string;
using std::vector;
using std::map;
using boost::assign::list_of;
using boost::ptr_map;
using boost::nullable;
using boost::tuple;
using boost::shared_ptr;
using boost::scoped_ptr;
using std::pair;
using std::auto_ptr;
using std::make_pair;

extern RedisAsyncConnection * rac_alloc(EventManager *, const std::string & ,unsigned short,
RedisAsyncConnection::ClientConnectCbFn ,
RedisAsyncConnection::ClientDisconnectCbFn );

extern RedisAsyncConnection * rac_alloc_nocheck(EventManager *, const std::string & ,unsigned short,
RedisAsyncConnection::ClientConnectCbFn ,
RedisAsyncConnection::ClientDisconnectCbFn );

SandeshTraceBufferPtr QeTraceBuf(SandeshTraceBufferCreate(QE_TRACE_BUF, 10000));

typedef pair<QEOpServerProxy::QPerfInfo,
             pair<shared_ptr<QEOpServerProxy::BufferT>,
                  shared_ptr<QEOpServerProxy::OutRowMultimapT> > > RawResultT; 
typedef pair<redisReply,vector<string> > RedisT;

bool RedisAsyncArgCommand(RedisAsyncConnection * rac,
            void *rpi, const vector<string>& args) {

    return rac->RedisAsyncArgCmd(rpi, args);
}

class QEOpServerProxy::QEOpServerImpl {
public:
    typedef std::vector<std::string> QEOutputT;

    struct Input {
        int cnum;
        string hostname;
        QueryEngine::QueryParams qp;
        vector<uint64_t> chunk_size;
        bool need_merge;
        bool map_output;
        string where;
        string select;
        string post;
        uint64_t time_period;
        string table;
        tbb::atomic<uint32_t> chunk_q;
    };

    void JsonInsert(std::vector<query_column> &columns,
            rapidjson::Document& dd,
            std::pair<const string,string> * map_it) {

        bool found = false;
        for (size_t j = 0; j < columns.size(); j++)
        {
            if (0 == map_it->first.compare(0,5,string("COUNT"))) {
                rapidjson::Value val(rapidjson::kNumberType);
                unsigned long num = 0;
                stringToInteger(map_it->second, num);
                val.SetUint64(num);
                dd.AddMember(map_it->first.c_str(), val, dd.GetAllocator());
                found = true;
            } else if (columns[j].name == map_it->first) {
                // find out type and convert
                if (columns[j].datatype == "string" || 
                    columns[j].datatype == "uuid")
                {
                    rapidjson::Value val(rapidjson::kStringType);
                    val.SetString(map_it->second.c_str());
                    dd.AddMember(map_it->first.c_str(), val, dd.GetAllocator());
                } else if (columns[j].datatype == "ipv4") {
                    rapidjson::Value val(rapidjson::kStringType);
                    char str[INET_ADDRSTRLEN];
                    uint32_t ipaddr = 0;

                    stringToInteger(map_it->second, ipaddr);
                    ipaddr = htonl(ipaddr);
                    inet_ntop(AF_INET, &(ipaddr), str, INET_ADDRSTRLEN);
                    map_it->second = str;

                    val.SetString(map_it->second.c_str(), map_it->second.size());
                    dd.AddMember(map_it->first.c_str(), val, dd.GetAllocator());

                } else if (columns[j].datatype == "double") {
                    rapidjson::Value val(rapidjson::kNumberType);
                    double dval = (double) strtod(map_it->second.c_str(), NULL);
                    val.SetDouble(dval);
                    dd.AddMember(map_it->first.c_str(), val, dd.GetAllocator());
                } else {
                    rapidjson::Value val(rapidjson::kNumberType);
                    unsigned long num = 0;
                    stringToInteger(map_it->second, num);
                    val.SetUint64(num);
                    dd.AddMember(map_it->first.c_str(), val, dd.GetAllocator());
                }
                found = true;
            }
        }
        assert(found);    
    }

    void QueryJsonify(const string& table, bool map_output,
        const BufferT* raw_res, const OutRowMultimapT* raw_mres, QEOutputT* raw_json) {

        vector<OutRowT>::iterator res_it;

        std::vector<query_column>  columns;

        if (!table.size()) return;
        bool found = false;
        for(size_t i = 0; i < g_viz_constants._TABLES.size(); i++)
        {
            if (g_viz_constants._TABLES[i].name == table) {
                found = true;
                columns = g_viz_constants._TABLES[i].schema.columns;
            }
        }
        if (!found) {
            if (g_viz_constants.OBJECT_VALUE_TABLE == table) {
                found = true;
                columns = g_viz_constants._OBJECT_TABLE_SCHEMA.columns; 
            }
        }
        if (!found) {
            for (std::map<std::string, objtable_info>::const_iterator it =
                    g_viz_constants._OBJECT_TABLES.begin();
                    it != g_viz_constants._OBJECT_TABLES.end(); it++) {
                if (it->first == table) {
                    found = true;
                    columns = g_viz_constants._OBJECT_TABLE_SCHEMA.columns;
                }
            }
        }
        assert(found || map_output);

        if (map_output) {
            OutRowMultimapT::const_iterator mres_it;
            for (mres_it = raw_mres->begin(); mres_it != raw_mres->end(); ++mres_it) {
                string jstr;
                StatsSelect::Jsonify(mres_it->second.first, mres_it->second.second, jstr);
                raw_json->push_back(jstr);
            }
        } else {
            QEOpServerProxy::BufferT* raw_result = 
                const_cast<QEOpServerProxy::BufferT*>(raw_res);
            QEOpServerProxy::BufferT::iterator res_it;
            for (res_it = raw_result->begin(); res_it != raw_result->end(); ++res_it) {
                std::map<std::string, std::string>::iterator map_it;
                rapidjson::Document dd;
                dd.SetObject();

                for (map_it = (*res_it).first.begin(); 
                     map_it != (*res_it).first.end(); ++map_it) {
                    // search for column name in the schema
                    JsonInsert(columns, dd, &(*map_it));
                }
                rapidjson::StringBuffer sb;
                rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
                dd.Accept(writer);
                raw_json->push_back(sb.GetString());
            }
        }        
    }


    void QECallback(void * qid, QPerfInfo qperf, auto_ptr<QEOpServerProxy::BufferT> res, 
            auto_ptr<QEOpServerProxy::OutRowMultimapT> mres) {

        RawResultT* raw(new RawResultT);
        raw->first = qperf;
        raw->second.first = res;
        raw->second.second = mres;

        ExternalProcIf<RawResultT> * rpi = NULL;
        if (qid)
            rpi = reinterpret_cast<ExternalProcIf<RawResultT> *>(qid);

        if (rpi) {
            QE_LOG_NOQID(DEBUG,  " Rx data from QE for " <<
                    rpi->Key());
            auto_ptr<RawResultT> rp(raw);
            rpi->Response(rp);
        }
    }
   
    struct Stage0Out {
        Input inp;
        bool ret_code;
        vector<QPerfInfo> ret_info;
        vector<uint32_t> chunk_merge_time;
        shared_ptr<BufferT> result;
        shared_ptr<OutRowMultimapT> mresult;
    };

    ExternalBase::Efn QueryExec(uint32_t inst, const vector<RawResultT*> & exts,
            const Input & inp, Stage0Out & res) { 
        uint32_t step = exts.size();


        if (!step) {
            res.inp = inp;
            res.ret_code = true;
         
            if (inp.map_output)
                res.mresult = shared_ptr<OutRowMultimapT>(new OutRowMultimapT());
            else
                res.result = shared_ptr<BufferT>(new BufferT());

            Input& cinp = const_cast<Input&>(inp);
            uint32_t chunknum = cinp.chunk_q.fetch_and_increment(); 
            if (chunknum < inp.chunk_size.size()) {

                string key = "QUERY:" + res.inp.qp.qid;

                // Update query status
                RedisAsyncConnection * rac = conns_[res.inp.cnum].get();
                string rkey = "REPLY:" + res.inp.qp.qid;
                char stat[40];
                uint prg = 10 + (chunknum * 75)/inp.chunk_size.size();
                QE_LOG_NOQID(DEBUG,  "QueryExec for inst " << inst <<
                    " step " << step << " PROGRESS " << prg);
                sprintf(stat,"{\"progress\":%d}", prg);
                RedisAsyncArgCommand(rac, NULL, 
                    list_of(string("RPUSH"))(rkey)(stat));

                return boost::bind(&QueryEngine::QueryExec, qosp_->qe_,
                        _1,
                        inp.qp,
                        chunknum);
            } else {
                return NULL;
            }
        }

        res.ret_info.push_back(exts[step-1]->first);
        if (exts[step-1]->first.error) {
            res.ret_code =false;
        }
        if (res.ret_code) {
            if (inp.need_merge) {
                uint64_t then = UTCTimestampUsec();
                if (inp.map_output) {
                    // TODO: This interface should not be Stats-Specific
                    StatsSelect::Merge(*(exts[step-1]->second.second), *(res.mresult));
                } else {
                    res.ret_code =
                        qosp_->qe_->QueryAccumulate(inp.qp,
                            *(exts[step-1]->second.first), *(res.result));
                }
                res.chunk_merge_time.push_back(
                    static_cast<uint32_t>((UTCTimestampUsec() - then)/1000));
        
            } else {
                // TODO : When merge is not needed, we can just send
                //        a result upto redis at this point.

                if (inp.map_output) {
                    OutRowMultimapT::iterator jt = res.mresult->begin();
                    for (OutRowMultimapT::const_iterator it = exts[step-1]->second.second->begin();
                            it != exts[step-1]->second.second->end(); it++ ) {

                        jt = res.mresult->insert(jt,
                                std::make_pair(it->first, it->second));
                    }
                } else {
                    res.result->insert(res.result->begin(),
                        exts[step-1]->second.first->begin(),
                        exts[step-1]->second.first->end());                        
                }
            }
            Input& cinp = const_cast<Input&>(inp);
            uint32_t chunknum = cinp.chunk_q.fetch_and_increment(); 
            if (chunknum < inp.chunk_size.size()) {
                string key = "QUERY:" + res.inp.qp.qid;

                // Update query status
                RedisAsyncConnection * rac = conns_[res.inp.cnum].get();
                string rkey = "REPLY:" + res.inp.qp.qid;
                char stat[40];
                uint prg = 10 + (chunknum * 75)/inp.chunk_size.size();
                QE_LOG_NOQID(DEBUG,  "QueryExec for inst " << inst <<
                    " step " << step << " PROGRESS " << prg);
                sprintf(stat,"{\"progress\":%d}", prg);
                RedisAsyncArgCommand(rac, NULL, 
                    list_of(string("RPUSH"))(rkey)(stat));         
                return boost::bind(&QueryEngine::QueryExec, qosp_->qe_,
                        _1,
                        inp.qp,
                        chunknum);
            } else {
                return NULL;
            }            
        }
        return NULL;
    }

    struct Stage0Merge {
        Input inp;
        bool ret_code;
        uint32_t fm_time;
        vector<vector<QPerfInfo> > ret_info;
        vector<vector<uint32_t> > chunk_merge_time;
        BufferT result;
        OutRowMultimapT mresult;
    };
    bool QueryMerge(const std::vector<boost::shared_ptr<Stage0Out> > & subs,
           const boost::shared_ptr<Input> & inp, Stage0Merge & res) {

        res.ret_code = true;
        res.inp = subs[0]->inp;
        res.fm_time = 0;
      
        std::vector<boost::shared_ptr<OutRowMultimapT> > mqsubs;
        std::vector<boost::shared_ptr<QEOpServerProxy::BufferT> > qsubs;
        for (vector<shared_ptr<Stage0Out> >::const_iterator it = subs.begin() ;
                it!=subs.end(); it++) {

            res.ret_info.push_back((*it)->ret_info);
            res.chunk_merge_time.push_back((*it)->chunk_merge_time);

            if ((*it)->ret_code == false) {
                res.ret_code = false;
            } else {
                if (res.inp.map_output)
                    mqsubs.push_back((*it)->mresult);
                else
                    qsubs.push_back((*it)->result);
            }
        }

        if (!res.ret_code) return true;

        if (res.inp.need_merge) {
            uint64_t then = UTCTimestampUsec();

            if (res.inp.map_output) {
                res.ret_code =
                    qosp_->qe_->QueryFinalMerge(res.inp.qp, mqsubs, res.mresult);

            } else {
                res.ret_code =
                    qosp_->qe_->QueryFinalMerge(res.inp.qp, qsubs, res.result);
            }

            uint64_t now = UTCTimestampUsec();
            res.fm_time = static_cast<uint32_t>((now - then)/1000);
        } else {
            // TODO : If a merge was not needed, results have been sent to 
            //        redis already. The only thing still needed is the status
            for (vector<shared_ptr<Stage0Out> >::const_iterator it = subs.begin() ;
                    it!=subs.end(); it++) {

                if (res.inp.map_output) {
                    OutRowMultimapT::iterator jt = res.mresult.begin();
                    for (OutRowMultimapT::const_iterator kt = (*it)->mresult->begin();
                            kt != (*it)->mresult->end(); kt++) {
                        jt = res.mresult.insert(jt,
                                std::make_pair(kt->first, kt->second));
                    }
                } else {
                    res.result.insert(res.result.begin(),
                        (*it)->result->begin(),
                        (*it)->result->end());
                }
            }
        }
        return true;
    }

    struct Output {
        Input inp;
        uint32_t redis_time;
        bool ret_code;
    };
    ExternalBase::Efn QueryResp(uint32_t inst, const vector<RedisT*> & exts,
            const Stage0Merge & inp, Output & ret) {
        uint32_t step = exts.size();
        switch (inst) {
        case 0: {
                if (!step)  {

                    ret.inp = inp.inp;
                    RedisAsyncConnection * rac = conns_[ret.inp.cnum].get();
                    std::stringstream keystr;
                    auto_ptr<QEOutputT> jsonresult(new QEOutputT);

                    QE_LOG_NOQID(INFO,  "Will Jsonify #rows " << 
                        inp.result.size() + inp.mresult.size());
                    QueryJsonify(inp.inp.table, inp.inp.map_output,
                        &inp.result, &inp.mresult, jsonresult.get());
                        
                    vector<string> const * const res = jsonresult.get();
                    vector<string>::size_type idx = 0;
                    uint32_t rownum = 0;

                    QE_LOG_NOQID(INFO,  "Did Jsonify #rows " << res->size());
                    
                    uint64_t then = UTCTimestampUsec();
                    char stat[80];
                    string key = "REPLY:" + ret.inp.qp.qid;
                    if (!inp.ret_code) {
                        sprintf(stat,"{\"progress\":%d}", - 5);
                    } else {
                        while (idx < res->size()) {
                            uint32_t rowsize = 0;
                            keystr.str(string());
                            keystr << "RESULT:" << ret.inp.qp.qid << ":" << rownum;
                            vector<string> command = list_of(string("RPUSH"))(keystr.str());
                            while ((idx < res->size()) && (((int)rowsize) < kMaxRowThreshold)) {
                                command.push_back(res->at(idx));
                                rowsize += res->at(idx).size();
                                idx++;
                            }
                            RedisAsyncArgCommand(rac, NULL, command);
                            RedisAsyncArgCommand(rac, NULL, 
                                list_of(string("EXPIRE"))(keystr.str())("300"));
                            sprintf(stat,"{\"progress\":90, \"lines\":%d}",
                                (int)rownum);
                            RedisAsyncArgCommand(rac, NULL, 
                                list_of(string("RPUSH"))(key)(stat));
                            rownum++;
                        }
                        sprintf(stat,"{\"progress\":100, \"lines\":%d, \"count\":%d}",
                            (int)rownum, (int)res->size());
                    }
                    uint64_t now = UTCTimestampUsec();
                    ret.redis_time = static_cast<uint32_t>((now - then)/1000);
                    QE_LOG_NOQID(DEBUG,  "QE Query Result is " << stat);
                    return boost::bind(&RedisAsyncArgCommand,
                            conns_[ret.inp.cnum].get(), _1,
                            list_of(string("RPUSH"))(key)(stat));   
                } else {
                    RedisAsyncConnection * rac = conns_[ret.inp.cnum].get();
                    string key = "REPLY:" + ret.inp.qp.qid;
                    RedisAsyncArgCommand(rac, NULL,
                        list_of(string("EXPIRE"))(key)("300"));

                    key = "QUERY:" + ret.inp.qp.qid;
                    RedisAsyncArgCommand(rac, NULL,
                        list_of(string("EXPIRE"))(key)("300"));

                    uint64_t now = UTCTimestampUsec();
                    uint32_t qtime = static_cast<uint32_t>(
                            (now - ret.inp.qp.query_starttm)/1000);

                    QueryPerfInfo qpi;
                    qpi.set_name(Sandesh::source());
                    qpi.set_table(inp.inp.table);

                    uint64_t enqtm = atol(ret.inp.qp.terms["enqueue_time"].c_str());
                    uint32_t enq_delay = static_cast<uint32_t>(
                            (ret.inp.qp.query_starttm - enqtm)/1000);
                    qpi.set_enq_delay(enq_delay);

                    QueryStats qs;
                    size_t outsize;

                    if (ret.inp.map_output)
                        outsize = inp.mresult.size();
                    else
                        outsize = inp.result.size();

                    qs.set_rows(static_cast<uint32_t>(outsize));                                           


                    qs.set_time(qtime);
                    qs.set_qid(ret.inp.qp.qid);
                    qs.set_chunks(inp.inp.chunk_size.size());
                    std::ostringstream wherestr, selstr, poststr;
                    for (size_t i=0; i < inp.ret_info.size(); i++) {
                        for (size_t j=0; j < inp.ret_info[i].size(); j++) {
                            wherestr << inp.ret_info[i][j].chunk_where_time << ",";
                            selstr << inp.ret_info[i][j].chunk_select_time << ",";
                            poststr << inp.ret_info[i][j].chunk_postproc_time << ",";
                        }
                        wherestr << " ";
                        selstr << " ";
                        poststr << " ";
                    }
                    qs.set_chunk_where_time(wherestr.str());
                    qs.set_chunk_select_time(selstr.str());
                    qs.set_chunk_postproc_time(poststr.str());

                    std::ostringstream mergestr;
                    for (size_t i=0; i < inp.chunk_merge_time.size(); i++) {
                        for (size_t j=0; j < inp.chunk_merge_time[i].size(); j++) {
                            mergestr << inp.chunk_merge_time[i][j] << ",";
                        }
                        mergestr << " ";
                    }

                    qs.set_chunk_merge_time(mergestr.str());
                    qs.set_final_merge_time(inp.fm_time);
                    qs.set_where(inp.inp.where);
                    qs.set_select(inp.inp.select);
                    qs.set_post(inp.inp.post);
                    qs.set_time_span(inp.inp.time_period);
                    std::vector<QueryStats> vqs;
                    vqs.push_back(qs);
                    qpi.set_query_stats(vqs);
                    QueryPerfInfoTrace::Send(qpi);

                    QueryObjectData qo;
                    qo.set_qid(ret.inp.qp.qid);
                    qo.set_table(inp.inp.table);
                    qo.set_ops_start_ts(enqtm);
                    qo.set_qed_start_ts(ret.inp.qp.query_starttm);
                    qo.set_qed_end_ts(now);
                    qo.set_flow_query_rows(static_cast<uint32_t>(outsize));
        		    QUERY_OBJECT_SEND(qo);

                    //g_viz_constants.COLLECTOR_GLOBAL_TABLE 
                    QE_LOG_NOQID(INFO, "Finished: QID " << ret.inp.qp.qid <<
                        " Table " << inp.inp.table <<
                        " Time(ms) " << qtime <<
                        " RedisTime(ms) " << ret.redis_time <<
                        " MergeTime(ms) " << inp.fm_time <<
                        " Rows " << outsize <<
                        " EnQ-delay" << enq_delay);

                    ret.ret_code = true;
                }
            }
            break;
        case 1: {
                if (!step)  {
                    string key = "ENGINE:" + inp.inp.hostname;
                    return boost::bind(&RedisAsyncArgCommand,
                            conns_[inp.inp.cnum].get(), _1,
                            list_of(string("LREM"))(key)("0")(inp.inp.qp.qid));
                } else {
                    ret.ret_code = true;
                }
            }
            break;
        }
        return NULL;
    }


    typedef WorkPipeline<Input, Stage0Merge, Output> QEPipeT;
               
    void QEPipeCb(QEPipeT *wp, bool ret_code) {
        tbb::mutex::scoped_lock lock(mutex_);

        boost::shared_ptr<Output> res = wp->Result();
        assert(pipes_.find(res->inp.qp.qid)->second == wp);
        pipes_.erase(res->inp.qp.qid);
        npipes_[res->inp.cnum-1]--;
        QE_LOG_NOQID(DEBUG,  " Result " << res->ret_code << " , " << res->inp.cnum << " conn");
        delete wp;
    }

    void ConnUpPostProcess(uint8_t cnum) {

        if (!connState_[cnum]) {
            QE_LOG_NOQID(DEBUG, "ConnUp SetCB" << cnum);
            conns_[cnum].get()->SetClientAsyncCmdCb(cb_proc_fn_[cnum]);
            connState_[cnum] = true;
        }

        bool isConnected = true;
        for(int i=0; i<kConnections+1; i++)
            if (!connState_[i]) isConnected = false;
        if (isConnected) {
            string key = "ENGINE:" + hostname_;
            conns_[0].get()->RedisAsyncArgCmd(0,
                    list_of(string("BRPOPLPUSH"))("QUERYQ")(key)("0"));            
        }
    }

    int LeastLoadedConnection() {
        int minvalue = npipes_[0];
        int minindex = 0;
        for (int i=1; i<kConnections; i++) {
            if (npipes_[i] < minvalue) {
                minvalue = npipes_[i];
                minindex = i;
            }
        }
        return minindex;
    }

    void QueryError(string qid, int ret_code) {
        redisContext *c = redisConnect(redis_host_.c_str(), port_);
        if (c->err) {
            QE_LOG_NOQID(ERROR, "Cannot report query error for " << qid <<
                " . No Redis Connection");
            return;
        }

        char stat[80];
        string key = "REPLY:" + qid;
        sprintf(stat,"{\"progress\":%d}", - ret_code);

        redisReply * reply = (redisReply *) redisCommand(c, "RPUSH %s %s",
            key.c_str(), stat);
        
        freeReplyObject(reply);
        redisFree(c);
    }


    void StartPipeline(const string qid) {
        QueryObjectData qo;
        qo.set_qid(qid);

        tbb::mutex::scoped_lock lock(mutex_);

        redisContext *c = redisConnect(redis_host_.c_str(), port_);

        if (c->err) {
            QE_LOG_NOQID(ERROR, "Cannot start Pipleline for " << qid <<
                " . No Redis Connection");
	    qo.set_error_string("No Redis Connection");
            QUERY_OBJECT_SEND(qo);
            return;
        }

        string key = "QUERY:" + qid;
        redisReply * reply = (redisReply *) redisCommand(c, "hgetall %s",
            key.c_str());
       
        map<string,string> terms; 
        if (!(c->err) && (reply->type == REDIS_REPLY_ARRAY)) {
            for (uint32_t i=0; i<reply->elements; i+=2) {
                string idx(reply->element[i]->str);
                string val(reply->element[i+1]->str);
                terms[idx] = val;
            }
        } else {
            QE_LOG_NOQID(ERROR, "Cannot start Pipleline for " << qid << 
                ". Could not read query input");
            freeReplyObject(reply);
            redisFree(c);
            QueryError(qid, 5);
	    qo.set_error_string("Cannot read query input");
            QUERY_OBJECT_SEND(qo);
            return;
        }

        freeReplyObject(reply);
        redisFree(c);

        QueryEngine::QueryParams qp(qid, terms, max_tasks_,
            UTCTimestampUsec());
       
        vector<uint64_t> chunk_size;
        bool need_merge;
        bool map_output;
        string table;
        string where;
        string select;
        string post;
        uint64_t time_period;

        int ret = qosp_->qe_->QueryPrepare(qp, chunk_size, need_merge, map_output,
            where, select, post, time_period, table);

        if (ret!=0) {
            QueryError(qid, ret);
            QE_LOG_NOQID(ERROR, "Cannot start Pipleline for " << qid << 
                ". Query Parsing Error " << ret);
	    qo.set_error_string("Query parse error");
            QUERY_OBJECT_SEND(qo);
            return;
        } else {
            QE_LOG_NOQID(INFO, "Chunks: " << chunk_size.size() <<
                " Need Merge: " << need_merge);
        }

        shared_ptr<Input> inp(new Input());
        inp.get()->hostname = hostname_;
        inp.get()->qp = qp;
        inp.get()->map_output = map_output;
        inp.get()->need_merge = need_merge;
        inp.get()->chunk_size = chunk_size;
        inp.get()->where = where;
        inp.get()->select = select;
        inp.get()->post = post;
        inp.get()->time_period = time_period;
        inp.get()->table = table;
        inp.get()->chunk_q = 0;

        vector<pair<int,int> > tinfo;
        for (uint idx=0; idx<(uint)max_tasks_; idx++) {
            tinfo.push_back(make_pair(0, -1));
        }

        QEPipeT  * wp = new QEPipeT(
            new WorkStage<Input, Stage0Merge,
                    RawResultT, Stage0Out>(
                tinfo,
                boost::bind(&QEOpServerImpl::QueryExec, this, _1,_2,_3,_4),
                boost::bind(&QEOpServerImpl::QueryMerge, this, _1,_2,_3)),
            new WorkStage<Stage0Merge, Output,
                    RedisT>(
                list_of(make_pair(0,-1))(make_pair(0,-1)),
                boost::bind(&QEOpServerImpl::QueryResp, this, _1,_2,_3,_4)));

        pipes_.insert(make_pair(qid, wp));
        int conn = LeastLoadedConnection();
        npipes_[conn]++;
        
        // The cnum with index 0 is only used for receiving new queries
        inp.get()->cnum = conn+1; 

        wp->Start(boost::bind(&QEOpServerImpl::QEPipeCb, this, wp, _1), inp);
        QE_LOG_NOQID(DEBUG, "Starting Pipeline for " << qid << " , " << conn+1 << 
            " conn, " << tinfo.size() << " tasks");
        
        // Update query status
        RedisAsyncConnection * rac = conns_[inp.get()->cnum].get();
        string rkey = "REPLY:" + qid;
        char stat[40];
        sprintf(stat,"{\"progress\":15}");
        RedisAsyncArgCommand(rac, NULL, 
            list_of(string("RPUSH"))(rkey)(stat));


    }

    void ConnUp(uint8_t cnum) {
        QE_LOG_NOQID(DEBUG, "ConnUp.. UP " << cnum);
        qosp_->evm_->io_service()->post(
                boost::bind(&QEOpServerImpl::ConnUpPostProcess,
                        this, cnum));
    }

    void ConnDown(uint8_t cnum) {
        QE_LOG_NOQID(DEBUG, "ConnDown.. DOWN.. Reconnect.." << cnum);
        connState_[cnum] = false;
        qosp_->evm_->io_service()->post(boost::bind(&RedisAsyncConnection::RAC_Connect,
            conns_[cnum].get()));
    }

    void CallbackProcess(uint8_t cnum, const redisAsyncContext *c, void *r, void *privdata) {

        //QE_TRACE_NOQID(DEBUG, "Redis CB" << cnum);
        if (0 == cnum) {
            if (r == NULL) {
                QE_LOG_NOQID(DEBUG,  __func__ << ": received NULL reply from redis");
                return;
            }

            redisReply reply = *reinterpret_cast<redisReply*>(r);
            QE_ASSERT(reply.type == REDIS_REPLY_STRING);
            string qid(reply.str);

            StartPipeline(qid);

            qosp_->evm_->io_service()->post(
                    boost::bind(&QEOpServerImpl::ConnUpPostProcess,
                    this, cnum));
            return;
        }

        auto_ptr<RedisT> fullReply;
        vector<string> elements;

        if (r == NULL) {
            //QE_TRACE_NOQID(DEBUG, "NULL Reply...\n");
        } else {
            redisReply reply = *reinterpret_cast<redisReply*>(r);
            fullReply.reset(new RedisT);
            fullReply.get()->first = reply;
            if (reply.type == REDIS_REPLY_ARRAY) {
                for (uint32_t i=0; i<reply.elements; i++) {
                    string element(reply.element[i]->str);
                    fullReply.get()->second.push_back(element);
                }
            } else if (reply.type == REDIS_REPLY_STRING) {
                fullReply.get()->second.push_back(string(reply.str));
            }
        }
        if (!privdata) {
            //QE_TRACE_NOQID(DEBUG, "Ignoring redis reply");
            return;
        }      
        ExternalProcIf<RedisT> * rpi = 
                reinterpret_cast<ExternalProcIf<RedisT> *>(privdata);
        QE_TRACE_NOQID(DEBUG,  " Rx data from REDIS for " << rpi->Key());
                    
        rpi->Response(fullReply);

    }
    
    QEOpServerImpl(const string & redis_host, uint16_t port, QEOpServerProxy * qosp,
                   int max_tasks) :
            hostname_(boost::asio::ip::host_name()),
            redis_host_(redis_host),
            port_(port),
            qosp_(qosp),
            max_tasks_(max_tasks) {
        for (int i=0; i<kConnections+1; i++) {
            cb_proc_fn_[i] = boost::bind(&QEOpServerImpl::CallbackProcess,
                    this, i, _1, _2, _3);
            connState_[i] = false;
            if (i)
                conns_[i].reset(rac_alloc(qosp->evm_, redis_host_, port,
                        boost::bind(&QEOpServerImpl::ConnUp, this, i),
                        boost::bind(&QEOpServerImpl::ConnDown, this, i)));
            else
                conns_[i].reset(rac_alloc_nocheck(qosp->evm_, redis_host_, port,
                        boost::bind(&QEOpServerImpl::ConnUp, this, i),
                        boost::bind(&QEOpServerImpl::ConnDown, this, i)));
              
            // The cnum with index 0 is only used for receiving new queries
            // It does not host any pipelines
            if (i) npipes_[i-1] = 0;
        }
    }

    ~QEOpServerImpl() {
    }
private:

    static const int kMaxRowThreshold = 10000;

    // We always have one connection to receive new queries from OpServer
    // This is the number of addition connections, which will be 
    // used to read query parameters and write query results
    static const uint8_t kConnections = 4;

    const string hostname_;
    const string redis_host_;
    const unsigned short port_;
    QEOpServerProxy * const qosp_;
    boost::shared_ptr<RedisAsyncConnection> conns_[kConnections+1];
    RedisAsyncConnection::ClientAsyncCmdCbFn cb_proc_fn_[kConnections+1];
    bool connState_[kConnections+1];

    tbb::mutex mutex_;
    map<string,QEPipeT*> pipes_;
    int npipes_[kConnections];
    int max_tasks_;

};

QEOpServerProxy::QEOpServerProxy(EventManager *evm, QueryEngine *qe,
            const string & hostname, uint16_t port, int max_tasks) :
        evm_(evm),
        qe_(qe),
        impl_(new QEOpServerImpl(hostname, port, this, max_tasks)) {}

QEOpServerProxy::~QEOpServerProxy() {}

void
QEOpServerProxy::QueryResult(void * qid, QPerfInfo qperf,
        auto_ptr<BufferT> res, auto_ptr<OutRowMultimapT> mres) {
        
    impl_->QECallback(qid, qperf, res, mres);
}


