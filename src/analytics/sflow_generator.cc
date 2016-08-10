/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "sflow_collector.h"
#include "sflow_generator.h"
#include "sflow_parser.h"
#include "uflow_constants.h"
#include "uflow_types.h"
#include "sflow_types.h"

SFlowQueueEntry::SFlowQueueEntry(boost::asio::const_buffer buf, size_t len,
                                 uint64_t ts, SFlowCollector* collector) 
    : buffer(buf), length(len), timestamp(ts), sflow_collector(collector) {
}

SFlowQueueEntry::~SFlowQueueEntry() {
    sflow_collector->DeallocateBuffer(buffer);
}

SFlowGenerator::SFlowGenerator(const std::string& ip_address,
                               SFlowCollector* sflow_collector,
                               DbHandlerPtr db_handler)
    : ip_address_(ip_address),
      sflow_collector_(sflow_collector),
      db_handler_(db_handler),
      sflow_pkt_queue_(TaskScheduler::GetInstance()->GetTaskId(
            "SFlowGenerator:"+ip_address), 0,
            boost::bind(&SFlowGenerator::ProcessSFlowPacket, this, _1)),
      trace_buf_(SandeshTraceBufferCreate("SFlowGenerator:"+ip_address, 1000)) {
}

SFlowGenerator::~SFlowGenerator() {
}

bool SFlowGenerator::EnqueueSFlowPacket(const boost::asio::const_buffer& buffer,
                                        size_t length, uint64_t timestamp) {
    num_packets_++;
    time_last_pkt_seen_ = timestamp;
    if (!time_first_pkt_seen_) {
        time_first_pkt_seen_ = time_last_pkt_seen_;
    }
    boost::shared_ptr<SFlowQueueEntry> qentry(new SFlowQueueEntry(buffer, 
        length, timestamp, sflow_collector_));
    return sflow_pkt_queue_.Enqueue(qentry);
}

bool SFlowGenerator::ProcessSFlowPacket(
        boost::shared_ptr<SFlowQueueEntry> qentry) {
    SFlowParser parser(boost::asio::buffer_cast<const uint8_t* const>(
                       qentry->buffer), qentry->length, trace_buf_);
    SFlowData sflow_data;
    if (parser.Parse(&sflow_data) < 0) {
        LOG(ERROR, "Error parsing sFlow packet");
        return false;
    }
    std::stringstream sflow_data_str;
    sflow_data_str << "sFlow Packet: " << sflow_data;
    SFLOW_PACKET_TRACE(trace_buf_, sflow_data_str.str());

    UFlowData flow_data;
    flow_data.set_name(ip_address_);
    std::string flow_type =
        g_uflow_constants.FlowTypeName.find(FlowType::SFLOW)->second;
    boost::ptr_vector<SFlowFlowSample>::const_iterator fs_it =
        sflow_data.flow_samples.begin();
    std::vector<UFlowSample> samples;
    for (; fs_it != sflow_data.flow_samples.end(); ++fs_it) {
        const SFlowFlowSample& flow_sample = *fs_it;
        UFlowSample sample;
        sample.set_pifindex(flow_sample.sourceid_index);
        boost::ptr_vector<SFlowFlowRecord>::const_iterator fr_it =
            flow_sample.flow_records.begin();
        for (; fr_it != flow_sample.flow_records.end(); ++fr_it) {
            switch(fr_it->type) {
            case SFLOW_FLOW_HEADER: {
                SFlowFlowHeader& record = (SFlowFlowHeader&)*fr_it;
                if (record.is_ip_data_set) {
                    SFlowFlowIpData& ip_data = record.decoded_ip_data;
                    sample.sip = ip_data.src_ip.to_string();
                    sample.dip = ip_data.dst_ip.to_string();
                    sample.sport = ip_data.src_port;
                    sample.dport = ip_data.dst_port;
                    sample.protocol = ip_data.protocol;
                    sample.flowtype = flow_type;
                    samples.push_back(sample);
                }
                break;
            }
            default:
                break;
            }
        }
    }
    if (samples.size()) {
        flow_data.set_flow(samples);
        db_handler_->UnderlayFlowSampleInsert(flow_data, qentry->timestamp,
            GenDb::GenDbIf::DbAddColumnCb());
    }
    return true;
}
