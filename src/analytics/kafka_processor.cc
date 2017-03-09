/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
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
#include <set>
#include <cstdlib>
#include <utility>
#include <pthread.h>
#include <algorithm>
#include <iterator>
#include <librdkafka/rdkafkacpp.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_uve.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>

#include "rapidjson/document.h"
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "kafka_processor.h"
#include "kafka_types.h"
#include <base/connection_info.h>
#include "viz_sandesh.h"
#include "viz_collector.h"
#include "uve_aggregator.h"

using std::map;
using std::string;
using std::vector;
using std::pair;
using std::make_pair;
using std::set;
using boost::shared_ptr;
using boost::assign::list_of;
using boost::system::error_code;
using process::ConnectionState;
using process::ConnectionType;
using process::ConnectionStatus;

/* This the consumer for Agg Kafka Topics
  
   Parse the XML contents and run aggregation
   using dervied stats
*/
class KafkaProcessor::KafkaWorker {
 public:
    static const uint64_t kPeriod = 300000000;
    KafkaWorker(shared_ptr<RdKafka::KafkaConsumer> consumer,
            map<string, shared_ptr<UVEAggregator> > aggs) :
        thread_id_(pthread_self()),
        disable_(false),
        consumer_(consumer),
        aggs_(aggs) {
    }
    static void *ThreadRun(void *objp) {
        KafkaWorker *obj = reinterpret_cast<KafkaWorker *>(objp);
    
        while (!obj->disable_) {
            /* The consume calls blocks for the timeout
               if these are no messages to read.
               Due to this blocking behaviour, pthread is being
               used instead of a TBB Task */
            std::auto_ptr<RdKafka::Message> message(obj->consumer_->consume(1000));
            if (message->err() ==  RdKafka::ERR_NO_ERROR) {
                const int mpart(message->partition());
                assert(mpart < SandeshUVETypeMaps::kProxyPartitions);
                const string payload(reinterpret_cast<char *>(message->payload()),
                        message->len());
                const string topic_name(message->topic_name());
                LOG(DEBUG, "Consuming topic=" << message->topic_name() << 
                        " partition " << mpart << 
                        " offset " << message->offset() <<
                        " key " << *(message->key()) <<
                        " payload " << payload);
                obj->aggs_[topic_name]->Update(message);

            } else if (message->err() ==  RdKafka::ERR__TIMED_OUT) {
                LOG(DEBUG, "Consuming Timeout");
            } else {
                LOG(ERROR, "Message consume failed : " << message->errstr());
            }
        }
        return NULL;
    }

    void Start() {
        int res = pthread_create(&thread_id_, NULL, &ThreadRun, this);
        assert(res == 0);
    }
    void Join() {
        disable_ = true;
        int res = pthread_join(thread_id_, NULL);
        consumer_->close();
        assert(res == 0);
    }
 private:
    pthread_t thread_id_;
    bool disable_;
    shared_ptr<RdKafka::KafkaConsumer> consumer_;
    map<string, shared_ptr<UVEAggregator> > aggs_;
};

class KafkaRebalanceCb : public RdKafka::RebalanceCb {
  public:
    void rebalance_cb (RdKafka::KafkaConsumer *consumer,
             RdKafka::ErrorCode err,
                std::vector<RdKafka::TopicPartition*> &pts) {
       map<string, map<int32_t,uint64_t> > topics;
   

       // Load all topic-partitions into a
       // map (key topic) of sets of ints (partitions)
       for (size_t idx=0; idx<pts.size(); idx++) {
           map<string, map<int32_t,uint64_t> >::iterator ti =
                   topics.find(pts[idx]->topic());
           if (ti == topics.end()) {
               topics.insert(make_pair(pts[idx]->topic(),
                   map<int32_t,uint64_t>()));
               topics.at(pts[idx]->topic()).insert(
                   make_pair(pts[idx]->partition(),
                             (uint64_t)pts[idx]->offset()));
           } else {
               ti->second.insert(
                   make_pair(pts[idx]->partition(),
                             (uint64_t)pts[idx]->offset()));
           }
           pts[idx]->set_offset(RdKafka::Topic::OFFSET_STORED);
       }
       
       std::stringstream ss;
       map<string, KafkaAggTopicOffsets> all_offsets;
       for (map<string, map<int32_t,uint64_t> >::iterator ti = topics.begin();
               ti != topics.end(); ti++) {
           ss << ti->first << " : [ ";
           vector<KafkaAggPartOffset> vec_part_offset;
           for (map<int32_t,uint64_t>::iterator si = ti->second.begin(); 
                   si != ti->second.end(); si++) {
               ss << si->first << ":" << si->second << ", ";
               KafkaAggPartOffset part_offset;
               part_offset.set_partition(si->first);
               part_offset.set_offset(si->second);
               vec_part_offset.push_back(part_offset);
           }
           KafkaAggTopicOffsets topic_offsets;
           topic_offsets.set_topic_offsets(vec_part_offset);
           all_offsets.insert(make_pair(ti->first, topic_offsets));
           ss << " ]" << std::endl;
       }
       if (err == RdKafka::ERR__ASSIGN_PARTITIONS) {
           // application may load offets from arbitrary external
           // storage here and update \p partitions

           // Get all topic partitions that used to be assigned to us
           // and are no longer assigned. Then clear the corresponding
           // Proxy UVEs
           for (map<string, pair <string, set<int> > >::iterator pti = 
                   topics_.begin(); pti != topics_.end(); pti++) {
               set<int> diff;
               for (set<int>::const_iterator ci = pti->second.second.begin();
                       ci != pti->second.second.end(); ci++) {
                   // Store all partitions that exist now but are 
                   // NOT in the new TopicPartition list
                   if ((topics.find(pti->first) == topics.end()) ||
                       (topics.at(pti->first).find(*ci) ==
                           topics.at(pti->first).end())) {
                       diff.insert(*ci);
                   }
               }
               for (set<int>::const_iterator di = diff.begin();
                       di != diff.end(); di++) {
                   uint32_t clear =
                           aggs_[pti->first]->Clear(pti->second.first, *di);
                   if (clear != 0) {
                       LOG(ERROR, "Clear on Assign " << pti->second.first << " : " 
                           << *di << " count " << clear);
                   }
               }
               pti->second.second.clear();
               // If the topic is still present, re-pollulate
               // the set of partitions
               if (topics.find(pti->first) != topics.end()) {
                   for (map<int32_t,uint64_t>::const_iterator mi = 
                           topics.at(pti->first).begin();
                           mi != topics.at(pti->first).end(); mi++) {
                       pti->second.second.insert(mi->first);
                   }
               }
           }
           consumer->assign(pts);
           LOG(ERROR, "Assign " << ss.str());
           {
               KafkaAggStatus data;
               data.set_name(collector_->name());
               data.set_assign_offsets(all_offsets);
               KafkaAggStatusTrace::Send(data);
           }

       } else if (err == RdKafka::ERR__REVOKE_PARTITIONS) {
         // Application may commit offsets manually here
         // if auto.commit.enable=false

         LOG(ERROR, "UnAssign " << ss.str());
         consumer->unassign();

       } else {
         LOG(ERROR, "Rebalancing error " << ss.str());
         consumer->unassign();
       }
    }
    void Init(VizCollector *collector,
            const map<string, pair <string, set<int> > > &topics,
            map<string, shared_ptr<UVEAggregator> > aggs) {
        collector_ = collector;
        topics_ = topics;
        aggs_ = aggs;
    }
    KafkaRebalanceCb() : collector_(NULL) {}
  private:
    VizCollector *collector_;
    // Key is topic.
    // Value is pair of proxy name and (empty) set of partitions
    map<string, pair <string, set<int> > > topics_;
    map<string, shared_ptr<UVEAggregator> > aggs_;
};

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

class KafkaPartitionerCb : public RdKafka::PartitionerCb {
  public:
    int32_t partitioner_cb (const RdKafka::Topic *topic,
                                  const std::string *key,
                                  int32_t partition_cnt,
                                  void *msg_opaque) {
        int32_t pt = djb_hash(key->c_str(), key->size()) % partition_cnt;
        LOG(DEBUG,"PartitionerCb key " << key->c_str()  << " len " << key->size() <<
                 key->size() << " count " << partition_cnt << " pt " << pt); 
        return pt;
    }
};


KafkaEventCb k_event_cb;
KafkaDeliveryReportCb k_dr_cb;
KafkaPartitionerCb k_part_cb;
KafkaRebalanceCb k_re_cb;

void
KafkaProcessor::KafkaPub(unsigned int pt,
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

void
KafkaProcessor::KafkaPub(const string& astream,
        const string& skey,
        const string& value) {
    if (k_event_cb.disableKafka) {
        LOG(INFO, "Kafka ignoring Agg KafkaPub");
        return;
    }
    if (producer_) {
        std::map<string, shared_ptr<RdKafka::Topic> >::const_iterator
                ait = aggtopic_.find(astream);
        if (ait!=aggtopic_.end()) {
            int32_t pt = k_part_cb.partitioner_cb(NULL, &skey,
                    SandeshUVETypeMaps::kProxyPartitions ,NULL); 
            producer_->produce(ait->second.get(), pt,
                RdKafka::Producer::MSG_COPY,
                const_cast<char *>(value.c_str()), value.length(),
                &skey, 0);
        }
    }
}

bool
KafkaProcessor::KafkaTimer() {
    {
        uint64_t new_tick_time = ClockMonotonicUsec() / 1000;
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


KafkaProcessor::KafkaProcessor(EventManager *evm, VizCollector *collector,
             const std::map<std::string, std::string>& aggconf,
             const std::string brokers,
             const std::string topic, 
             uint16_t partitions) :
    partitions_(partitions),
    aggconf_(aggconf),
    evm_(evm),
    collector_(collector),
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

    kafka_timer_->Start(1000,
        boost::bind(&KafkaProcessor::KafkaTimer, this), NULL);
    if (brokers.empty()) return;
    ConnectionState::GetInstance()->Update(ConnectionType::KAFKA_PUB,
        brokers_, ConnectionStatus::INIT, process::Endpoint(), std::string());
    assert(StartKafka());
}

void
KafkaProcessor::StopKafka(void) {
    if (kafkaworker_) {
        kafkaworker_->Join();
        kafkaworker_.reset();
    }
    if (producer_) {
        for (unsigned int i=0; i<partitions_; i++) {
            topic_[i].reset();
        }
        topic_.clear();
        for (map<string,shared_ptr<RdKafka::Topic> >::iterator
                it = aggtopic_.begin();
                it != aggtopic_.end(); it++) {
            it->second.reset();
        }
        aggtopic_.clear();

        producer_.reset();

        assert(RdKafka::wait_destroyed(8000) == 0);
        LOG(ERROR, "Kafka Stopped");
    }
}

bool
KafkaProcessor::StartKafka(void) {
    string errstr;
    RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    conf->set("metadata.broker.list", brokers_, errstr);
    conf->set("event_cb", &k_event_cb, errstr);
    conf->set("dr_cb", &k_dr_cb, errstr);
    producer_.reset(RdKafka::Producer::create(conf, errstr));
    LOG(ERROR, "Kafka new Prod " << errstr);
    delete conf;
    if (!producer_) { 
        return false;
    }
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
    vector<string> subs;

    // Key is topic.
    // Value is pair of proxy name and (empty) set of partitions
    map<string, pair <string, set<int> > > topic_parts;

    for (map<string,string>::const_iterator it = aggconf_.begin();
            it != aggconf_.end(); it++) {
        std::stringstream ss;
        ss << topicpre_;
        ss << "agg-";
        ss << it->first;
        errstr = string();
        shared_ptr<RdKafka::Topic> sr(RdKafka::Topic::create(producer_.get(), ss.str(), NULL, errstr));
        LOG(ERROR,"Kafka new topic " << ss.str() << " Err" << errstr);
        if (!sr) return false;
        aggtopic_.insert(make_pair(it->first, sr));
        topic_parts.insert(make_pair(ss.str(), make_pair(it->first, set<int>())));
        subs.push_back(ss.str());
    }
    RdKafka::Conf *cconf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    cconf->set("metadata.broker.list", brokers_, errstr);
    cconf->set("event_cb", &k_event_cb, errstr);
    cconf->set("rebalance_cb", &k_re_cb, errstr);
    cconf->set("group.id", "agg", errstr);
    cconf->set("enable.auto.commit", "false", errstr);
    shared_ptr<RdKafka::KafkaConsumer> consumer(
            RdKafka::KafkaConsumer::create(cconf, errstr));
    LOG(ERROR,"Kafka consumer Err " << errstr);
    delete cconf;
    if (!consumer)
        return false;

    // Key is topic.
    // Value is Ptr to Aggregator object
    map<string, shared_ptr<UVEAggregator> > aggs;
    for (map<string,string>::const_iterator it = aggconf_.begin();
            it != aggconf_.end(); it++) {
        std::stringstream ss;
        ss << topicpre_;
        ss << "agg-";
        ss << it->first;
        aggs.insert(make_pair(ss.str(),
                new UVEAggregator(it->first, it->second,
                        boost::bind(static_cast<
                            RdKafka::ErrorCode (RdKafka::KafkaConsumer::*)(RdKafka::Message*)>
                            (&RdKafka::KafkaConsumer::commitSync),
                                    consumer, _1),
                        SandeshUVETypeMaps::kProxyPartitions)));
    }

    k_re_cb.Init(collector_, topic_parts, aggs);
    consumer->subscribe(subs);

    kafkaworker_.reset(new KafkaWorker(consumer, aggs));
    kafkaworker_->Start();
    return true;
}

void
KafkaProcessor::Shutdown() {
    TimerManager::DeleteTimer(kafka_timer_);
    kafka_timer_ = NULL;
    StopKafka();
}

KafkaProcessor::~KafkaProcessor() {
    assert(kafka_timer_ == NULL);
}
