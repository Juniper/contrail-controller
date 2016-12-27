import sys
import mock
import unittest
import os
import types
from mock import patch
from mock import Mock

from kube_cni.kube_cni import *

class KubeCniTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    @patch('__builtin__.open')
    @patch("sys.stdin")
    def test_read_config_file(self, mock_stdin, mock_open):
        mock_file = Mock(spec=file)
        mock_file.read = Mock(return_value='{ "key": "value"}')
        mock_stdin.read = Mock(return_value='{ "key2": "value2"}')
        mock_context = Mock()
        mock_context.__exit__ = Mock()
        mock_context.__enter__ = Mock(return_value = mock_file)
        mock_open.return_value = mock_context
        self.assertEqual(read_config_file("/dir/file.txt"), { 'key': 'value' })
        self.assertEqual(read_config_file(), { 'key2': 'value2' })
