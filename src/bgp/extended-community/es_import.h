/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_ES_IMPORT_H_
#define SRC_BGP_EXTENDED_COMMUNITY_ES_IMPORT_H_

#include "bgp/extended-community/types.h"

#include <array>
#include <stdint.h>

#include <string>

class MacAddress;

class EsImport {
public:
    static const int kSize = 8;
    typedef std::array<uint8_t, kSize> bytes_type;

    explicit EsImport(const bytes_type &data);
    std::string ToString() const;

private:
    MacAddress mac_address() const;

    bytes_type data_;
};

#endif  // SRC_BGP_EXTENDED_COMMUNITY_ES_IMPORT_H_
