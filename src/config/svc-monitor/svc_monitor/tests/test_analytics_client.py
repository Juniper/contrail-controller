import mock
import unittest

from svc_monitor.analytics_client import Client


class TestOpencontrailClient(unittest.TestCase):

    def setUp(self):
        super(TestOpencontrailClient, self).setUp()
        self.client = Client('http://127.0.0.1:8081', {'arg1': 'aaa'})

        self.get_resp = mock.MagicMock()
        self.get = mock.patch('requests.get',
                              return_value=self.get_resp).start()
        self.get_resp.raw_version = 1.1
        self.get_resp.status_code = 200

    def test_analytics_request_without_data(self):
        self.client.request('/fake/path/', 'fake_uuid')

        call_args = self.get.call_args_list[0][0]
        call_kwargs = self.get.call_args_list[0][1]

        expected_url = ('http://127.0.0.1:8081/fake/path/fake_uuid')
        self.assertEqual(expected_url, call_args[0])

        data = call_kwargs.get('data')

        expected_data = {'arg1': 'aaa'}
        self.assertEqual(expected_data, data)

    def test_analytics_request_with_data(self):
        self.client.request('fake/path/', 'fake_uuid',
                            {'key1': 'value1',
                             'key2': 'value2'})

        call_args = self.get.call_args_list[0][0]
        call_kwargs = self.get.call_args_list[0][1]

        expected_url = ('http://127.0.0.1:8081/fake/path/fake_uuid')
        self.assertEqual(expected_url, call_args[0])

        data = call_kwargs.get('data')

        expected_data = {'arg1': 'aaa',
                         'key1': 'value1',
                         'key2': 'value2'}
        self.assertEqual(expected_data, data)