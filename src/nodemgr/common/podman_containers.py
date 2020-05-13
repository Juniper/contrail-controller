import json
import docker
import logging
import subprocess
from nodemgr.common.sandesh.nodeinfo.cpuinfo.ttypes import ProcessCpuInfo

class PodmanContainerMemoryCpuUsage:
    def __init__(self, query_):
        self._query = query_

    @property
    def last_cpu(self):
        return 0

    @property
    def last_time(self):
        return 0

    def get_process_mem_cpu_info(self):
        y = self._query();
        if not y:
            return None

        output = ProcessCpuInfo()
        u = y['cpu_percent'] if 'cpu_percent' in y else 0.0
        u = 0.0 if '--' == u else float(u[0:-1])

        output.cpu_share = u

        u = y['mem_percent'] if 'mem_percent' in y else 0

        u = 0
        if 'mem_usage' in y:
            m, _ = y['mem_usage'].split('/', 1)
            m = m.rstrip()
        if m and m.endswith('kB'):
            u = int((2**10)*float(m[0:-2]))
        if m and m.endswith('MB'):
            u = int((2**20)*float(m[0:-2]))
        if m and m.endswith('GB'):
            u = int((2**30)*float(m[0:-2]))

        output.mem_virt = u // 1024
        output.mem_res = 0

        return output

class PodmanContainersInterface:
    def _execute(self, arguments_, timeout_ = 10):
	a = ["podman"]
	a.extend(arguments_)
	p = subprocess.Popen(a, stdout = subprocess.PIPE, stderr = subprocess.PIPE)
	try:
	    o, e = p.communicate(timeout_)
	except TimeoutExpired:
	    p.kill()
	    o, e = p.communicate()
	p.wait()
	if e:
	    logging.critical(e)

	return (p.returncode, o)

    def _parse_json(self, arguments_):
	a = []
	a.extend(arguments_)
	a.extend(["--format", "json"])
	c, o = self._execute(a)
	if 0 != c:
	    # NB. there is nothing to parse
	    return (c, None)

	try:
	    return (c, json.loads(o))
	except Exception:
	    logging.exception('json parsing')
	    return (c, None)

    def _decorate(self, container_):
        if container_:
            if 'ID' in container_:
                container_['Id'] = container_['ID']
            if 'State' in container_:
                s = container_['State']
                container_['State'] = ['unknown', 'configured', 'created',
                        'running', 'stopped', 'paused', 'exited', 'removing'][s]

        return container_

    def list(self, all_ = True):
        a = ["ps"]
        if all_:
            a.append("-a")

        _, output = self._parse_json(a)
        if output:
            for i in output:
                self._decorate(i)

        return output

    def inspect(self, id_):
        _, output = self._parse_json(["inspect", id_])
        if output and len(output) > 0:
            return output[0]

        return None

    def execute(self, id_, line_):
        return self._execute(["exec", id_, '/usr/bin/sh', '-c', line_])

    def query_usage(self, id_, last_cpu_ = None, last_time_ = None):
        def do_query():
            _, x = self._parse_json(["stats", "--no-stream", format(id_, 'x').zfill(12)]);
            if not x or len(x) == 0:
                return None

            return x[0]

        return PodmanContainerMemoryCpuUsage(do_query)
