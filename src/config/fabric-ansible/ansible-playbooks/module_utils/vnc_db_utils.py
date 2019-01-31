#!/usr/bin/python

import logging
import sys
from cfgm_common.exceptions import *
from cfgm_common.vnc_object_db import VncObjectDBClient
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
from filter_utils import FilterLog, _task_info_log, _task_error_log, \
    _task_warn_log, _task_debug_log


"""
This helps in reading objects from the DB directly without passing the
data through the additional layers that would encrypt the data. For example
useful in cases when we need access to the password for the devices etc.
"""


class FabricVncObjectDBClient(object):

    def __init__(self):
        self.db_client = None
    # end __init__

    def _instantiate_filter_log_instance(self):
        FilterLog.instance('DbFilter')
    # end _instantiate_filter_log_instance

    def log(self, msg, level):
        if level is not None and msg is not None:
            if level == SandeshLevel.SYS_WARN or SandeshLevel.SYS_NOTICE:
                _task_warn_log(msg)
            elif level == SandeshLevel.SYS_EMERG or \
                    level == SandeshLevel.SYS_ALERT or \
                    level == SandeshLevel.SYS_CRIT or \
                    level == SandeshLevel.SYS_ERR:
                _task_error_log(msg)
            elif level == SandeshLevel.SYS_DEBUG:
                _task_debug_log(msg)
            elif level == SandeshLevel.SYS_INFO:
                _task_info_log(msg)
        elif msg is not None: # for all other sandesh levels just log as info
            _task_info_log(msg)
    # end log

    def _initialize_db_client(self, job_ctx):
        db_init_params = job_ctx.get('db_init_params')
        cred = None
        if (db_init_params.get('cassandra_user') is not None and
                db_init_params.get('cassandra_password') is not None):
            cred = {'username': db_init_params.get('cassandra_user'),
                    'password': db_init_params.get('cassandra_password')}
        if self.db_client is None:
            self.db_client = VncObjectDBClient(
                db_init_params.get('cassandra_server_list'),
                job_ctx.get('cluster_id'), None, None,
                logger=self.log, credential=cred,
                ssl_enabled=db_init_params.get('cassandra_use_ssl'),
                ca_certs=db_init_params.get('cassandra_ca_certs'))
    # end _initialize_db_client

    def read_from_db(self, job_ctx, obj_type, obj_id, obj_fields=None,
                     ret_readonly=False):
        self._instantiate_filter_log_instance()
        if self.db_client is None:
            try:
                self._initialize_db_client(job_ctx)
            except Exception as e:
                msg = "Error while initializing the cassandra DB " \
                      "client. %s " % repr(e)
                _task_error_log(msg)
                return 'failure', msg
        try:
            is_ok, cassandra_result = self.db_client.object_read(
                obj_type, [obj_id], obj_fields, ret_readonly=ret_readonly)
        except Exception as e:
            _task_error_log("Exception while trying to read %s %s from db: %s"
                            % (obj_type, obj_id, repr(e)))
            return 'failure', str(e)

        return 'success' if is_ok is not None and is_ok else 'failure', \
               cassandra_result[0]
    # end read_from_db


def __main__():
    # mock job_ctx
    job_ctx = {
        'cluster_id': "",
        'db_init_params': {
            'cassandra_user': None,
            'cassandra_password': None,
            'cassandra_server_list': ['10.155.75.111:9161'],
            'cassandra_use_ssl': False,
            'cassandra_ca_certs': None
        }

    }
    db_client = FabricVncObjectDBClient()
    device_id = "aa994cdb-e53f-4236-b8be-c26f70bf6e12"
    try:
        status, result = db_client.read_from_db(
            job_ctx, "physical-router", device_id,
            ['physical_router_user_credentials'])
        print "%s" % result
        if status is not 'success':
            msg = "Error while reading the physical router " \
                  "with id %s : %s" % (device_id, result)
            print msg
    except Exception as e:
        msg = "Exception while reading device %s %s " % \
              (device_id, str(e))
        print msg

# end __main__


if __name__ == '__main__':
    __main__()
