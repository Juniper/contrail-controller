/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_esi_label_h
#define ctrlplane_esi_label_h

#include <boost/array.hpp>
#include <stdint.h>

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

#endif
