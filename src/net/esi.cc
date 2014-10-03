/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "net/esi.h"

#include "base/parse_object.h"
#include "base/util.h"
#include "net/address.h"

using namespace std;

static const uint8_t max_esi_bytes[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

const EthernetSegmentId EthernetSegmentId::kZeroEsi;
const EthernetSegmentId EthernetSegmentId::kMaxEsi(max_esi_bytes);

EthernetSegmentId::EthernetSegmentId() {
    memset(data_, 0, kSize);
}

EthernetSegmentId::EthernetSegmentId(const uint8_t *data) {
    memcpy(data_, data, kSize);
}

EthernetSegmentId EthernetSegmentId::FromString(const std::string &str,
    boost::system::error_code *errorp) {
    if (str == "zero_esi")
        return EthernetSegmentId::kZeroEsi;
    if (str == "max_esi")
        return EthernetSegmentId::kMaxEsi;

    size_t num_colons = count(str.begin(), str.end(), ':');
    if (num_colons != 1 && num_colons != 9) {
        if (errorp != NULL)
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        return EthernetSegmentId::kMaxEsi;
    }

    uint8_t data[kSize];
    memset(data, 0, kSize);

    // AS based or IP based.
    if (num_colons == 1) {
        size_t pos = str.find(':');
        assert(pos != string::npos);
        string asn_or_ip(str.substr(0, pos));

        size_t num_dots = count(asn_or_ip.begin(), asn_or_ip.end(), '.');
        if (num_dots != 0 && num_dots != 3) {
            if (errorp != NULL)
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            return EthernetSegmentId::kMaxEsi;
        }

        // AS based.
        if (num_dots == 0) {
            uint32_t asn;
            bool ret = stringToInteger(asn_or_ip, asn);
            if (!ret) {
                if (errorp != NULL) {
                    *errorp = make_error_code(boost::system::errc::invalid_argument);
                    return EthernetSegmentId::kMaxEsi;
                }
            }

            data[0] = AS_BASED;
            put_value(&data[1], 4, asn);
        }

        // IP based.
        if (num_dots == 3) {
            boost::system::error_code ec;
            Ip4Address addr = Ip4Address::from_string(asn_or_ip, ec);
            if (ec.value() != 0) {
                if (errorp != NULL) {
                    *errorp = make_error_code(boost::system::errc::invalid_argument);
                    return EthernetSegmentId::kMaxEsi;
                }
            }

            data[0] = IP_BASED;
            const Ip4Address::bytes_type &bytes = addr.to_bytes();
            copy(bytes.begin(), bytes.begin() + 4, &data[1]);
        }

        // Parse discriminator - common for AS based and IP based.
        string disc_str(str, pos + 1);
        uint32_t disc;
        bool ret = stringToInteger(disc_str, disc);
        if (!ret) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
                return EthernetSegmentId::kMaxEsi;
            }
        }
        put_value(&data[5], 4, disc);
    }

    // All other formats - raw colon separated bytes.
    if (num_colons == 9) {
        char extra;
        int ret = sscanf(str.c_str(),
            "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx%c",
            &data[0], &data[1], &data[2], &data[3], &data[4],
            &data[5], &data[6], &data[7], &data[8], &data[9], &extra);
        if (ret != kSize || strchr(str.c_str(), 'x') || strchr(str.c_str(), 'X')) {
            if (errorp != NULL)
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            return EthernetSegmentId::kMaxEsi;
        }
    }

    return EthernetSegmentId(data);
}

string EthernetSegmentId::ToString() const {
    if (CompareTo(kZeroEsi) == 0)
        return "zero_esi";
    if (CompareTo(kMaxEsi) == 0)
        return "max_esi";

    switch (Type()) {
    case AS_BASED: {
        uint32_t asn = get_value(data_ + 1, 4);
        uint32_t value = get_value(data_ + 5, 4);
        return integerToString(asn) + ":" + integerToString(value);
        break;
    }
    case IP_BASED: {
        Ip4Address addr(get_value(data_ + 1, 4));
        uint32_t value = get_value(data_ + 5, 4);
        return addr.to_string() + ":" + integerToString(value);
        break;
    }
    case MAC_BASED:
    case STP_BASED:
    case LACP_BASED:
    case CONFIGURED:
    default: {
        char temp[64];
        snprintf(temp, sizeof(temp),
            "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
            data_[0], data_[1], data_[2], data_[3], data_[4],
            data_[5], data_[6], data_[7], data_[8], data_[9]);
        return temp;
    }
    }

    return "bad_esi";
}

int EthernetSegmentId::CompareTo(const EthernetSegmentId &rhs) const {
    return memcmp(data_, rhs.data_, kSize);
}
