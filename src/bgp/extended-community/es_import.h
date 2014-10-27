/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_es_import_h
#define ctrlplane_es_import_h

#include <boost/array.hpp>
#include <stdint.h>

class MacAddress;

class EsImport {
public:
    static const int kSize = 8;
    typedef boost::array<uint8_t, kSize> bytes_type;

    explicit EsImport(const bytes_type &data);
    std::string ToString() const;

private:
    MacAddress mac_address() const;

    bytes_type data_;
};

#endif
