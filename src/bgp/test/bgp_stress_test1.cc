/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_stress_test.cc"

int main(int argc, char **argv) {
    const char *largv[] = {
        __FILE__, "--log-disable",

        "--nagents=2",
        "--nroutes=2",
        "--ninstances=2",
        "--npeers=2",
    };
    return bgp_stress_test_main(sizeof(largv)/sizeof(largv[0]), largv);
}
