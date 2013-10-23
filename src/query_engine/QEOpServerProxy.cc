/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <tbb/mutex.h>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/tuple/tuple.hpp>
#include "base/util.h"
#include "base/logging.h"
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
#include "analytics_cpuinfo_types.h"

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

typedef pair<int,shared_ptr<QEOpServerProxy::BufferT> > RawResultT; 
typedef pair<redisReply,vector<string> > RedisT;

bool RedisAsyncArgCommand(RedisAsyncConnection * rac,
            void *rpi, const vector<string>& args) {

    return rac->RedisAsyncArgCmd(rpi, args);
}

class QEOpServerProxy::QEOpServerImpl {
public:
    typedef std::vector<std::string> QEOutputT;

    static const int nMaxChunks = 16;

    struct Input {
        int cnum;
        string hostname;
        QueryEngine::QueryParams qp;
        vector<uint64_t> chunk_size;
        bool need_merge;
    };

    void QueryJsonify(const BufferT* raw_res, QEOutputT* raw_json) {

        vector<OutRowT>::iterator res_it;
        std::vector<query_column>  columns;

        if (!raw_res->first.size()) return;
        bool found = false;
        for(size_t i = 0; i < g_viz_constants._TABLES.size(); i++)
        {
            if (g_viz_constants._TABLES[i].name == raw_res->first) {
                found = true;
                columns = g_viz_constants._TABLES[i].schema.columns;
            }
        }
        if (!found) {
            for (std::map<std::string, objtable_info>::const_iterator it =
                    g_viz_constants._OBJECT_TABLES.begin();
                    it != g_viz_constants._OBJECT_TABLES.end(); it++) {
                if (it->first == raw_res->first) {
                    found = true;
                    columns = g_viz_constants._OBJECT_TABLE_SCHEMA.columns;
                }
            }
        }
        assert(found);

        //jsonresult.reset(new QEOpServerProxy::BufferT);
        //QEOpServerProxy::BufferT *raw_json = jsonresult_.get();

        vector<OutRowT>* raw_result = const_cast<vector<OutRowT>*>(&raw_res->second);
        for (res_it = raw_result->begin(); res_it != raw_result->end(); ++res_it) {
            std::map<std::string, std::string>::iterator map_it;
            rapidjson::Document dd;
            dd.SetObject();

            for (map_it = (*res_it).begin(); map_it != (*res_it).end(); ++map_it) {
                // search for column name in the schema
                bool found = false;
                for (size_t j = 0; j < columns.size(); j++)
                {
                    if (columns[j].name == map_it->first)
                    {
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
                            uint32_t ipaddr;

                            stringToInteger(map_it->second, ipaddr);
                            ipaddr = htonl(ipaddr);
                            inet_ntop(AF_INET, &(ipaddr), str, INET_ADDRSTRLEN);
                            map_it->second = str;

                            val.SetString(map_it->second.c_str(), map_it->second.size());
                            dd.AddMember(map_it->first.c_str(), val, dd.GetAllocator());
                        } else {
                            rapidjson::Value val(rapidjson::kNumberType);
                            unsigned long num;
                            stringToInteger(map_it->second, num);
                            val.SetUint64(num);
                            dd.AddMember(map_it->first.c_str(), val, dd.GetAllocator());
                        }
                        found = true;
                    }
                }
                assert(found);
            }
            rapidjson::StringBuffer sb;
            rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
            dd.Accept(writer);
            raw_json->push_back(sb.GetString());
        }
    }

    void QECallback(void * qid, int error, auto_ptr<QEOpServerProxy::BufferT> res) {

        RawResultT* raw(new RawResultT);
        raw->first = error;
        raw->second = res;

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
        shared_ptr<BufferT> result;
    };
    ExternalBase::Efn QueryExec(uint32_t inst, const vector<RawResultT*> & exts,
            const Input & inp, Stage0Out & res) { 
        uint32_t step = exts.size();
        if (!step) {
            res.inp = inp;
            string key = "QUERY:" + res.inp.qp.qid;

            // Update query status
            RedisAsyncConnection * rac = conns_[res.inp.cnum].get();
            string rkey = "REPLY:" + res.inp.qp.qid;
            char stat[40];
            sprintf(stat,"{\"progress\":15}");
            RedisAsyncArgCommand(rac, NULL, 
                list_of(string("RPUSH"))(rkey)(stat));

            res.result = shared_ptr<BufferT>(new BufferT());

            // TODO : The chunk number should be picked off a queue
            //        This queue may be part of the input for this stage
            return boost::bind(&QueryEngine::QueryExec, qosp_->qe_,
                    _1,
                    inp.qp,
                    inst);
        } else {
            QE_ASSERT(step==1);
            res.ret_code = (exts[0]->first == 0) ? true : false;
            if (res.ret_code) {
                res.result->first = exts[0]->second->first;
                if (inp.need_merge) {
                    res.ret_code =
                        qosp_->qe_->QueryAccumulate(inp.qp, *(exts[0]->second), *(res.result));
                } else {
                    // TODO : When merge is not needed, we can just send
                    //        a result upto redis at this point.
                    res.result->second.insert(res.result->second.begin(),
                        exts[0]->second->second.begin(),
                        exts[0]->second->second.end());
                }
            }
        }
        return NULL;
    }

    struct Stage0Merge {
        Input inp;
        bool ret_code;
        uint32_t fm_time;
        BufferT result;
    };
    bool QueryMerge(const std::vector<boost::shared_ptr<Stage0Out> > & subs,
           const boost::shared_ptr<Input> & inp, Stage0Merge & res) {

        res.ret_code = true;
        res.inp = subs[0]->inp;
        res.fm_time = 0;
        
        std::vector<boost::shared_ptr<QEOpServerProxy::BufferT> > qsubs;
        for (vector<shared_ptr<Stage0Out> >::const_iterator it = subs.begin() ;
                it!=subs.end(); it++) {
            if ((*it)->ret_code == false) {
                res.ret_code = false;
            } else {
                qsubs.push_back((*it)->result);
            }
        }
        if (!res.ret_code) return true;

        if (res.inp.need_merge) {
            uint64_t then = UTCTimestampUsec();
            res.ret_code = qosp_->qe_->QueryFinalMerge(res.inp.qp, qsubs, res.result);
            uint64_t now = UTCTimestampUsec();
            res.fm_time = static_cast<uint32_t>((now - then)/1000);
        } else {
            res.result.first = string();
            // TODO : If a merge was not needed, results have been sent to 
            //        redis already. The only thing still needed is the status
            for (vector<shared_ptr<Stage0Out> >::const_iterator it = subs.begin() ;
                    it!=subs.end(); it++) {
                if ((*it)->result->first.size())
                    res.result.first = (*it)->result->first;
                res.result.second.insert(res.result.second.begin(),
                    (*it)->result->second.begin(),
                    (*it)->result->second.end());
            }
        }
        return true;
    }

    struct Output {
        Input inp;
        uint32_t redis_time;
        uint32_t fm_time;
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
                    // TODO : QueryMergeFinal needs to be called at this point
                    //        For now, I'm assuming a single chunk
                    QueryJsonify(&inp.result, jsonresult.get());
                        
                    vector<string> const * const res = jsonresult.get();
                    vector<string>::size_type idx = 0;
                    uint32_t rownum = 0;
                    
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
                            sprintf(stat,"{\"progress\":80, \"lines\":%d}",
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
                    ret.fm_time = inp.fm_time;
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

                    uint64_t enqtm = atol(ret.inp.qp.terms["enqueue_time"].c_str());
                    uint32_t enq_delay = static_cast<uint32_t>(
                            (ret.inp.qp.query_starttm - enqtm)/1000);
                    qpi.set_enq_delay(enq_delay);
                    if (g_viz_constants.COLLECTOR_GLOBAL_TABLE == inp.result.first) {
                        qpi.set_log_query_time(qtime);
                        qpi.set_log_query_rows(static_cast<uint32_t>(inp.result.second.size()));
                    } else if ((g_viz_constants.FLOW_TABLE == inp.result.first) ||
                            (g_viz_constants.FLOW_SERIES_TABLE == inp.result.first)) {
                        qpi.set_flow_query_time(qtime);
                        qpi.set_flow_query_rows(static_cast<uint32_t>(inp.result.second.size()));                                
                    } else {
                        qpi.set_object_query_time(qtime);
                        qpi.set_object_query_rows(static_cast<uint32_t>(inp.result.second.size()));                        
                    }
                    QueryPerfInfoTrace::Send(qpi);

		    QueryObjectData qo;
		    qo.set_qid(ret.inp.qp.qid);
		    qo.set_table(inp.result.first);
		    qo.set_ops_start_ts(enqtm);
		    qo.set_qed_start_ts(ret.inp.qp.query_starttm);
		    qo.set_qed_end_ts(now);
		    qo.set_flow_query_rows(static_cast<uint32_t>(inp.result.second.size()));
		    QUERY_OBJECT_SEND(qo);

                    //g_viz_constants.COLLECTOR_GLOBAL_TABLE 
                    QE_LOG_NOQID(INFO, "Finished: QID " << ret.inp.qp.qid <<
                        " Table " << inp.result.first <<
                        " Time(ms) " << qtime <<
                        " RedisTime(ms) " << ret.redis_time <<
                        " MergeTime(ms) " << ret.fm_time <<
                        " Rows " << inp.result.second.size() <<
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

        QueryEngine::QueryParams qp(qid, terms, nMaxChunks,
            UTCTimestampUsec());
       
        vector<uint64_t> chunk_size;
        bool need_merge;
        int ret = qosp_->qe_->QueryPrepare(qp, chunk_size, need_merge);

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
        inp.get()->need_merge = need_merge;
        inp.get()->chunk_size = chunk_size;
  
        vector<pair<int,int> > tinfo;
        for (uint idx=0; idx<chunk_size.size(); idx++) {
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
        QE_LOG_NOQID(DEBUG, "Starting Pipeline for " << qid << " , " << conn+1 << " conn");
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
    
    QEOpServerImpl(const string & redis_host, uint16_t port, QEOpServerProxy * qosp) :
            hostname_(boost::asio::ip::host_name()),
            redis_host_(redis_host),
            port_(port),
            qosp_(qosp) {
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


};

QEOpServerProxy::QEOpServerProxy(EventManager *evm, QueryEngine *qe,
            const string & hostname, uint16_t port) :
        evm_(evm),
        qe_(qe),
        impl_(new QEOpServerImpl(hostname, port, this)) {}

QEOpServerProxy::~QEOpServerProxy() {}

void
QEOpServerProxy::QueryResult(void * qid, int error, auto_ptr<BufferT> res) {
    impl_->QECallback(qid, error, res);
}


