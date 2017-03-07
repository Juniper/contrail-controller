/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <pugixml/pugixml.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_uve.h>
#include <librdkafka/rdkafkacpp.h>
#include "uve_aggregator.h"
#include "uve_aggregator_types.h"

using std::map;
using std::string;
using pugi::xml_document;
using pugi::xml_parse_result;
using pugi::xml_node;

uint32_t
UVEAggregator::Clear(const std::string& proxy, int partition) {
    {
        tbb::mutex::scoped_lock lock(mutex_);
        offsets_[partition] = 0;
        part_period_[partition] = 0;
        if (part_commit_msg_[partition]) {
            part_commit_msg_[partition].reset();
        }
    }
    return SandeshUVETypeMaps::Clear(proxy, partition);
}

void
UVEAggregator::Update(std::auto_ptr<RdKafka::Message> message,
        uint64_t ts) {

    const string fkey(*(message->key()));
    const size_t pos(fkey.find(":"));
    const string table(fkey.substr(0, pos));
    const string key(fkey.substr(pos+1, string::npos));
    const string value(reinterpret_cast<char *>(message->payload()),
            message->len());
    const int32_t partition(message->partition());
    const uint64_t offset(message->offset());

    const uint64_t mono_ts((ts==0) ? ClockMonotonicUsec() : ts);
    const uint64_t clock_ts((ts==0) ? UTCTimestampUsec() : ts);

    const uint64_t new_period = mono_ts / kCommitPeriod_us;
    {
        tbb::mutex::scoped_lock lock(mutex_);
        if (new_period != part_period_[partition]) {
            // We just rolled over into a new epoch.
            // Commit the stored message, and use this
            // message to commit for the next epoch
            if (part_commit_msg_[partition]) {
                LOG(INFO, "Commiting offset " <<
                        part_commit_msg_[partition]->offset() << " part " <<
                        partition << " topic " <<
                        part_commit_msg_[partition]->topic_name() << 
                        " Err " << commit_cb_(part_commit_msg_[partition].get()));
                part_commit_msg_[partition].reset();
            }
            // Accept ownership of the message
            part_commit_msg_[partition].reset(message.get());
            message.release();
            part_period_[partition] = new_period;
        }

        // We need to ignore messages that have already been
        // processed into the UVE Cache.
        // This can happen during a partition rebalance, for
        // partitions that we have NOT lost. Scince we delay
        // commiting offsets, we can see messages again.
        if (offsets_[partition] != 0) {
            if (offset <= offsets_[partition]) {
                LOG(INFO, __func__ << " stale message by " <<
                    (offsets_[partition] - offset) << " offsets for " <<
                    topic_ << " partition " << partition);
                return;
            }
        }
        offsets_[partition] = offset;
    }
    xml_document xdoc;
    xml_parse_result result = xdoc.load_buffer(value.c_str(), value.size());
    if (result) {
        xml_node node  =  xdoc.first_child();
        
        // TODO: The message type used should be  based on conf_.
        //       The type of value should be verified.

        uint64_t sample = (uint64_t) strtoul(node.child_value(), NULL, 10);
        string tsstr(node.attribute("timestamp").value());
        if (tsstr.empty()) {
            return;
        }
        uint64_t ts = (uint64_t) strtoul(tsstr.c_str(), NULL, 10);
        uint64_t age = clock_ts - ts;

        // TODO: The age should be extracted from the Proxy UVE itself.
        if (conf_.find("AggProxySumAnomalyEWM01") != conf_.end()) {
            if (age <= 60000000) { 
                AggProxySumAnomalyEWM01 data;
                data.set_name(key);
                data.set_proxy(topic_);
                data.set_raw(sample);
                AggProxySumAnomalyEWM01Trace::Send(data, table,
                        0, partition);
            } else {
                LOG(INFO, __func__ << " stale message by " << age/1000000 <<
                    " sec for " << topic_ << " partition " << partition <<
                    " type AggProxySumAnomalyEWM01");
            }
        }
        if (conf_.find("AggProxySum") != conf_.end()){
            if (age <= 60000000) { 
                AggProxySum data;
                data.set_name(key);
                data.set_proxy(topic_);
                data.set_raw(sample);
                AggProxySumTrace::Send(data, table,
                        0, partition);
            } else {
                LOG(INFO, __func__ << " stale message by " << age/1000000 <<
                    " sec for " << topic_ << " partition " << partition <<
                    " type AggProxySum");
            }
        }
    }
}
