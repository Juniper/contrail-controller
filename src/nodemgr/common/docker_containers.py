import sys
import docker
import logging
from nodemgr.common.docker_mem_cpu import DockerMemCpuUsageData

class DockerContainersInterface:
    def __init__(self):
        self._client = docker.from_env()
        if hasattr(self._client, 'api'):
            self._client = self._client.api

    def list(self, all_ = True):
        return self._client.containers(all = all_)

    def inspect(self, id_):
        try:
            return self._client.inspect_container(id_)
        except docker.errors.APIError:
            logging.exception('docker')
            return None

    def execute(self, id_, line_):
        exec_op = self._client.exec_create(id_, line_, tty=True)
        res = ''
        try:
            # string or stream result works unstable
            # using socket with own read implementation
            socket = self._client.exec_start(exec_op["Id"], tty=True, socket=True)
            socket.settimeout(10.0)
            while True:
                part = socket.recv(1024)
                if len(part) == 0:
                    break
                res += part
        finally:
            if socket:
                # There is cyclic reference there
                # https://github.com/docker/docker-py/blob/master/docker/api/client.py#L321
                # sock => response => socket
                # https://github.com/docker/docker-py/issues/2137
                try:
                    socket._response = None
                except AttributeError:
                    pass
                socket.close()

        data = self._client.exec_inspect(exec_op["Id"])
        return (data.get("ExitCode", 0), res)

    def query_usage(self, id_, last_cpu_, last_time_):
        return DockerMemCpuUsageData(id_, last_cpu_, last_time_)
