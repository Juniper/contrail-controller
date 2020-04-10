from __future__ import unicode_literals
from future import standard_library
standard_library.install_aliases()
from builtins import object
import socket
import gevent
from six import StringIO
from pprint import pformat
from requests.exceptions import ConnectionError

from cfgm_common.utils import cgitb_hook
from cfgm_common.exceptions import NoIdError
from cfgm_common.vnc_kombu import VncKombuClient
from cfgm_common.dependency_tracker import DependencyTracker
from cfgm_common.uve.msg_traces.ttypes import MessageBusNotifyTrace,\
                        DependencyTrackerResource


class VncAmqpHandle(object):

    def __init__(self, sandesh, logger, db_cls, reaction_map, q_name_prefix,
                 rabbitmq_cfg, host_ip, trace_file=None, timer_obj=None,
                 register_handler=True):
        self.sandesh = sandesh
        self.logger = logger
        self.db_cls = db_cls
        self.reaction_map = reaction_map
        self.q_name_prefix = q_name_prefix
        self._db_resync_done = gevent.event.Event()
        self._rabbitmq_cfg = rabbitmq_cfg
        self._trace_file = trace_file
        self.timer = timer_obj
        self.host_ip = host_ip
        self.register_handler = register_handler

    def establish(self):
        q_name = '.'.join([self.q_name_prefix, socket.getfqdn(self.host_ip)])
        self._vnc_kombu = VncKombuClient(
                self._rabbitmq_cfg['servers'], self._rabbitmq_cfg['port'],
                self._rabbitmq_cfg['user'], self._rabbitmq_cfg['password'],
                self._rabbitmq_cfg['vhost'], self._rabbitmq_cfg['ha_mode'],
                q_name, self._vnc_subscribe_callback,
                self.logger.log, rabbit_use_ssl=self._rabbitmq_cfg['use_ssl'],
                kombu_ssl_version=self._rabbitmq_cfg['ssl_version'],
                kombu_ssl_keyfile=self._rabbitmq_cfg['ssl_keyfile'],
                kombu_ssl_certfile=self._rabbitmq_cfg['ssl_certfile'],
                kombu_ssl_ca_certs=self._rabbitmq_cfg['ssl_ca_certs'],
                register_handler=self.register_handler)

    def msgbus_store_err_msg(self, msg):
        self.msg_tracer.error = msg

    def msgbus_trace_msg(self):
        self.msg_tracer.trace_msg(name='MessageBusNotifyTraceBuf',
                                  sandesh=self.sandesh)

    def log_exception(self):
        string_buf = StringIO()
        cgitb_hook(file=string_buf, format="text")
        self.logger.error(string_buf.getvalue())
        self.msgbus_store_err_msg(string_buf.getvalue())
        if not self._trace_file:
            return
        try:
            with open(self._trace_file, 'a') as err_file:
                err_file.write(string_buf.getvalue())
        except IOError:
            pass

    def _vnc_subscribe_callback(self, oper_info):
        self._db_resync_done.wait()
        try:
            self.oper_info = oper_info
            self.vnc_subscribe_actions()

        except ConnectionError:
            try:
                # retry write during api-server ConnectionError
                self.vnc_subscribe_actions()
            except ConnectionError:
                # log the exception, and exit during api-server
                # ConnectionError on retry to let standby to become active.
                self.log_exception()
                self.logger.error("Api-server connection lost. Exiting")
                self.close()
                raise SystemExit
            except Exception:
                self.log_exception()
        except Exception:
            self.log_exception()
        finally:
            try:
                self.msgbus_trace_msg()
            except Exception:
                pass
            del self.oper_info
            del self.obj_type
            del self.obj_class
            del self.obj
            del self.dependency_tracker

    def create_msgbus_trace(self, request_id, oper, uuid):
        self.msg_tracer = MessageBusNotifyTrace(request_id=request_id,
                                                operation=oper, uuid=uuid)

    def vnc_subscribe_actions(self):
        msg = "Notification Message: %s" % (pformat(self.oper_info))
        self.logger.debug(msg)

        self.obj = None
        self.dependency_tracker = None
        self.obj_type = self.oper_info['type'].replace('-', '_')
        self.obj_class = self.db_cls.get_obj_type_map().get(self.obj_type)
        if self.obj_class is None:
            return

        oper = self.oper_info['oper']
        obj_id = self.oper_info['uuid']
        self.request_id = self.oper_info.get('request-id')
        self.create_msgbus_trace(self.oper_info.get('request_id'),
                                 oper, obj_id)
        if oper == 'CREATE':
            self.handle_create()
        elif oper == 'UPDATE':
            self.handle_update()
        elif oper == 'DELETE':
            self.handle_delete()
        elif oper == 'UPDATE-IMPLICIT':
            # Ignore this operation
            return
        else:
            self.handle_unknown()
            return
        if self.obj is None:
            self.logger.warning(
                    "Object %s uuid %s was not found for operation %s" %
                    (self. obj_type, obj_id, oper))
            return
        self.evaluate_dependency()

    def _get_key_from_oper_info(self):
        if self.db_cls._indexed_by_name:
            return ':'.join(self.oper_info['fq_name'])
        return self.oper_info['uuid']

    def handle_create(self):
        obj_key = self._get_key_from_oper_info()
        obj_id = self.oper_info['uuid']
        obj_fq_name = self.oper_info['fq_name']
        self.db_cls._object_db.cache_uuid_to_fq_name_add(
                obj_id, obj_fq_name, self.obj_type)
        self.obj = self.obj_class.locate(obj_key, None, self.request_id)
        if self.obj is None:
            self.logger.info('%s id %s fq_name %s not found' % (
                self.obj_type, obj_id, obj_fq_name))
            return
        self.dependency_tracker = DependencyTracker(
            self.db_cls.get_obj_type_map(), self.reaction_map)
        self.dependency_tracker.evaluate(self.obj_type, self.obj)

    def handle_update(self):
        obj_id = self.oper_info['uuid']
        self.obj = self.obj_class.get_by_uuid(obj_id)
        old_dt = None
        if self.obj is not None:
            old_dt = DependencyTracker(
                self.db_cls.get_obj_type_map(), self.reaction_map)
            old_dt.evaluate(self.obj_type, self.obj)
        else:
            self.logger.info('%s id %s not found' % (self.obj_type,
                                                     obj_id))
            return

        try:
            ret = self.obj.update(request_id = self.request_id)

            if ret is not None and not ret:
                # If update returns None, the function may not support a
                # return value, hence treat it as if something might have
                # changed. If a value is returned, use its truth value.
                # If it True, then some change was detected.
                # If no change, then terminate dependency tracker
                return
        except NoIdError:
            obj_id = self.oper_info['uuid']
            self.logger.warning('%s uuid %s update caused NoIdError' %
                                (self.obj_type, obj_id))
            return

        self.dependency_tracker = DependencyTracker(
                self.db_cls.get_obj_type_map(), self.reaction_map)
        self.dependency_tracker.evaluate(self.obj_type, self.obj)
        if old_dt:
            for resource, ids in list(old_dt.resources.items()):
                if resource not in self.dependency_tracker.resources:
                    self.dependency_tracker.resources[resource] = ids
                else:
                    self.dependency_tracker.resources[resource] = list(
                        set(self.dependency_tracker.resources[resource]) |
                        set(ids))

    def handle_delete(self):
        obj_id = self.oper_info['uuid']
        self.obj = self.obj_class.get_by_uuid(obj_id)
        self.db_cls._object_db.cache_uuid_to_fq_name_del(obj_id)
        if self.obj is None:
            return
        self.dependency_tracker = DependencyTracker(
            self.db_cls.get_obj_type_map(), self.reaction_map)
        self.dependency_tracker.evaluate(self.obj_type, self.obj)
        obj_key = self._get_key_from_oper_info()
        self.obj_class.delete(obj_key, request_id=self.request_id)

    def handle_unknown(self):
        # unknown operation
        self.logger.error('Unknown operation %s' % self.oper_info['oper'])

    def init_msgbus_fq_name(self):
        self.msg_tracer.fq_name = self.obj.name

    def init_msgbus_dtr(self):
        self.msg_tracer.dependency_tracker_resources = []

    def add_msgbus_dtr(self, res_type, res_id_list):
        dtr = DependencyTrackerResource(obj_type=res_type,
                                        obj_keys=res_id_list)
        self.msg_tracer.dependency_tracker_resources.append(dtr)

    def evaluate_dependency(self):
        if not self.dependency_tracker:
            return

        self.init_msgbus_fq_name()
        self.init_msgbus_dtr()

        evaluate_kwargs = {}
        if self.timer and self.timer.yield_in_evaluate:
            evaluate_kwargs['timer'] = self.timer

        for res_type, res_id_list in list(self.dependency_tracker.resources.items()):
            if not res_id_list:
                continue
            self.add_msgbus_dtr(res_type, res_id_list)
            cls = self.db_cls.get_obj_type_map().get(res_type)
            if cls is None:
                continue
            for res_id in res_id_list:
                res_obj = cls.get(res_id)
                if res_obj is not None:
                    if evaluate_kwargs:
                        res_obj.evaluate(**evaluate_kwargs)
                    else:
                        res_obj.evaluate()
                    if self.timer:
                        self.timer.timed_yield()

    def close(self):
        self._vnc_kombu.shutdown()
