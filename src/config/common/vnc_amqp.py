import socket
import cStringIO
from pprint import pformat

from cfgm_commont.utils import cgitb_hook
from cfgm_common.exceptions import NoIdError
from cfgm_common.vnc_kombu import VncKombuClient
from cfgm_common.dependency_tracker import DependencyTracker

from schema_transformer.config_db import DBBaseST, VirtualNetworkST
from schema_transformer.sandesh.traces.ttypes import MessageBusNotifyTrace,\
        DependencyTrackerResource


class VncAmqpHandle(object):

    def __init__(self, logger, db_cls, reaction_map, q_name_prefix,
                 db_resync_done, msg_tracer=None, args=None):
        self.logger = logger
        self.db_cls = db_cls
        self.reaction_map = reaction_map
        self.q_name_prefix = q_name_prefix
        self.db_resync_done = db_resync_done
        self.msg_tracer = msg_tracer
        self._args = args

    def establish(self):
        q_name = '.'.join([self._prefix, socket.gethostname()])
        self._vnc_kombu = VncKombuClient(
                self._args.rabbit_server, self._args.rabbit_port,
                self._args.rabbit_user, self._args.rabbit_password,
                self._args.rabbit_vhost, self._args.rabbit_ha_mode,
                q_name, self._vnc_subscribe_callback,
                self.logger.log, rabbit_use_ssl=self._args.rabbit_use_ssl,
                kombu_ssl_version=self._args.kombu_ssl_version,
                kombu_ssl_keyfile=self._args.kombu_ssl_keyfile,
                kombu_ssl_certfile=self._args.kombu_ssl_certfile,
                kombu_ssl_ca_certs=self._args.kombu_ssl_ca_certs)

    def _vnc_subscribe_callback(self, oper_info):
        self.db_resync_done.wait()
        try:
            self.oper_info = oper_info
            self.vnc_subscribe_actions()

        except Exception:
            string_buf = cStringIO.StringIO()
            cgitb_hook(file=string_buf, format="text")
            self.logger.error(string_buf.getvalue())

            if self.msg_tracer:
                self.msg_tracer.error = string_buf.getvalue()
            try:
                with open(self._args.trace_file, 'a') as err_file:
                    err_file.write(string_buf.getvalue())
            except IOError:
                pass
        finally:
            try:
                if self.msg_tracer:
                    self.msg_tracer.trace_msg(name='MessageBusNotifyTraceBuf',
                                              sandesh=self.logger._sandesh)
            except Exception:
                pass
            del self.oper_info
            del self.obj_type
            del self.obj_class
            del self.obj
            del self.dependency_tracker

    def vnc_subscribe_actions(self):
        msg = "Notification Message: %s" % (pformat(self.oper_info))
        self.logger.debug(msg)
        self.obj_type = self.oper_info['type'].replace('-', '_')
        self.obj_class = self.db_cls.get_obj_type_map().get(self.obj_type)
        if self.obj_class is None:
            return

        self.obj = None
        self.dependency_tracker = None
        oper = self.oper_info['oper']
        obj_id = self.oper_info['uuid']
        if self.msg_tracer:
            self.msg_tracer = MessageBusNotifyTrace(
                    request_id=self.oper_info.get('request_id'),
                    operation=oper, uuid=obj_id)
        if oper == 'CREATE':
            self.handle_create()
        elif oper == 'UPDATE':
            self.handle_update()
        elif oper == 'DELETE':
            self.handle_delete()
        else:
            self.handle_unknown()
            return
        if self.obj is None:
            self.logger.error('Error while accessing %s uuid %s' % (
                self.obj_type, obj_id))
            return
        self.evaluate_dependency()

    def get_index_to_locate(self):
        obj_dict = self.oper_info['obj_dict']
        if self.db_cls._indexed_by_name:
            obj_key = ':'.join(obj_dict['fq_name'])
        else:
            obj_key = self.oper_info['uuid']
        return obj_key

    def handle_create(self):
        obj_dict = self.oper_info['obj_dict']
        obj_key = self.get_index_to_locate()
        obj_id = self.oper_info['uuid']
        obj_fq_name = obj_dict['fq_name']
        self.db_cls._cassandra.cache_uuid_to_fq_name_add(
                obj_id, obj_fq_name, self.obj_type)
        self.obj = self.obj_class.locate(obj_key)
        if self.obj is None:
            self.logger.info('%s id %s fq_name %s not found' % (
                self.obj_type, obj_id, obj_fq_name))
            return
        self.dependency_tracker = DependencyTracker(
            self.db_cls.get_obj_type_map(), self.reaction_map)
        self.dependency_tracker.evaluate(self.obj_type, self.obj)

    def get_obj(self):
        obj_id = self.oper_info['uuid']
        if self.db_cls._indexed_by_name:
            self.obj = self.obj_class.get_by_uuid(obj_id)
        else:
            self.obj = self.obj_class.get(obj_id)

    def handle_update(self):
        self.obj = self.get_obj()
        old_dt = None
        if self.obj is not None:
            old_dt = DependencyTracker(
                self.db_cls.get_obj_type_map(), self.reaction_map)
            old_dt.evaluate(self.obj_type, self.obj)
        else:
            obj_id = self.oper_info['uuid']
            if self.db_cls._indexed_by_name:
                self.logger.info('%s id %s not found' % (self.obj_type,
                                                         obj_id))
                return
            else:
                # Create it
                self.obj = self.obj_class.locate(obj_id)

        if self.obj is not None:
            try:
                self.obj.update()
            except NoIdError:
                obj_id = self.oper_info['uuid']
                self.logger.warning('%s uuid %s update caused NoIdError' %
                                    (self.obj_type, obj_id))
                return

        self.dependency_tracker = DependencyTracker(
                self.db_cls.get_obj_type_map(), self.reaction_map)
        self.dependency_tracker.evaluate(self.obj_type, self.obj)
        if old_dt:
            for resource, ids in old_dt.resources.items():
                if resource not in self.dependency_tracker.resources:
                    self.dependency_tracker.resources[resource] = ids
                else:
                    self.dependency_tracker.resources[resource] = list(
                        set(self.dependency_tracker.resources[resource]) |
                        set(ids))

    def handle_delete(self):
        self.obj = self.get_obj()
        if self.obj is None:
            return
        self.dependency_tracker = DependencyTracker(
            self.db_cls.get_obj_type_map(), self.reaction_map)
        self.dependency_tracker.evaluate(self.obj_type, self.obj)
        obj_key = self.get_index_to_locate()
        self.obj_class.delete(obj_key)

    def handle_unknown(self):
        # unknown operation
        self.logger.error('Unknown operation %s' % self.oper_info['oper'])

    def evaluate_dependency(self):
        if not self.dependency_tracker:
            return

        if self.msg_tracer:
            self.msg_tracer.fq_name = self.obj.name
            self.msg_tracer.dependency_tracker_resources = []

        for res_type, res_id_list in self.dependency_tracker.resources.items():
            if not res_id_list:
                continue
            if self.msg_tracer:
                dtr = DependencyTrackerResource(obj_type=res_type,
                                                obj_keys=res_id_list)
                self.msg_tracer.dependency_tracker_resources.append(dtr)
            cls = self.db_cls.get_obj_type_map().get(res_type)
            if cls is None:
                continue
            for res_id in res_id_list:
                res_obj = cls.get(res_id)
                if res_obj is not None:
                    res_obj.evaluate()

        # Only in case of schema tranformer
        if issubclass(self.db_class, DBBaseST):
            for vn_id in self.dependency_tracker.resources.get(
                    'virtual_network', []):
                vn = VirtualNetworkST.get(vn_id)
                if vn is not None:
                    vn.uve_send()

    def close(self):
        self._vnc_kombu.shutdown()
