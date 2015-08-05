/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#define __BGP_STRESS_TEST_SUITE__
#include "bgp_stress_test.cc"

// Inject events to a larger proportion.
int main(int argc, char **argv) {
    const char *largv[] = {
        __FILE__, "--log-disable",

        "--nagents=20",
        "--nroutes=20",
        "--ninstances=5",
        "--npeers=20",
        "--event-proportion=0.75",
    };

    return bgp_stress_test_main(sizeof(largv)/sizeof(largv[0]), largv);
}
