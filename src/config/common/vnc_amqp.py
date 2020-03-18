import socket
import gevent
import cStringIO
from pprint import pformat

from cfgm_common.utils import cgitb_hook
from cfgm_common.exceptions import NoIdError
from cfgm_common.vnc_kombu import VncKombuClient
from cfgm_common.dependency_tracker import DependencyTracker


class VncAmqpHandle(object):

    def __init__(self, logger, db_cls, reaction_map, q_name_prefix, args=None,
            timer_obj=None):
        self.logger = logger
        self.db_cls = db_cls
        self.reaction_map = reaction_map
        self.q_name_prefix = q_name_prefix
        self._db_resync_done = gevent.event.Event()
        self._args = args
        self.timer = timer_obj

    def establish(self):
        q_name = '.'.join([self.q_name_prefix, socket.gethostname()])
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

    def msgbus_store_err_msg(self, msg):
        pass

    def msgbus_trace_msg(self):
        pass

    def _vnc_subscribe_callback(self, oper_info):
        self._db_resync_done.wait()
        try:
            self.oper_info = oper_info
            self.vnc_subscribe_actions()
        except Exception:
            string_buf = cStringIO.StringIO()
            cgitb_hook(file=string_buf, format="text")
            self.logger.error(string_buf.getvalue())

            self.msgbus_store_err_msg(string_buf.getvalue())
            try:
                with open(self._args.trace_file, 'a') as err_file:
                    err_file.write(string_buf.getvalue())
            except IOError:
                pass
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
        pass

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
        self.create_msgbus_trace(self.oper_info.get('request_id'),
                                 oper, obj_id)
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

    def handle_create(self):
        obj_dict = self.oper_info['obj_dict']
        obj_key = self.db_cls.get_key_from_dict(obj_dict)
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
            ret = self.obj.update()
            if ret is not None and not(ret):
                # If update returns None, the function may not support a
                # return value, hence treat it as if something might have
                # changed. If a valus is returned, use its truth value.
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
            for resource, ids in old_dt.resources.items():
                if resource not in self.dependency_tracker.resources:
                    self.dependency_tracker.resources[resource] = ids
                else:
                    self.dependency_tracker.resources[resource] = list(
                        set(self.dependency_tracker.resources[resource]) |
                        set(ids))

    def handle_delete(self):
        obj_id = self.oper_info['uuid']
        self.obj = self.obj_class.get_by_uuid(obj_id)
        self.db_cls._cassandra.cache_uuid_to_fq_name_del(obj_id)
        if self.obj is None:
            return
        self.dependency_tracker = DependencyTracker(
            self.db_cls.get_obj_type_map(), self.reaction_map)
        self.dependency_tracker.evaluate(self.obj_type, self.obj)
        obj_key = self.db_cls.get_key_from_dict(self.oper_info['obj_dict'])
        self.obj_class.delete(obj_key)

    def handle_unknown(self):
        # unknown operation
        self.logger.error('Unknown operation %s' % self.oper_info['oper'])

    def init_msgbus_fq_name(self):
        pass

    def init_msgbus_dtr(self):
        pass

    def add_msgbus_dtr(self, res_type, res_id_list):
        pass

    def evaluate_dependency(self):
        if not self.dependency_tracker:
            return

        self.init_msgbus_fq_name()
        self.init_msgbus_dtr()

        evaluate_kwargs = {}
        if self.timer and self.timer.yield_in_evaluate:
            evaluate_kwargs['timer'] = self.timer

        for res_type, res_id_list in self.dependency_tracker.resources.items():
            if not res_id_list:
                continue
            self.add_msgbus_dtr(res_type, res_id_list)
            cls = self.db_cls.get_obj_type_map().get(res_type)
            if cls is None:
                continue
            for res_id in res_id_list:
                res_obj = cls.get(res_id)
                if res_obj is not None:
                    res_obj.evaluate()
                    if evaluate_kwargs:
                        res_obj.evaluate(**evaluate_kwargs)
                    else:
                        res_obj.evaluate()
                    if self.timer:
                        self.timer.timed_yield()

    def close(self):
        self._vnc_kombu.shutdown()
