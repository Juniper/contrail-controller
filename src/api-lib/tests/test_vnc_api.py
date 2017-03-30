import platform
import test_common
import requests
import json
import httpretty

import testtools
from testtools.matchers import Not, Equals, MismatchError, Contains
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
            responses=[httpretty.Response(status=401, body='""'),
                       httpretty.Response(status=200, body='""')])

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
            responses=[httpretty.Response(status=401, body='""')])

        auth_url = "http://127.0.0.1:35357/v2.0/tokens"

        def _auth_request_failure(request, url, *args):
            return _auth_request_status(request, url, 401)

        httpretty.register_uri(httpretty.POST, auth_url,
            body=_auth_request_failure)

        with ExpectedException(RuntimeError) as e:
            self._vnc_lib._request_server(rest.OP_GET, url=uri_with_auth)
    # end test_retry_after_auth_failure

    def test_contrail_useragent_header(self):

        def _check_header(uri, headers=None, query_params=None):
            useragent = headers['X-Contrail-Useragent']
            hostname = platform.node()
            self.assertThat(useragent, Contains(hostname))
            return (200, json.dumps({}))

        orig_http_get = self._vnc_lib._http_get
        try:
            self._vnc_lib._http_get = _check_header
            self._vnc_lib._request_server(rest.OP_GET, url='/')
        finally:
            self._vnc_lib._http_get = orig_http_get
    # end test_contrail_useragent_header

    def test_server_has_more_types_than_client(self):
        links = [
            {
            "link": {
                "href": "http://localhost:8082/foos",
                "name": "foo",
                "rel": "collection"
            }
            },
            {
            "link": {
                "href": "http://localhost:8082/foo",
                "name": "foo",
                "rel": "resource-base"
            }
            },
        ]
        httpretty.register_uri(httpretty.GET, "http://127.0.0.1:8082/",
            body=json.dumps({'href': "http://127.0.0.1:8082", 'links':links}))

        lib = vnc_api.VncApi()
    # end test_server_has_more_types_than_client

    def test_supported_auth_strategies(self):
        uri_with_auth = '/try-after-auth'
        url_with_auth = 'http://127.0.0.1:8082%s' % uri_with_auth
        httpretty.register_uri(
            httpretty.GET, url_with_auth,
            responses=[httpretty.Response(status=401, body='""'),
                       httpretty.Response(status=200, body='""')],
        )
        auth_url = "http://127.0.0.1:35357/v2.0/tokens"
        keystone_api_called = [False]

        def keytone_api_request(request, url, *args):
            keystone_api_called[0] = True
            return _auth_request_status(request, url, 200)

        httpretty.register_uri(httpretty.POST, auth_url,
                               body=keytone_api_request)

        # Verify auth strategy is Keystone if not provided and check Keystone
        # API is requested
        self.assertEqual(self._vnc_lib._authn_type, 'keystone')
        self._vnc_lib._request_server(rest.OP_GET, url=uri_with_auth)
        self.assertTrue(keystone_api_called[0])

        # Validate we can use 'noauth' auth strategy and check Keystone API is
        # not requested
        keystone_api_called = [False]
        self._vnc_lib = vnc_api.VncApi(conf_file='/tmp/fake-config-file',
                                       auth_type='noauth')
        self.assertEqual(self._vnc_lib._authn_type, 'noauth')
        self._vnc_lib._request_server(rest.OP_GET, url=uri_with_auth)
        self.assertFalse(keystone_api_called[0])

        # Validate we cannot use unsupported authentication strategy
        with ExpectedException(NotImplementedError):
            self._vnc_lib = vnc_api.VncApi(auth_type='fake-auth')

# end class TestVncApi
