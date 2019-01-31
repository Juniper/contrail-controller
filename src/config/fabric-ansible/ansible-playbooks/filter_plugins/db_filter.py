#!/usr/bin/python

import sys

from cfgm_common.exceptions import *
from cfgm_common.vnc_object_db import VncObjectDBClient
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
from vnc_db_utils import FabricVncObjectDBClient

"""
This filter helps in reading objects from the DB directly without passing the
data through the additional layers that would encrypt the data. For example
useful in cases when we need access to the password for the devices etc.
"""

class FilterModule(object):

    def __init__(self):
        self.db_client = None
    # end __init__

    def filters(self):
        return {
            'read_from_db': self.read_from_db

        }
    # end filters

    def read_from_db(self, job_ctx, obj_type, obj_id, obj_fields=None,
                     ret_readonly=False):
        if self.db_client is None:
            self.db_client = FabricVncObjectDBClient()
        status, result = self.db_client.read_from_db(job_ctx,
            obj_type, [obj_id], obj_fields, ret_readonly=ret_readonly)

        return {
            'status': status,
            'result': result
        }
    # end db_read

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

