/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#define __BGP_IP_TEST_WRAPPER_TEST_SUITE__
#include "bgp_ip_test.cc"

int main(int argc, char **argv) {
    const char *largv[] = {
        __FILE__,
        "--nexthop-address-family=inet",
    };
    return bgp_ip_test_main(sizeof(largv)/sizeof(largv[0]), largv);
}

