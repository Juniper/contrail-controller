import os
import grpc
import json
import logging
import datetime
from nodemgr.common import utils
from nodemgr.common.cri import api_pb2
from nodemgr.common.cri import api_pb2_grpc
from nodemgr.common.sandesh.nodeinfo.cpuinfo.ttypes import ProcessCpuInfo


class CriContainerMemoryCpuUsage:
    def __init__(self, last_cpu_, last_time_, query_, pid_):
        self._query = query_
        self._cgroup = '/sys/fs/cgroup/memory{0}/memory.stat'\
            .format(utils.get_memory_cgroup(pid_))
        self._last_cpu = last_cpu_
        self._last_time = last_time_

    @property
    def last_cpu(self):
        return self._last_cpu

    @property
    def last_time(self):
        return self._last_time

    def _get_rss_from_cgroup(self):
        try:
            with open(self._cgroup, 'r') as f:
                while True:
                    ll = f.readline()
                    if not ll:
                        break
                    v = ll.partition('rss ')[2]
                    if v:
                        return int(v.strip())
        except Exception:
            logging.exception('memory stat reading')

        return 0

    def get_process_mem_cpu_info(self):
        y = self._query()
        if not y:
            return None

        output = ProcessCpuInfo()
        output.mem_res = self._get_rss_from_cgroup() // 1024
        if hasattr(y, 'memory'):
            output.mem_virt = y.memory.working_set_bytes.value // 1024

        if hasattr(y, 'cpu'):
            u = self._last_cpu
            d = y.cpu.usage_core_nano_seconds.value - u.usage_core_nano_seconds.value \
                if u else 0
            D = y.cpu.timestamp - self._last_time

            self._last_cpu = y.cpu
            self._last_time = y.cpu.timestamp

            output.cpu_share = round(d / D, 2) if 0 < D else 0

        return output


class Instrument:
    @staticmethod
    def craft_date_string(nanos_):
        t = divmod(nanos_, 1e9)
        d = datetime.datetime.fromtimestamp(t[0])
        return d.strftime('%Y-%m-%dT%H:%M:%S.{:09.0f}%z').format(t[1])

    @staticmethod
    def craft_status_string(enum_):
        if api_pb2.CONTAINER_CREATED == enum_:
            return 'created'
        if api_pb2.CONTAINER_RUNNING == enum_:
            return 'running'
        if api_pb2.CONTAINER_EXITED == enum_:
            return 'exited'

        # CONTAINER_UNKNOWN and the rest
        return 'unknown'

    @staticmethod
    def craft_node_type_surrogate(labels_):
        v = os.getenv('VENDOR_DOMAIN', 'tungsten.io')
        if not v:
            return None

        n = labels_.get('io.kubernetes.container.name', '')
        s = labels_.get(v + '.service')
        c = n.split('_', 2)
        return c[1] if 3 == len(c) and (c[0], c[2]) == ('contrail', s) \
            else None


class ViewAdapter:
    @staticmethod
    def craft_list_item(protobuf_):
        if not protobuf_:
            return None

        output = dict()
        output['Id'] = protobuf_.id
        output['Name'] = protobuf_.metadata.name \
            if hasattr(protobuf_, 'metadata') else protobuf_.id
        output['State'] = Instrument.craft_status_string(protobuf_.state)
        output['Labels'] = protobuf_.labels \
            if hasattr(protobuf_, 'labels') else dict()
        output['Created'] = Instrument.craft_date_string(protobuf_.created_at)

        return output

    @staticmethod
    def craft_info(protobuf_):
        if not(protobuf_ and hasattr(protobuf_, 'info')):
            return None

        c = dict()
        i = protobuf_.info
        if 'info' in protobuf_.info:
            i = json.loads(i['info'])
            if 'runtimeSpec' in i:
                s = i['runtimeSpec']
                if 'process' in s:
                    c['Env'] = s['process'].get('env', [])

        output = {'Config': c}
        if hasattr(protobuf_, 'status'):
            s = protobuf_.status
            S = dict()
            c['Id'] = output['Id'] = s.id
            if 'Env' not in c:
                x = Instrument.craft_node_type_surrogate(s.labels)
                if x:
                    c['Env'] = ['NODE_TYPE=%s' % x]

            output['Name'] = s.metadata.name if hasattr(s, 'metadata') else s.id
            if not output['Name']:
                output['Name'] = s.id

            output['State'] = S
            output['Labels'] = s.labels
            output['Created'] = Instrument.craft_date_string(s.created_at)

            S['Pid'] = i.get('pid', 0)
            S['Status'] = Instrument.craft_status_string(s.state)
            S['StatedAt'] = Instrument.craft_date_string(s.started_at) \
                if hasattr(s, 'started_at') and s.started_at > 0 else ''

        return output


class CriContainersInterface:
    @staticmethod
    def craft_crio_peer():
        return CriContainersInterface()._set_channel('/var/run/crio/crio.sock')

    @staticmethod
    def craft_containerd_peer():
        return CriContainersInterface()._set_channel('/run/containerd/containerd.sock')

    def _set_channel(self, value_):
        if not os.path.exists(value_):
            raise LookupError(value_)

        c = grpc.insecure_channel('unix://{0}'.format(value_))
        self._client = api_pb2_grpc.RuntimeServiceStub(c)
        return self

    def list(self, all_=True):
        q = api_pb2.ListContainersRequest() if all_ \
            else api_pb2.ListContainersRequest(
                filter=api_pb2.ContainerFilter(
                    state=api_pb2.ContainerStateValue(
                        state=api_pb2.CONTAINER_RUNNING)))

        try:
            return [ViewAdapter.craft_list_item(i) for i in
                    self._client.ListContainers(q).containers]
        except Exception:
            logging.exception('gRPC')
            return None

    def inspect(self, id_):
        q = api_pb2.ContainerStatusRequest(container_id=id_, verbose=True)
        try:
            a = self._client.ContainerStatus(q)
            return ViewAdapter.craft_info(a)

        except Exception:
            logging.exception('gRPC')
            return None

    def execute(self, id_, line_):
        x = ['/bin/sh', '-c', line_]
        q = api_pb2.ExecSyncRequest(container_id=id_, cmd=x, timeout=10)
        try:
            a = self._client.ExecSync(q)
            if 0 != a.exit_code:
                logging.critical(a.stderr)

            return (a.exit_code, a.stdout)

        except Exception:
            logging.exception('gRPC')
            return (-1, None)

    def query_usage(self, id_, last_cpu_, last_time_):
        i = format(id_, 'x').zfill(64)
        s = self.inspect(i)
        if not s:
            raise LookupError(i)

        def do_query():
            q = api_pb2.ContainerStatsRequest(container_id=i)
            try:
                output = self._client.ContainerStats(q)
                return output.stats if hasattr(output, 'stats') else None

            except Exception:
                logging.exception('gRPC')
                return None

        return CriContainerMemoryCpuUsage(last_cpu_, last_time_, do_query, s['State']['Pid'])
