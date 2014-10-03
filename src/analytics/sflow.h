/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __SFLOW_H__
#define __SFLOW_H__

enum SFlowIpaddressType {
    SFLOW_IPADDR_UNKNOWN = 0,
    SFLOW_IPADDR_V4,
    SFLOW_IPADDR_V6
};

struct SFlowIpaddress {
    uint32_t type;
    union {
        uint8_t ipv4[4];
        uint8_t ipv6[16];
    } address;

    explicit SFlowIpaddress() : type(), address() {
    }
    ~SFlowIpaddress() {
    }
    const std::string ToString() const {
        std::stringstream ss;
        if (type == SFLOW_IPADDR_V4) {
            char ipv4_str[INET_ADDRSTRLEN];
            ss << inet_ntop(AF_INET, address.ipv4,
                            ipv4_str, INET_ADDRSTRLEN);
        } else if (type == SFLOW_IPADDR_V6) {
            char ipv6_str[INET6_ADDRSTRLEN];
            ss << inet_ntop(AF_INET6, address.ipv6,
                            ipv6_str, INET6_ADDRSTRLEN);
        }
        return ss.str();
    }
    friend inline std::ostream& operator<<(std::ostream& out, 
                    const SFlowIpaddress& sflow_ipaddr) {
        out << sflow_ipaddr.ToString();
        return out;
    }
};

struct SFlowHeader {
    uint32_t version;
    SFlowIpaddress agent_ip_address;
    uint32_t agent_subid;
    uint32_t seqno;
    uint32_t uptime;
    uint32_t nsamples;

    explicit SFlowHeader() 
        : version(), agent_ip_address(), agent_subid(), seqno(),
          uptime(), nsamples() {
    }
    ~SFlowHeader() {
    }
    friend inline std::ostream& operator<<(std::ostream& out, 
                    const SFlowHeader& sflow_header) {
        out << "== sFlow Header ==" << std::endl;
        out << "Version: " << sflow_header.version << std::endl;
        out << "Agent " << sflow_header.agent_ip_address 
            << std::endl;
        out << "Agent subid: " << sflow_header.agent_subid << std::endl;
        out << "Sequence no: " << sflow_header.seqno << std::endl;
        out << "Uptime: " << sflow_header.uptime << std::endl;
        out << "Num samples: " << sflow_header.nsamples << std::endl;
        return out;
    }
};

enum SFlowSampleType {
    SFLOW_FLOW_SAMPLE = 1,
    SFLOW_FLOW_SAMPLE_EXPANDED = 3
};

struct SFlowFlowSample {
    uint32_t seqno;
    uint32_t sourceid_type;
    uint32_t sourceid_index;
    uint32_t sample_rate;
    uint32_t sample_pool;
    uint32_t drops;
    uint32_t input_port_format;
    uint32_t input_port;
    uint32_t output_port_format;
    uint32_t output_port;
    uint32_t nflow_records;

    explicit SFlowFlowSample()
        : seqno(), sourceid_type(), sourceid_index(), sample_rate(),
          sample_pool(), drops(), input_port_format(), input_port(),
          output_port_format(), output_port(), nflow_records() {
    }
    ~SFlowFlowSample() {
    }
    friend std::ostream& operator<<(std::ostream& out, 
                                    const SFlowFlowSample& flow_sample) {
        out << "== Flow Sample ==" << std::endl;
        out << "Seqno: " << flow_sample.seqno << std::endl;
        out << "Sourceid type: " << flow_sample.sourceid_type << std::endl;
        out << "Sourceid index: " << flow_sample.sourceid_index << std::endl;
        out << "Sample rate: " << flow_sample.sample_rate << std::endl;
        out << "Sample pool: " << flow_sample.sample_pool << std::endl;
        out << "Drops: " << flow_sample.drops << std::endl;
        out << "Input port format: " << flow_sample.input_port_format 
            << std::endl;
        out << "Input port: " << flow_sample.input_port << std::endl;
        out << "Output port format: " << flow_sample.output_port_format
            << std::endl;
        out << "Output port: " << flow_sample.output_port << std::endl;
        out << "Num Flow records: " << flow_sample.nflow_records << std::endl;
        return out;
    }
};

enum SFlowFlowRecordType {
    SFLOW_FLOW_HEADER = 1
};

struct SFlowFlowRecord {
    explicit SFlowFlowRecord() {
    }
    virtual ~SFlowFlowRecord() {
    }
};

enum SFlowFlowHeaderProtocol {
    SFLOW_FLOW_HEADER_ETHERNET_ISO8023 = 1,
    SFLOW_FLOW_HEADER_IPV4 = 11
};

struct SFlowFlowIpData {
    uint32_t length;
    uint32_t protocol;
    SFlowIpaddress src_ip;
    SFlowIpaddress dst_ip;
    uint32_t src_port;
    uint32_t dst_port;
    uint32_t tcp_flags;
    uint32_t tos;

    explicit SFlowFlowIpData() 
        : length(), protocol(), src_ip(), dst_ip(), src_port(),
          dst_port(), tcp_flags(), tos() {
    }
    friend std::ostream& operator<<(std::ostream& out, 
                                    const SFlowFlowIpData& ip_data) { 
        out << "== IP Data ==" << std::endl;
        out << "Length: " << ip_data.length << std::endl;
        out << "Source " << ip_data.src_ip << std::endl;
        out << "Destination " << ip_data.dst_ip << std::endl;
        out << "Source port: " << ip_data.src_port << std::endl;
        out << "Destination port: " << ip_data.dst_port << std::endl;
        out << "Tos: " << ip_data.tos << std::endl;
        return out;
    }
};

struct SFlowFlowHeader : public SFlowFlowRecord {
    uint32_t protocol;
    uint32_t frame_length;
    uint32_t stripped;
    uint32_t header_length;
    uint8_t* header;
    bool is_ip_data_set;
    SFlowFlowIpData decoded_ip_data;

    explicit SFlowFlowHeader() 
        : protocol(), frame_length(), stripped(), header_length(),
          header(), is_ip_data_set(), decoded_ip_data() {
    }
    virtual ~SFlowFlowHeader() {
    }
    friend std::ostream& operator<<(std::ostream& out, 
                                    const SFlowFlowHeader& flow_header) {
        out << "== Flow Header ==" << std::endl;
        out << "Protocol: " << flow_header.protocol << std::endl;
        out << "Frame length: " << flow_header.frame_length << std::endl;
        out << "Stripped: " << flow_header.stripped << std::endl;
        out << "Header length: " << flow_header.header_length << std::endl;
        if (flow_header.is_ip_data_set) {
            out << flow_header.decoded_ip_data;
        }
        return out;
    }
};

#endif // __SFLOW_H__
