import errno
import json
import mock
import os
import requests
import sys
import unittest
from mock import patch

from cni.contrail import vrouter


class ContrailVRouterTest(unittest.TestCase):
    def setUp(self):
        self._vrouter_json = """
{
    "contrail": {
        "vrouter-ip": "%s",
        "vrouter-port": %s,
        "poll-timeout": %s,
        "poll-retries": %s,
        "config-dir": "%s"
    }
}
"""

    def tearDown(self):
        pass

    def _build_vrouter_json(self, ip, port, timeout, retries, dir):
        return self._vrouter_json % (ip, port, timeout, retries, dir)

    def test_make_filename(self):
        vjson = self._build_vrouter_json('10.0.0.1', 9000, 30, 3, '/dir1')
        v = vrouter.VRouter(vjson)
        self.assertEqual(v.make_filename('vm123', None), '/dir1/vm123')
        self.assertEqual(v.make_filename('vm123', '456'), '/dir1/vm123/456')

    def test_make_url(self):
        vjson = self._build_vrouter_json('10.0.0.1', 9000, 30, 3, '/dir1')
        v = vrouter.VRouter(vjson)
        self.assertEqual(v.make_url('vm123', None, '/vm'),
                         ('http://10.0.0.1:9000/vm/vm123',
                         {'content-type': 'application/json'}))
        self.assertEqual(v.make_url('vm123', '456', '/vm'),
                         ('http://10.0.0.1:9000/vm/vm123/456',
                         {'content-type': 'application/json'}))

    @patch('requests.get')
    def test_get_cmd(self, get):
        vjson = self._build_vrouter_json('10.0.0.1', 9000, 30, 3, '/dir1')
        v = vrouter.VRouter(vjson)
        response = mock.Mock()
        response.raise_for_status = mock.Mock(side_effect=KeyError('foo'))
        get.return_value = response
        with self.assertRaises(vrouter.Error) as exc:
            v.get_cmd('a', 'b')
        get.assert_called_once_with('http://10.0.0.1:9000/vm/a/b')
        self.assertEqual(exc.exception.code, vrouter.VROUTER_CONN_ERROR)
        get.reset_mock()
        response.raise_for_status.side_effect = None
        response.text = '{ "a": "b", "c": [3,2,1] }'
        self.assertEqual(v.get_cmd('c', 'd'), {'a': 'b', 'c': [3, 2, 1]})
        get.assert_called_once_with('http://10.0.0.1:9000/vm/c/d')

    def test_poll_cmd(self):
        with patch.object(vrouter.VRouter, 'get_cmd',
                          return_value={'name': ['json']}) as get_cmd:
            vjson = self._build_vrouter_json('10.0.0.1', 9000,
                                                1, 3, '/dir1')
            v = vrouter.VRouter(vjson)
            self.assertEqual(v.poll_cmd('vm', 'nw', '/vm'),
                             {'name': ['json']})
        get_cmd.assert_called_once_with('vm', 'nw', '/vm')
        with patch.object(vrouter.VRouter, 'get_cmd',
                          side_effect=vrouter.Error(123, 'error message')) as get_cmd:
            vjson = self._build_vrouter_json('10.0.0.1', 9000,
                                                0.01, 3, '/dir1')
            v = vrouter.VRouter(vjson)
            with self.assertRaises(vrouter.Error) as error:
                v.poll_cmd('vm', 'nw')
            self.assertEqual(error.exception.code, 123)
            self.assertEqual(error.exception.msg, 'error message')
        self.assertEqual(get_cmd.call_count, 3)

    @patch('os.remove')
    @patch('os.path.exists', new=mock.Mock(return_value=True))
    def test_delete_file(self, remove):
        vjson = self._build_vrouter_json('10.0.0.1', 9000, 30, 3, '/dir1')
        v = vrouter.VRouter(vjson)
        v.delete_file('vm', 'nw')
        remove.assert_called_once_with('/dir1/vm/nw')

    @patch('requests.delete')
    def test_delete_from_vrouter(self, delete):
        vjson = self._build_vrouter_json('10.0.0.1', 9000, 30, 3, '/dir1')
        v = vrouter.VRouter(vjson)
        response = mock.Mock()
        response.raise_for_status = mock.Mock(side_effect=KeyError('foo'))
        delete.return_value = response
        with self.assertRaises(vrouter.Error) as exc:
            v.delete_from_vrouter('a', 'b')
        self.assertEqual(delete.call_count, 1)
        self.assertEqual(
            delete.call_args[0][0],
            'http://10.0.0.1:9000/vm/a/b')
        self.assertEqual(
            json.loads(delete.call_args[1]['data']),
            {'vm': 'a', 'nw': 'b'})
        self.assertEqual(exc.exception.code, vrouter.VROUTER_CONN_ERROR)
        delete.reset_mock()
        response.raise_for_status.side_effect = None
        v.delete_from_vrouter('c', None)
        self.assertEqual(delete.call_count, 1)
        self.assertEqual(
            delete.call_args[0][0],
            'http://10.0.0.1:9000/vm/c')
        self.assertEqual(
            json.loads(delete.call_args[1]['data']),
            {'vm': 'c'})

    def test_delete_cmd(self):
        with patch.object(vrouter.VRouter, 'delete_from_vrouter') as delete_from_vr:
            with patch.object(vrouter.VRouter, 'delete_file') as delete_file:
                vjson = self._build_vrouter_json('10.0.0.1', 9000, 30, 3, '/dir1')
                v = vrouter.VRouter(vjson)
                v.delete_cmd('id', 'aaa')
        delete_from_vr.assert_called_once_with('id', 'aaa')
        delete_file.assert_called_once_with('id', 'aaa')

    @patch('os.path.isdir')
    @patch('os.makedirs')
    def test_create_directory(self, makedirs, isdir):
        vjson = self._build_vrouter_json('10.0.0.1', 9000, 30, 3, '/dir1')
        v = vrouter.VRouter(vjson)
        v.create_directory()
        makedirs.assert_called_once_with('/dir1')

        def raise_errno_perm(dirname):
            err = OSError()
            err.errno = errno.EPERM
            raise err

        makedirs.side_effect = raise_errno_perm
        with self.assertRaises(vrouter.Error) as err:
            v.create_directory()
        self.assertEqual(err.exception.code, vrouter.VROUTER_INVALID_DIR)

        def raise_errno_exists(dirname):
            err = OSError()
            err.errno = errno.EEXIST
            raise err

        isdir.return_value = True
        makedirs.side_effect = raise_errno_exists
        v.create_directory()
        isdir.return_value = False
        with self.assertRaises(vrouter.Error) as err:
            v.create_directory()
        self.assertEqual(err.exception.code, vrouter.VROUTER_INVALID_DIR)

    @patch('__builtin__.open')
    @patch('os.makedirs', new=mock.Mock())
    def test_write_file(self, open_mock):
        file_mock = mock.Mock(spec=file)
        file_mock.write = mock.Mock()
        file_mock.close = mock.Mock()
        open_mock.return_value = file_mock
        vjson = self._build_vrouter_json('10.0.0.1', 9000, 30, 3, '/dir1')
        v = vrouter.VRouter(vjson)
        v.write_file('some text', 'vm', 'nw')
        open_mock.assert_called_once_with('/dir1/vm/nw', 'w')
        file_mock.write.assert_called_once_with('some text')
        file_mock.close.assert_called_once_with()

    @patch('requests.post')
    def test_add_to_vrouter(self, post):
        vjson = self._build_vrouter_json('10.0.0.1', 9000, 30, 3, '/dir1')
        v = vrouter.VRouter(vjson)
        response = mock.Mock()
        response.raise_for_status = mock.Mock(side_effect=KeyError('foo'))
        post.return_value = response
        with self.assertRaises(vrouter.Error) as exc:
            v.add_to_vrouter('data', 'a', 'b')
        post.assert_called_once_with(
            'http://10.0.0.1:9000/vm',
            data='data',
            headers={'content-type': 'application/json'})
        self.assertEqual(exc.exception.code, vrouter.VROUTER_CONN_ERROR)
        post.reset_mock()
        response.raise_for_status.side_effect = None
        v.add_to_vrouter('text', 'c', None)
        post.assert_called_once_with(
            'http://10.0.0.1:9000/vm',
            data='text',
            headers={'content-type': 'application/json'})

        post.reset_mock()
        response.text = 'text'
        response.status_code = '999'
        response.raise_for_status.side_effect = requests.exceptions.HTTPError
        with self.assertRaises(vrouter.Error) as exc:
            v.add_to_vrouter('data', 'a', 'b')
        self.assertEqual(exc.exception.code, vrouter.VROUTER_CONN_ERROR)
        response.raise_for_status.side_effect = requests.exceptions.RequestException
        with self.assertRaises(vrouter.Error) as exc:
            v.add_to_vrouter('data', 'a', 'b')
        self.assertEqual(exc.exception.code, vrouter.VROUTER_CONN_ERROR)


    @patch('datetime.datetime')
    def test_add_cmd(self, datetime):
        datetime.now = lambda: 123456
        with patch.object(vrouter.VRouter, 'add_to_vrouter') as add_to_vrouter:
            with patch.object(vrouter.VRouter, 'write_file') as write_file:
                with patch.object(vrouter.VRouter, 'get_cmd',
                                  return_value={'name': ['json']}):
                    vjson = self._build_vrouter_json('10.0.0.1', 9000, 30, 3, '/dir1')
                    v = vrouter.VRouter(vjson)
                    ret = v.add_cmd('uuid', 'id', 'name', 'namespace',
                                    'host_ifname', 'container_ifname', 'nw')
                    self.assertEqual(ret, {'name': ['json']})
        json_data = json.dumps({
            'time': '123456',
            'vm-id': 'id',
            'vm-uuid': 'uuid',
            'host-ifname': 'host_ifname',
            'vm-ifname': 'container_ifname',
            'vm-name': 'name',
            'vm-namespace': 'namespace'
        }, indent=4)
        write_file.assert_called_once_with(json_data, 'uuid', 'nw')
        add_to_vrouter.assert_called_once_with(json_data, 'uuid', 'nw')
