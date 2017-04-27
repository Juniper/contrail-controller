/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "viz_collector.h"
#include "viz_constants.h"
#include "OpServerProxy.h"
#include <tbb/mutex.h>
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/assert.hpp>
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

using std::map;
using std::string;
using std::vector;
using boost::shared_ptr;
using boost::assign::list_of;
using boost::system::error_code;
using process::ConnectionState;
using process::ConnectionType;
using process::ConnectionStatus;


class KafkaDeliveryReportCb : public RdKafka::DeliveryReportCb {
 public:
  unsigned int count;
  // This is to count the number of successful
  // kafka operations
  KafkaDeliveryReportCb() : count(0) {}

  void dr_cb (RdKafka::Message &message) {
    if (message.err() != RdKafka::ERR_NO_ERROR) {
        LOG(ERROR, "Message delivery for " << message.key() << " " <<
            message.errstr() << " gen " <<
            string((char *)(message.msg_opaque())));
    } else {
        count++;
    }
    char * cc = (char *)message.msg_opaque();
    delete[] cc;
  }
};


class KafkaEventCb : public RdKafka::EventCb {
 public:
  bool disableKafka;
  KafkaEventCb() : disableKafka(false) {}

  void event_cb (RdKafka::Event &event) {
    switch (event.type())
    {
      case RdKafka::Event::EVENT_ERROR:
        LOG(ERROR, RdKafka::err2str(event.err()) << " : " << event.str());
        if (event.err() == RdKafka::ERR__ALL_BROKERS_DOWN) disableKafka = true;
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

        static const int kActivityCheckPeriod_ms_ = 30000;

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
            if (k_event_cb.disableKafka) {
                LOG(INFO, "Kafka ignoring KafkaPub");
                return;
            }

            if (producer_) {
                char* gn = new char[gen.length()+1];
                strcpy(gn,gen.c_str());

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
            if (collector_) {
                collector_->RedisUpdate(true);
                redis_up_ = true;
            }
        }

        void toConnectCallbackProcess(const redisAsyncContext *c, void *r, void *privdata) {
            if (r == NULL) {
                LOG(DEBUG, "In toConnectCallbackProcess.. NULL Reply");
                return;
            }
            // Handle the AUTH callback
            redisReply reply = *reinterpret_cast<redisReply*>(r);
            if (reply.type != REDIS_REPLY_ERROR) {
                {
                   tbb::mutex::scoped_lock lock(rac_mutex_);
                   redis_uve_.RedisStatusUpdate(RAC_UP);
                }
                ConnectionState::GetInstance()->Update(ConnectionType::REDIS_UVE,
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
            if (r == NULL) {
                LOG(DEBUG, "In fromConnectCallbackProcess.. NULL Reply");
                return;
            }
            // Handle the AUTH callback
            redisReply reply = *reinterpret_cast<redisReply*>(r);
            if (reply.type != REDIS_REPLY_ERROR) {
                ConnectionState::GetInstance()->Update(ConnectionType::REDIS_UVE,
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
            started_ = false;
            collector_->RedisUpdate(false);
            redis_up_ = false;

            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::REDIS_UVE,
                "To", ConnectionStatus::DOWN, to_ops_conn_->Endpoint(),
                std::string());
            evm_->io_service()->post(boost::bind(&OpServerProxy::OpServerImpl::RAC_ConnectProcess,
                        this, RAC_CONN_TYPE_TO_OPS));
        }

        void FromOpsConnDown() {
            LOG(DEBUG, "FromOpsConnDown.. DOWN.. Reconnect..");
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::REDIS_UVE,
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
            // If redis returns error for async request, then perhaps it
            // is busy executing a script and it has reached the maximum
            // execution time limit.
            BOOST_ASSERT_MSG(reply->type != REDIS_REPLY_ERROR, reply->str);

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
            
            {
		uint64_t new_tick_time = UTCTimestampUsec() / 1000;
		// We track how long it has been since the timer was last called
		// This is because the execution time of this function is highly variable.
		// StartKafka can take several seconds
		kafka_elapsed_ms_ += (new_tick_time - kafka_tick_ms_);
		kafka_tick_ms_ = new_tick_time;
            }

            // Connection Status is periodically updated
            // based on Kafka piblish activity.
            // Update Connection Status more often during startup or during failures
            if ((((kafka_tick_ms_ - kafka_start_ms_) < kActivityCheckPeriod_ms_) &&
                 (kafka_elapsed_ms_ >= kActivityCheckPeriod_ms_/3)) ||
                (k_event_cb.disableKafka &&
                 (kafka_elapsed_ms_ >= kActivityCheckPeriod_ms_/3)) ||
                (kafka_elapsed_ms_ > kActivityCheckPeriod_ms_)) {
                
                kafka_elapsed_ms_ = 0;

                if (k_dr_cb.count==0) {
                    LOG(ERROR, "No Kafka Callbacks");
                    ConnectionState::GetInstance()->Update(ConnectionType::KAFKA_PUB,
                        brokers_, ConnectionStatus::DOWN, process::Endpoint(), std::string());
                } else {
                    ConnectionState::GetInstance()->Update(ConnectionType::KAFKA_PUB,
                        brokers_, ConnectionStatus::UP, process::Endpoint(), std::string());
                    LOG(INFO, "Got Kafka Callbacks " << k_dr_cb.count);
                }
                k_dr_cb.count = 0;

		if (k_event_cb.disableKafka) {
		    LOG(ERROR, "Kafka Needs Restart");
                    class RdKafka::Metadata *metadata;
                    /* Fetch metadata */
                    RdKafka::ErrorCode err = producer_->metadata(true, NULL,
                                          &metadata, 5000);
                    if (err != RdKafka::ERR_NO_ERROR) {
                        LOG(ERROR, "Failed to acquire metadata: " << RdKafka::err2str(err));
                    } else {
                        LOG(ERROR, "Kafka Metadata Detected");
                        LOG(ERROR, "Metadata for " << metadata->orig_broker_id() <<
                            ":" << metadata->orig_broker_name());

                        if (collector_ && redis_up_) {
                            LOG(ERROR, "Kafka Restarting Redis");
                            collector_->RedisUpdate(true);
                            k_event_cb.disableKafka = false;
                        }
                    }
		}
            } 

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
            brokers_(brokers),
            topicpre_(topic),
            redis_up_(false),
            kafka_elapsed_ms_(0),
            kafka_start_ms_(UTCTimestampUsec()/1000),
            kafka_tick_ms_(0),
            kafka_timer_(TimerManager::CreateTimer(*evm->io_service(),
                         "Kafka Timer", 
                         TaskScheduler::GetInstance()->GetTaskId(
                         "Kafka Timer"))) {
            to_ops_conn_.reset(new RedisAsyncConnection(evm_, 
                redis_uve_ip, redis_uve_port, 
                boost::bind(&OpServerProxy::OpServerImpl::ToOpsConnUp, this),
                boost::bind(&OpServerProxy::OpServerImpl::ToOpsConnDown, this)));
            // Update connection 
            ConnectionState::GetInstance()->Update(ConnectionType::REDIS_UVE,
                "To", ConnectionStatus::INIT, to_ops_conn_->Endpoint(),
                std::string());
            to_ops_conn_.get()->RAC_Connect();
            from_ops_conn_.reset(new RedisAsyncConnection(evm_,
                redis_uve_ip, redis_uve_port,
                boost::bind(&OpServerProxy::OpServerImpl::FromOpsConnUp, this),
                boost::bind(&OpServerProxy::OpServerImpl::FromOpsConnDown, this)));
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::REDIS_UVE,
                "From", ConnectionStatus::INIT, from_ops_conn_->Endpoint(),
                std::string());
            from_ops_conn_.get()->RAC_Connect();

            kafka_timer_->Start(1000,
                boost::bind(&OpServerImpl::KafkaTimer, this), NULL);
            if (brokers.empty()) return;
	    ConnectionState::GetInstance()->Update(ConnectionType::KAFKA_PUB,
		brokers_, ConnectionStatus::INIT, process::Endpoint(), std::string());
            assert(StartKafka());
        }

        void StopKafka(void) {
            if (producer_) {
                for (unsigned int i=0; i<partitions_; i++) {
                    topic_[i].reset();
                }
                topic_.clear();
                producer_.reset();

                assert(RdKafka::wait_destroyed(8000) == 0);
                LOG(ERROR, "Kafka Stopped");
            }
        }

        bool StartKafka(void) {
            string errstr;
            RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
            conf->set("metadata.broker.list", brokers_, errstr);
            conf->set("event_cb", &k_event_cb, errstr);
            conf->set("dr_cb", &k_dr_cb, errstr);
            producer_.reset(RdKafka::Producer::create(conf, errstr));
            LOG(ERROR, "Kafka new Prod " << errstr);
            if (!producer_) 
                return false;
            for (unsigned int i=0; i<partitions_; i++) {
                std::stringstream ss;
                ss << topicpre_;
                ss << i;
                errstr = string();
                shared_ptr<RdKafka::Topic> sr(RdKafka::Topic::create(producer_.get(), ss.str(), NULL, errstr));
                LOG(ERROR,"Kafka new topic " << ss.str() << " Err" << errstr);
         
                topic_.push_back(sr);
                if (!topic_[i])
                    return false;
            }
            delete conf;
            return true;
        }

        void Shutdown() {
            TimerManager::DeleteTimer(kafka_timer_);
            kafka_timer_ = NULL;
            StopKafka();
        }

        ~OpServerImpl() {
            assert(kafka_timer_ == NULL);
        }

        RedisInfo redis_uve_;
        bool IsInitDone() { return started_;}

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
        std::string brokers_;
        std::string topicpre_;
        bool redis_up_;
        uint64_t kafka_elapsed_ms_;
        const uint64_t kafka_start_ms_;
        uint64_t kafka_tick_ms_;
        Timer *kafka_timer_;
};

OpServerProxy::OpServerProxy(EventManager *evm, VizCollector *collector,
                             const std::string& redis_uve_ip,
                             unsigned short redis_uve_port,
                             const std::string& redis_password, 
                             const std::string& brokers,
                             uint16_t partitions,
                             const std::string& kafka_prefix=std::string()) {
    impl_ = new OpServerImpl(evm, collector, redis_uve_ip, redis_uve_port,
                             redis_password,
                             brokers, kafka_prefix + string("-uve-"), partitions);
}

OpServerProxy::~OpServerProxy() {
    if (impl_)
        delete impl_;
}

void
OpServerProxy::Shutdown() {
    if (impl_) {
        impl_->Shutdown();
    }
}

bool
OpServerProxy::UVENotif(const std::string &type,
                       const std::string &source, const std::string &node_type,
                       const std::string &module, 
                       const std::string &instance_id,
                       const std::string &table, const std::string &barekey,
                       const std::map<std::string,std::string>& value,
                       bool deleted) {
     
    std::string key = table + ":" + barekey;

    unsigned int pt;
    PartType::type ptype = PartType::PART_TYPE_OTHER;
    std::map<std::string, PartType::type>::const_iterator mit = 
            g_viz_constants.PART_TYPES.find(table);
    if (mit != g_viz_constants.PART_TYPES.end()) {
        ptype = mit->second;
    }
    std::pair<unsigned int,unsigned int> partdesc =
        VizCollector::PartitionRange(ptype, impl_->partitions_);
    pt = partdesc.first + (djb_hash(key.c_str(), key.size()) % partdesc.second);

    std::stringstream ss;
    ss << source << ":" << node_type << ":" << module << ":" << instance_id;
    string genstr = ss.str();

    std::stringstream collss;
    collss << Collector::GetSelfIp() << ":" <<
               impl_->redis_uve_.GetPort();
    string collstr = collss.str();

    std::stringstream ks;
    ks << key << "|" << type << "|" << genstr << "|" << collstr;
    string kstr = ks.str();

    if (deleted) {
        impl_->KafkaPub(pt, kstr.c_str(), genstr, string());
    } else {
        rapidjson::Document dd;
        dd.SetObject();
        for (map<string,string>::const_iterator it = value.begin();
                    it != value.end(); it++) {
            rapidjson::Value sval(rapidjson::kStringType);
            sval.SetString((it->second).c_str());
            dd.AddMember(it->first.c_str(), sval, dd.GetAllocator());
        }
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
        dd.Accept(writer);
        string jsonline(sb.GetString());

        impl_->KafkaPub(pt, kstr.c_str(), genstr, jsonline);
    }

    rapidjson::Document dd;
    dd.SetObject();

    return true;
}

bool
OpServerProxy::UVEUpdate(const std::string &type, const std::string &attr,
                       const std::string &source, const std::string &node_type,
                       const std::string &module, 
                       const std::string &instance_id,
                       const std::string &table,
                       const std::string &barekey, const std::string &message,
                       int32_t seq, const std::string& agg, 
                       int64_t ts, bool is_alarm) {

    shared_ptr<RedisAsyncConnection> prac = impl_->to_ops_conn();
    if (!prac) {
        impl_->redis_uve_.RedisUveUpdateNoConn();
        return false;
    }
    std::string key = table + ":" + barekey;
    unsigned int pt = 0;
    if (!is_alarm) {
        PartType::type ptype = PartType::PART_TYPE_OTHER;
        std::map<std::string, PartType::type>::const_iterator mit = 
                g_viz_constants.PART_TYPES.find(table);
        if (mit != g_viz_constants.PART_TYPES.end()) {
            ptype = mit->second;
        }
        std::pair<unsigned int,unsigned int> partdesc =
            VizCollector::PartitionRange(ptype, impl_->partitions_);
        pt = partdesc.first + (djb_hash(key.c_str(), key.size()) % partdesc.second);
    }

    bool ret = RedisProcessorExec::UVEUpdate(prac.get(), NULL, type, attr,
            source, node_type, module, instance_id, key, message,
            seq, agg, ts, pt, is_alarm);
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
                       const std::string &key, int32_t seq, bool is_alarm) {

    shared_ptr<RedisAsyncConnection> prac = impl_->to_ops_conn();
    if (!prac) {
        impl_->redis_uve_.RedisUveDeleteNoConn();
        return false;
    }

    bool ret = RedisProcessorExec::UVEDelete(prac.get(), NULL, type, source, 
            node_type, module, instance_id, key, seq, is_alarm);
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
   
    std::vector<std::pair<std::string,std::string> > delReply;
    bool ret =  RedisProcessorExec::SyncDeleteUVEs(impl_->redis_uve_.GetIp(),
            impl_->redis_uve_.GetPort(), impl_->get_redis_password(), source,
            node_type, module, instance_id, delReply);

    // TODO: If we cannot get uve delete information here, we need to
    //       restart the kakfa topic
    assert(ret);

    for(std::vector<std::pair<std::string,std::string> >::const_iterator
            it = delReply.begin(); it != delReply.end(); it++) {
        string uve = it->first;
        string typ = it->second;    

        vector<string> v;
        boost::split(v, uve, boost::is_any_of(":"));
        string table(v[0]);
        string barekey(v[1]);
        for (size_t idx=2; idx<v.size(); idx++) {
            barekey += ":";
            barekey += v[idx];
        }
        std::map<std::string,std::string> val;
        assert(UVENotif(typ, source, node_type, module, instance_id,
                table, barekey, val, true));
    }
    return true;
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

/*
 * After redis collector connection is UP,
 * we do initialization as part of a callback.
 * We set the started_ flag during that step.
 */
bool
OpServerProxy::IsRedisInitDone() {
    return impl_->IsInitDone();
}

