#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import etcd3


class VncEtcdClient(object):
    def __init__(self, host, port, prefix, logger,
                 credential, ssl_enabled, ca_certs):
        self._host = host
        self._port = port
        self._prefix = prefix
        self._logger = logger
        self._credential = credential
        self._ssl_enabled = ssl_enabled
        self._ca_certs = ca_certs

        self._client = self._new_client()

    def __getattr__(self, name):
        return getattr(self._client, name)

    def _new_client(self):
        kwargs = {'host': self._host, 'port': self._port}

        if self._credential:
            kwargs['user'] = self._credential['username']
            kwargs['password'] = self._credential['password']

        if self._ssl_enabled:
            kwargs['ca_cert'] = self._credential['ca_cert']
            kwargs['cert_key'] = self._credential['ca_key']

        return etcd3.Etcd3Client(**kwargs)

    def add(self, cf_name, key, value):
        pass  # TODO: implement 3

    def get(self, cf_name, key, columns=None, start='', finish=''):
        pass  # TODO: implement 4

    def delete(self, cf_name, key, columns=None):
        pass  # TODO: implement 6

    # TODO: handle_exception decorator

    def object_create(self, obj_type, obj_id, obj_dict,
                      uuid_batch=None, fqname_batch=None):
        pass  # TODO: implement 9

    def object_read(self, obj_type, obj_uuids, field_names=None,
                    ret_readonly=False):
        pass  # TODO: implement 11

    def object_update(self, obj_type, obj_uuid, new_obj_dict, uuid_batch=None):
        pass  # TODO: implement 15

    def object_list(self, obj_type, parent_uuids=None, back_ref_uuids=None,
                    obj_uuids=None, count=False, filters=None,
                    paginate_start=None, paginate_count=None):
        pass  # TODO: implement 16

    def object_delete(self, obj_type, obj_uuid):
        pass  # TODO: implement 17

    def fq_name_to_uuid(self, obj_type, fq_name):
        pass  # TODO: implement 23

    def walk(self, fn=None):
        pass  # TODO: implement 26
