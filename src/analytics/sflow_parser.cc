/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include "base/logging.h"
#include "analytics/sflow_parser.h"
#include "sflow_types.h"


bool SFlowData::operator==(const SFlowData& rhs) const {
    if (!(sflow_header == rhs.sflow_header)) {
        return false;
    }
    if (flow_samples.size() != rhs.flow_samples.size()) {
        return false;
    }
    boost::ptr_vector<SFlowFlowSample>::const_iterator it1 =
        flow_samples.begin();
    boost::ptr_vector<SFlowFlowSample>::const_iterator it2 =
        rhs.flow_samples.begin();
    for (; it1 != flow_samples.end(); ++it1, ++it2) {
        if (!(*it1 == *it2)) {
            return false;
        }
    }
    return true;
}

SFlowParser::SFlowParser(const uint8_t* buf, size_t len,
                         SandeshTraceBufferPtr trace_buf)
    : raw_datagram_(buf), length_(len), end_ptr_(buf+len),
      decode_ptr_(reinterpret_cast<const uint32_t*>(buf)),
      trace_buf_(trace_buf) {
}

SFlowParser::~SFlowParser() {
}

int SFlowParser::Parse(SFlowData* const sflow_data) {
    if (ReadSFlowHeader(sflow_data->sflow_header) < 0) {
        SFLOW_PACKET_TRACE(trace_buf_, "Failed to parse sFlow header");
        return -1;
    }
    if (sflow_data->sflow_header.version != 5) {
        // unsupported version. Don't proceed futher.
        SFLOW_PACKET_TRACE(trace_buf_, "Unsupported sFlow version: " +
            integerToString(sflow_data->sflow_header.version));
        return -1;
    }
    for (uint32_t nsamples = 0; nsamples < sflow_data->sflow_header.nsamples;
         nsamples++) {
        if (!CanReadBytes(SFlowSample::kMinSampleLen)) {
            SFLOW_PACKET_TRACE(trace_buf_, "Invalid sample count [" +
                integerToString(sflow_data->sflow_header.nsamples) +
                "] in sFlow header (or) Tuncated sFlow packet");
            return -1;
        }
        uint32_t sample_type, sample_len;
        ReadData32NoCheck(sample_type);
        ReadData32NoCheck(sample_len);
        // Check if we can read sample_len bytes.
        if (!CanReadBytes(sample_len)) {
            SFLOW_PACKET_TRACE(trace_buf_, "Invalid sample length [" +
                integerToString(sample_len) + "] : sample type [" +
                integerToString(sample_type) + "] (or) "
                "Truncated sFlow packet");
            return -1;
        }
        // Preserve the start of the sample to verify that we
        // read exactly the sample_len bytes.
        const uint32_t* const sample_start = decode_ptr_;
        switch(sample_type) {
        case SFLOW_FLOW_SAMPLE: {
            SFlowFlowSample* flow_sample(new SFlowFlowSample(
                                SFLOW_FLOW_SAMPLE, sample_len));
            if (ReadSFlowFlowSample(*flow_sample) < 0) {
                delete flow_sample;
                return -1;
            }
            sflow_data->flow_samples.push_back(flow_sample);
            break;
        }
        case SFLOW_FLOW_SAMPLE_EXPANDED: {
            SFlowFlowSample* flow_sample(new SFlowFlowSample(
                                SFLOW_FLOW_SAMPLE_EXPANDED, sample_len));
            if (ReadSFlowFlowSample(*flow_sample) < 0) {
                delete flow_sample;
                return -1;
            }
            sflow_data->flow_samples.push_back(flow_sample);
            break;
        }
        default:
            SkipBytesNoCheck(sample_len);
            SFLOW_PACKET_TRACE(trace_buf_, "Skip sFlow Sample Type: " +
                integerToString(sample_type));
        }
        if (!VerifyLength(sample_start, sample_len)) {
            // sample length error
            SFLOW_PACKET_TRACE(trace_buf_, "Invalid sample length [" +
                integerToString(sample_len) + "] : sample type ["+
                integerToString(sample_type) + "]");
            return -1;
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

int SFlowParser::ReadSFlowFlowSample(SFlowFlowSample& flow_sample) {
    size_t min_flow_sample_len = SFlowFlowSample::kMinFlowSampleLen;
    if (flow_sample.type == SFLOW_FLOW_SAMPLE_EXPANDED) {
        min_flow_sample_len = SFlowFlowSample::kMinExpandedFlowSampleLen;
    }
    if (!CanReadBytes(min_flow_sample_len)) {
        SFLOW_PACKET_TRACE(trace_buf_, "Not enough bytes left to read "
            "Flow sample type: " + integerToString(flow_sample.type));
        return -1;
    }
    ReadData32NoCheck(flow_sample.seqno);
    if (flow_sample.type == SFLOW_FLOW_SAMPLE_EXPANDED) {
        ReadData32NoCheck(flow_sample.sourceid_type);
        ReadData32NoCheck(flow_sample.sourceid_index);
    } else {
        uint32_t sourceid;
        ReadData32NoCheck(sourceid);
        flow_sample.sourceid_type = sourceid >> 24;
        flow_sample.sourceid_index = sourceid & 0x00FFFFFF;
    }
    ReadData32NoCheck(flow_sample.sample_rate);
    ReadData32NoCheck(flow_sample.sample_pool);
    ReadData32NoCheck(flow_sample.drops);
    if (flow_sample.type == SFLOW_FLOW_SAMPLE_EXPANDED) {
        ReadData32NoCheck(flow_sample.input_port_format);
        ReadData32NoCheck(flow_sample.input_port);
        ReadData32NoCheck(flow_sample.output_port_format);
        ReadData32NoCheck(flow_sample.output_port);
    } else {
        uint32_t input, output;
        ReadData32NoCheck(input);
        ReadData32NoCheck(output);
        flow_sample.input_port_format = input >> 30;
        flow_sample.input_port = input & 0x3FFFFFFF;
        flow_sample.output_port_format = output >> 30;
        flow_sample.output_port = output & 0x3FFFFFFF; 
    }
    ReadData32NoCheck(flow_sample.nflow_records);
    for (uint32_t flow_rec = 0; flow_rec < flow_sample.nflow_records; 
         ++flow_rec) {
        if (!CanReadBytes(SFlowFlowRecord::kMinFlowRecordLen)) {
            SFLOW_PACKET_TRACE(trace_buf_, "Invalid flow record count [" +
                integerToString(flow_sample.nflow_records) + "] (or) "
                "Truncated sFlow packet");
            return -1;
        }
        uint32_t flow_record_type, flow_record_len;
        ReadData32NoCheck(flow_record_type);
        ReadData32NoCheck(flow_record_len);
        // Check if we can read flow_record_len bytes.
        if (!CanReadBytes(flow_record_len)) {
            SFLOW_PACKET_TRACE(trace_buf_, "Invalid flow record length [" +
                integerToString(flow_record_len) + "] : flow record type [" +
                integerToString(flow_record_type) + "] (or) "
                "Truncated sFlow packet");
            return -1;
        }
        // Preserve the start of the flow record to verify that we
        // read exactly the flow_record_len bytes.
        const uint32_t* const flow_record_start = decode_ptr_;
        switch(flow_record_type) {
        case SFLOW_FLOW_HEADER: {
            int ret = 0;
            SFlowFlowHeader* flow_header(new SFlowFlowHeader(flow_record_len));
            if ((ret = ReadSFlowFlowHeader(*flow_header)) < 0) {
                delete flow_header;
                return -1;
            }
            flow_sample.flow_records.push_back(flow_header);
            break;
        }
        default:
            SkipBytesNoCheck(flow_record_len);
            SFLOW_PACKET_TRACE(trace_buf_, "Skip processing of Flow Record: " +
                integerToString(flow_record_type));
        }
        if (!VerifyLength(flow_record_start, flow_record_len)) {
            SFLOW_PACKET_TRACE(trace_buf_, "Invalid flow record length [" +
                integerToString(flow_record_len) + "] : flow record type [" +
                integerToString(flow_record_type) + "]");
            return -1;
        }
    }
    return 0;
}

int SFlowParser::ReadSFlowFlowHeader(SFlowFlowHeader& flow_header) {
    if (!CanReadBytes(SFlowFlowHeader::kFlowHeaderInfoLen)) {
        SFLOW_PACKET_TRACE(trace_buf_, "Not enough bytes left to read "
            "Flow header info");
        return -1;
    }
    ReadData32NoCheck(flow_header.protocol);
    ReadData32(flow_header.frame_length);
    ReadData32(flow_header.stripped);
    ReadData32(flow_header.header_length);
    flow_header.header = (uint8_t*)decode_ptr_;
    if (SkipBytes(flow_header.header_length) < 0) {
        SFLOW_PACKET_TRACE(trace_buf_, "Invalid header length [" +
            integerToString(flow_header.header_length) + "] (or) "
            "Truncated sFlow packet");
        return -1;
    }
    switch(flow_header.protocol) {
    case SFLOW_FLOW_HEADER_ETHERNET_ISO8023: {
        int offset = 0;
        int eth_header_len = 0;
        if ((eth_header_len = DecodeEthernetHeader(
                                flow_header.header,
                                flow_header.header_length,
                                offset,
                                flow_header.decoded_eth_data)) < 0) {
            SFLOW_PACKET_TRACE(trace_buf_, "Flow Header protocol [" +
                integerToString(flow_header.protocol) + "] : Failed to "
                "decode Ethernet header");
            return 0;
        }
        offset += eth_header_len;
        flow_header.is_eth_data_set = true;
        // is this ip packet?
        if (flow_header.decoded_eth_data.ether_type == ETHERTYPE_IP) {
            int ip_header_len = 0;
            if ((ip_header_len = DecodeIpv4Header(
                                    flow_header.header,
                                    flow_header.header_length,
                                    offset,
                                    flow_header.decoded_ip_data)) < 0) {
                SFLOW_PACKET_TRACE(trace_buf_, "Flow Header protocol [" +
                    integerToString(flow_header.protocol) + "] : Failed to "
                    "decode Ipv4 header");
                return 0;
            }
            offset += ip_header_len;
            if (DecodeLayer4Header(flow_header.header,
                                   flow_header.header_length,
                                   offset,
                                   flow_header.decoded_ip_data) < 0) {
                SFLOW_PACKET_TRACE(trace_buf_, "Flow Header protocol [" +
                    integerToString(flow_header.protocol) + "] : Failed to "
                    "decode Layer4 header");
                return 0;
            }
            flow_header.is_ip_data_set = true;
        }
        break;
    }
    case SFLOW_FLOW_HEADER_IPV4: {
        int offset = 0;
        int ip_header_len = 0;
        if ((ip_header_len = DecodeIpv4Header(
                                    flow_header.header,
                                    flow_header.header_length,
                                    offset,
                                    flow_header.decoded_ip_data)) < 0) {
            SFLOW_PACKET_TRACE(trace_buf_, "Flow Header protocol [" +
                integerToString(flow_header.protocol) + "] : Failed to "
                "decode Ipv4 header");
            return 0;
        }
        offset += ip_header_len;
        if (DecodeLayer4Header(flow_header.header,
                               flow_header.header_length,
                               offset,
                               flow_header.decoded_ip_data) < 0) {
            SFLOW_PACKET_TRACE(trace_buf_, "Flow Header protocol [" +
                integerToString(flow_header.protocol) + "] : Failed to "
                "decode Layer4 header");
            return 0;
        }
        flow_header.is_ip_data_set = true;
        break;
    }
    default:
        SFLOW_PACKET_TRACE(trace_buf_, "Skip processing of protocol header: " +
            integerToString(flow_header.protocol));
    }
    return 0;
}

int SFlowParser::DecodeEthernetHeader(const uint8_t* header,
                                      size_t header_len,
                                      size_t offset,
                                      SFlowFlowEthernetData& eth_data) {
    // sanity check
    size_t ether_header_len = sizeof(struct ether_header);
    if (header_len < (offset + ether_header_len)) {
        SFLOW_PACKET_TRACE(trace_buf_, "Header length [" +
            integerToString(header_len) + "] not enough to decode "
            "Ethernet header");
        return -1;
    }

    struct ether_header* eth = (struct ether_header*)(header + offset);
    eth_data.src_mac = MacAddress(eth->ether_shost);
    eth_data.dst_mac = MacAddress(eth->ether_dhost);
    eth_data.ether_type = ntohs(eth->ether_type);
    if (eth_data.ether_type == ETHERTYPE_VLAN) {
        if (header_len < (offset + ether_header_len + 4)) {
            SFLOW_PACKET_TRACE(trace_buf_, "Header length [" +
                integerToString(header_len) + "] not enough to decode "
                "vlan header");
            return -1;
        }
        uint8_t *vlan_data = reinterpret_cast<uint8_t*>(eth) +
            sizeof(ether_header);
        eth_data.vlan_id = ((vlan_data[0] << 8) + vlan_data[1]) & 0x0FFF;
        // Now, read the ether_type
        eth_data.ether_type = ntohs(*(uint16_t*)(vlan_data + 2));
        return (ether_header_len + 4);
    }
    return ether_header_len;
}

int SFlowParser::DecodeIpv4Header(const uint8_t* header,
                                  size_t header_len,
                                  size_t offset,
                                  SFlowFlowIpData& ip_data) {
    // sanity check
    if (header_len < (offset + sizeof(struct ip))) {
        SFLOW_PACKET_TRACE(trace_buf_, "Header length [" +
            integerToString(header_len) + "] not enough to decode "
            "Ipv4 header");
        return -1;
    }

    struct ip* ip = (struct ip*)(header + offset);
    ip_data.length = ntohs(ip->ip_len);
    ip_data.protocol = ip->ip_p;
    ip_data.src_ip = IpAddress(Ip4Address(ntohl(ip->ip_src.s_addr)));
    ip_data.dst_ip = IpAddress(Ip4Address(ntohl(ip->ip_dst.s_addr)));
    ip_data.tos = ntohs(ip->ip_tos);
    return (ip->ip_hl << 2);
}

int SFlowParser::DecodeLayer4Header(const uint8_t* header,
                                    size_t header_len,
                                    size_t offset,
                                    SFlowFlowIpData& ip_data) {
    int len = 0;

    switch(ip_data.protocol) {
    case IPPROTO_ICMP: {
        len = sizeof(struct icmp);
        if (header_len < (offset + len)) {
            SFLOW_PACKET_TRACE(trace_buf_, "Header length [" +
                integerToString(header_len) + "] not enough to decode "
                "ICMP header");
            return -1;
        }
        struct icmp* icmp = (struct icmp*)(header + offset);
        if (icmp->icmp_type == ICMP_ECHO ||
            icmp->icmp_type == ICMP_ECHOREPLY) {
            ip_data.src_port = ntohs(icmp->icmp_id);
            ip_data.dst_port = ICMP_ECHOREPLY;
        } else {
            ip_data.src_port = 0;
            ip_data.dst_port = ntohs(icmp->icmp_type);
        }
        break;
    }
    case IPPROTO_TCP: {
        len = sizeof(tcphdr);
        if (header_len < (offset + len)) {
            SFLOW_PACKET_TRACE(trace_buf_, "Header length [" +
                integerToString(header_len) + "] not enough to decode "
                "TCP header");
            return -1;
        }
        tcphdr* tcp = (tcphdr*)(header + offset);
        ip_data.src_port = ntohs(tcp->source);
        ip_data.dst_port = ntohs(tcp->dest);
        break;
    }
    case IPPROTO_UDP: {
        len = sizeof(udphdr);
        if (header_len < (offset + len)) {
            SFLOW_PACKET_TRACE(trace_buf_, "Header length [" +
                integerToString(header_len) + "] not enough to decode "
                "UDP header");
            return -1;
        }
        udphdr* udp = (udphdr*)(header + offset);
        ip_data.src_port = ntohs(udp->source);
        ip_data.dst_port = ntohs(udp->dest);
        break;
    }
    default:
        SFLOW_PACKET_TRACE(trace_buf_, "Skip processing of layer 4 protocol: "
                           + integerToString(ip_data.protocol));
    }
    return len;
}

bool SFlowParser::VerifyLength(const uint32_t* start_decode_ptr,
                               size_t len) {
    return (start_decode_ptr+((len+3)/4) == decode_ptr_);
}

bool SFlowParser::CanReadBytes(size_t len) {
    return (decode_ptr_+((len+3)/4) <=
            reinterpret_cast<const uint32_t*>(end_ptr_));
}

void SFlowParser::SkipBytesNoCheck(size_t len) {
    // All the fields in SFlow datagram are 4-byte aligned
    decode_ptr_ += ((len+3)/4);
}

int SFlowParser::SkipBytes(size_t len) {
    SkipBytesNoCheck(len);
    if (decode_ptr_ > reinterpret_cast<const uint32_t*>(end_ptr_)) {
        return -1;
    }
    return 0;
}

void SFlowParser::ReadData32NoCheck(uint32_t& data32) {
    data32 = ntohl(*decode_ptr_++);
}

int SFlowParser::ReadData32(uint32_t& data32) {
    if ((decode_ptr_+1) > reinterpret_cast<const uint32_t*>(end_ptr_)) {
        return -1;
    }
    ReadData32NoCheck(data32);
    return 0;
}

int SFlowParser::ReadBytes(uint8_t *bytes, size_t len) {
    size_t rlen = (len+3)/4;
    if ((decode_ptr_+rlen) > reinterpret_cast<const uint32_t*>(end_ptr_)) {
        return -1;
    }
    memcpy(bytes, decode_ptr_, len);
    decode_ptr_ += rlen;
    return 0;
}

int SFlowParser::ReadIpaddress(IpAddress& ipaddr) {
    uint32_t ipaddr_type;
    if (ReadData32(ipaddr_type) < 0) {
        return -1;
    }
    if (ipaddr_type == SFLOW_IPADDR_V4) {
        Ip4Address::bytes_type ipv4;
        if (ReadBytes(ipv4.c_array(), ipv4.size()) < 0) {
            return -1;
        }
        ipaddr = Ip4Address(ipv4);
    } else if (ipaddr_type == SFLOW_IPADDR_V6) {
        Ip6Address::bytes_type ipv6;
        if (ReadBytes(ipv6.c_array(), ipv6.size()) < 0) {
            return -1;
        }
        ipaddr = Ip6Address(ipv6);
    } else {
        return -1;
    }
    return 0;
}
