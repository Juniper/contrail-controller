# -*- coding: utf-8 -*-

#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import unittest
import mock

from cfgm_common.vnc_amqp import VncAmqpHandle


class TestVncAmqp(unittest.TestCase):

    def test_close(self):
        vnc = VncAmqpHandle(
            *(7 * [mock.MagicMock()]))

        # Should not raise anything
        vnc.close()

        # Pretends call of establish()
        vnc._vnc_kombu = mock.MagicMock()
        vnc.close()
        vnc._vnc_kombu.shutdown.assert_called_once_with()
