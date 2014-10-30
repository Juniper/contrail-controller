import mock
import unittest
import docker
from opencontrail_vrouter_netns.vrouter_docker import VRouterDocker


class DockerTest(unittest.TestCase):
    MOCK_IMAGE = {
        "ContainerConfig": {
            "Cmd": ":"
        }
    }
    MOCK_CONTAINER = {
        "Id": "test123",
        "State": {
            "Pid": 123
        }
    }
    VM_ID = "9f52496a-b230-4209-8715-a87d57138c0f"
    VM_IMAGE = "ubuntu"
    INSTANCE_DATA = '{"command":"/bin/bash"}'
    VMI_RIGHT = {
        "id": "cf49987f-23d8-4936-818c-ffd52e51e415",
        "ip": "30.1.1.253/24",
        "mac": "02:cf:49:98:7f:23"
    }
    VMI_LEFT = {
        "id": "bbec5ec9-eb3b-48b1-8f7f-0cb980e3ee6a",
        "ip": "10.1.1.253/24",
        "mac": "02:bb:ec:5e:c9:eb"
    }
    VMI_MANAGEMENT = {
        "id": "652b2f0f-181a-4a1c-a28e-ad45f59ad593",
        "ip": "20.1.1.253/24",
        "mac": "02:65:2b:2f:0f:18"
    }

    def mock_app(self, cmd):
        app = VRouterDocker(cmd)
        docker_client_mock = mock.create_autospec(docker.Client)
        docker_client_mock.inspect_image.return_value = self.MOCK_IMAGE
        docker_client_mock.create_container.return_value = self.MOCK_CONTAINER
        docker_client_mock.inspect_container.return_value = self.MOCK_CONTAINER
        app._client = docker_client_mock
        return app


    @mock.patch('opencontrail_vrouter_netns.vrouter_docker.NetnsManager')
    @mock.patch('opencontrail_vrouter_netns.vrouter_docker.os')
    def test_create(self, mock_os, mock_netns):
        cmd = "create"
        cmd += " " + self.VM_ID
        cmd += " --image " + self.VM_IMAGE
        cmd += " --vmi-left-id " + self.VMI_LEFT['id']
        cmd += " --vmi-left-ip " + self.VMI_LEFT['ip']
        cmd += " --vmi-left-mac " + self.VMI_LEFT['mac']
        cmd += " --vmi-right-id " + self.VMI_RIGHT['id']
        cmd += " --vmi-right-ip " + self.VMI_RIGHT['ip']
        cmd += " --vmi-right-mac " + self.VMI_RIGHT['mac']
        cmd += " --vmi-management-id " + self.VMI_LEFT['id']
        cmd += " --vmi-management-ip " + self.VMI_MANAGEMENT['ip']
        cmd += " --vmi-management-mac " + self.VMI_MANAGEMENT['mac']
        cmd += " --instance-data " + self.INSTANCE_DATA
        app = self.mock_app(cmd)
        app.args.func()

        docker_pid = self.MOCK_CONTAINER["State"]["Pid"]
        app._client.create_container.assert_called_with(
            image=self.VM_IMAGE, name=self.VM_ID, command="/bin/bash",
            detach=True, stdin_open=True, tty=True)
        app._client.start.assert_called_with(self.MOCK_CONTAINER["Id"],
                                             network_mode='none')
        mock_os.symlink.assert_called_with("/proc/%s/ns/net" % docker_pid,
                                           "/var/run/netns/%s" % docker_pid)
        mocked_netns = mock_netns.return_value
        mocked_netns.create.assert_called_with()
        mocked_netns.plug_namespace_interface.assert_called_with()


    @mock.patch('opencontrail_vrouter_netns.vrouter_docker.NetnsManager')
    def test_delete(self, mock_netns):
        cmd = "destroy"
        cmd += " " + self.VM_ID
        cmd += " --vmi-left-id " + self.VMI_LEFT['id']
        cmd += " --vmi-right-id " + self.VMI_RIGHT['id']
        cmd += " --vmi-management-id " + self.VMI_LEFT['id']
        app = self.mock_app(cmd)
        app.args.func()
        app._client.inspect_container.assert_called_with(self.VM_ID)
        mocked_netns = mock_netns.return_value
        mocked_netns.unplug_namespace_interface.assert_called_with()
        mocked_netns.destroy.assert_called_with()