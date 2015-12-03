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

    def setUp(self):
        self.running_mock_containers_by_name = {}
        self.running_mock_containers_by_id = {}

        def on_inspect_container(vm):
            if vm in self.running_mock_containers_by_name:
                return self.running_mock_containers_by_name[vm]
            if vm in self.running_mock_containers_by_id:
                return self.running_mock_containers_by_id[vm]
            response = mock.MagicMock()
            response.status_code = 404
            raise docker.errors.APIError('', response)

        def on_create_container(name, *args, **kwargs):
            if name in self.running_mock_containers_by_name:
                response = mock.MagicMock()
                response.status_code = 409
                raise docker.errors.APIError('', response)
            self.running_mock_containers_by_name[name] = self.MOCK_CONTAINER
            id = self.MOCK_CONTAINER['Id']
            self.running_mock_containers_by_id[id] = self.MOCK_CONTAINER
            return self.MOCK_CONTAINER

        self.docker_client_mock = mock.create_autospec(docker.Client)
        self.docker_client_mock.inspect_image.return_value = self.MOCK_IMAGE
        self.docker_client_mock.create_container.\
            side_effect = on_create_container
        self.docker_client_mock.inspect_container.\
            side_effect = on_inspect_container

    def insert_mock_container(self):
        name = self.VM_ID
        id = self.MOCK_CONTAINER['Id']
        self.running_mock_containers_by_name[name] = self.MOCK_CONTAINER
        self.running_mock_containers_by_id[id] = self.MOCK_CONTAINER

    def mock_app(self, cmd):
        app = VRouterDocker(cmd)
        app._client = self.docker_client_mock
        return app

    def make_create_command(self):
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
        return cmd

    @mock.patch('opencontrail_vrouter_netns.vrouter_docker.NetnsManager')
    @mock.patch('opencontrail_vrouter_netns.vrouter_docker.os')
    def test_create(self, mock_os, mock_netns):
        cmd = self.make_create_command()
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
    @mock.patch('opencontrail_vrouter_netns.vrouter_docker.os')
    def test_create_when_running(self, mock_os, mock_netns):
        cmd = self.make_create_command()
        app = self.mock_app(cmd)
        self.insert_mock_container()
        app.args.func()

        assert not app._client.create_container.called
        app._client.start.assert_called_with(self.MOCK_CONTAINER["Id"],
                                             network_mode='none')

    def test_create_with_exception(self):
        cmd = self.make_create_command()
        app = self.mock_app(cmd)

        def on_inspect_container_docker_server_error(vm):
            response = mock.MagicMock()
            response.status_code = 500
            raise docker.errors.APIError('', response)

        self.docker_client_mock.inspect_container.\
            side_effect = on_inspect_container_docker_server_error
        self.assertRaises(docker.errors.APIError, app.args.func)

    @mock.patch('opencontrail_vrouter_netns.vrouter_docker.NetnsManager')
    @mock.patch('opencontrail_vrouter_netns.vrouter_docker.os')
    def test_delete(self, mock_os, mock_netns):
        cmd = "destroy"
        cmd += " " + self.VM_ID
        cmd += " --vmi-left-id " + self.VMI_LEFT['id']
        cmd += " --vmi-right-id " + self.VMI_RIGHT['id']
        cmd += " --vmi-management-id " + self.VMI_LEFT['id']
        app = self.mock_app(cmd)
        self.insert_mock_container()
        mock_os.path.islink.return_value = True
        app.args.func()

        app._client.inspect_container.assert_called_with(self.VM_ID)
        mocked_netns = mock_netns.return_value
        mocked_netns.unplug_namespace_interface.assert_called_with()
        mocked_netns.destroy.assert_called_with()
        docker_pid = self.MOCK_CONTAINER["State"]["Pid"]
        mock_os.remove.assert_called_once_with("/var/run/netns/%s" % docker_pid)
        mock_os.path.islink.assert_called_once_with("/var/run/netns/%s" % docker_pid)
