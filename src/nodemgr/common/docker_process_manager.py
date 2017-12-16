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


def _get_nodemgr_name(container):
    labels = container.get('Labels')
    name = labels.get('net.juniper.nodemgr.filter.name') if labels is not None else None
    if name is None:
        name = container['Names'][0] if len(container['Names']) != 0 else container['Command']
    return name


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
        self.__unit_names = [name.rsplit('.service', 1)[0] for name in unit_names]
        self.__event_handlers = event_handlers
        self.__update_process_list = update_process_list
        self.__cached_process_infos = {}
        self.__client = docker.from_env()

    def __list_containers(self, names=None):
        client = self.__client
        containers = []
        all_containers = client.containers(all=True)
        if names is None or len(names) == 0:
            containers = all_containers
        else:
            for container in all_containers:
                name = _get_nodemgr_name(container)
                if name is not None and name in names:
                    containers.append(container)
        return containers

    def __find_container(self, name):
        containers = self.__list_containers(names=[name])
        return containers[0] if len(containers) > 0 else None

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

    def __get_start_time(self, cid):
        try:
            info = self.__client.inspect_container(cid)
        except docker.errors.APIError:
            return None
        state = info.get('State')
        start_time = state.get('StartedAt') if state else None
        if start_time is None:
            start_time = info.get('Created')
        if not start_time:
            return None
        return time.mktime(time.strptime(start_time.split('.')[0], '%Y-%m-%dT%H:%M:%S'))

    def __container_to_process_info(self, container):
        info = {}
        name = _get_nodemgr_name(container)
        info['name'] = name
        info['group'] = name
        cid = container['Id']
        info['pid'] = int(cid, 16)
        start_time = self.__get_start_time(cid)
        info['start'] = str(int(start_time * 1000000)) if start_time else None
        info['statename'] = _convert_to_process_state(container['State'])
        if info['statename'] == 'PROCESS_STATE_EXITED':
            info['expected'] = -1
        return info

    def __poll_containers(self):
        for name in self.__unit_names:
            container = self.__find_container(name)
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
        for container in self.__list_containers(self.__unit_names):
            info = self.__container_to_process_info(container)
            processes_info_list.append(info)
            self.__update_cache(info)
        return processes_info_list

    def run(self, test):
        # TODO: probaly use subscription on events..
        while True:
            self.__poll_containers()
            gevent.sleep(seconds=5)

    def get_mem_cpu_usage_data(self, pid, last_cpu, last_time):
        return DockerMemCpuUsageData(pid, last_cpu, last_time)

    def find_pid(self, name, pattern):
        container = self.__find_container(name)
        return container['Id'] if container is not None else None
