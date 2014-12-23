/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/default_gateway.h"

#include <algorithm>
#include <string>

using std::copy;
using std::string;

DefaultGateway::DefaultGateway(const bytes_type &data) {
    copy(data.begin(), data.end(), data_.begin());
}

string DefaultGateway::ToString() const {
    return string("defaultgw:0");
}
