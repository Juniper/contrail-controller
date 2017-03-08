/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __KAFKAPROCESSOR_H__
#define __KAFKAPROCESSOR_H__

#include <string>
#include <librdkafka/rdkafkacpp.h>
#include "io/event_manager.h"

class VizCollector;
class KafkaProcessor {
    public:
        class KafkaWorker;

        static const int kActivityCheckPeriod_ms_ = 30000;

        const unsigned int partitions_;
        const std::map<std::string, std::string> aggconf_;

     
        // This is to publish to the raw UVE topics,
        // which are consumed by contrail-alarm-gen
        void KafkaPub(unsigned int pt,
                          const std::string& skey,
                          const std::string& gen,
                          const std::string& value);

        // This is to publish to the aggregate UVE
        // topics (used to create proxy UVEs), which
        // are consumed by contrail-collector
        void KafkaPub(const std::string& astream,
                      const std::string& skey,
                      const std::string& value);


        void SetRedisState(bool up) {
            redis_up_ = up;
        }

        KafkaProcessor(EventManager *evm, VizCollector *collector,
                     const std::map<std::string, std::string>& aggconf,
                     const std::string brokers,
                     const std::string topic, 
                     uint16_t partitions);


        void Shutdown();

        virtual ~KafkaProcessor();

    private:
        bool KafkaTimer();
        void StopKafka(void);
        bool StartKafka(void);

        EventManager *evm_;
        VizCollector *collector_;
        
        boost::shared_ptr<RdKafka::Producer> producer_;
        boost::shared_ptr<KafkaWorker> kafkaworker_;
        std::vector<boost::shared_ptr<RdKafka::Topic> > topic_;
        map<string, boost::shared_ptr<RdKafka::Topic> > aggtopic_;
        std::string brokers_;
        std::string topicpre_;
        bool redis_up_;
        uint64_t kafka_elapsed_ms_;
        const uint64_t kafka_start_ms_;
        uint64_t kafka_tick_ms_;
        Timer *kafka_timer_;
};

#endif
