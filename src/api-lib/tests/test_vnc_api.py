import test_common
import requests
import json
import httpretty

import testtools
from testtools.matchers import Equals, MismatchError
from testtools import content, content_type, ExpectedException

from cfgm_common import rest
import vnc_api
from vnc_api import vnc_api

def _auth_request_status(request, url, status_code):
    ret_headers = {'server': '127.0.0.1'}
    ret_headers.update(request.headers.dict)
    ret_body = {'access': {'token': {'id': 'foo'}}}
    return (status_code, ret_headers, json.dumps(ret_body))

class TestVncApi(test_common.TestCase):
    def test_retry_after_auth_success(self):
        uri_with_auth = '/try-after-auth'
        url_with_auth = 'http://127.0.0.1:8082/try-after-auth'
        httpretty.register_uri(httpretty.GET, url_with_auth,
            responses=[httpretty.Response(status=401, body=''),
                       httpretty.Response(status=200, body='')])

        auth_url = "http://127.0.0.1:35357/v2.0/tokens"

        def _auth_request_success(request, url, *args):
            return _auth_request_status(request, url, 200)

        httpretty.register_uri(httpretty.POST, auth_url,
            body=_auth_request_success)

        self._vnc_lib._request_server(rest.OP_GET, url=uri_with_auth)
    # end test_retry_after_auth_success

    def test_retry_after_auth_failure(self):
        uri_with_auth = '/try-after-auth'
        url_with_auth = 'http://127.0.0.1:8082/try-after-auth'
        httpretty.register_uri(httpretty.GET, url_with_auth,
            responses=[httpretty.Response(status=401, body='')])

        auth_url = "http://127.0.0.1:35357/v2.0/tokens"

        def _auth_request_failure(request, url, *args):
            return _auth_request_status(request, url, 401)

        httpretty.register_uri(httpretty.POST, auth_url,
            body=_auth_request_failure)

        with ExpectedException(RuntimeError) as e:
            self._vnc_lib._request_server(rest.OP_GET, url=uri_with_auth)
    # end test_retry_after_auth_failure
# end class TestVncApi
