/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_ESI_LABEL_H_
#define SRC_BGP_EXTENDED_COMMUNITY_ESI_LABEL_H_

#include <boost/array.hpp>
#include <stdint.h>

#include <string>

class EsiLabel {
public:
    static const int kSize = 8;
    typedef boost::array<uint8_t, kSize> bytes_type;

    explicit EsiLabel(const bytes_type &data);
    std::string ToString() const;

private:
    std::string flags() const;
    int label() const;

    bytes_type data_;
};

#endif  // SRC_BGP_EXTENDED_COMMUNITY_ESI_LABEL_H_
