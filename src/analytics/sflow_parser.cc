/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/ptr_container/ptr_map.hpp>

#include "base/logging.h"

#include "sflow_parser.h"

SFlowParser::SFlowParser(const uint8_t* buf, size_t len)
    : raw_datagram_(buf), length_(len), end_ptr_(buf+len),
      decode_ptr_(reinterpret_cast<const uint32_t*>(buf)) {
}

SFlowParser::~SFlowParser() {
}

int SFlowParser::Parse(SFlowData* const sflow_data) {
    if (ReadSFlowHeader(sflow_data->sflow_header) < 0) {
        return -1;
    }
    if (sflow_data->sflow_header.version != 5) {
        // unsupported version. Don't proceed futher.
        return -1;
    }
    for (uint32_t nsamples = 0; nsamples < sflow_data->sflow_header.nsamples;
         nsamples++) {
        uint32_t sample_type, sample_len;
        if (ReadData32(sample_type) < 0) {
            return -1;
        }
        if (ReadData32(sample_len) < 0) {
            return -1;
        }
        switch(sample_type) {
        case SFLOW_FLOW_SAMPLE: {
            SFlowFlowSampleData* fs_data(new SFlowFlowSampleData());
            if (ReadSFlowFlowSample(*fs_data, false) < 0) {
                delete fs_data;
                return -1;
            }
            sflow_data->flow_samples.push_back(fs_data);
            break;
        }
        case SFLOW_FLOW_SAMPLE_EXPANDED: {
            SFlowFlowSampleData* fs_data(new SFlowFlowSampleData());
            if (ReadSFlowFlowSample(*fs_data, true) < 0) {
                delete fs_data;
                return -1;
            }
            sflow_data->flow_samples.push_back(fs_data);
            break;
        }
        default:
            LOG(DEBUG, "Skip SFlow Sample Type: " << sample_type);
            if (SkipBytes(sample_len) < 0) {
                return -1;
            }
        }
    }
    return 0;
}

int SFlowParser::ReadSFlowHeader(SFlowHeader& sflow_header) {
    if (ReadData32(sflow_header.version) < 0) {
        return -1;
    }
    if (ReadIpaddress(sflow_header.agent_ip_address) < 0) {
        return -1;
    }
    if (ReadData32(sflow_header.agent_subid) < 0) {
        return -1;
    }
    if (ReadData32(sflow_header.seqno) < 0) {
        return -1;
    }
    if (ReadData32(sflow_header.uptime) < 0) {
        return -1;
    }
    if (ReadData32(sflow_header.nsamples) < 0) {
        return -1;
    }
    return 0;
}

int SFlowParser::ReadSFlowFlowSample(SFlowFlowSampleData& flow_sample_data, 
                                     bool expanded) {
    SFlowFlowSample& flow_sample = flow_sample_data.flow_sample;
    if (ReadData32(flow_sample.seqno) < 0) {
        return -1;
    }
    if (expanded) {
        if (ReadData32(flow_sample.sourceid_type) < 0) {
            return -1;
        }
        if (ReadData32(flow_sample.sourceid_index) < 0) {
            return -1;
        }
    } else {
        uint32_t sourceid;
        if (ReadData32(sourceid) < 0) {
            return -1;
        }
        flow_sample.sourceid_type = sourceid >> 24;
        flow_sample.sourceid_index = sourceid & 0x00FFFFFF;
    }
    if (ReadData32(flow_sample.sample_rate) < 0) {
        return -1;
    }
    if (ReadData32(flow_sample.sample_pool) < 0) {
        return -1;
    }
    if (ReadData32(flow_sample.drops) < 0) {
        return -1;
    }
    if (expanded) {
        if (ReadData32(flow_sample.input_port_format) < 0) {
            return -1;
        }
        if (ReadData32(flow_sample.input_port) < 0) {
            return -1;
        }
        if (ReadData32(flow_sample.output_port_format) < 0) {
            return -1;
        }
        if (ReadData32(flow_sample.output_port) < 0) {
            return -1;
        }
    } else {
        uint32_t input, output;
        if (ReadData32(input) < 0) {
            return -1;
        }
        if (ReadData32(output) < 0) {
            return -1;
        }
        flow_sample.input_port_format = input >> 30;
        flow_sample.input_port = input & 0x3FFFFFFF;
        flow_sample.output_port_format = output >> 30;
        flow_sample.output_port = output & 0x3FFFFFFF; 
    }
    if (ReadData32(flow_sample.nflow_records) < 0) {
        return -1;
    }
    for (uint32_t flow_rec = 0; flow_rec < flow_sample.nflow_records; 
         ++flow_rec) {
        uint32_t flow_record_type, flow_record_len;
        if (ReadData32(flow_record_type) < 0) {
            return -1;
        }
        if (ReadData32(flow_record_len) < 0) {
            return -1;
        }
        switch(flow_record_type) {
        case SFLOW_FLOW_HEADER: {
            SFlowFlowHeader* flow_header(new SFlowFlowHeader());
            if (ReadSFlowFlowHeader(*flow_header) < 0) {
                delete flow_header;
                return -1;
            }
            flow_sample_data.flow_records.insert(flow_record_type, 
                                                 flow_header);
            break;
        }
        default:
            if (SkipBytes(flow_record_len) < 0) {
                return -1;
            }
        }
    }
    return 0;
}

int SFlowParser::ReadSFlowFlowHeader(SFlowFlowHeader& flow_header) {
    if (ReadData32(flow_header.protocol) < 0) {
        return -1;
    }
    if (ReadData32(flow_header.frame_length) < 0) {
        return -1;
    }
    if (ReadData32(flow_header.stripped) < 0) {
        return -1;
    }
    if (ReadData32(flow_header.header_length) < 0) {
        return -1;
    }
    flow_header.header = (uint8_t*)decode_ptr_;
    if (SkipBytes(flow_header.header_length) < 0) {
        return -1;
    }
    switch(flow_header.protocol) {
    case SFLOW_FLOW_HEADER_IPV4: {
        if (DecodeIpv4Header(flow_header.header,
                             flow_header.decoded_ip_data) < 0) {
            return -1;
        }
        flow_header.is_ip_data_set = true;
    }
        break;
    default:
        LOG(DEBUG, "Skip processing of protocol header: " <<
            flow_header.protocol);
    }
    return 0;
}

int SFlowParser::DecodeIpv4Header(const uint8_t* ipv4h,
                                  SFlowFlowIpData& ip_data) {
    // add sanity check

    struct ip* ip = (struct ip*)ipv4h;
    ip_data.length = ntohs(ip->ip_len);
    ip_data.protocol = ip->ip_p;
    ip_data.src_ip.type = SFLOW_IPADDR_V4;
    memcpy(ip_data.src_ip.address.ipv4, &ip->ip_src.s_addr, 4);
    ip_data.dst_ip.type = SFLOW_IPADDR_V4;
    memcpy(ip_data.dst_ip.address.ipv4, &ip->ip_dst.s_addr, 4);
    ip_data.tos = ntohs(ip->ip_tos);
    const uint8_t* l4h = ipv4h + (ip->ip_hl << 2);
    DecodeLayer4Header(l4h, ip_data);
    return 0;
}

int SFlowParser::DecodeLayer4Header(const uint8_t* l4h,
                                    SFlowFlowIpData& ip_data) {
    // add sanity check

    switch(ip_data.protocol) {
    case IPPROTO_ICMP: {
        struct icmp* icmp = (struct icmp*)l4h;
        if (icmp->icmp_type == ICMP_ECHO || icmp->icmp_type == ICMP_ECHOREPLY) {
            ip_data.src_port = ntohs(icmp->icmp_id);
            ip_data.dst_port = ICMP_ECHOREPLY;
        } else {
            ip_data.src_port = 0;
            ip_data.dst_port = ntohs(icmp->icmp_type);
        }
        break;
    }
    case IPPROTO_TCP: {
        tcphdr* tcp = (tcphdr*)l4h;
        ip_data.src_port = ntohs(tcp->source);
        ip_data.dst_port = ntohs(tcp->dest);
        break;
    }
    case IPPROTO_UDP: {
        udphdr* udp = (udphdr*)l4h;
        ip_data.src_port = ntohs(udp->source);
        ip_data.dst_port = ntohs(udp->dest);
        break;
    }
    }
    return 0;
}
