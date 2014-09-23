/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __SFLOW_PARSER_H__
#define __SFLOW_PARSER_H__

#include "base/util.h"
#include "sflow.h"

struct SFlowFlowSampleData {
    SFlowFlowSample flow_sample;
    boost::ptr_map<uint32_t, SFlowFlowRecord> flow_records;

    explicit SFlowFlowSampleData() : 
        flow_sample(), flow_records() {
    }
    ~SFlowFlowSampleData() {
    }
    friend inline std::ostream& operator<<(std::ostream& out, 
                    const SFlowFlowSampleData& fs_data);
};

std::ostream& operator<<(std::ostream& out, 
            const SFlowFlowSampleData& fs_data) {
    out << fs_data.flow_sample;
    boost::ptr_map<uint32_t, SFlowFlowRecord>::const_iterator it =
        fs_data.flow_records.begin();
    for (; it != fs_data.flow_records.end(); ++it) {
        SFlowFlowHeader* sflow_header = (SFlowFlowHeader*)it->second;
        out << *sflow_header;
    }
    return out;
}

struct SFlowData {
    SFlowHeader sflow_header;
    boost::ptr_vector<SFlowFlowSampleData> flow_samples;

    explicit SFlowData() : sflow_header(), flow_samples() {
    }
    ~SFlowData() {
    }
    friend inline std::ostream& operator<<(std::ostream& out, 
                                           const SFlowData& sflow_data);
};

std::ostream& operator<<(std::ostream& out, const SFlowData& sflow_data) {
    out << std::endl;
    out << sflow_data.sflow_header;
    boost::ptr_vector<SFlowFlowSampleData>::const_iterator it = 
        sflow_data.flow_samples.begin();
    out << "Num of Flow Samples: " << sflow_data.flow_samples.size() 
        << std::endl;
    for (; it != sflow_data.flow_samples.end(); ++it) {
        out << *it << std::endl;
    }
    return out;
}

class SFlowParser {
public:
    explicit SFlowParser(const uint8_t* buf, size_t len);
    ~SFlowParser();
    int Parse(SFlowData* const sflow_data);
private:
    int ReadSFlowHeader(SFlowHeader& sflow_header);
    int ReadSFlowFlowSample(SFlowFlowSampleData& flow_sample_data, 
                            bool expanded);
    int ReadSFlowFlowHeader(SFlowFlowHeader& flow_header);
    // move decoding functions to a different class
    int DecodeIpv4Header(const uint8_t* ipv4h, SFlowFlowIpData& ip_data);
    int DecodeLayer4Header(const uint8_t* l4h, SFlowFlowIpData& ip_data);
    int SkipBytes(size_t len) {
        // All the fields in SFlow datagram are 4-byte aligned
        decode_ptr_ += ((len+3)/4);
        if (decode_ptr_ > reinterpret_cast<const uint32_t*>(end_ptr_)) {
            return -1;
        }
        return 0;
    }
    int ReadData32(uint32_t& data32) {
        if ((decode_ptr_+1) > reinterpret_cast<const uint32_t*>(end_ptr_)) {
            return -1;
        }
        data32 = ntohl(*decode_ptr_++);
        return 0;
    }
    int ReadBytes(uint8_t *bytes, size_t len) {
        memcpy(bytes, decode_ptr_, len);
        return SkipBytes(len);
    }
    int ReadIpaddress(SFlowIpaddress& ipaddr) {
        if (ReadData32(ipaddr.type) < 0) {
            return -1;
        }
        if (ipaddr.type == SFLOW_IPADDR_V4) {
            if (ReadBytes(ipaddr.address.ipv4, 4) < 0) {
                return -1;
            }
        } else if (ipaddr.type == SFLOW_IPADDR_V6) {
            if (ReadBytes(ipaddr.address.ipv6, 16) < 0) {
                return -1;
            }
        } else {
            return -1;
        }
        return 0;
    }
    
    const uint8_t* const raw_datagram_;
    uint16_t length_;
    const uint8_t* const end_ptr_;
    const uint32_t* decode_ptr_;
    
    DISALLOW_COPY_AND_ASSIGN(SFlowParser);
};

#endif // __SFLOW_PARSER_H__

