/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#define __PATH_RESOLVER_TEST_WRAPPER_TEST_SUITE__
#include "path_resolver_test.cc"

int main(int argc, char **argv) {
    const char *largv[] = {
        __FILE__,
        "--nexthop-address-family=inet6",
    };
    return path_resolver_test_main(sizeof(largv)/sizeof(largv[0]), largv);
}
