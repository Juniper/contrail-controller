/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */


#include <testing/gunit.h>
#include <boost/assign/list_of.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/random/mersenne_twister.hpp>

#include <base/logging.h>

#include <analytics/sflow_parser.h>
#include <analytics/test/sflow_pktgen.h>

class SFlowParserTest : public ::testing::Test {
protected:
    SFlowParserTest() :
        trace_buf_(SandeshTraceBufferCreate("SFlowParserTest", 100)) {
    }
    ~SFlowParserTest() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    void VerifySFlowParse(const SFlowData& exp_sflow_data,
                          const SFlowPktGen& sflow_pktgen) const {
        SFlowParser parser(sflow_pktgen.GetSFlowPkt(),
                           sflow_pktgen.GetSFlowPktLen(), trace_buf_);
        LOG(INFO, "sFlow Packet Len: " << sflow_pktgen.GetSFlowPktLen());
        SFlowData act_sflow_data;
        EXPECT_EQ(0, parser.Parse(&act_sflow_data));
        std::stringstream exp_sflow_data_str;
        exp_sflow_data_str << exp_sflow_data;
        LOG(INFO, "Expected sFlow Data: " << exp_sflow_data_str.str());
        std::stringstream act_sflow_data_str;
        act_sflow_data_str << act_sflow_data;
        LOG(INFO, "Actual sFlow Data: " << act_sflow_data_str.str());
        EXPECT_TRUE(exp_sflow_data == act_sflow_data);
    }

    void VerifySFlowParseError(const SFlowPktGen& sflow_pktgen,
                               size_t sflow_pktlen) const {
        SFlowParser parser(sflow_pktgen.GetSFlowPkt(),
                           sflow_pktlen, trace_buf_);
        SFlowData act_sflow_data;
        EXPECT_EQ(-1, parser.Parse(&act_sflow_data));
    }

    size_t ComputeSFlowFlowRecordLength(
                const SFlowFlowRecord& flow_record) const {
        size_t len = 0;

        switch(flow_record.type) {
            case SFLOW_FLOW_HEADER: {
                SFlowFlowHeader& sflow_header = (SFlowFlowHeader&)flow_record;
                // header_protocol + frame_length + stripped + header_length
                len += 16;
                len += ((sflow_header.header_length + 3)/4) * 4;
                break;
            }
            default:
                assert(0);
        }
        return len;
    }

    size_t ComputeSFlowFlowSampleLength(
                const SFlowFlowSample& flow_sample) const {
        size_t len;

        if (flow_sample.type == SFLOW_FLOW_SAMPLE_EXPANDED) {
            len = 44;
        } else {
            len = 32;
        }
        boost::ptr_vector<SFlowFlowRecord>::const_iterator it =
            flow_sample.flow_records.begin();
        for (; it != flow_sample.flow_records.end(); ++it) {
            len += 8; // type + length
            len += ComputeSFlowFlowRecordLength(*it);
        }
        return len;
    }

    void CreateSFlowHeader(SFlowHeader& sflow_header,
                           const std::string& agent_addr,
                           uint32_t nsamples, uint32_t version=5) {
        FillSFlowHeader(sflow_header, version, agent_addr,
                        ++sflow_seqno_, nsamples);
    }

    void CreateSFlowFlowSample1(SFlowFlowSample& flow_sample,
                                uint32_t protocol,
                                const std::string& sip,
                                const std::string& dip,
                                int ip_proto, int sport, int dport) {
        uint32_t sourceid_index;
        if (flow_sample.type == SFLOW_FLOW_SAMPLE_EXPANDED) {
            sourceid_index = 0xFF010203;
        } else {
            sourceid_index = 0x00FF1020;
        }
        FillSFlowFlowSample(flow_sample, ++sflow_flow_sample_seqno_,
                            sourceid_index, 1);
        SFlowFlowHeader* flow_header(new SFlowFlowHeader(0));
        FillSFlowFlowHeader(*flow_header, protocol, sip, dip,
                            ip_proto, sport, dport);
        flow_header->length = ComputeSFlowFlowRecordLength(*flow_header);
        pkt_headers_.push_back(flow_header->header);
        flow_sample.flow_records.push_back(flow_header);
    }

private:
    void FillSFlowHeader(SFlowHeader& sflow_header,
                         uint32_t version,
                         const std::string& agent_ipaddr,
                         uint32_t seqno, uint32_t nsamples) {
        sflow_header.version = version;
        sflow_header.agent_ip_address =
            IpAddress::from_string(agent_ipaddr);
        sflow_header.agent_subid = 10;
        sflow_header.seqno = seqno;
        sflow_header.uptime = UTCTimestampUsec()/1000;
        sflow_header.nsamples = nsamples;
    }

    void FillSFlowFlowSample(SFlowFlowSample& flow_sample,
                             uint32_t seqno, uint32_t sourceid_index,
                             uint32_t nflow_records) {
        flow_sample.seqno = seqno;
        flow_sample.sourceid_type = 0;
        flow_sample.sourceid_index = sourceid_index;
        flow_sample.sample_rate = 1000;
        flow_sample.sample_pool = 1020;
        flow_sample.drops = 15;
        flow_sample.input_port_format = 0;
        flow_sample.input_port = 1;
        flow_sample.output_port_format = 0;
        flow_sample.output_port = 4;
        flow_sample.nflow_records = nflow_records;
    }

    void FillSFlowFlowHeader(SFlowFlowHeader& flow_header,
                             uint32_t protocol,
                             const std::string& sip, const std::string& dip,
                             int ip_proto, int sport, int dport) {
        PktHeaderGen pkt_hdrgen;
        flow_header.protocol = protocol;
        flow_header.frame_length = 1496;
        flow_header.stripped = 96;
        if (protocol == SFLOW_FLOW_HEADER_ETHERNET_ISO8023) {
            std::string smac("00:0A:0B:0C:0D:0E");
            std::string dmac("00:01:02:03:04:05");
            flow_header.is_eth_data_set = true;
            SFlowFlowEthernetData& eth_data = flow_header.decoded_eth_data;
            MacAddress::FromString(smac).ToArray(eth_data.src_mac, 6);
            MacAddress::FromString(dmac).ToArray(eth_data.dst_mac, 6);
            eth_data.ether_type = ETHERTYPE_IP;
            pkt_hdrgen.WriteEthHdr(dmac, smac, ETHERTYPE_IP);
        }
        if (protocol == SFLOW_FLOW_HEADER_IPV4 || ip_proto != -1) {
            SFlowFlowIpData& ip_data = flow_header.decoded_ip_data;
            ip_data.length = 1480;
            ip_data.protocol = ip_proto;
            ip_data.src_ip = Ip4Address::from_string(sip);
            ip_data.dst_ip = Ip4Address::from_string(dip);
            pkt_hdrgen.WriteIpHdr(sip, dip, ip_proto, 1480);
        }
        if (sport != -1 && dport != -1) {
            SFlowFlowIpData& ip_data = flow_header.decoded_ip_data;
            switch(ip_proto) {
                case IPPROTO_ICMP: {
                    ip_data.src_port = 0;
                    ip_data.dst_port = ICMP_ECHOREPLY;
                    pkt_hdrgen.WriteIcmpHdr(ICMP_ECHOREPLY, 0);
                    break;
                }
                case IPPROTO_TCP : {
                    ip_data.src_port = sport;
                    ip_data.dst_port = dport;
                    pkt_hdrgen.WriteTcpHdr(sport, dport);
                    break;
                }
                case IPPROTO_UDP : {
                    ip_data.src_port = sport;
                    ip_data.dst_port = dport;
                    pkt_hdrgen.WriteUdpHdr(sport, dport);
                    break;
                }
                default:
                    assert(0);
            }
            flow_header.is_ip_data_set = true;
        }
        flow_header.header_length = pkt_hdrgen.GetPktHeaderLen();
        uint8_t* header(new uint8_t[flow_header.header_length]);
        memcpy(header, pkt_hdrgen.GetPktHeader(), flow_header.header_length);
        flow_header.header = header;
    }

    uint32_t sflow_seqno_;
    uint32_t sflow_flow_sample_seqno_;
    SandeshTraceBufferPtr trace_buf_;
    boost::ptr_vector<uint8_t> pkt_headers_;
};

TEST_F(SFlowParserTest, ParseFlowSample) {
    SFlowData exp_sflow_data;
    SFlowFlowSample* flow_sample(new SFlowFlowSample(SFLOW_FLOW_SAMPLE, 0));
    CreateSFlowFlowSample1(*flow_sample, SFLOW_FLOW_HEADER_ETHERNET_ISO8023,
                           "192.168.1.1", "10.9.8.7",
                           IPPROTO_TCP, 23451, 8086);
    flow_sample->length = ComputeSFlowFlowSampleLength(*flow_sample);
    exp_sflow_data.flow_samples.push_back(flow_sample);
    CreateSFlowHeader(exp_sflow_data.sflow_header, "10.204.217.1", 1);
    SFlowPktGen sflow_pktgen;
    sflow_pktgen.WriteHeader(exp_sflow_data.sflow_header);
    sflow_pktgen.WriteFlowSample(*flow_sample);
    VerifySFlowParse(exp_sflow_data, sflow_pktgen);
}

TEST_F(SFlowParserTest, ParseExpandedFlowSample) {
    SFlowData exp_sflow_data;
    SFlowFlowSample* flow_sample(new SFlowFlowSample(
                                    SFLOW_FLOW_SAMPLE_EXPANDED, 0));
    CreateSFlowFlowSample1(*flow_sample, SFLOW_FLOW_HEADER_IPV4,
                           "1.1.1.10", "2.2.2.20",
                           IPPROTO_UDP, 22222, 5765);
    flow_sample->length = ComputeSFlowFlowSampleLength(*flow_sample);
    exp_sflow_data.flow_samples.push_back(flow_sample);
    CreateSFlowHeader(exp_sflow_data.sflow_header, "100.100.100.1", 1);
    SFlowPktGen sflow_pktgen;
    sflow_pktgen.WriteHeader(exp_sflow_data.sflow_header);
    sflow_pktgen.WriteFlowSample(*flow_sample);
    VerifySFlowParse(exp_sflow_data, sflow_pktgen);
}

TEST_F(SFlowParserTest, ParseMultipleFlowSamples) {
    SFlowData exp_sflow_data;
    SFlowFlowSample* flow_sample1(new SFlowFlowSample(
                                    SFLOW_FLOW_SAMPLE_EXPANDED, 0));
    CreateSFlowFlowSample1(*flow_sample1, SFLOW_FLOW_HEADER_ETHERNET_ISO8023,
                           "127.0.0.1", "192.168.12.1",
                           IPPROTO_TCP, 12341, 4567);
    flow_sample1->length = ComputeSFlowFlowSampleLength(*flow_sample1);
    exp_sflow_data.flow_samples.push_back(flow_sample1);
    SFlowFlowSample* flow_sample2(new SFlowFlowSample(SFLOW_FLOW_SAMPLE, 0));
    CreateSFlowFlowSample1(*flow_sample2, SFLOW_FLOW_HEADER_IPV4,
                           "33.33.33.1", "44.44.44.1",
                           IPPROTO_ICMP, 0, 0);
    flow_sample2->length = ComputeSFlowFlowSampleLength(*flow_sample2);
    exp_sflow_data.flow_samples.push_back(flow_sample2);
    CreateSFlowHeader(exp_sflow_data.sflow_header, "11.12.13.14", 2);
    SFlowPktGen sflow_pktgen;
    sflow_pktgen.WriteHeader(exp_sflow_data.sflow_header);
    sflow_pktgen.WriteFlowSample(*flow_sample1);
    sflow_pktgen.WriteFlowSample(*flow_sample2);
    VerifySFlowParse(exp_sflow_data, sflow_pktgen);
}

TEST_F(SFlowParserTest, ParseCounterSample) {
    SFlowData exp_sflow_data;
    CreateSFlowHeader(exp_sflow_data.sflow_header, "1.2.3.40", 1);
    SFlowPktGen sflow_pktgen;
    sflow_pktgen.WriteHeader(exp_sflow_data.sflow_header);
    SFlowSample counter_sample(SFLOW_COUNTER_SAMPLE, 32);
    sflow_pktgen.WriteCounterSample(counter_sample);
    VerifySFlowParse(exp_sflow_data, sflow_pktgen);
}

TEST_F(SFlowParserTest, ParseFlowAndCounterSamples) {
    SFlowData exp_sflow_data;
    SFlowFlowSample* flow_sample1(new SFlowFlowSample(SFLOW_FLOW_SAMPLE, 0));
    CreateSFlowFlowSample1(*flow_sample1, SFLOW_FLOW_HEADER_ETHERNET_ISO8023,
                           "192.169.1.1", "11.11.11.7",
                           IPPROTO_UDP, 12121, 5997);
    flow_sample1->length = ComputeSFlowFlowSampleLength(*flow_sample1);
    exp_sflow_data.flow_samples.push_back(flow_sample1);
    SFlowFlowSample* flow_sample2(new SFlowFlowSample(
                                    SFLOW_FLOW_SAMPLE_EXPANDED, 0));
    CreateSFlowFlowSample1(*flow_sample2, SFLOW_FLOW_HEADER_IPV4,
                           "192.169.1.2", "22.22.22.8",
                           IPPROTO_TCP, 32321, 8089);
    flow_sample2->length = ComputeSFlowFlowSampleLength(*flow_sample2);
    exp_sflow_data.flow_samples.push_back(flow_sample2);
    CreateSFlowHeader(exp_sflow_data.sflow_header, "10.1.1.1", 3);
    SFlowPktGen sflow_pktgen;
    sflow_pktgen.WriteHeader(exp_sflow_data.sflow_header);
    sflow_pktgen.WriteFlowSample(*flow_sample1);
    SFlowSample counter_sample(SFLOW_COUNTER_SAMPLE_EXPANDED, 64);
    sflow_pktgen.WriteCounterSample(counter_sample);
    sflow_pktgen.WriteFlowSample(*flow_sample2);
    VerifySFlowParse(exp_sflow_data, sflow_pktgen);
}

TEST_F(SFlowParserTest, ParseErrorSFlowHeader) {
    SFlowHeader sflow_header;
    CreateSFlowHeader(sflow_header, "10.10.10.10", 1);
    SFlowPktGen sflow_pktgen;
    sflow_pktgen.WriteHeader(sflow_header);
    boost::random::mt19937 gen;
    boost::random::uniform_int_distribution<> dist(3, 27);
    // Get random values for the SFlow datagram length < header length
    for (int i = 0; i < 5; ++i) {
        VerifySFlowParseError(sflow_pktgen, dist(gen));
    }
}

TEST_F(SFlowParserTest, UnsupportedSFlowVersion) {
    SFlowHeader sflow_header;
    CreateSFlowHeader(sflow_header, "10.0.20.1", 1, 4);
    SFlowPktGen sflow_pktgen;
    sflow_pktgen.WriteHeader(sflow_header);
    VerifySFlowParseError(sflow_pktgen, sflow_pktgen.GetSFlowPktLen());
}

TEST_F(SFlowParserTest, ParseErrorSFlowSampleData) {
    SFlowHeader sflow_header;
    CreateSFlowHeader(sflow_header, "10.204.217.1", 1);
    SFlowPktGen sflow_pktgen1;
    sflow_pktgen1.WriteHeader(sflow_header);
    // Don't write the Flow sample data
    // Parsing of SFlow pkt should fail as the number of samples is set to 1
    VerifySFlowParseError(sflow_pktgen1, sflow_pktgen1.GetSFlowPktLen());

    // Now, write the Flow sample data
    SFlowFlowSample flow_sample(SFLOW_FLOW_SAMPLE, 0);
    CreateSFlowFlowSample1(flow_sample, SFLOW_FLOW_HEADER_ETHERNET_ISO8023,
                           "192.168.1.1", "10.9.8.7",
                           IPPROTO_TCP, 24441, 8086);
    uint32_t sample_length = ComputeSFlowFlowSampleLength(flow_sample);
    // set sample length > actual sample length
    flow_sample.length = sample_length + 4;
    sflow_pktgen1.WriteFlowSample(flow_sample);
    VerifySFlowParseError(sflow_pktgen1, sflow_pktgen1.GetSFlowPktLen());

    // set sample length < actual sample length
    SFlowPktGen sflow_pktgen2;
    sflow_pktgen2.WriteHeader(sflow_header);
    flow_sample.length = sample_length - 4;
    sflow_pktgen2.WriteFlowSample(flow_sample);
    VerifySFlowParseError(sflow_pktgen2, sflow_pktgen2.GetSFlowPktLen());
}

TEST_F(SFlowParserTest, ParseErrorSFlowFlowSample) {
    SFlowHeader sflow_header;
    CreateSFlowHeader(sflow_header, "10.205.213.14", 1);
    SFlowPktGen sflow_pktgen1;
    sflow_pktgen1.WriteHeader(sflow_header);
    size_t sflow_hdrlen = sflow_pktgen1.GetSFlowPktLen();
    size_t min_val = sflow_hdrlen + SFlowSample::kMinSampleLen;
    size_t max_val = min_val + SFlowFlowSample::kMinExpandedFlowSampleLen - 1;
    boost::random::mt19937 gen;
    boost::random::uniform_int_distribution<> dist(min_val, max_val);
    for (int i = 0; i < 5; ++i) {
        VerifySFlowParseError(sflow_pktgen1, dist(gen));
    }

    SFlowFlowSample flow_sample(SFLOW_FLOW_SAMPLE_EXPANDED, 0);
    CreateSFlowFlowSample1(flow_sample, SFLOW_FLOW_HEADER_ETHERNET_ISO8023,
                           "192.168.1.1", "192.168.1.2",
                           IPPROTO_TCP, 24441, 8081);
    flow_sample.length = ComputeSFlowFlowSampleLength(flow_sample);
    boost::ptr_vector<SFlowFlowRecord>::iterator fr1 =
        flow_sample.flow_records.begin();
    uint32_t flow_record_len = fr1->length;
    // set flow record length > actual flow record length
    fr1->length = flow_record_len + 4;
    sflow_pktgen1.WriteFlowSample(flow_sample);
    VerifySFlowParseError(sflow_pktgen1, sflow_pktgen1.GetSFlowPktLen());

    // set flow record length < actual flow record length
    SFlowPktGen sflow_pktgen2;
    sflow_pktgen2.WriteHeader(sflow_header);
    fr1->length = flow_record_len - 4;
    sflow_pktgen2.WriteFlowSample(flow_sample);
    VerifySFlowParseError(sflow_pktgen2, sflow_pktgen2.GetSFlowPktLen());

    // set invalid number of flow records
    SFlowPktGen sflow_pktgen3;
    sflow_pktgen3.WriteHeader(sflow_header);
    fr1->length = flow_record_len;
    uint32_t nflow_records = flow_sample.nflow_records;
    // set nflow_records < actual number of flow records
    flow_sample.nflow_records = nflow_records - 1;
    sflow_pktgen3.WriteFlowSample(flow_sample);
    VerifySFlowParseError(sflow_pktgen3, sflow_pktgen3.GetSFlowPktLen());

    // set nflow_records > actual number of flow records
    SFlowPktGen sflow_pktgen4;
    sflow_pktgen4.WriteHeader(sflow_header);
    flow_sample.nflow_records = nflow_records + 1;
    sflow_pktgen4.WriteFlowSample(flow_sample);
    VerifySFlowParseError(sflow_pktgen4, sflow_pktgen4.GetSFlowPktLen());
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
