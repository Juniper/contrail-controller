#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import bottle
from datetime import datetime

from uve.vnc_api.ttypes import VncApiStats, VncApiStatsLog
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel


def log_api_stats(func):
    def wrapper(api_server_obj, resource_type, *args, **kwargs):
        try:
            statistics = VncApiStatistics(
                obj_type=resource_type.replace('-', '_'))
            return func(api_server_obj, resource_type, *args, **kwargs)
        finally:
            # Collect api stats and send to analytics
            statistics.collect()
            statistics.sendwith(api_server_obj._sandesh)
    return wrapper


class VncApiStatistics(object):
    def __init__(self, obj_type):
        self.obj_type = obj_type
        self.time_start = datetime.now()

    def collect(self):
        self.time_finish = datetime.now()
        self.api_stats = VncApiStats(
            object_type=self.obj_type,
            operation_type=bottle.request.method,
            user=bottle.request.headers.get('X-User-Name'),
            useragent=bottle.request.headers.get('X-Contrail-Useragent',
                bottle.request.headers.get('User-Agent')),
            remote_ip=bottle.request.headers.get('Host'),
            domain_name=bottle.request.headers.get(
                'X-Domain-Name', 'default-domain'),
            project_name=bottle.request.headers.get(
                'X-Project-Name', 'default-project'),
            response_time_in_usec=(self.time_finish -
                self.time_start).total_seconds() * 1000000,
            response_size=bottle.request.headers.get(
                'Content-Length', 0),
            response_code=bottle.response.status_code,
        )

    def sendwith(self, sandesh):
        stats_log = VncApiStatsLog(api_stats=self.api_stats, sandesh=sandesh)
        stats_log.send(sandesh=sandesh)
