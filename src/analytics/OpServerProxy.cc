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
#include <librdkafka/rdkafkacpp.h>
#include <sandesh/sandesh.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>

#include "rapidjson/document.h"
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <base/connection_info.h>
#include "redis_connection.h"
#include "redis_processor_vizd.h"
#include "viz_sandesh.h"
#include "viz_collector.h"

using std::string;
using boost::shared_ptr;
using boost::assign::list_of;
using boost::system::error_code;
using process::ConnectionState;
using process::ConnectionType;
using process::ConnectionStatus;

class KafkaDeliveryReportCb : public RdKafka::DeliveryReportCb {
 public:
  void dr_cb (RdKafka::Message &message) {
    if (message.err() != RdKafka::ERR_NO_ERROR) {
        LOG(ERROR, "Message delivery for " << message.key() << " " <<
            message.errstr() << " gen " <<
            string((char *)(message.msg_opaque())));
    }
    char * cc = (char *)message.msg_opaque();
    delete[] cc;
  }
};


class KafkaEventCb : public RdKafka::EventCb {
 public:
  void event_cb (RdKafka::Event &event) {
    switch (event.type())
    {
      case RdKafka::Event::EVENT_ERROR:
        LOG(ERROR, RdKafka::err2str(event.err()) << " : " << event.str());
        if (event.err() == RdKafka::ERR__ALL_BROKERS_DOWN) assert(0);
        break;

      case RdKafka::Event::EVENT_LOG:
        LOG(INFO, "LOG-" << event.severity() << "-" << event.fac().c_str() <<
            ": " << event.str().c_str());
        break;

      default:
        LOG(INFO, "EVENT " << event.type() <<
            " (" << RdKafka::err2str(event.err()) << "): " <<
            event.str());
        break;
    }
  }
};

static inline unsigned int djb_hash (const char *str, size_t len) {
    unsigned int hash = 5381;
    for (size_t i = 0 ; i < len ; i++)
        hash = ((hash << 5) + hash) + str[i];
    return hash;
}

class OpServerProxy::OpServerImpl {
    public:
        enum RacConnType {
            RAC_CONN_TYPE_INVALID = 0,
            RAC_CONN_TYPE_TO_OPS = 1,
            RAC_CONN_TYPE_FROM_OPS = 2,
        };

        enum RacStatus {
            RAC_UP = 1,
            RAC_DOWN = 2
        };
        const unsigned int partitions_;

        static const char* RacStatusToString(RacStatus status) {
            switch(status) {
            case RAC_UP:
                return "Connected";
            case RAC_DOWN:
                return "Disconnected";
            default:
                return "Invalid";
            }
        }

        string name(void) { return collector_->name(); }

        void KafkaPub(unsigned int pt,
                          const string& skey,
                          const string& gen,
                          const string& value) {

            char* gn = new char[gen.length()+1];
            strcpy(gn,gen.c_str());

            if (producer_) {
                // Key in Kafka Topic includes UVE Key, Type
                producer_->produce(topic_[pt].get(), 0, 
                    RdKafka::Producer::MSG_COPY,
                    const_cast<char *>(value.c_str()), value.length(),
                    &skey, (void *)gn);
                }
        }

        struct RedisInfo {
            RedisInfo(const std::string& redis_ip, 
                      unsigned short redis_port) {
                rinfo_.set_ip(redis_ip);
                rinfo_.set_port(redis_port);
                rinfo_.set_status(RacStatusToString(RAC_DOWN));
                rinfo_.set_delete_failed(0);
                rinfo_.set_delete_succeeded(0);
                rinfo_.set_delete_no_conn(0);
                rinfo_.set_update_failed(0);
                rinfo_.set_update_succeeded(0);
                rinfo_.set_update_no_conn(0);
                rinfo_.set_conn_cb_succeeded(0);
                rinfo_.set_conn_cb_failed(0);
                rinfo_.set_conn_cb_null(0);
                rinfo_.set_conn_call_disconnected(0);
                rinfo_.set_conn_call_succeeded(0);
                rinfo_.set_conn_call_failed(0);
            }

            void RedisUveUpdate() {
                rinfo_.set_update_succeeded(rinfo_.get_update_succeeded()+1);
            }
            void RedisUveUpdateFail() {
                rinfo_.set_update_failed(rinfo_.get_update_failed()+1);
            }
            void RedisUveUpdateNoConn() {
                rinfo_.set_update_no_conn(rinfo_.get_update_no_conn()+1);
            }
            void RedisUveDelete() {
                rinfo_.set_delete_succeeded(rinfo_.get_delete_succeeded()+1);
            }
            void RedisUveDeleteFail() {
                rinfo_.set_delete_failed(rinfo_.get_delete_failed()+1);
            }
            void RedisUveDeleteNoConn() {
                rinfo_.set_delete_no_conn(rinfo_.get_delete_no_conn()+1);
            }

            void RedisStatusUpdate(RacStatus connection_status) {
                rinfo_.set_status(RacStatusToString(connection_status));
            }

            const std::string& GetIp() {
                return rinfo_.get_ip();
            }

            unsigned short GetPort() {
                return rinfo_.get_port();
            }
            
            RedisUveInfo rinfo_;
        };

        void FillRedisUVEInfo(RedisUveInfo& redis_uve_info) {
            tbb::mutex::scoped_lock lock(rac_mutex_); 
            redis_uve_info = redis_uve_.rinfo_;
            if (to_ops_conn_) {
                redis_uve_info.set_conn_call_disconnected(to_ops_conn_->CallDisconnected());
                redis_uve_info.set_conn_call_failed(to_ops_conn_->CallFailed());
                redis_uve_info.set_conn_call_succeeded(to_ops_conn_->CallSucceeded()); 
                redis_uve_info.set_conn_cb_null(to_ops_conn_->CallbackNull());
                redis_uve_info.set_conn_cb_failed(to_ops_conn_->CallbackFailed());
                redis_uve_info.set_conn_cb_succeeded(to_ops_conn_->CallbackSucceeded());
            }
        }

        void ToOpsConnUpPostProcess() {
            processor_cb_proc_fn = boost::bind(&OpServerImpl::processorCallbackProcess, this, _1, _2, _3);
            to_ops_conn_.get()->SetClientAsyncCmdCb(processor_cb_proc_fn);

            string module = Sandesh::module();
            string source = Sandesh::source();
            string instance_id = Sandesh::instance_id();
            string node_type = Sandesh::node_type();
            
            if (!started_) {
                RedisProcessorExec::FlushUVEs(redis_uve_.GetIp(),
                                              redis_uve_.GetPort(),
                                              redis_password_);
                started_=true;
            }
            if (collector_) 
                collector_->RedisUpdate(true);
        }

        void toConnectCallbackProcess(const redisAsyncContext *c, void *r, void *privdata) {
           //Handle the AUTH callback
            redisReply reply = *reinterpret_cast<redisReply*>(r);
            if (reply.type != REDIS_REPLY_ERROR) {
                {
                   tbb::mutex::scoped_lock lock(rac_mutex_);
                   redis_uve_.RedisStatusUpdate(RAC_UP);
                }
                ConnectionState::GetInstance()->Update(ConnectionType::REDIS,
                "To", ConnectionStatus::UP, to_ops_conn_->Endpoint(),
                std::string());
                evm_->io_service()->post(boost::bind(&OpServerProxy::OpServerImpl::ToOpsConnUpPostProcess, this));
                return;

            } else {
                LOG(ERROR,"In connectCallbackProcess.. Error");
                assert(reply.type != REDIS_REPLY_ERROR);
            }

       }

        void fromConnectCallbackProcess(const redisAsyncContext *c, void *r, void *privdata) {
            //Handle the AUTH callback
            redisReply reply = *reinterpret_cast<redisReply*>(r);
            if (reply.type != REDIS_REPLY_ERROR) {
                ConnectionState::GetInstance()->Update(ConnectionType::REDIS,
                "From", ConnectionStatus::UP, from_ops_conn_->Endpoint(),
                std::string());
                evm_->io_service()->post(boost::bind(&OpServerProxy::OpServerImpl::FromOpsConnUpPostProcess, this));
                return;

            } else {
                LOG(ERROR,"In connectCallbackProcess.. Error");
                assert(reply.type != REDIS_REPLY_ERROR);
            }

        }

        void ToOpsAuthenticated() {
            //Set callback for connection status
            to_ops_conn_.get()->SetClientAsyncCmdCb(boost::bind(
                                                        &OpServerImpl::toConnectCallbackProcess,
                                                         this, _1, _2, _3));
            if (!redis_password_.empty()) {
                //Call the AUTH command
                to_ops_conn_.get()->RedisAsyncCommand(NULL,"AUTH %s",redis_password_.c_str());
            } else {
                to_ops_conn_.get()->RedisAsyncCommand(NULL,"PING");
	    }
        }

        void FromOpsAuthenticated() {
            //Set callback for connection status
            from_ops_conn_.get()->SetClientAsyncCmdCb(boost::bind(
                                                        &OpServerImpl::fromConnectCallbackProcess,
                                                         this, _1, _2, _3));
            if (!redis_password_.empty()) {
                //Call the AUTH command
                from_ops_conn_.get()->RedisAsyncCommand(NULL,"AUTH %s",redis_password_.c_str());
            } else {
                from_ops_conn_.get()->RedisAsyncCommand(NULL,"PING");
            }

        }

        void ToOpsConnUp() {
            LOG(DEBUG, "ToOpsConnUp.. UP");
            evm_->io_service()->post(boost::bind(&OpServerProxy::OpServerImpl::ToOpsAuthenticated, this));
        }

        void FromOpsConnUpPostProcess() {
            analytics_cb_proc_fn = boost::bind(&OpServerImpl::analyticsCallbackProcess, this, _1, _2, _3);
            from_ops_conn_.get()->SetClientAsyncCmdCb(analytics_cb_proc_fn);
            from_ops_conn_.get()->RedisAsyncCommand(NULL, "SUBSCRIBE analytics");
        }

        void FromOpsConnUp() {
            LOG(DEBUG, "FromOpsConnUp.. UP");
            evm_->io_service()->post(boost::bind(&OpServerProxy::OpServerImpl::FromOpsAuthenticated, this));
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
            {
                tbb::mutex::scoped_lock lock(rac_mutex_);
                redis_uve_.RedisStatusUpdate(RAC_DOWN);
            }
            collector_->RedisUpdate(false);
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::REDIS,
                "To", ConnectionStatus::DOWN, to_ops_conn_->Endpoint(),
                std::string());
            evm_->io_service()->post(boost::bind(&OpServerProxy::OpServerImpl::RAC_ConnectProcess,
                        this, RAC_CONN_TYPE_TO_OPS));
        }

        void FromOpsConnDown() {
            LOG(DEBUG, "FromOpsConnDown.. DOWN.. Reconnect..");
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::REDIS,
                "From", ConnectionStatus::DOWN, from_ops_conn_->Endpoint(),
                std::string());
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

        shared_ptr<RedisAsyncConnection> to_ops_conn() {
            tbb::mutex::scoped_lock lock(rac_mutex_);
            return to_ops_conn_;
        }

        shared_ptr<RedisAsyncConnection> from_ops_conn() {
            tbb::mutex::scoped_lock lock(rac_mutex_);
            return from_ops_conn_;
        }
        bool KafkaTimer() {
            if (producer_) {
                producer_->poll(0);
            }
            return true;
        }

        const string get_redis_password() {
            return redis_password_;
        }

        OpServerImpl(EventManager *evm, VizCollector *collector,
                     const std::string redis_uve_ip, 
                     unsigned short redis_uve_port,
                     const std::string redis_password,
                     const std::string brokers,
                     const std::string topic, 
                     uint16_t partitions) :
            partitions_(partitions),
            redis_uve_(redis_uve_ip, redis_uve_port),
            evm_(evm),
            collector_(collector),
            started_(false),
            analytics_cb_proc_fn(NULL),
            processor_cb_proc_fn(NULL),
            redis_password_(redis_password),
            k_event_cb(),
            k_dr_cb(),
            kafka_timer_(TimerManager::CreateTimer(*evm->io_service(),
                         "Kafka Timer", 
                         TaskScheduler::GetInstance()->GetTaskId(
                         "Kafka Timer"))) {
            to_ops_conn_.reset(new RedisAsyncConnection(evm_, 
                redis_uve_ip, redis_uve_port, 
                boost::bind(&OpServerProxy::OpServerImpl::ToOpsConnUp, this),
                boost::bind(&OpServerProxy::OpServerImpl::ToOpsConnDown, this)));
            // Update connection 
            ConnectionState::GetInstance()->Update(ConnectionType::REDIS,
                "To", ConnectionStatus::INIT, to_ops_conn_->Endpoint(),
                std::string());
            to_ops_conn_.get()->RAC_Connect();
            from_ops_conn_.reset(new RedisAsyncConnection(evm_,
                redis_uve_ip, redis_uve_port,
                boost::bind(&OpServerProxy::OpServerImpl::FromOpsConnUp, this),
                boost::bind(&OpServerProxy::OpServerImpl::FromOpsConnDown, this)));
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::REDIS,
                "From", ConnectionStatus::INIT, from_ops_conn_->Endpoint(),
                std::string());
            from_ops_conn_.get()->RAC_Connect();

            kafka_timer_->Start(1000,
                boost::bind(&OpServerImpl::KafkaTimer, this), NULL);
            if (brokers.empty()) return;

            string errstr;
            RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
            conf->set("metadata.broker.list", brokers, errstr);
            conf->set("event_cb", &k_event_cb, errstr);
            conf->set("dr_cb", &k_dr_cb, errstr);
            producer_.reset(RdKafka::Producer::create(conf, errstr));
            if (!producer_) 
                assert(0);
            for (unsigned int i=0; i<partitions_; i++) {
                std::stringstream ss;
                ss << topic;
                ss << i;
                shared_ptr<RdKafka::Topic> sr(RdKafka::Topic::create(producer_.get(), ss.str(), NULL, errstr));
                topic_.push_back(sr);
                if (!topic_[i])
                    assert(0);
            }
            delete conf;
        }

        ~OpServerImpl() {
            TimerManager::DeleteTimer(kafka_timer_);
            kafka_timer_ = NULL;
            if (producer_) {
                for (unsigned int i=0; i<partitions_; i++) {
                    topic_[i].reset();
                }
                producer_.reset();

                RdKafka::wait_destroyed(5000);
            }
        }

        RedisInfo redis_uve_;
    private:
        /* these are made public, so they are accessed by OpServerProxy */
        EventManager *evm_;
        VizCollector *collector_;
        
        bool started_;
        shared_ptr<RedisAsyncConnection> to_ops_conn_;
        shared_ptr<RedisAsyncConnection> from_ops_conn_;
        RedisAsyncConnection::ClientAsyncCmdCbFn analytics_cb_proc_fn;
        RedisAsyncConnection::ClientAsyncCmdCbFn processor_cb_proc_fn;
        tbb::mutex rac_mutex_;
        const std::string redis_password_;
        shared_ptr<RdKafka::Producer> producer_;
        std::vector<shared_ptr<RdKafka::Topic> > topic_;
        KafkaEventCb k_event_cb;
        KafkaDeliveryReportCb k_dr_cb;
        Timer *kafka_timer_;
};

OpServerProxy::OpServerProxy(EventManager *evm, VizCollector *collector,
                             const std::string& redis_uve_ip,
                             unsigned short redis_uve_port,
                             const std::string& redis_password, 
                             const std::string& brokers,
                             uint16_t partitions) {
    impl_ = new OpServerImpl(evm, collector, redis_uve_ip, redis_uve_port,
                             redis_password,
                             brokers, string("uve-"), partitions);
}

OpServerProxy::~OpServerProxy() {
    if (impl_)
        delete impl_;
}

bool
OpServerProxy::UVENotif(const std::string &type,
                       const std::string &source, const std::string &node_type,
                       const std::string &module, 
                       const std::string &instance_id,
                       const std::string &key) {

    // Hashing into a partition is based on UVE Key
    unsigned int pt = djb_hash(key.c_str(), key.size()) % impl_->partitions_;

    std::stringstream ss;
    ss << source << ":" << node_type << ":" << module << ":" << instance_id;
    string genstr = ss.str();

    std::stringstream ks;
    ks << key << "|" << type;
    string kstr = ks.str();

    std::stringstream collss;
    collss << impl_->redis_uve_.GetIp() << ":" <<
               impl_->redis_uve_.GetPort();
    string collstr = collss.str();

    rapidjson::Document dd;
    dd.SetObject();

    {
        rapidjson::Value val(rapidjson::kStringType);
        val.SetString("UVEUpdate");
        dd.AddMember("message", val, dd.GetAllocator());
    }

    {
        rapidjson::Value val(rapidjson::kStringType);
        val.SetString(key.c_str());
        dd.AddMember("key", val, dd.GetAllocator());
    }

    {
        rapidjson::Value val(rapidjson::kStringType);
        val.SetString(type.c_str());
        dd.AddMember("type", val, dd.GetAllocator());
    }

    {
        rapidjson::Value val(rapidjson::kStringType);
        val.SetString(genstr.c_str(), genstr.size());
        dd.AddMember("gen", val, dd.GetAllocator());
    }

    {
        rapidjson::Value val(rapidjson::kStringType);
        val.SetString(collstr.c_str());
        dd.AddMember("coll", val, dd.GetAllocator());
    } 

    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    dd.Accept(writer);
    string jsonline(sb.GetString());

    impl_->KafkaPub(pt, kstr.c_str(), genstr, jsonline);

    return true;
}

bool
OpServerProxy::UVEUpdate(const std::string &type, const std::string &attr,
                       const std::string &source, const std::string &node_type,
                       const std::string &module, 
                       const std::string &instance_id,
                       const std::string &key, const std::string &message,
                       int32_t seq, const std::string& agg, 
                       const std::string& atyp, int64_t ts) {

    shared_ptr<RedisAsyncConnection> prac = impl_->to_ops_conn();
    if (!prac) {
        impl_->redis_uve_.RedisUveUpdateNoConn();
        return false;
    }

    // Hashing into a partition is based on UVE Key
    unsigned int pt = djb_hash(key.c_str(), key.size()) % impl_->partitions_;

    bool ret = RedisProcessorExec::UVEUpdate(prac.get(), NULL, type, attr,
            source, node_type, module, instance_id, key, message, seq, agg, atyp, ts, pt);
    if (ret) {
        impl_->redis_uve_.RedisUveUpdate();

    } else {
        impl_->redis_uve_.RedisUveUpdateFail();
    }
    return ret;
}

bool
OpServerProxy::UVEDelete(const std::string &type,
                       const std::string &source, const std::string &node_type,
                       const std::string &module, 
                       const std::string &instance_id,
                       const std::string &key, int32_t seq) {

    shared_ptr<RedisAsyncConnection> prac = impl_->to_ops_conn();
    if (!prac) {
        impl_->redis_uve_.RedisUveDeleteNoConn();
        return false;
    }

    bool ret = RedisProcessorExec::UVEDelete(prac.get(), NULL, type, source, 
            node_type, module, instance_id, key, seq);
    ret ? impl_->redis_uve_.RedisUveDelete() : impl_->redis_uve_.RedisUveDeleteFail(); 
    return ret;
}

bool
OpServerProxy::GetSeq(const string &source, const string &node_type,
        const string &module, const string &instance_id,
        std::map<std::string,int32_t> & seqReply) {

    shared_ptr<RedisAsyncConnection> prac = impl_->to_ops_conn();
    if  (!(prac && prac->IsConnUp())) return false;

    if (!impl_->to_ops_conn()) return false;

    return RedisProcessorExec::SyncGetSeq(impl_->redis_uve_.GetIp(),
            impl_->redis_uve_.GetPort(), impl_->get_redis_password(),
            source, node_type, module, instance_id, seqReply);
}

bool
OpServerProxy::DeleteUVEs(const string &source, const string &module,
                          const string &node_type, const string &instance_id) {

    shared_ptr<RedisAsyncConnection> prac = impl_->to_ops_conn();
    if  (!(prac && prac->IsConnUp())) return false;
    bool ret =  RedisProcessorExec::SyncDeleteUVEs(impl_->redis_uve_.GetIp(),
            impl_->redis_uve_.GetPort(), impl_->get_redis_password(), source,
            node_type, module, instance_id);

    // Generator Delete notification must go to all partitions
    for (unsigned int pt = 0; pt < impl_->partitions_; pt++) {

        std::stringstream ss;
        ss << source << ":" << node_type << ":" << module << ":" << instance_id;
        string genstr = ss.str();

        std::stringstream collss;
        collss << impl_->redis_uve_.GetIp() << ":" <<
                   impl_->redis_uve_.GetPort();
        string collstr = collss.str();

        rapidjson::Document dd;
        dd.SetObject();

        {
            rapidjson::Value val(rapidjson::kStringType);
            val.SetString("GenDelete");
            dd.AddMember("message", val, dd.GetAllocator());
        }

        {
            rapidjson::Value val(rapidjson::kStringType);
            val.SetString(genstr.c_str(), genstr.size());
            dd.AddMember("gen", val, dd.GetAllocator());
        }

        {
            rapidjson::Value val(rapidjson::kStringType);
            val.SetString(collstr.c_str());
            dd.AddMember("coll", val, dd.GetAllocator());
        } 

        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
        dd.Accept(writer);
        string jsonline(sb.GetString());

        impl_->KafkaPub(pt, genstr.c_str(), genstr, jsonline);
    }
    return ret;
}

void 
OpServerProxy::FillRedisUVEInfo(RedisUveInfo& redis_uve_info) {
    impl_->FillRedisUVEInfo(redis_uve_info);
}

void 
RedisUVERequest::HandleRequest() const {
    RedisUVEResponse *resp(new RedisUVEResponse);
    VizSandeshContext *vsc = static_cast<VizSandeshContext *>(
                                        Sandesh::client_context());
    assert(vsc); 
    RedisUveInfo redis_uve_info; 
    vsc->Analytics()->GetOsp()->FillRedisUVEInfo(redis_uve_info);
    resp->set_redis_uve_info(redis_uve_info);
    resp->set_context(context());
    resp->Response();
}

