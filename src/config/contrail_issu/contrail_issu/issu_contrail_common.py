#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common.vnc_cassandra import VncCassandraClient
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
import logging


class ICCassandraInfo():
    def __init__(self, addr_info, user, password, use_ssl, ca_certs,
                 db_prefix, issu_info, keyspace_info, logger):
        self.addr_info = addr_info
        self.user = user
        self.password = password
        self.use_ssl = use_ssl
        self.ca_certs = ca_certs
        self.db_prefix = db_prefix
        self.issu_info = issu_info
        self.keyspace_info = keyspace_info
        self.logger = logger
    # end


class ICCassandraClient():

    def _issu_basic_function(self, kspace, cfam, cols):
        return dict(cols)
    # end

    def __init__(self, oldversion_server_list, newversion_server_list,
                 old_user, old_password, new_user, new_password,
                 odb_use_ssl, odb_ca_certs, ndb_use_ssl, ndb_ca_certs,
                 odb_prefix, ndb_prefix,
                 issu_info, logger):
        self._oldversion_server_list = oldversion_server_list
        self._newversion_server_list = newversion_server_list
        self._odb_prefix = odb_prefix
        self._ndb_prefix = ndb_prefix
        self._odb_use_ssl = odb_use_ssl
        self._odb_ca_certs = odb_ca_certs
        self._ndb_use_ssl = ndb_use_ssl
        self._ndb_ca_certs = ndb_ca_certs
        self._issu_info = issu_info
        self._logger = logger
        self._ks_issu_func_info = {}
        self._nkeyspaces = {}
        self._okeyspaces = {}

        self._old_creds = None
        if old_user and old_password:
            self._old_creds = {
                'username': old_user,
                'password': old_password,
            }
        self._new_creds = None
        if new_user and new_password:
            self._new_creds = {
                'username': new_user,
                'password': new_password,
            }

        self._logger(
            "Issu contrail cassandra initialized...",
            level=SandeshLevel.SYS_INFO,
        )
        self.issu_prepare()
    # end

    def issu_prepare(self):
        self._logger(
            "Issu contrail cassandra prepare...",
            level=SandeshLevel.SYS_INFO,
        )
        for issu_func, ks, cflist in self._issu_info:

            if issu_func is None:
                issu_func = self._issu_basic_function
            ks_issu_func_info = {ks: issu_func}

            nks = {ks: cflist}
            oks = {ks: cflist}
            self._nkeyspaces.update(nks)
            self._okeyspaces.update(oks)
            self._ks_issu_func_info.update(ks_issu_func_info)

        self._oldversion_handle = VncCassandraClient(
            self._oldversion_server_list, db_prefix=self._odb_prefix,
            ro_keyspaces=self._okeyspaces, logger=self._logger,
            credential=self._old_creds,
            ssl_enabled=self._odb_use_ssl,
            ca_certs=self._odb_ca_certs)

        self._newversion_handle = VncCassandraClient(
            self._newversion_server_list, db_prefix=self._ndb_prefix,
            rw_keyspaces=self._nkeyspaces, logger=self._logger,
            credential=self._new_creds,
            ssl_enabled=self._ndb_use_ssl,
            ca_certs=self._ndb_ca_certs)
    # end

    def _fetch_issu_func(self, ks):
        return self._ks_issu_func_info[ks]
    # end

    # Overwrite what is seen in the newer with the old version.
    def _merge_overwrite(self, new, current):
        updated = current
        updated.update(new)
        return updated
    # end

    #  For now this function should be called for only config_db_uuid.
    #  If we separate out config_db_uuid keyspace from VncCassandraClient,
    #  then we don't need to pass keyspaces here.
    def issu_merge_copy(self, keyspaces):
        for ks, cflist in keyspaces.items():
            self._logger(
                "Issu contrail cassandra merge copy, keyspace: " +
                str(ks), level=SandeshLevel.SYS_INFO)
            issu_funct = self._fetch_issu_func(ks)
            for cf in cflist:
                newList = []
                newversion_result = (self._newversion_handle._cassandra_driver.
                                        get_range(cf) or {})
                self._logger(
                        "Building New DB memory for columnfamily: " + str(cf),
                        level=SandeshLevel.SYS_INFO)
                new_db = dict(newversion_result)

                oldversion_result = (self._oldversion_handle._cassandra_driver.
                                        get_range(cf) or {})
                self._logger(
                    "Doing ISSU copy for columnfamily: " + str(cf),
                    level=SandeshLevel.SYS_INFO)
                for rows, columns in oldversion_result:
                    out = issu_funct(ks, cf, columns)
                    current = new_db.pop(rows, None)
                    if current is not None:
                        updated = self._merge_overwrite(out, dict(current))
                        x = self._newversion_handle.add(cf, rows, updated)
                    else:
                        updated = []
                        x = self._newversion_handle.add(cf, rows, out)
                    diff = set(updated) - set(out)
                    y = (self._newversion_handle._cassandra_driver.
                            get_cf(cf).remove(rows, diff))
                self._logger(
                    "Pruning New DB if entires don't exist in old DB column "
                    "family: " + str(cf), level=SandeshLevel.SYS_INFO)
                for item in new_db:
                    # TBD should be catch exception and fail ISSU
                    self._newversion_handle.delete(cf, item)
    # end

    #  This is issu_copy function.
    def issu_copy(self, keyspaces):
        for ks, cflist in keyspaces.items():
            issu_funct = self._fetch_issu_func(ks)
            for cf in cflist:
                self._logger(
                    "Issu Copy KeySpace: " + str(ks) +
                    " Column Family: " + str(cf), level=SandeshLevel.SYS_INFO)
                oldversion_result = (self._oldversion_handle._cassandra_driver.
                                        get_range(cf) or {})

                for rows, columns in oldversion_result:
                    out = issu_funct(ks, cf, columns)

                    x = self._newversion_handle.add(cf, rows, out)
                    # TBD If failure to add, fail ISSU
    # end

    def issu_read_row(self, msg):
        try:
            (ok, result) = self._newversion_handle.object_read(
                msg['type'], [msg['uuid']], field_names=['fq_name'])
        except Exception as e:
            self._logger(str(e), level=SandeshLevel.SYS_ERR)
            return {}
        return result[0]

    def issu_sync_row(self, msg, cf):
        if msg['oper'] == "CREATE":
            self._logger(msg, level=SandeshLevel.SYS_INFO)
            try:
                self._newversion_handle.object_create(
                    msg['type'], msg['uuid'], msg['obj_dict'])
            except Exception as e:
                self._logger(str(e), level=SandeshLevel.SYS_ERR)

        elif msg['oper'] == "UPDATE":
            self._logger(msg, level=SandeshLevel.SYS_INFO)
            uuid_list = []
            uuid_list.append(msg['uuid'])
            try:
                bool1, current = self._newversion_handle.object_read(
                    msg['type'], uuid_list)
                bool2, new = self._oldversion_handle.object_read(
                    msg['type'], uuid_list)
            except Exception as e:
                self._logger(str(e), level=SandeshLevel.SYS_ERR)
                return
            updated = self._merge_overwrite(
                dict(new.pop()), dict(current.pop()))
            #  New object dictionary should be created, for now passing as is
            try:
                self._newversion_handle.object_update(
                    msg['type'], msg['uuid'], updated)
            except Exception as e:
                self._logger(str(e), level=SandeshLevel.SYS_ERR)

        elif msg['oper'] == "DELETE":
            self._logger(msg, level=SandeshLevel.SYS_INFO)
            try:
                self._newversion_handle.object_delete(
                    msg['type'], msg['uuid'])
            except Exception as e:
                self._logger(str(e), level=SandeshLevel.SYS_ERR)
        return
    # end
