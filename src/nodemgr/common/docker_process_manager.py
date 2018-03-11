#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import docker
import gevent
import time

from docker_mem_cpu import DockerMemCpuUsageData


def _convert_to_process_state(state):
    state_mapping = {
        'running': 'PROCESS_STATE_RUNNING',
        'exited': 'PROCESS_STATE_EXITED',
        'paused': 'PROCESS_STATE_STOPPED',
        'restarting': 'PROCESS_STATE_BACKOFF',
    }
    return state_mapping.get(state, 'PROCESS_STATE_UNKNOWN')


def _dummy_process_info(name):
    info = dict()
    info['name'] = name
    info['group'] = name
    info['pid'] = 0
    info['statename'] = 'PROCESS_STATE_EXITED'
    info['expected'] = -1
    return info


def _convert_to_pi_event(info):
    pi_event = info.copy()
    pi_event['state'] = pi_event.pop('statename')
    if 'start' in pi_event:
        del pi_event['start']
    return pi_event


class DockerProcessInfoManager(object):
    def __init__(self, unit_names, event_handlers, update_process_list):
        self.__unit_names = unit_names
        self.__event_handlers = event_handlers
        self.__update_process_list = update_process_list
        self.__cached_process_infos = {}
        self.__client = docker.from_env()

    def __get_full_info(self, cid):
        try:
            return self.__client.inspect_container(cid)
        except docker.errors.APIError:
            return None

    def __get_nodemgr_name(self, container):
        labels = container.get('Labels', dict())
        pod = labels.get('net.juniper.contrail.pod')
        service = labels.get('net.juniper.contrail.service')
        if pod and service:
            return pod + '-' + service

        name = labels.get('net.juniper.contrail')
        if name != 'nodemgr':
            return name

        # 'nodemgr' is a special image that must be parameterized at start
        # with NODE_TYPE env variable
        if 'Env' not in container:
            # list_containers does not return 'Env' information
            info = self.__get_full_info(container['Id'])
            if info:
                container = info['Config']
        if 'Env' in container:
            env = container.get('Env', list())
            node_type = next(iter(
                [i for i in env if i.startswith('NODE_TYPE=')]), None)
            if node_type:
                node_type = node_type.split('=')[1]
                name = 'contrail-' + node_type + '-nodemgr'
        return name

    def __list_containers(self, names):
        client = self.__client
        containers = dict()
        all_containers = client.containers(all=True)
        for container in all_containers:
            name = self.__get_nodemgr_name(container)
            if name is not None and name in names:
                containers[name] = container
        return containers

    def __update_cache(self, info):
        name = info['name']
        cached_info = self.__cached_process_infos.get(name)
        if cached_info is None:
            self.__cached_process_infos[name] = info
            return True
        if info['name'] != cached_info['name'] or \
                info['group'] != cached_info['group'] or \
                info['pid'] != cached_info['pid'] or \
                info['statename'] != cached_info['statename']:
            self.__cached_process_infos[name] = info
            return True
        return False

    def __get_start_time(self, info):
        state = info.get('State')
        start_time = state.get('StartedAt') if state else None
        if start_time is None:
            start_time = info.get('Created')
        if not start_time:
            return None
        return time.mktime(time.strptime(start_time.split('.')[0],
                                         '%Y-%m-%dT%H:%M:%S'))

    def __container_to_process_info(self, container):
        info = {}
        cid = container['Id']
        full_info = self.__get_full_info(cid)
        name = self.__get_nodemgr_name(full_info['Config'] if full_info else container)
        info['name'] = name
        info['group'] = name
        info['pid'] = int(cid, 16)
        start_time = self.__get_start_time(full_info) if full_info else None
        info['start'] = str(int(start_time * 1000000)) if start_time else None
        info['statename'] = _convert_to_process_state(container['State'])
        if info['statename'] == 'PROCESS_STATE_EXITED':
            info['expected'] = -1
        return info

    def __poll_containers(self):
        containers = self.__list_containers(self.__unit_names)
        for name in self.__unit_names:
            container = containers.get(name)
            if container is not None:
                info = self.__container_to_process_info(container)
            else:
                info = _dummy_process_info(name)
            if self.__update_cache(info):
                self.__event_handlers['PROCESS_STATE'](_convert_to_pi_event(info))
                if self.__update_process_list:
                    self.__event_handlers['PROCESS_LIST_UPDATE']()

    def get_all_processes(self):
        processes_info_list = []
        containers = self.__list_containers(self.__unit_names)
        for unit_name in containers:
            container = containers[unit_name]
            if not container:
                continue
            info = self.__container_to_process_info(container)
            processes_info_list.append(info)
            self.__update_cache(info)
        return processes_info_list

    def runforever(self):
        # TODO: probaly use subscription on events..
        while True:
            self.__poll_containers()
            gevent.sleep(seconds=5)

    def get_mem_cpu_usage_data(self, pid, last_cpu, last_time):
        return DockerMemCpuUsageData(pid, last_cpu, last_time)

    def exec_cmd(self, unit_name, cmd):
        containers = self.__list_containers(names=[unit_name])
        if not len(containers):
            raise LookupError(unit_name)
        container = containers[0]
        exec_op = self.__client.exec_create(container['Id'], cmd, tty=True)
        result = self.__client.exec_start(exec_op["Id"], detach=False)
        data = self.__clientexec_inspect(exec_op["Id"])
        exit_code = data.get("ExitCode", 0)
        if exit_code:
            raise RuntimeError(result)
        return result
