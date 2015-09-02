/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DNS_UVE_H_
#define __DNS_UVE_H_

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include <uve/uve_types.h>

class DnsUveClient {
    static DnsState prev_state_;
public:
    static void SendDnsUve(uint64_t start_time);
};

#endif // __DNS_UVE_H_
