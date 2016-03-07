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
            response = func(api_server_obj, resource_type, *args, **kwargs)
            statistics.response_size = len(str(response))
            statistics.response_code = bottle.response.status_code
            return response
        except bottle.HTTPError as err_response:
            statistics.response_size = len(err_response.body)
            statistics.response_code = err_response.status_code
            raise
        finally:
            # Collect api stats and send to analytics
            statistics.collect()
            statistics.sendwith(api_server_obj._sandesh)
    return wrapper


class VncApiStatistics(object):
    def __init__(self, obj_type):
        self.obj_type = obj_type
        self.response_size = 0
        self.response_code = 520 # Unknown Error
        self.time_start = datetime.now()

    def collect(self):
        self.time_finish = datetime.now()
        response_time = (self.time_finish - self.time_start)
        response_time_in_usec = ((response_time.days*24*60*60) +
                                 (response_time.seconds*1000000) +
                                 response_time.microseconds)
        domain_name = bottle.request.headers.get('X-Domain-Name', 'None')
        if domain_name.lower() == 'none':
            domain_name = 'default-domain'
        project_name = bottle.request.headers.get('X-Project-Name', 'None')
        if project_name.lower() == 'none':
            project_name = 'default-project'

        # Create api stats object
        self.api_stats = VncApiStats(
            object_type=self.obj_type,
            operation_type=bottle.request.method,
            user=bottle.request.headers.get('X-User-Name'),
            useragent=bottle.request.headers.get('X-Contrail-Useragent',
                bottle.request.headers.get('User-Agent')),
            remote_ip=bottle.request.headers.get('Host'),
            domain_name=domain_name,
            project_name=project_name,
            response_time_in_usec=response_time_in_usec,
            response_size=self.response_size,
            resp_code=str(self.response_code),
        )

    def sendwith(self, sandesh):
        stats_log = VncApiStatsLog(api_stats=self.api_stats, sandesh=sandesh)
        stats_log.send(sandesh=sandesh)
