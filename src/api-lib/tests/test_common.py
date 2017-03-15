#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
from pprint import pformat
import fixtures
import testtools
from testtools import content
import httpretty
import json
import logging

sys.path.insert(0, '../../../../build/debug/api-lib/')
sys.path.insert(0, '../../../../build/debug/config/api-server/')
sys.path.insert(0, '../../../../build/debug/config/common/')

from vnc_api import vnc_api


def setup_flexmock():
    pass
# end setup_flexmock

cov_handle = None


class TestCase(testtools.TestCase, fixtures.TestWithFixtures):
    _HTTP_HEADERS = {
        'Content-type': 'application/json; charset="UTF-8"',
    }

    def _add_request_detail(self, op, url, headers=None, query_params=None,
                            body=None):
        request_str = ' URL: ' + pformat(url) + \
                      ' OPER: ' + pformat(op) + \
                      ' Headers: ' + pformat(headers) + \
                      ' Query Params: ' + pformat(query_params) + \
                      ' Body: ' + pformat(body)
        self.addDetail('Requesting: ', content.text_content(request_str))

    def setUp(self):
        super(TestCase, self).setUp()
        setup_flexmock()

        httpretty.enable()
        httpretty.register_uri(
                httpretty.GET, "http://127.0.0.1:8082/",
                body=json.dumps({'href': "http://127.0.0.1:8082",
                                 'links': []}))

        self._logger = logging.getLogger(__name__)
        logging.basicConfig()
        self._logger.setLevel(logging.DEBUG)
        # Use empty config file (does not exist) to be sure using default
        # parameter values
        self._vnc_lib = vnc_api.VncApi(conf_file='/tmp/fake-config-file')
    # end setUp

    def tearDown(self):
        httpretty.disable()
        httpretty.reset()
        super(TestCase, self).tearDown()
    # end tearDown
# end TestCommon
