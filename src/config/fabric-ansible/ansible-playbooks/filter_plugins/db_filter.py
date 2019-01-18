#!/usr/bin/python

import logging
import json

from cfgm_common.exceptions import *
from cfgm_common.vnc_object_db import VncObjectDBClient
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

"""
This filter helps in reading objects from the DB directly without passing the
data through the additional layers that would encrypt the data. For example
useful in cases when we need access to the password for the devices etc.
"""


class DbLogger(object):
    def __init__(self):
        self.logger = logging.getLogger('DbFilter')
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.INFO)

        formatter = logging.Formatter(
            '%(asctime)s %(levelname)-8s %(message)s',
            datefmt='%Y/%m/%d %H:%M:%S'
        )
        console_handler.setFormatter(formatter)
        self.logger.addHandler(console_handler)

    def log(self, msg, level):
        if level is not None and msg is not None:
            if level == SandeshLevel.SYS_WARN or SandeshLevel.SYS_NOTICE:
                self.logger.warn(msg)
            elif level == SandeshLevel.SYS_EMERG or \
                    level == SandeshLevel.SYS_ALERT or \
                    level == SandeshLevel.SYS_CRIT or \
                    level == SandeshLevel.SYS_ERR:
                self.logger.error(msg)
            elif level == SandeshLevel.SYS_DEBUG:
                self.logger.debug(msg)
            elif level == SandeshLevel.SYS_INFO:
                self.logger.info(msg)
        elif msg is not None: # for all other sandesh levels just log as info
            self.logger.info(msg)


class FilterModule(object):

    def __init__(self):
        self._db_logger = DbLogger()
        self._logger = self._db_logger.logger
        self.db_client = None
    # end __init__

    def filters(self):
        return {
            'read_from_db': self.read_from_db

        }
    # end filters

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
                logger=self._db_logger.log, credential=cred,
                ssl_enabled=db_init_params.get('cassandra_use_ssl'),
                ca_certs=db_init_params.get('cassandra_ca_certs'))
    # end _initialize_db_client

    def read_from_db(self, job_ctx, obj_type, obj_id, obj_fields=None,
                     ret_readonly=False):
        if self.db_client is None:
            try:
                self._initialize_db_client(job_ctx)
            except Exception as e:
                msg = "Error while initializing the cassandra DB " \
                      "client. %s " % repr(e)
                self._logger.error(msg)
                return self._build_result(False, msg)
        try:
            ok, cassandra_result = self.db_client.object_read(
                obj_type, [obj_id], obj_fields, ret_readonly=ret_readonly)
        except Exception as e:
            self._logger.error("Exception while trying to read %s %s from db"
                               " %s " % (obj_type, obj_id, repr(e)))
            return self._build_result(False, str(e))

        return self._build_result(ok, cassandra_result[0])
    # end db_read

    @staticmethod
    def _build_result(is_ok, result):
        if is_ok is not None and is_ok:
            status = 'success'
        else:
            status = 'false'
        return {
            'status': status,
            'result': result
        }


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
    db_filter = FilterModule()
    device_id = "aa994cdb-e53f-4236-b8be-c26f70bf6e12"
    try:
        output = db_filter.read_from_db(
            job_ctx, "physical-router", device_id,
            ['physical_router_user_credentials'])
        print "%s" % output.get("result")
        if output.get('status') is not 'success':
            msg = "Error while reading the physical router " \
                  "with id %s : %s" % (device_id, output.get('result'))
            print msg
    except Exception as e:
        msg = "Exception while reading device %s %s " % \
              (device_id, str(e))
        print msg

# end __main__


if __name__ == '__main__':
    __main__()

