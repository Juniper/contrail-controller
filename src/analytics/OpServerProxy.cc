/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "viz_collector.h"
#include "viz_constants.h"
#include "OpServerProxy.h"
#include <tbb/mutex.h>
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include "base/util.h"
#include "base/logging.h"
#include "base/parse_object.h"
#include <cstdlib>
#include <utility>
#include "hiredis/hiredis.h"
#include "hiredis/base64.h"
#include "hiredis/boostasio.hpp"
#include <sandesh/sandesh.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>

#include "rapidjson/document.h"

#include "redis_connection.h"
#include "redis_processor_vizd.h"
#include "viz_sandesh.h"
#include "viz_collector.h"

using std::string;
using boost::assign::list_of;
using boost::system::error_code;

class OpServerProxy::OpServerImpl {
    public:
        enum RacConnType {
            RAC_CONN_TYPE_INVALID = 0,
            RAC_CONN_TYPE_TO_OPS = 1,
            RAC_CONN_TYPE_FROM_OPS = 2,
        };

        void ToOpsConnUpPostProcess() {
            processor_cb_proc_fn = boost::bind(&OpServerImpl::processorCallbackProcess, this, _1, _2, _3);
            to_ops_conn_.get()->SetClientAsyncCmdCb(processor_cb_proc_fn);

            string module = g_vns_constants.ModuleNames.find(Module::COLLECTOR)->second;
            VizSandeshContext * vsc = static_cast<VizSandeshContext *>(Sandesh::client_context());
            string source;
            if (vsc)
                source = vsc->Analytics()->name();
            else 
                source = Sandesh::source();
            
            if (!started_) {
                RedisProcessorExec::SyncDeleteUVEs(redis_ip_, redis_port_,
                    source, module, "", 0);
                started_=true;
            }
            if (collector_) 
                collector_->RedisUpdate(true);
        }

        void ToOpsConnUp() {
            LOG(DEBUG, "ToOpsConnUp.. UP");
            evm_->io_service()->post(boost::bind(&OpServerProxy::OpServerImpl::ToOpsConnUpPostProcess, this));
        }

        void FromOpsConnUpPostProcess() {
            analytics_cb_proc_fn = boost::bind(&OpServerImpl::analyticsCallbackProcess, this, _1, _2, _3);
            from_ops_conn_.get()->SetClientAsyncCmdCb(analytics_cb_proc_fn);
            from_ops_conn_.get()->RedisAsyncCommand(NULL, "SUBSCRIBE analytics");
        }

        void FromOpsConnUp() {
            LOG(DEBUG, "FromOpsConnUp.. UP");

            evm_->io_service()->post(boost::bind(&OpServerProxy::OpServerImpl::FromOpsConnUpPostProcess, this));
        }

        void RAC_ConnectProcess(RacConnType type) {
            if (type == RAC_CONN_TYPE_TO_OPS) {
                LOG(DEBUG, "Retry Connect to FromOpsConn");
                to_ops_conn_.get()->RAC_Connect();
            } else if (type == RAC_CONN_TYPE_FROM_OPS) {
                from_ops_conn_.get()->RAC_Connect();
            }
        }

        void ToOpsConnDown() {
            LOG(DEBUG, "ToOpsConnDown.. DOWN.. Reconnect..");
            collector_->RedisUpdate(false);
            evm_->io_service()->post(boost::bind(&OpServerProxy::OpServerImpl::RAC_ConnectProcess,
                        this, RAC_CONN_TYPE_TO_OPS));
        }

        void FromOpsConnDown() {
            LOG(DEBUG, "FromOpsConnDown.. DOWN.. Reconnect..");
            evm_->io_service()->post(boost::bind(&OpServerProxy::OpServerImpl::RAC_ConnectProcess,
                        this, RAC_CONN_TYPE_FROM_OPS));
        }

        void processorCallbackProcess(const redisAsyncContext *c, void *r, void *privdata) {
            redisReply *reply = (redisReply*)r;
            RedisProcessorIf * rpi = NULL;

            if (privdata)
                rpi = reinterpret_cast<RedisProcessorIf *>(privdata);

            if (reply == NULL) {
                LOG(DEBUG, "NULL Reply...\n");
                return;
            }

            if (rpi) {
                rpi->ProcessCallback(reply);
            }

        }


        void analyticsCallbackProcess(const redisAsyncContext *c, void *r, void *privdata) {
            redisReply *reply = (redisReply*)r;

            LOG(DEBUG, "Received data on analytics channel from REDIS...\n");

            if (reply == NULL) {
                LOG(DEBUG, "NULL Reply...\n");
                return;
            }

            if (reply->type == REDIS_REPLY_ARRAY) {
                LOG(DEBUG, "REDIS_REPLY_ARRAY == " << reply->elements);
                int i;
                for (i = 0; i < (int)reply->elements; i++) {
                    if (reply->element[i]->type == REDIS_REPLY_STRING) {
                        LOG(DEBUG, "Element" << i << "== " << reply->element[i]->str);
                    } else {
                        LOG(DEBUG, "Element" << i << " type == " << reply->element[i]->type);
                    }
                }
            } else if (reply->type == REDIS_REPLY_STRING) {
                LOG(DEBUG, "REDIS_REPLY_STRING == " << reply->str);
                return;
            } else {
                LOG(DEBUG, "reply->type == " << reply->type);
                return;
            }

            assert(reply->type == REDIS_REPLY_ARRAY);
            assert(reply->elements == 3);

            if (!strncmp(reply->element[0]->str, "subscribe", strlen("subscribe"))) {
                /* nothing to do, return */
                return;
            }

            assert(!strncmp(reply->element[0]->str, "message", strlen("message")));
            assert(!strncmp(reply->element[1]->str, "analytics", strlen("analytics")));
            assert(reply->element[2]->type == REDIS_REPLY_STRING);
            std::string message = base64_decode(reply->element[2]->str);
            //std::string message(reply->element[2]->str);

            LOG(DEBUG, "message ==" << reply->element[2]->str);

            rapidjson::Document document;	// Default template parameter uses UTF8 and MemoryPoolAllocator.
            if (document.ParseInsitu<0>(reply->element[2]->str).HasParseError()) {
                assert(0);
            }
            assert(document.HasMember("type"));
            assert(document["type"].IsString());

            assert(document.HasMember("destination"));
            assert(document["destination"].IsString());
            std::string destination(document["destination"].GetString());

            assert(document.HasMember("message"));
            assert(document["message"].IsString());
            std::string enc_sandesh(document["message"].GetString());

            std::string dec_sandesh = base64_decode(enc_sandesh);
            //std::string dec_sandesh(enc_sandesh);

            LOG(DEBUG, "decoded sandesh_message ==" << dec_sandesh);

            collector_->SendRemote(destination, dec_sandesh);
        }

        RedisAsyncConnection *to_ops_conn() {
            return (to_ops_conn_.get());
        }

        RedisAsyncConnection *from_ops_conn() {
            return (from_ops_conn_.get());
        }

        OpServerImpl(EventManager *evm, VizCollector *collector,
                const std::string & redis_ip, unsigned short redis_port) :
            evm_(evm),
            collector_(collector),
            started_(false),
            analytics_cb_proc_fn(NULL),
            processor_cb_proc_fn(NULL),
            redis_ip_(redis_ip),
            redis_port_(redis_port) {
                to_ops_conn_.reset(new RedisAsyncConnection(evm, redis_ip, redis_port,
                            boost::bind(&OpServerProxy::OpServerImpl::ToOpsConnUp, this),
                            boost::bind(&OpServerProxy::OpServerImpl::ToOpsConnDown, this)));
                to_ops_conn_.get()->RAC_Connect();

                from_ops_conn_.reset(new RedisAsyncConnection(evm, redis_ip,
                            redis_port, boost::bind(&OpServerProxy::OpServerImpl::FromOpsConnUp, this),
                            boost::bind(&OpServerProxy::OpServerImpl::FromOpsConnDown, this)));
                from_ops_conn_.get()->RAC_Connect();
            }

        ~OpServerImpl() {
        }

    private:
        /* these are made public, so they are accessed by OpServerProxy */
        EventManager *evm_;
        VizCollector *collector_;
        int gen_timeout_;
        bool started_;
        boost::scoped_ptr<RedisAsyncConnection> to_ops_conn_;
        boost::scoped_ptr<RedisAsyncConnection> from_ops_conn_;
        RedisAsyncConnection::ClientAsyncCmdCbFn analytics_cb_proc_fn;
        RedisAsyncConnection::ClientAsyncCmdCbFn processor_cb_proc_fn;
    public:
        std::string redis_ip_;
        unsigned short redis_port_;
};

OpServerProxy::OpServerProxy(EventManager *evm, VizCollector *collector,
        const std::string & redis_ip, unsigned short redis_port, int gen_timeout) :
            gen_timeout_(gen_timeout) {
    impl_ = new OpServerImpl(evm, collector, redis_ip, redis_port);
}

OpServerProxy::~OpServerProxy() {
    if (impl_)
        delete impl_;
}

bool
OpServerProxy::UVEUpdate(const std::string &type, const std::string &attr,
                       const std::string &source, const std::string &module,
                       const std::string &key, const std::string &message,
                       int32_t seq, const std::string& agg, 
                       const std::string& atyp, int64_t ts) {

    if ((!impl_->to_ops_conn()) || (!impl_->to_ops_conn()->IsConnUp()))
        return false;

    RedisProcessorExec::UVEUpdate(impl_->to_ops_conn(), NULL, type, attr,
            source, module, key, message, seq, agg, atyp, ts);

    return true;
}

bool
OpServerProxy::UVEDelete(const std::string &type,
                       const std::string &source, const std::string &module,
                       const std::string &key, int32_t seq) {

    if ((!impl_->to_ops_conn()) || (!impl_->to_ops_conn()->IsConnUp()))
        return false;

    RedisProcessorExec::UVEDelete(impl_->to_ops_conn(), NULL, type, source, 
            module, key, seq);

    return true;
}

bool 
OpServerProxy::GetSeq(const string &source, const string &module, 
        std::map<std::string,int32_t> & seqReply) {
    if (!impl_->to_ops_conn()) return false;
    VizSandeshContext * vsc = static_cast<VizSandeshContext *>(Sandesh::client_context());
    string coll;
    if (vsc)
        coll = vsc->Analytics()->name();
    else 
        coll = Sandesh::source();

    return RedisProcessorExec::SyncGetSeq(impl_->redis_ip_, impl_->redis_port_,
            source, module, coll, gen_timeout_, seqReply);
}

bool 
OpServerProxy::DeleteUVEs(const string &source, const string &module) {
    if (!impl_->to_ops_conn()) return false;

    VizSandeshContext * vsc = static_cast<VizSandeshContext *>(Sandesh::client_context());
    string coll;
    if (vsc)
        coll = vsc->Analytics()->name();
    else 
        coll = Sandesh::source();

    return RedisProcessorExec::SyncDeleteUVEs(impl_->redis_ip_, impl_->redis_port_,
            source, module, coll, gen_timeout_);
}

bool 
OpServerProxy::GeneratorCleanup(GenCleanupReply gcr) {
    if (!impl_->to_ops_conn()) return false;

    GenCleanupReq * dr = new GenCleanupReq(impl_->to_ops_conn(), 
            boost::bind(gcr, _2));
    return dr->RedisSend();
}

bool
OpServerProxy::RefreshGenerator(const std::string &source, const std::string &module) {
    if ((!impl_->to_ops_conn()) || (!impl_->to_ops_conn()->IsConnUp()))
        return false;

    if (!gen_timeout_) return true;

    VizSandeshContext * vsc = static_cast<VizSandeshContext *>(Sandesh::client_context());
    string coll;
    if (vsc)
        coll = vsc->Analytics()->name();
    else 
        coll = Sandesh::source();

    RedisProcessorExec::RefreshGenerator(impl_->to_ops_conn(), source, module, coll, gen_timeout_); 

    return true;    
}

bool
OpServerProxy::WithdrawGenerator(const std::string &source, const std::string &module) {
    if ((!impl_->to_ops_conn()) || (!impl_->to_ops_conn()->IsConnUp()))
        return false;

    VizSandeshContext * vsc = static_cast<VizSandeshContext *>(Sandesh::client_context());
    string coll;
    if (vsc)
        coll = vsc->Analytics()->name();
    else 
        coll = Sandesh::source();

    RedisProcessorExec::WithdrawGenerator(impl_->to_ops_conn(), source, module, coll);

    return true;
}

