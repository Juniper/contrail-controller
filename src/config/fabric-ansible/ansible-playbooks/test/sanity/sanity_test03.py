#!/usr/bin/python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains sanity test for all major workflows supported by
fabric ansible
"""

from sanity_base import SanityBase
import config


# pylint: disable=E1101
class SanityTest03(SanityBase):
    """
    Sanity test for ZTP
    """

    def __init__(self, cfg):
        SanityBase.__init__(self, cfg, 'sanity_test_03')
        self._prouter = cfg['prouter']
    # end __init__

    def test(self):
        try:
            self.cleanup_fabric('fab01')
            fabric = self.create_fabric('fab01', self._prouter['passwords'])

            self.ztp(fabric.uuid)

        except Exception as ex:
            self._exit_with_error(
                "Test failed due to unexpected error: %s" % str(ex))
    # end test


if __name__ == "__main__":
    SanityTest03(config.load('config/test_config.yml')).test()
# end __main__

