/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __STRUCTURED_SYSLOG_KAFKAFORWARDER_H__
#define __STRUCTURED_SYSLOG_KAFKAFORWARDER_H__

#include <string>
#include <librdkafka/rdkafkacpp.h>
#include "io/event_manager.h"

class KafkaForwarder {
    public:

        static const int kActivityCheckPeriod_ms_ = 30000;

        const unsigned int partitions_;

        void Send(const std::string& value, const std::string& skey);

        KafkaForwarder(EventManager *evm,
                     const std::string brokers,
                     const std::string topic, 
                     uint16_t partitions);

        void Shutdown();

        virtual ~KafkaForwarder();

    private:
        bool KafkaTimer();
        void Stop(void);
        bool Init(void);

        EventManager *evm_;
        
        boost::shared_ptr<RdKafka::Producer> producer_;
        std::string brokers_;
        std::string topic_str_;
        boost::shared_ptr<RdKafka::Topic> topic_;
        uint64_t kafka_elapsed_ms_;
        const uint64_t kafka_start_ms_;
        uint64_t kafka_tick_ms_;
        Timer *kafka_timer_;
};

#endif
