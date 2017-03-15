import platform
import test_common
import json
import httpretty
from requests.exceptions import ConnectionError

from testtools.matchers import Contains
from testtools import ExpectedException

from cfgm_common import rest
import vnc_api


def _auth_request_status(request, url, status_code):
    ret_headers = {'server': '127.0.0.1'}
    ret_headers.update(request.headers.dict)
    ret_body = {'access': {'token': {'id': 'foo'}}}
    return (status_code, ret_headers, json.dumps(ret_body))


class TestVncApi(test_common.TestCase):
    def test_retry_after_auth_success(self):
        uri_with_auth = '/try-after-auth'
        url_with_auth = 'http://127.0.0.1:8082/try-after-auth'
        httpretty.register_uri(
                httpretty.GET, url_with_auth,
                responses=[httpretty.Response(status=401, body='""'),
                           httpretty.Response(status=200, body='""')])

        auth_url = "http://127.0.0.1:35357/v2.0/tokens"

        def _auth_request_success(request, url, *args):
            return _auth_request_status(request, url, 200)

        httpretty.register_uri(
                httpretty.POST, auth_url, body=_auth_request_success)

        self._vnc_lib._request_server(rest.OP_GET, url=uri_with_auth)
    # end test_retry_after_auth_success

    def test_retry_after_auth_failure(self):
        uri_with_auth = '/try-after-auth'
        url_with_auth = 'http://127.0.0.1:8082/try-after-auth'
        httpretty.register_uri(
                httpretty.GET, url_with_auth,
                responses=[httpretty.Response(status=401, body='""')])

        auth_url = "http://127.0.0.1:35357/v2.0/tokens"

        def _auth_request_failure(request, url, *args):
            return _auth_request_status(request, url, 401)

        httpretty.register_uri(
                httpretty.POST, auth_url, body=_auth_request_failure)

        with ExpectedException(RuntimeError):
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
            {"link": {
                "href": "http://localhost:8082/foos",
                "name": "foo",
                "rel": "collection"
            }
            },
            {"link": {
                "href": "http://localhost:8082/foo",
                "name": "foo",
                "rel": "resource-base"
            }
            },
        ]
        httpretty.register_uri(
                httpretty.GET, "http://127.0.0.1:8082/",
                body=json.dumps({'href': "http://127.0.0.1:8082",
                                 'links': links}))

        vnc_api.VncApi()
    # end test_server_has_more_types_than_client

    def test_multiple_server_active_session(self):
        httpretty.register_uri(
                httpretty.GET, "http://127.0.0.2:8082/",
                body=json.dumps({'href': "http://127.0.0.2:8082",
                                 'links': []}))
        vnclib = vnc_api.VncApi(
                api_server_host=['127.0.0.1', '127.0.0.2', '127.0.0.3'])

        # Connect to inactive server 127.0.0.3
        # Expected to connect to next available active server
        url = 'http://127.0.0.3:8082'
        response = vnclib._request_server(rest.OP_GET, url=url)
        active_session = response.text['href']

        # Connect to active server 127.0.0.1
        # Expected to connect to the cached active server
        url = 'http://127.0.0.1:8082'
        resp = vnclib._request_server(rest.OP_GET, url=url)
        self.assertEqual(resp.text['href'], active_session)

        # Connect to active server 127.0.0.2
        # Expected to connect to the cached active server
        url = 'http://127.0.0.2:8082'
        resp = vnclib._request_server(rest.OP_GET, url=url)
        self.assertEqual(resp.text['href'], active_session)
    # end test_multiple_server_active_session

    def test_multiple_server_all_servers_down(self):
        httpretty.register_uri(
                httpretty.GET, "http://127.0.0.2:8082/",
                body=json.dumps({'href': "http://127.0.0.2:8082",
                                 'links': []}))
        httpretty.register_uri(
                httpretty.GET, "http://127.0.0.3:8082/",
                body=json.dumps({'href': "http://127.0.0.3:8082",
                                 'links': []}))
        vnclib = vnc_api.VncApi(
                api_server_host=['127.0.0.1', '127.0.0.2', '127.0.0.3'])
        # Connect to a server
        # Expected to connect to one of the server
        url = 'http://127.0.0.3:8082'
        vnclib._request_server(rest.OP_GET, url=url)

        # Bring down all fake servers
        httpretty.disable()

        # Connect to a server
        # Expected to raise ConnectionError
        url = 'http://127.0.0.3:8082'
        with ExpectedException(ConnectionError):
            vnclib._request_server(rest.OP_GET, url=url)

        # Bring up all fake servers
        httpretty.enable()

        # Connect to a server
        # Expected to connect to one of the server
        url = 'http://127.0.0.3:8082'
        vnclib._request_server(rest.OP_GET, url=url)
    # end test_multiple_server_all_servers_down


# end class TestVncApi
