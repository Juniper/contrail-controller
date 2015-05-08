#    Copyright
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.

import json
import uuid as UUID

from cfgm_common import exceptions as vnc_exc
from vnc_api import vnc_api


class MockVnc(object):
    def __init__(self):
        self.resources_collection = dict()
        self._kv_dict = dict()

    def _break_method(self, method):
        rin = method.rindex('_')
        return (method[:rin], method[rin+1:])

    class Callables(object):
        def __init__(self, resource_type, resource,
                     resource_collection, server_conn):
            self._resource_type = resource_type.replace('_', '-')
            self._resource = resource
            self._resource_collection = resource_collection
            self._server_conn = server_conn

        def delete_back_refs(self, ref_name, ref_uuid, back_ref_name,
                             back_ref_uuid):
            _ref_name = ref_name
            if (_ref_name not in self._resource_collection or
                    ref_uuid not in self._resource_collection[_ref_name]):
                # TODO(anbu): Implement if needed
                print(" -- Unable to locate %s resource with uuid %s" % (
                    _ref_name, ref_uuid))
            else:
                ref_obj = self._resource_collection[_ref_name][ref_uuid]
                back_ref = getattr(ref_obj, back_ref_name)
                for index, value in enumerate(back_ref):
                    if value['uuid'] == back_ref_uuid:
                        back_ref.pop(index)
                        break

        def update_back_ref(self, ref_name, refs,
                            back_ref_name, back_ref_obj):
            _ref_name = ref_name[:-5]
            for ref in refs:
                if 'uuid' not in ref:
                    ref['uuid'] = str(UUID.uuid4())
                ref_uuid = ref['uuid']
                if (_ref_name not in self._resource_collection or
                        ref_uuid not in
                        self._resource_collection[_ref_name]):
                    # TODO(anbu): Implement if needed
                    msg = (" -- Unable to locate %s resource with uuid %s"
                           % (_ref_name, ref_uuid))
                    print(msg)
                else:
                    ref_obj = (
                        self._resource_collection[_ref_name][ref_uuid])
                    back_ref = {'uuid': back_ref_obj.uuid,
                                'to': back_ref_obj.get_fq_name()}
                    back_ref_name = ("%s_back_refs"
                                     % back_ref_name.replace("-", "_"))
                    if hasattr(ref_obj, back_ref_name) and (
                            getattr(ref_obj, back_ref_name)):
                        getattr(ref_obj, back_ref_name).append(back_ref)
                    else:
                        setattr(ref_obj, back_ref_name, [back_ref])

    class ReadCallables(Callables):
        def __call__(self, **kwargs):
            if 'id' in kwargs:
                if kwargs['id'] in self._resource:
                    return self._resource[kwargs['id']]
            if ('fq_name_str' in kwargs or (
                    'fq_name' in kwargs and kwargs['fq_name'])):
                fq_name_str = (kwargs['fq_name_str']
                               if 'fq_name_str' in kwargs else
                               ':'.join(kwargs['fq_name']))
                if fq_name_str in self._resource:
                    return self._resource[fq_name_str]

            # Not found yet
            raise vnc_exc.NoIdError(
                kwargs['id'] if 'id' in kwargs else None)

    class ListCallables(Callables):
        def __call__(self, parent_id=None, parent_fq_name=None,
                     back_ref_id=None, obj_uuids=None, fields=None,
                     detail=False, count=False):
            ret = []
            ret_resource_name = None
            if parent_fq_name:
                for res in set(self._resource.values()):
                    if set(res.get_parent_fq_name()) == set(parent_fq_name):
                        ret.append(res)
            elif obj_uuids:
                for res in set(self._resource.values()):
                    if res.uuid in obj_uuids:
                        ret.append(res)
            elif parent_id:
                for res in set(self._resource.values()):
                    if isinstance(parent_id, list):
                        if res.parent_uuid in parent_id:
                            ret.append(res)
                    elif res.parent_uuid == parent_id:
                        ret.append(res)
            elif back_ref_id:
                for res in set(self._resource.values()):
                    back_ref_fields = getattr(res, 'back_ref_fields', [])
                    ref_fields = getattr(res, 'ref_fields', [])
                    back_ref_fields.extend(ref_fields)
                    for field in back_ref_fields:
                        back_ref_field = getattr(res, field, [])
                        for f in back_ref_field:
                            if f['uuid'] in back_ref_id:
                                ret.append(res)

                            if field == 'project_refs':
                                if f['uuid'].replace('-', '') in back_ref_id:
                                    ret.append(res)
            else:
                for res in set(self._resource.values()):
                    ret.append(res)

            ret_resource_name = self._resource_type + 's'

            if count:
                return {ret_resource_name: {"count": len(ret)}}

            if not detail:
                sret = []
                for res in ret:
                    sret.append(res.serialize_to_json())
                return {ret_resource_name: sret}
            return ret

    class CreateCallables(Callables):
        def __call__(self, obj):
            if not obj:
                raise ValueError("Create called with null object")
            uuid = getattr(obj, 'uuid', None)
            obj._server_conn = self._server_conn
            if not uuid:
                uuid = obj.uuid = str(UUID.uuid4())

            if hasattr(obj, 'parent_type'):
                rc = MockVnc.ReadCallables(
                    obj.parent_type,
                    self._resource_collection[
                        obj.parent_type.replace("-", "_")],
                    self._resource_collection,
                    self._server_conn)
                parent = rc(fq_name=obj.fq_name[:-1])
                obj.parent_uuid = parent.uuid

            fq_name_str = getattr(obj, 'fq_name_str', None)
            if not fq_name_str:
                fq_name_str = ":".join(obj.get_fq_name())

            self._resource[uuid] = obj

            for field in obj._pending_field_updates:
                if field.endswith("_refs"):
                    for r in getattr(obj, field):
                        setattr(obj, "processed_" + field,
                                list(getattr(obj, field)))
                        self.update_back_ref(field, getattr(obj, field),
                                             self._resource_type, obj)

            self._pending_ref_updates = self._pending_field_updates = set([])

            if fq_name_str and fq_name_str != uuid:
                if fq_name_str in self._resource:
                    raise vnc_exc.RefsExistError(
                        "%s fq_name already exists, please use "
                        "a different name" % fq_name_str)

                self._resource[fq_name_str] = obj

            if self._resource_type == 'virtual-machine-interface':
                # generate a dummy mac address
                def random_mac():
                    import random
                    mac = [0x00, 0x00, 0x00]
                    for i in range(3, 6):
                        mac.append(random.randint(0x00, 0x7f))

                    return ":".join(map(lambda x: "%02x" % x, mac))
                if not obj.get_virtual_machine_interface_mac_addresses():
                    obj.set_virtual_machine_interface_mac_addresses(
                        vnc_api.MacAddressesType([random_mac()]))
            elif self._resource_type == "instance-ip":
                if not obj.get_instance_ip_address():
                    obj.set_instance_ip_address('1.1.1.1')
            elif self._resource_type == 'security-group':
                if not obj.get_id_perms():
                    obj.set_id_perms(vnc_api.IdPermsType(enable=True))
                proj_obj = self._resource_collection['project'][
                    obj.parent_uuid]
                sgs = getattr(proj_obj, 'security_groups', None)
                sg_ref = {'to': obj.get_fq_name(), 'uuid': obj.uuid}
                if not sgs:
                    setattr(proj_obj, 'security_groups', [sg_ref])
                else:
                    sgs.append(sg_ref)
            return uuid

    class UpdateCallables(Callables):
        def __call__(self, obj):
            if obj.uuid:
                cur_obj = self._resource[obj.uuid]
            else:
                cur_obj = self._resource[':'.join(obj.get_fq_name())]

            if obj._pending_ref_updates:
                for ref in obj._pending_ref_updates:
                    if ref.endswith("_refs"):
                        proc_refs = getattr(cur_obj, "processed_" + ref, [])
                        obtained_refs = getattr(cur_obj, ref)
                        if len(obtained_refs) > len(proc_refs):
                            self.update_back_ref(ref, getattr(obj, ref),
                                                 self._resource_type, cur_obj)
                        elif len(obtained_refs) < len(proc_refs):
                            proc_uuids = [x['uuid'] for x in proc_refs]
                            obtained_uuids = [x['uuid']
                                              for x in obtained_refs]
                            back_ref_name = (
                                self._resource_type.replace("-", "_") +
                                "_back_refs")
                            ref_name = ref[:-5]
                            for i in set(proc_uuids) - set(obtained_uuids):
                                self.delete_back_refs(ref_name, i,
                                                      back_ref_name, obj.uuid)
                        setattr(cur_obj, "processed_" + ref,
                                list(getattr(cur_obj, ref)))

            if obj._pending_field_updates:
                for ref in obj._pending_field_updates:
                    if ref.endswith("_refs"):
                        setattr(obj, "processed_" + ref,
                                list(getattr(cur_obj, ref)))
                        self.update_back_ref(ref, getattr(cur_obj, ref),
                                             self._resource_type, cur_obj)

    class DeleteCallables(Callables):
        def __call__(self, **kwargs):
            obj = None
            if 'fq_name' in kwargs and kwargs['fq_name']:
                fq_name_str = ':'.join(kwargs['fq_name'])
                obj = self._resource[fq_name_str]

            if 'id' in kwargs and kwargs['id'] in self._resource:
                obj = self._resource[kwargs['id']]

            if not obj:
                raise vnc_exc.NoIdError(
                    kwargs['id'] if 'id' in kwargs else None)

            self._resource.pop(obj.uuid)
            self._resource.pop(':'.join(obj.get_fq_name()), None)

            # remove all the back refs
            for ref in obj.ref_fields:
                back_ref_name = (self._resource_type.replace("-", "_") +
                                 "_back_refs")
                ref_name = ref[:-5]
                if not hasattr(obj, ref):
                    continue
                ref_value = getattr(obj, ref)
                for r in ref_value:
                    self.delete_back_refs(ref_name, r['uuid'],
                                          back_ref_name,
                                          obj.uuid)

    def __getattr__(self, method):
        (resource, action) = self._break_method(method)
        if action not in ['list', 'read', 'create',
                          'update', 'delete']:
            raise ValueError("Unknown action %s received for %s method" %
                             (action, method))

        if action == 'list':
            # for 'list' action resource will be like resourceS
            resource = resource[:-1]
        callables_map = {'list': MockVnc.ListCallables,
                         'read': MockVnc.ReadCallables,
                         'create': MockVnc.CreateCallables,
                         'update': MockVnc.UpdateCallables,
                         'delete': MockVnc.DeleteCallables}

        if resource not in self.resources_collection:
            self.resources_collection[resource] = dict()

        return callables_map[action](
            resource, self.resources_collection[resource],
            self.resources_collection, self)

    def _obj_serializer_all(self, obj):
        if hasattr(obj, 'serialize_to_json'):
            return obj.serialize_to_json()
        else:
            return dict((k, v) for k, v in obj.__dict__.iteritems())

    def obj_to_json(self, obj):
        return json.dumps(obj, default=self._obj_serializer_all)

    def obj_to_dict(self, obj):
        return json.loads(self.obj_to_json(obj))

    def obj_to_id(self, obj):
        if obj.uuid:
            return obj.uuid
        else:
            return "%031d" % 0

    def kv_store(self, key, value):
        self._kv_dict[key] = value

    def kv_retrieve(self, key):
        try:
            return self._kv_dict[key]
        except KeyError:
            raise vnc_exc.NoIdError(key)

    def kv_delete(self, key):
        return self._kv_dict.pop(key, None)

    def fq_name_to_id(self, resource, fq_name):
        res = resource.replace("-", "_")
        fq_name_str = ":".join(fq_name)
        obj = self.resources_collection[res].get(fq_name_str, None)
        return obj.uuid if obj else None
