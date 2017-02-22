import sys
import mock
import unittest
import os
import types
import requests
from mock import patch, Mock

from cni.mesos_cni.contrail_mesos_cni import *
from cni.common.cni import Cni

class ContrailMesosCniTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    @patch("requests.post")
    def test_send_params_to_mesos_mgr(self, mock_post):
        c_cni = Mock()
        c_cni.cni = Mock()
        c_cni.cni.container_id = "123"
        c_cni.cni.stdin_json = {
            "a": "b",
            "c": "d"
        }
        expected_json = {
            "a": "b",
            "c": "d",
            "cid": "123"
        }
        response = Mock()
        response.text = ""
        mock_post.return_value = response

        response.status_code = requests.status_codes.codes.ok
        c_cni.cni.command = Cni.CNI_CMD_ADD
        send_params_to_mesos_mgr(c_cni)
        expected_json["cmd"] = "add"
        mock_post.assert_called_once_with(
            "http://127.0.0.1:6999/add_cni_info",
            json=expected_json)

        mock_post.reset_mock()
        c_cni.cni.command = Cni.CNI_CMD_DEL
        send_params_to_mesos_mgr(c_cni)
        expected_json["cmd"] = "del"
        mock_post.assert_called_once_with(
            "http://127.0.0.1:6999/del_cni_info",
            json=expected_json)

        response.status_code = requests.status_codes.codes.ok + 1
        with self.assertRaises(CniError) as err:
            send_params_to_mesos_mgr(c_cni)
        self.assertEquals(CNI_ERR_POST_PARAMS, err.exception.code)
