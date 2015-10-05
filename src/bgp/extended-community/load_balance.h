/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_LOAD_BALANCE_H_
#define SRC_BGP_EXTENDED_COMMUNITY_LOAD_BALANCE_H_

#include <boost/array.hpp>
#include <boost/system/error_code.hpp>

#include <endian.h>
#include <string>

#include "base/parse_object.h"
#include "bgp/extended-community/types.h"
#include "schema/xmpp_unicast_types.h"

class LoadBalance {
public:
    static const int kSize = 8;
    typedef boost::array<uint8_t, kSize> bytes_type;

    struct LoadBalanceAttribute {
        union {
            struct {
#if BYTE_ORDER == BIG_ENDIAN
                // Opaque extended community header
                uint8_t type;
                uint8_t sub_type;

                // ecmp hash fields selection list
                uint8_t l2_source_address:1;
                uint8_t l2_destination_address:1;
                uint8_t l3_source_address:1;      // Set by default
                uint8_t l3_destination_address:1; // Set by default
                uint8_t l4_protocol:1;            // Set by default
                uint8_t l4_source_port:1;         // Set by default
                uint8_t l4_destination_port:1;    // Set by default
                uint8_t reserved1:1;

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
                uint8_t reserved1:1;
                uint8_t l4_destination_port:1;    // Set by default
                uint8_t l4_source_port:1;         // Set by default
                uint8_t l4_protocol:1;            // Set by default
                uint8_t l3_destination_address:1; // Set by default
                uint8_t l3_source_address:1;      // Set by default
                uint8_t l2_destination_address:1;
                uint8_t l2_source_address:1;

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
        void encode(autogen::LoadBalanceType &) const;
        bool operator==(const LoadBalanceAttribute &other) const;
    };

    explicit LoadBalance();
    explicit LoadBalance(const bytes_type &data);
    explicit LoadBalance(const LoadBalance::LoadBalanceAttribute &attr);
    explicit LoadBalance(const autogen::LoadBalanceType &item);

    uint8_t Type() const { return data_[0]; }
    uint8_t Subtype() const { return data_[1]; }
    const bytes_type &GetExtCommunity() const { return data_; }
    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }
    const LoadBalanceAttribute ToAttribute() const;
    void fillAttribute(LoadBalance::LoadBalanceAttribute &attr);
    const bool isDefault() const;
    std::string ToString() const;
private:
    bytes_type data_;
};

#endif  // SRC_BGP_EXTENDED_COMMUNITY_LOAD_BALANCE_H_
