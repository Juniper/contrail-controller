from cfgm_common.vnc_cassandra import VncCassandraClient
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
import logging


class ICCassandraInfo():
    def __init__(self, addr_info, db_prefix,
                 issu_info, keyspace_info, logger):
        self.addr_info = addr_info
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
                 odb_prefix, ndb_prefix, issu_info, logger):
        self._oldversion_server_list = oldversion_server_list
        self._newversion_server_list = newversion_server_list
        self._odb_prefix = odb_prefix
        self._ndb_prefix = ndb_prefix
        self._issu_info = issu_info
        self._logger = logger
        self._ks_issu_func_info = {}
        self._nkeyspaces = {}
        self._okeyspaces = {}
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
            self._oldversion_server_list, self._odb_prefix,
            None, self._okeyspaces, self._logger)

        self._newversion_handle = VncCassandraClient(
            self._newversion_server_list, self._ndb_prefix,
            self._nkeyspaces, None, self._logger)
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
                try:
                    newversion_result = self._newversion_handle.get_range(cf)
                    self._logger(
                        "Building New DB memory for columnfamily: " + str(cf),
                        level=SandeshLevel.SYS_INFO)
                    new_db = dict(newversion_result)
                except Exception:
                    self._logger(str(Exception), level=SandeshLevel.SYS_INFO)

                oldversion_result = self._oldversion_handle.get_range(cf)
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
                    y = self._newversion_handle.get_cf(cf).remove(rows, diff)
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
                oldversion_result = self._oldversion_handle.get_range(cf)

                for rows, columns in oldversion_result:
                    out = issu_funct(ks, cf, columns)

                    x = self._newversion_handle.add(cf, rows, out)
                    # TBD If failure to add, fail ISSU
    # end

    def issu_sync_row(self, msg, cf):
        if msg['oper'] == "CREATE":
            self._logger(msg, level=SandeshLevel.SYS_INFO)
            self._newversion_handle.object_create(
                msg['type'], msg['uuid'], msg['obj_dict'])
        elif msg['oper'] == "UPDATE":
            self._logger(msg, level=SandeshLevel.SYS_INFO)
            uuid_list = []
            uuid_list.append(msg['uuid'])
            bool1, current = self._newversion_handle.object_read(
                msg['type'], uuid_list)
            bool2, new = self._oldversion_handle.object_read(
                msg['type'], uuid_list)
            updated = self._merge_overwrite(
                dict(new.pop()), dict(current.pop()))
            #  New object dictionary should be created, for now passing as is
            self._newversion_handle.object_update(
                msg['type'], msg['uuid'], updated)
        elif msg['oper'] == "DELETE":
            self._logger(msg, level=SandeshLevel.SYS_INFO)
            self._newversion_handle.object_delete(msg['type'], msg['uuid'])
        return
    # end
