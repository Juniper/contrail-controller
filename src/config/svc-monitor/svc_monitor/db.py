# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Service monitor DB to store VM, SI information
"""
import inspect

from cfgm_common import jsonutils as json
from cfgm_common.vnc_cassandra import VncCassandraClient
from sandesh_common.vns.constants import SVC_MONITOR_KEYSPACE_NAME

class ServiceMonitorDB(VncCassandraClient):

    _KEYSPACE = SVC_MONITOR_KEYSPACE_NAME
    _SVC_SI_CF = 'service_instance_table'
    _LB_CF = 'pool_table'

    def __init__(self, args, logger):
        self._db_logger = logger

        keyspaces = {
            self._KEYSPACE: {
                self._SVC_SI_CF: {},
                self._LB_CF: {}
            }
        }

        cred = None
        if (args.cassandra_user is not None and
            args.cassandra_password is not None):
            cred={'username':args.cassandra_user,
                  'password':args.cassandra_password}

        super(ServiceMonitorDB, self).__init__(args.cassandra_server_list,
                                               args.cluster_id,
                                               keyspaces,
                                               None,
                                               self._db_logger.log,
                                               reset_config=args.reset_config,
                                               credential=cred)

        self._svc_si_cf = self._cf_dict[self._SVC_SI_CF]
        self._lb_cf = self._cf_dict[self._LB_CF]

    # db CRUD
    def _db_get(self, table, key, column):
        try:
            entry = self.get_one_col(table.column_family, key, column)
        except Exception:
            # TODO(ethuleau): VncError is raised if more than one row was
            #                 fetched from db with get_one_col method.
            #                 Probably need to be cleaned
            self._db_logger.log("DB: %s %s get failed" %
                             (inspect.stack()[1][3], key))
            return None

        return entry

    def _db_insert(self, table, key, entry):
        try:
            table.insert(key, entry)
        except Exception:
            self._db_logger.log("DB: %s %s insert failed" %
                             (inspect.stack()[1][3], key))
            return False

        return True

    def _db_remove(self, table, key, columns=None):
        try:
            if columns:
                table.remove(key, columns=columns)
            else:
                table.remove(key)
        except Exception:
            self._db_logger.log("DB: %s %s remove failed" %
                             (inspect.stack()[1][3], key))
            return False

        return True

    def _db_list(self, table):
        try:
            entries = list(table.get_range())
        except Exception:
            self._db_logger.log("DB: %s list failed" %
                             (inspect.stack()[1][3]))
            return None

        return entries

    def get_vm_db_prefix(self, inst_count):
        return('vm' + str(inst_count) + '-')

    def remove_vm_info(self, si_fq_str, vm_uuid):
        si_info = self.service_instance_get(si_fq_str)
        if not si_info:
            return

        prefix = None
        for key, item in si_info.items():
            if item == vm_uuid:
                prefix = key.split('-')[0]
                break
        if not prefix:
            return

        vm_column_list = []
        for key in si_info.keys():
            if key.startswith(prefix):
                vm_column_list.append(key)
        self.service_instance_remove(si_fq_str, vm_column_list)

    # service instance CRUD
    def service_instance_get(self, si_fq_str):
        return self._db_get(self._svc_si_cf, si_fq_str)

    def service_instance_insert(self, si_fq_str, entry):
        return self._db_insert(self._svc_si_cf, si_fq_str, entry)

    def service_instance_remove(self, si_fq_str, columns=None):
        return self._db_remove(self._svc_si_cf, si_fq_str, columns)

    def service_instance_list(self):
        return self._db_list(self._svc_si_cf)

    def pool_config_get(self, pool_id):
        return self._db_get(self._lb_cf, pool_id, 'config_info')

    def pool_driver_info_get(self, pool_id):
        return self._db_get(self._lb_cf, pool_id, 'driver_info')

    def pool_config_insert(self, pool_id, pool_obj):
        entry = json.dumps(pool_obj)
        return self._db_insert(self._lb_cf, pool_id, {'config_info': entry})

    def pool_driver_info_insert(self, pool_id, pool_obj):
        entry = json.dumps(pool_obj)
        return self._db_insert(self._lb_cf, pool_id, {'driver_info': entry})

    def pool_remove(self, pool_id, columns=None):
        return self._db_remove(self._lb_cf, pool_id, columns)

    def pool_list(self):
        ret_list = []
        for each_entry_id, each_entry_data in self._db_list(self._lb_cf) or []:
            config_info_obj_dict = json.loads(each_entry_data['config_info'])
            driver_info_obj_dict = None
            if 'driver_info' in each_entry_data:
                driver_info_obj_dict = json.loads(each_entry_data['driver_info'])
            ret_list.append((each_entry_id, config_info_obj_dict, driver_info_obj_dict))
        return ret_list
