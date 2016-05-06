/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>

#include <analytics/sflow.h>

class PktHeaderGen {
public:
    const static int kMaxPktHeaderLen = 128;

    explicit PktHeaderGen() :
        len_(0) {
        memset(buff_, 0, kMaxPktHeaderLen);
    }
    ~PktHeaderGen() {
    }

    void WriteEthHdr(const std::string& dmac, const std::string& smac,
                     uint16_t proto, int16_t vlan_id) {
        struct ether_header* eth = (struct ether_header*)(buff_ + len_);
        len_ += sizeof(struct ether_header);

        MacAddress::FromString(dmac).ToArray(eth->ether_dhost,
                                             sizeof(eth->ether_dhost));
        MacAddress::FromString(smac).ToArray(eth->ether_shost,
                                             sizeof(eth->ether_shost));
        uint16_t* ptr = (uint16_t*)(buff_ + (ETHER_ADDR_LEN*2));
        if (vlan_id != -1) {
            *ptr++ = htons(ETHERTYPE_VLAN);
            *ptr++ = htons(vlan_id & 0xFFF);
            len_ += 4;
        }
        *ptr = htons(proto);
    }

    void WriteIpHdr(const std::string& sip, const std::string& dip,
                    uint16_t proto, uint16_t ip_len) {
        struct ip* iph = (struct ip*)(buff_ + len_);
        len_ += sizeof(struct ip);

        iph->ip_hl = 5;
        iph->ip_v = 4;
        iph->ip_len = htons(ip_len);
        iph->ip_src.s_addr = inet_addr(sip.c_str());
        iph->ip_dst.s_addr = inet_addr(dip.c_str());
        iph->ip_p = proto;
    }

    void WriteUdpHdr(uint16_t sport, uint16_t dport) {
        struct udphdr* udp = (struct udphdr*)(buff_ + len_);
        len_ += sizeof(udphdr);
        udp->source = htons(sport);
        udp->dest = htons(dport);
    }

    void WriteTcpHdr(uint16_t sport, uint16_t dport) {
        struct tcphdr* tcp = (struct tcphdr*)(buff_ + len_);
        len_ += sizeof(tcphdr);
        tcp->source = htons(sport);
        tcp->dest = htons(dport);
    }

    void WriteIcmpHdr(uint16_t icmp_type, uint16_t icmpid) {
        struct icmp* icmp = (struct icmp*)(buff_ + len_);
        len_ += sizeof(struct icmp);
        icmp->icmp_type = icmp_type;
        icmp->icmp_id = icmpid;
    }

    const uint8_t* GetPktHeader() const {
        return buff_;
    }

    size_t GetPktHeaderLen() const {
        return len_;
    }

private:
    uint8_t buff_[kMaxPktHeaderLen];
    size_t len_;
};

class SFlowPktGen {
public:
    const static int kMaxSFlowPktLen = 1024;

    explicit SFlowPktGen() :
        encode_ptr_(buff_),
        end_ptr_(buff_+(kMaxSFlowPktLen/4)) {
        memset(buff_, 0, kMaxSFlowPktLen);
    }
    ~SFlowPktGen() {
    }

    void WriteHeader(const SFlowHeader& sflow_header) {
        WriteData32(sflow_header.version);
        WriteIpAddress(sflow_header.agent_ip_address);
        WriteData32(sflow_header.agent_subid);
        WriteData32(sflow_header.seqno);
        WriteData32(sflow_header.uptime);
        WriteData32(sflow_header.nsamples);
    }

    void WriteFlowSample(const SFlowFlowSample& flow_sample) {
        WriteData32(flow_sample.type);
        WriteData32(flow_sample.length);
        WriteData32(flow_sample.seqno);
        if (flow_sample.type == SFLOW_FLOW_SAMPLE_EXPANDED) {
            WriteData32(flow_sample.sourceid_type);
            WriteData32(flow_sample.sourceid_index);
        } else {
            uint32_t sourceid = (flow_sample.sourceid_type << 24) |
                                (flow_sample.sourceid_index);
            WriteData32(sourceid);
        }
        WriteData32(flow_sample.sample_rate);
        WriteData32(flow_sample.sample_pool);
        WriteData32(flow_sample.drops);
        if (flow_sample.type == SFLOW_FLOW_SAMPLE_EXPANDED) {
            WriteData32(flow_sample.input_port_format);
            WriteData32(flow_sample.input_port);
            WriteData32(flow_sample.output_port_format);
            WriteData32(flow_sample.output_port);
        } else {
            uint32_t input = (flow_sample.input_port_format << 30) |
                             (flow_sample.input_port);
            WriteData32(input);
            uint32_t output = (flow_sample.output_port_format << 30) |
                              (flow_sample.output_port);
            WriteData32(output);
        }
        WriteData32(flow_sample.nflow_records);
        boost::ptr_vector<SFlowFlowRecord>::const_iterator it =
            flow_sample.flow_records.begin();
        for (; it != flow_sample.flow_records.end(); ++it) {
            WriteData32(it->type);
            WriteData32(it->length);
            switch(it->type) {
            case SFLOW_FLOW_HEADER: {
                SFlowFlowHeader& flow_header = (SFlowFlowHeader&)*it;
                WriteData32(flow_header.protocol);
                WriteData32(flow_header.frame_length);
                WriteData32(flow_header.stripped);
                WriteData32(flow_header.header_length);
                WriteBytes(flow_header.header, flow_header.header_length);
                break;
            }
            default:
                assert(0);
            }
        }
    }

    void WriteCounterSample(const SFlowSample& counter_sample) {
        std::unique_ptr<uint8_t> counter_sample_data(
                                    new uint8_t[counter_sample.length]);

        WriteData32(counter_sample.type);
        WriteData32(counter_sample.length);
        WriteBytes(counter_sample_data.get(), counter_sample.length);
    }

    const uint8_t* GetSFlowPkt() const {
        return reinterpret_cast<const uint8_t*>(buff_);
    }

    size_t GetSFlowPktLen() const {
        return (encode_ptr_ - buff_) * 4;
    }

private:
    void WriteData32(uint32_t data) {
        assert(encode_ptr_+1 <= end_ptr_);
        *encode_ptr_++ = htonl(data);
    }

    void WriteBytes(const uint8_t* bytes, size_t len) {
        assert(encode_ptr_+((len+3)/4) <= end_ptr_);
        memcpy(encode_ptr_, bytes, len);
        encode_ptr_ += ((len+3)/4);
    }

    void WriteIpAddress(const IpAddress& ipaddr) {
        if (ipaddr.is_v4()) {
            WriteData32(SFLOW_IPADDR_V4);
            WriteBytes(ipaddr.to_v4().to_bytes().c_array(), 4);
        } else {
            WriteData32(SFLOW_IPADDR_V6);
            WriteBytes(ipaddr.to_v6().to_bytes().c_array(), 16);
        }
    }

    uint32_t buff_[kMaxSFlowPktLen/4];
    uint32_t* encode_ptr_;
    const uint32_t* const end_ptr_;
};
