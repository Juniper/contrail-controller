/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#define __BGP_STRESS_TEST_SUITE__
#include "bgp_stress_test.cc"

// Unidimensional test with maximum allowed agents
int main(int argc, char **argv) {
    const char *largv[] = {
        __FILE__, "--log-disable",

        "--nagents=127",
    };

    return bgp_stress_test_main(sizeof(largv)/sizeof(largv[0]), largv);
}
