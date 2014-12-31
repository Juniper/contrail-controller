/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __SFLOW_H__
#define __SFLOW_H__

#include <stdint.h>
#include <ostream>
#include <sstream>

struct SFlowMacaddress {
    uint8_t addr[6];

    explicit SFlowMacaddress() : addr() {
    }
    ~SFlowMacaddress() {
    }
    friend std::ostream &operator<<(std::ostream &out,
                                    const SFlowMacaddress &sflow_mac);
};

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
    const std::string ToString() const;
    friend std::ostream &operator<<(std::ostream &out,
                                    const SFlowIpaddress &sflow_ipaddr);
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
    friend std::ostream &operator<<(std::ostream &out,
                                    const SFlowHeader &sflow_header);
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
    friend std::ostream &operator<<(std::ostream &out,
                                    const SFlowFlowSample &flow_sample);
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

struct SFlowFlowEthernetData {
    SFlowMacaddress src_mac;
    SFlowMacaddress dst_mac;
    uint16_t vlan_id;
    uint16_t ether_type;

    explicit SFlowFlowEthernetData()
        : src_mac(), dst_mac(), vlan_id(), ether_type() {
    }
    ~SFlowFlowEthernetData() {
    }
    friend std::ostream& operator<<(std::ostream& out,
                                    const SFlowFlowEthernetData &eth_data);
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
    ~SFlowFlowIpData() {
    }
    friend std::ostream &operator<<(std::ostream &out,
                                    const SFlowFlowIpData &ip_data);
};

struct SFlowFlowHeader : public SFlowFlowRecord {
    uint32_t protocol;
    uint32_t frame_length;
    uint32_t stripped;
    uint32_t header_length;
    uint8_t* header;
    bool is_eth_data_set;
    bool is_ip_data_set;
    SFlowFlowEthernetData decoded_eth_data;
    SFlowFlowIpData decoded_ip_data;

    explicit SFlowFlowHeader() 
        : protocol(), frame_length(), stripped(), header_length(),
          header(), is_eth_data_set(), is_ip_data_set(),
          decoded_eth_data(), decoded_ip_data() {
    }
    virtual ~SFlowFlowHeader() {
    }
    friend std::ostream& operator<<(std::ostream& out,
                                    const SFlowFlowHeader& flow_header);
};

#endif // __SFLOW_H__
