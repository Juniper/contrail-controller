/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_LOAD_BALANCE_H_
#define SRC_BGP_EXTENDED_COMMUNITY_LOAD_BALANCE_H_

#include <array>
#include <boost/system/error_code.hpp>

#include <endian.h>
#include <string>

#include "base/parse_object.h"
#include "bgp/bgp_path.h"
#include "bgp/extended-community/types.h"
#include "bgp/bgp_peer_types.h"
#include "schema/xmpp_unicast_types.h"

/*
 * BGP LoadBalance Opaque Extended Community with SubType 0xAA (TBA)
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Type  0x03    | Sub-Type 0xAA |s d c p P R R R|R R R R R R R R|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Reserved      |B R R R R R R R| Reserved      | Reserved      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Type: 0x03 Opaque
 * SubType: 0xAA LoadBalance attribute information (TBA)
 * s: Use l3_source_address ECMP Load-balancing
 * d: Use l3_destination_address ECMP Load-balancing
 * c: Use l4_protocol ECMP Load-balancing
 * p: Use l4_source_port ECMP Load-balancing
 * P: Use l4_destination_port ECMP Load-balancing
 * B: Use source_bias (instead of ECMP load-balancing)
 * R: Reserved
*/
class LoadBalance {
public:
    static const int kSize = 8;
    typedef std::array<uint8_t, kSize> bytes_type;

    struct LoadBalanceAttribute {
        static const LoadBalanceAttribute kDefaultLoadBalanceAttribute;
        union {
            struct {
#if BYTE_ORDER == BIG_ENDIAN
                // Opaque extended community header
                uint8_t type;
                uint8_t sub_type;

                // ecmp hash fields selection list
                uint8_t l3_source_address:1;      // Set by default
                uint8_t l3_destination_address:1; // Set by default
                uint8_t l4_protocol:1;            // Set by default
                uint8_t l4_source_port:1;         // Set by default
                uint8_t l4_destination_port:1;    // Set by default
                uint8_t reserved1:3;

                uint8_t  reserved2; // For future fields such as interface

                // Misc bool actions
                uint8_t  source_bias:1;
                uint8_t  reserved3:7;

                uint8_t  reserved4;
                uint8_t  reserved5;
                uint8_t  reserved6;
#else
                uint8_t  reserved2; // For future fields such as interface

                // ecmp hash fields selection list
                uint8_t reserved1:3;
                uint8_t l4_destination_port:1;    // Set by default
                uint8_t l4_source_port:1;         // Set by default
                uint8_t l4_protocol:1;            // Set by default
                uint8_t l3_destination_address:1; // Set by default
                uint8_t l3_source_address:1;      // Set by default

                // Opaque extended community header
                uint8_t sub_type;
                uint8_t type;

                uint8_t  reserved6;
                uint8_t  reserved5;
                uint8_t  reserved4;

                // Misc bool actions
                uint8_t  reserved3:7;
                uint8_t  source_bias:1;
#endif
            } __attribute__((packed));
            struct {
                uint32_t value1;
                uint32_t value2;
            } __attribute__((packed));
        };
        LoadBalanceAttribute();
        LoadBalanceAttribute(uint32_t value1, uint32_t value2);
        void Encode(autogen::LoadBalanceType *lb_type) const;
        bool operator==(const LoadBalanceAttribute &other) const;
        bool operator!=(const LoadBalanceAttribute &other) const;
        const bool IsDefault() const;
    };

    LoadBalance();
    explicit LoadBalance(const bytes_type &data);
    explicit LoadBalance(const LoadBalanceAttribute &attr);
    explicit LoadBalance(const autogen::LoadBalanceType &lb_type);
    explicit LoadBalance(const BgpPath *path);

    bool operator==(const LoadBalance &other) const;
    bool operator!=(const LoadBalance &other) const;
    uint8_t Type() const { return data_[0]; }
    uint8_t Subtype() const { return data_[1]; }
    const bytes_type &GetExtCommunity() const { return data_; }
    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }
    const LoadBalanceAttribute ToAttribute() const;
    void FillAttribute(LoadBalanceAttribute *attr);
    const bool IsDefault() const;
    void ShowAttribute(ShowLoadBalance *show_load_balance) const;
    std::string ToString() const;
    void SetL3SourceAddress(bool flag);
    void SetL3DestinationAddress(bool flag);
    void SetL4Protocol(bool flag);
    void SetL4SourcePort(bool flag);
    void SetL4DestinationPort(bool flag);
    void SetSourceBias(bool flag);

    static bool IsPresent(const BgpPath *path);

private:

    bytes_type data_;
};

#endif  // SRC_BGP_EXTENDED_COMMUNITY_LOAD_BALANCE_H_
