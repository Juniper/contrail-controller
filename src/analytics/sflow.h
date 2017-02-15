/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __SFLOW_H__
#define __SFLOW_H__

#include <stdint.h>
#include <ostream>
#include <sstream>

#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/bind.hpp>

#include "net/address.h"
#include "net/mac_address.h"


enum SFlowIpaddressType {
    SFLOW_IPADDR_UNKNOWN = 0,
    SFLOW_IPADDR_V4,
    SFLOW_IPADDR_V6
};

enum SFlowFlowRecordType {
    SFLOW_FLOW_HEADER = 1
};

struct SFlowFlowRecord {
    SFlowFlowRecordType type;
    uint32_t length;

    static const size_t kMinFlowRecordLen = 8;

    explicit SFlowFlowRecord(SFlowFlowRecordType record_type,
                             uint32_t record_len)
        : type(record_type),
          length(record_len) {
    }
    virtual ~SFlowFlowRecord() {
    }
};

enum SFlowFlowHeaderProtocol {
    SFLOW_FLOW_HEADER_ETHERNET_ISO8023 = 1,
    SFLOW_FLOW_HEADER_IPV4 = 11
};

struct SFlowFlowEthernetData {
    MacAddress src_mac;
    MacAddress dst_mac;
    uint16_t vlan_id;
    uint16_t ether_type;

    explicit SFlowFlowEthernetData()
        : src_mac(), dst_mac(), vlan_id(), ether_type() {
    }
    ~SFlowFlowEthernetData() {
    }
    bool operator==(const SFlowFlowEthernetData& rhs) const;
    friend std::ostream& operator<<(std::ostream& out,
                                    const SFlowFlowEthernetData &eth_data);
};

struct SFlowFlowIpData {
    uint32_t length;
    uint32_t protocol;
    IpAddress src_ip;
    IpAddress dst_ip;
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
    bool operator==(const SFlowFlowIpData& rhs) const;
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

    // protocol + frame_length + stripped + header_length
    static const size_t kFlowHeaderInfoLen = 16;

    explicit SFlowFlowHeader(uint32_t flow_record_len)
        : SFlowFlowRecord(SFLOW_FLOW_HEADER, flow_record_len),
          protocol(), frame_length(), stripped(), header_length(),
          header(), is_eth_data_set(), is_ip_data_set(),
          decoded_eth_data(), decoded_ip_data() {
    }
    virtual ~SFlowFlowHeader() {
    }
    bool operator==(const SFlowFlowHeader& rhs) const;
    friend std::ostream& operator<<(std::ostream& out,
                                    const SFlowFlowHeader& flow_header);
};

enum SFlowSampleType {
    SFLOW_FLOW_SAMPLE = 1,
    SFLOW_COUNTER_SAMPLE = 2,
    SFLOW_FLOW_SAMPLE_EXPANDED = 3,
    SFLOW_COUNTER_SAMPLE_EXPANDED = 4
};

struct SFlowSample {
    SFlowSampleType type;
    uint32_t length;

    static const size_t kMinSampleLen = 8;

    explicit SFlowSample(SFlowSampleType sample_type, uint32_t sample_len)
        : type(sample_type),
          length(sample_len) {
    }
    virtual ~SFlowSample() {
    }
};

struct SFlowFlowSample : SFlowSample {
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
    boost::ptr_vector<SFlowFlowRecord> flow_records;

    static const size_t kMinFlowSampleLen = 32;
    static const size_t kMinExpandedFlowSampleLen = 44;

    explicit SFlowFlowSample(SFlowSampleType sample_type, uint32_t sample_len)
        : SFlowSample(sample_type, sample_len),
          seqno(), sourceid_type(), sourceid_index(), sample_rate(),
          sample_pool(), drops(), input_port_format(), input_port(),
          output_port_format(), output_port(), nflow_records(),
          flow_records() {
    }
    ~SFlowFlowSample() {
    }
    bool operator==(const SFlowFlowSample& rhs) const;
    friend std::ostream &operator<<(std::ostream &out,
                                    const SFlowFlowSample &flow_sample);
};

struct SFlowHeader {
    uint32_t version;
    IpAddress agent_ip_address;
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
    bool operator==(const SFlowHeader&) const;
    friend std::ostream &operator<<(std::ostream &out,
                                    const SFlowHeader &sflow_header);
};

#endif // __SFLOW_H__
