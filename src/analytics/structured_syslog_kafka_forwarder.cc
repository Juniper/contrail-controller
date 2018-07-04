/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <cstdlib>
#include <utility>
#include <algorithm>

#include <tbb/mutex.h>
#include <tbb/atomic.h>

#include <boost/bind.hpp>
#include <boost/assert.hpp>

#include <base/time_util.h>
#include <base/timer.h>
#include <base/logging.h>

#include <librdkafka/rdkafkacpp.h>
#include "structured_syslog_kafka_forwarder.h"

using std::map;
using std::string;
using boost::system::error_code;

class KafkaForwarderDeliveryReportCb : public RdKafka::DeliveryReportCb {
 public:
  tbb::atomic<size_t> count;
  // This is to count the number of successful
  // kafka operations
  KafkaForwarderDeliveryReportCb() {
    count = 0;
  }

  void dr_cb(RdKafka::Message &message) {
    if (message.err() != RdKafka::ERR_NO_ERROR) {
        if (message.msg_opaque() != NULL) {
            LOG(ERROR, "KafkaForwarder: Message delivery for " << message.key()
                << ": FAILED: " << message.errstr() << ": gen: " <<
                string((char *)(message.msg_opaque())));
        } else {
            LOG(ERROR, "KafkaForwarder: Message delivery for " << message.key()
                << ": FAILED: " << message.errstr());
        }
    } else {
        count.fetch_and_increment();
    }
    char * cc = (char *)message.msg_opaque();
    delete[] cc;
  }
};

class KafkaForwarderEventCb : public RdKafka::EventCb {
 public:
  bool disableKafka;
  KafkaForwarderEventCb() : disableKafka(false) {}

  void event_cb(RdKafka::Event &event) {
    switch (event.type())
    {
      case RdKafka::Event::EVENT_ERROR:
        LOG(ERROR, RdKafka::err2str(event.err()) << " : " << event.str());
        if (event.err() == RdKafka::ERR__ALL_BROKERS_DOWN) disableKafka = true;
        break;

      case RdKafka::Event::EVENT_LOG:
        LOG(INFO, "KafkaForwarder: LOG-" << event.severity() << "-" <<
            event.fac().c_str() << ": " << event.str().c_str());
        break;

      default:
        LOG(INFO, "KafkaForwarder: EVENT " << event.type() <<
            " (" << RdKafka::err2str(event.err()) << "): " <<
            event.str());
        break;
    }
  }
};

static inline unsigned int djb_hash(const char *str, size_t len) {
    unsigned int hash = 5381;
    for (size_t i = 0 ; i < len ; i++)
        hash = ((hash << 5) + hash) + str[i];
    return hash;
}

class KafkaForwarderPartitionerCb : public RdKafka::PartitionerCb {
  public:
    tbb::atomic<size_t> count;
    KafkaForwarderPartitionerCb() {
        count = 0;
    }
    int32_t partitioner_cb(const RdKafka::Topic *topic,
                                  const std::string *key,
                                  int32_t partition_cnt,
                                  void *msg_opaque) {
        int32_t pt = djb_hash(key->c_str(), key->size()) % partition_cnt;
        LOG(DEBUG,"KafkaForwarder PartitionerCb key " << key->c_str()  <<
            " len " << key->size() << " count " << partition_cnt << " pt " <<
            pt);
        count.fetch_and_increment();
        return pt;
    }
};

KafkaForwarderEventCb k_forwarder_event_cb;
KafkaForwarderDeliveryReportCb k_forwarder_dr_cb;
KafkaForwarderPartitionerCb k_forwarder_part_cb;

void
KafkaForwarder::Send(const string& value, const string& skey) {
    if (k_forwarder_event_cb.disableKafka) {
        LOG(INFO, "KafkaForwarder ignoring Send");
        return;
    }

    if (producer_) {
        int32_t partition = k_forwarder_part_cb.partitioner_cb(NULL, &skey,
                        partitions_ , NULL);
        producer_->produce(topic_.get(), partition,
            RdKafka::Producer::MSG_COPY,
            const_cast<char *>(value.c_str()), value.length(),
            NULL, NULL);
    }
}

bool
KafkaForwarder::KafkaTimer() {
    {
        uint64_t new_tick_time = ClockMonotonicUsec() / 1000;
        // We track how long it has been since the timer was last called
        // This is because the execution time of this function is highly variable.
        // Init can take several seconds
        kafka_elapsed_ms_ += (new_tick_time - kafka_tick_ms_);
        kafka_tick_ms_ = new_tick_time;
    }

    // Connection Status is periodically updated
    // based on Kafka publish activity.
    // Update Connection Status more often during startup or during failures
    if ((((kafka_tick_ms_ - kafka_start_ms_) < kActivityCheckPeriod_ms_) &&
         (kafka_elapsed_ms_ >= kActivityCheckPeriod_ms_/3)) ||
        (k_forwarder_event_cb.disableKafka &&
         (kafka_elapsed_ms_ >= kActivityCheckPeriod_ms_/3)) ||
        (kafka_elapsed_ms_ > kActivityCheckPeriod_ms_)) {

        kafka_elapsed_ms_ = 0;

        if ((k_forwarder_dr_cb.count==0) && (k_forwarder_part_cb.count!=0)) {
            LOG(INFO, "No KafkaForwarder Callbacks");
        } else if (k_forwarder_dr_cb.count==k_forwarder_part_cb.count) {
            LOG(INFO, "Got KafkaForwarder Callbacks " << k_forwarder_dr_cb.count);
        } else {
            LOG(INFO, "Some KafkaForwarder Callbacks missed - got " << k_forwarder_dr_cb.count
                       << "/" << k_forwarder_part_cb.count);
        }
        k_forwarder_dr_cb.count = 0;
        k_forwarder_part_cb.count = 0;

        if (k_forwarder_event_cb.disableKafka) {
            LOG(ERROR, "KafkaForwarder Needs Restart");
            class RdKafka::Metadata *metadata;
            /* Fetch metadata */
            RdKafka::ErrorCode err = producer_->metadata(true, NULL,
                                  &metadata, 5000);
            if (err != RdKafka::ERR_NO_ERROR) {
                LOG(ERROR, "Failed to acquire metadata: " << RdKafka::err2str(err));
            } else {
                LOG(ERROR, "KafkaForwarder Metadata Detected");
                LOG(ERROR, "KafkaForwarder Metadata for " <<
                    metadata->orig_broker_id() << ":" <<
                    metadata->orig_broker_name());
                LOG(DEBUG, "Initializing k_forwarder_event_cb.disableKafka to false!");
                k_forwarder_event_cb.disableKafka = false;
            }
            // deleting metadata pointer object as a fix for memory leak
            delete metadata;
        }
    }

    if (producer_) {
        producer_->poll(0);
    }
    return true;
}

KafkaForwarder::KafkaForwarder(EventManager *evm,
             const std::string brokers,
             const std::string topic,
             uint16_t partitions) :
    partitions_(partitions),
    evm_(evm),
    brokers_(brokers),
    topic_str_(topic),
    kafka_elapsed_ms_(0),
    kafka_start_ms_(UTCTimestampUsec()/1000),
    kafka_tick_ms_(0),
    kafka_timer_(TimerManager::CreateTimer(*evm->io_service(),
                 "KafkaForwarder Timer",
                 TaskScheduler::GetInstance()->GetTaskId(
                 "KafkaForwarder Timer"))) {

    kafka_timer_->Start(1000,
        boost::bind(&KafkaForwarder::KafkaTimer, this), NULL);
    if (brokers.empty()) return;
    assert(Init());
}

void
KafkaForwarder::Stop(void) {
    if (producer_) {
        topic_.reset();
        producer_.reset();
        assert(RdKafka::wait_destroyed(8000) == 0);
        LOG(ERROR, "KafkaForwarder Stopped");
    }
}

bool
KafkaForwarder::Init(void) {
    string errstr;
    RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    conf->set("metadata.broker.list", brokers_, errstr);
    conf->set("event_cb", &k_forwarder_event_cb, errstr);
    conf->set("dr_cb", &k_forwarder_dr_cb, errstr);
    producer_.reset(RdKafka::Producer::create(conf, errstr));
    LOG(ERROR, "KafkaForwarder new Prod " << errstr);
    delete conf;
    if (!producer_) {
        return false;
    }
    errstr = string();
    topic_.reset(RdKafka::Topic::create(producer_.get(), topic_str_, NULL, errstr));
    return true;
}

void
KafkaForwarder::Shutdown() {
    TimerManager::DeleteTimer(kafka_timer_);
    kafka_timer_ = NULL;
    Stop();
}

KafkaForwarder::~KafkaForwarder() {
    assert(kafka_timer_ == NULL);
}
