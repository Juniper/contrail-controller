#!/usr/bin/env python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import logging

from contrail_vrouter_provisioning.cmdparser import ComputeArgsParser
from contrail_vrouter_provisioning.common import CommonComputeSetup


log = logging.getLogger('contrail_vrouter_provisioning.setup')


def main():
    try:
        log.info("Compute provisioning initiated:\n %s" % sys.argv)
        parser = ComputeArgsParser(sys.argv[1:])
        args = parser._args

        compute = CommonComputeSetup(args)
        compute.setup()
        log.info("Compute provisioning complete")
    except Exception:
        log.exception("Aborting")
        log.info("Compute provisioning failed")
        raise

if __name__ == "__main__":
    sys.exit(main())
