import sys
import mock
import unittest
import os
import types
from mock import patch, Mock

docker = Mock()
docker.client = Mock()
sys.modules['docker'] = docker

from cni.kube_cni import kube_params

class DockerClientMock(object):
    def __init__(self):
        pass

    def inspect_container(self, id):
        return {
            'Config': {
                'Labels': {
                    'io.kubernetes.pod.uid': "id" + id
                }
            }
        }

class K8SParamsTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    @patch('logging.getLogger', new=Mock())
    def test_init(self):
        os.environ['CNI_ARGS'] = "IgnoreUnknown=1;"\
                "K8S_POD_NAMESPACE=default-ns;"\
                "K8S_POD_NAME=hello-world-1-81nl8;"\
                "K8S_POD_INFRA_CONTAINER_ID=abcdef;;TEST;"
        mock_cni = Mock()
        mock_cni.container_id = "123"
        mock_cni.container_uuid = None
        mock_cni.update = Mock()

        docker.client.APIClient = Mock(return_value=DockerClientMock())
        p = kube_params.K8SParams(mock_cni)
        self.assertEquals("id123", p.pod_uuid)
        self.assertEquals("hello-world-1-81nl8", p.pod_name)
        mock_cni.update.assert_called_once_with("id123", "hello-world-1-81nl")

        docker.client.APIClient = Mock(return_value=None)
        with self.assertRaises(kube_params.Error) as err:
            kube_params.K8SParams(mock_cni)
        self.assertEquals(kube_params.K8S_PARAMS_ERR_GET_UUID, err.exception.code)

        docker.client.APIClient = Mock(return_value=DockerClientMock())
        os.environ['CNI_ARGS'] = "IgnoreUnknown=1;"\
                "K8S_POD_NAMESPACE=default-ns;"\
                "K8S_POD_INFRA_CONTAINER_ID=id123"
        with self.assertRaises(kube_params.Error) as err:
            kube_params.K8SParams(mock_cni)
        self.assertEquals(kube_params.K8S_ARGS_MISSING_POD_NAME, err.exception.code)

