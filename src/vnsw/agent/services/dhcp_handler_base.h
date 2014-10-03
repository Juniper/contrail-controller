/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_dhcp_handler_base_hpp
#define vnsw_agent_dhcp_handler_base_hpp

#include <bitset>
#include "pkt/proto_handler.h"
#include "vnc_cfg_types.h"

#define DHCP_BASE_TRACE(arg)                                                 \
do {                                                                         \
    std::ostringstream _str;                                                 \
    _str << arg;                                                             \
    DhcpTrace(_str.str());                                                   \
} while (false)                                                              \

// DHCP protocol handler Base Class
class DhcpHandlerBase : public ProtoHandler {
public:
    enum DhcpOptionCategory {
        None,
        NoData,               // no data for this option
        Bool,                 // data has bool value ( 0 or 1)
        Byte,                 // data has byte value
        ByteArray,            // data has array of bytes
        ByteString,           // data has byte followed by string
        ByteOneIPPlus,        // data has byte followed by one or more IPs
        String,               // data has a string
        Int32bit,             // data 32 bit int
        Uint32bit,            // data has 32 bit uint
        Uint16bit,            // data has 16 bit uint
        Uint16bitArray,       // data has array of 16 bit uint
        OneIPv4,              // data has one IPv4
        ZeroIPv4Plus,         // data has zero or more IPv4s
        OneIPv4Plus,          // data has one or more IPv4s
        TwoIPv4Plus,          // data has multiples of two IPv4s
        OneIPv6,              // data has one IPv6
        OneIPv6Plus,          // data has one or more IPv6s
        ClasslessRoute,       // data is classless route option
        NameCompression,      // data is name compressed (rfc1035)
        NameCompressionArray, // data is name compressed (rfc1035)
        ByteNameCompression   // data is byte followed by name compressed (rfc1035)
    };

    enum DhcpOptionLevel {
        Invalid,
        InterfaceLevel,
        SubnetLevel,
        IpamLevel
    };

    struct ConfigRecord {
        ConfigRecord() : plen(0), lease_time(-1), valid_time(-1), preferred_time(-1) {}

        IpAddress ip_addr;
        uint32_t subnet_mask;
        uint32_t bcast_addr;
        IpAddress gw_addr;
        IpAddress dns_addr;
        uint32_t plen;
        uint32_t lease_time;
        uint32_t valid_time;
        uint32_t preferred_time;
        std::string client_name_;
        std::string domain_name_;
    };

    struct DhcpOptionHandler {
        virtual ~DhcpOptionHandler() {}
        virtual void WriteData(uint8_t c, uint8_t l, const void *d,
                               uint16_t *optlen) = 0;
        virtual void AppendData(uint16_t l, const void *d,
                                uint16_t *optlen) = 0;
        virtual uint16_t GetCode() const = 0;
        virtual uint16_t GetLen() const = 0;
        virtual uint16_t GetFixedLen() const = 0;
        virtual uint8_t *GetData() = 0;
        virtual void SetCode(uint16_t len) = 0;
        virtual void SetLen(uint16_t len) = 0;
        virtual void AddLen(uint16_t len) = 0;
        virtual void SetNextOptionPtr(uint16_t optlen) = 0;
        virtual void SetDhcpOptionPtr(uint8_t *hdr) = 0;
    };

    DhcpHandlerBase(Agent *agent, boost::shared_ptr<PktInfo> info,
                    boost::asio::io_service &io);
    virtual ~DhcpHandlerBase();

protected:
    uint16_t AddNoDataOption(uint32_t option, uint16_t opt_len);
    uint16_t AddByteOption(uint32_t option, uint16_t opt_len,
                           const std::string &input);
    uint16_t AddByteArrayOption(uint32_t option, uint16_t opt_len,
                                const std::string &input);
    uint16_t AddByteStringOption(uint32_t option, uint16_t opt_len,
                                 const std::string &input);
    uint16_t AddByteIPOption(uint32_t option, uint16_t opt_len,
                             const std::string &input);
    uint16_t AddStringOption(uint32_t option, uint16_t opt_len,
                             const std::string &input);
    uint16_t AddIntegerOption(uint32_t option, uint16_t opt_len,
                              const std::string &input);
    uint16_t AddShortArrayOption(uint32_t option, uint16_t opt_len,
                                 const std::string &input, bool array);
    uint16_t AddIpv4Option(uint32_t option, uint16_t opt_len,
                           const std::string &input, uint8_t min_count,
                           uint8_t max_count, uint8_t multiples);
    uint16_t AddIpv6Option(uint32_t option, uint16_t opt_len,
                           const std::string &input, bool list);
    virtual uint16_t AddIP(uint16_t opt_len,
                           const std::string &input) = 0;
    uint16_t AddCompressedNameOption(uint32_t option, uint16_t opt_len,
                                     const std::string &input, bool list);
    uint16_t AddByteCompressedNameOption(uint32_t option, uint16_t opt_len,
                                         const std::string &input);
    uint16_t AddCompressedName(uint16_t opt_len, const std::string &input);
    uint16_t AddDnsServers(uint16_t opt_len);
    void ReadClasslessRoute(uint32_t option, uint16_t opt_len,
                            const std::string &input);
    uint16_t AddClasslessRouteOption(uint16_t opt_len);
    uint16_t AddConfigDhcpOptions(uint16_t opt_len, bool is_v6);
    uint16_t AddDhcpOptions(uint16_t opt_len,
                            std::vector<autogen::DhcpOptionType> &options,
                            DhcpOptionLevel level);
    void FindDomainName(const IpAddress &vm_addr);

    virtual void DhcpTrace(const std::string &msg) const = 0;

    virtual DhcpOptionCategory OptionCategory(uint32_t option) const = 0;
    virtual uint32_t OptionCode(const std::string &option) const = 0;

    bool is_flag_set(uint8_t flag) const { return flags_[flag]; }
    void set_flag(uint8_t flag) { flags_.set(flag); }

    VmInterface *vm_itf_;
    uint32_t vm_itf_index_;
    ConfigRecord config_;
    boost::scoped_ptr<DhcpOptionHandler> option_;  // option being processed

    // bitset to indicate whether these options are added to the response or not
    std::bitset<256> flags_;

    // store the DHCP routers coming in config
    std::string routers_;

    // parse and store the host routes coming in config
    std::vector<OperDhcpOptions::Subnet> host_routes_;
    DhcpOptionLevel host_routes_level_;

    std::string ipam_name_;
    autogen::IpamType ipam_type_;
    autogen::VirtualDnsType vdns_type_;

    DISALLOW_COPY_AND_ASSIGN(DhcpHandlerBase);
};

#endif // vnsw_agent_dhcp_handler_base_hpp
