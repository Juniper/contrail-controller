import sys
import mock
import unittest
import os
import types
from mock import patch

sys.modules['docker'] = mock.Mock()

from kube_cni.params import params

class KubeCniTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    @patch('os.environ', new={})
    def test_get_env(self):
        os.environ["KEY"] = "VALUE"
        self.assertEqual(params.get_env("KEY"), "VALUE")
        self.assertRaises(params.ParamsError, params.get_env, "UNKNOWN")

class KubeCniContrailParamsTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    def test_get_params(self):
        p = params.ContrailParams()
        p.get_params({
            'config-dir': '/dir',
            'vrouter-ip': '10.10.10.10',
            'vrouter-port': '9000',
            'poll-timeout': '10',
            'poll-retries': '3'
        })
        self.assertEqual(p.directory, '/dir')
        self.assertEqual(p.vrouter_ip, '10.10.10.10')
        self.assertEqual(p.vrouter_port, '9000')
        self.assertEqual(p.poll_timeout, '10')
        self.assertEqual(p.poll_retries, '3')

    def test_get_loggin_params(self):
        p = params.ContrailParams()
        p.get_loggin_params({
            'log-file': '/dir/file',
            'log-level': '1'
        })
        self.assertEqual(p.log_file, '/dir/file')
        self.assertEqual(p.log_level, '1')

class KubeCniK8SParamsTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    @patch('docker.client')
    def test_get_pod_info(self, docker):
        docker_client = mock.Mock()
        docker_client.inspect_container = lambda id: {
            'State': {
                'Pid': int(id) * 2
            },
            'Config': {
                'Labels': {
                    'io.kubernetes.pod.uid': id + "uuid"
                }
            }
        }
        docker.Client = mock.Mock(return_value=docker_client)

        p = params.K8SParams()
        p.get_pod_info("12")
        self.assertEqual(p.pod_pid, 24)
        self.assertEqual(p.pod_uuid, "12uuid")

    @patch('docker.client')
    @patch('os.environ', new={})
    def test_get_params(self, docker):
        os.environ['CNI_ARGS'] = "IgnoreUnknown=1;"\
                "K8S_POD_NAMESPACE=default-ns;"\
                "K8S_POD_NAME=hello-world-1-81nl8;"\
                "K8S_POD_INFRA_CONTAINER_ID=id123"
        with patch.object(params.K8SParams, 'get_pod_info') as k8s_get_pod_info_mock:
            p = params.K8SParams()
            p.get_params("id123")
        k8s_get_pod_info_mock.assert_called_once_with("id123")
        self.assertEqual(p.pod_name, "hello-world-1-81nl8")
        self.assertEqual(p.pod_namespace, "default-ns")

class KubeCniParamsTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    @patch('os.environ', new={})
    def test_params_get_params(self):
        json_input = {
            'contrail': None,
            'k8s': None
        }
        with patch.object(params.K8SParams, 'get_params') as k8s_get_params_mock:
            p = params.Params()
            self.assertRaises(params.ParamsError, p.get_params, json_input)
            os.environ["CNI_COMMAND"] = "UNKNOWN"
            os.environ["CNI_CONTAINERID"] = "12345"
            os.environ["CNI_NETNS"] = "ns"
            os.environ["CNI_IFNAME"] = "eth0"
            self.assertRaises(params.ParamsError, p.get_params, json_input)
            for cmd in ['ADD', 'DELETE']:
                os.environ["CNI_COMMAND"] = cmd
                p = params.Params()
                p.get_params(json_input)
                self.assertEqual(p.command, cmd)
                self.assertEqual(p.container_id, "12345")
                self.assertEqual(p.container_netns, "ns")
                self.assertEqual(p.container_ifname, "eth0")

