#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#


def dummy_process_info(name):
    info = dict()
    info['name'] = name
    info['group'] = name
    info['pid'] = 0
    info['statename'] = 'PROCESS_STATE_EXITED'
    info['expected'] = -1
    return info


def convert_to_pi_event(info):
    pi_event = info.copy()
    pi_event['state'] = pi_event.pop('statename')
    if 'start' in pi_event:
        del pi_event['start']
    return pi_event


class ProcessInfoCache(object):
    def __init__(self):
        self._cached_process_infos = {}

    def update_cache(self, info):
        name = info['name']
        cached_info = self._cached_process_infos.get(name)
        if cached_info is None:
            self._cached_process_infos[name] = info
            return True
        if info['name'] != cached_info['name'] or \
                info['group'] != cached_info['group'] or \
                info['pid'] != cached_info['pid'] or \
                info['statename'] != cached_info['statename']:
            self._cached_process_infos[name] = info
            return True
        return False
