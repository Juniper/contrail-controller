import gevent
import socket

from vnc_api.vnc_api import *
from cfgm_common.vnc_kombu import VncKombuClient
from config_db import *
from cfgm_common.dependency_tracker import DependencyTracker
from reaction_map import REACTION_MAP
import svc_monitor

class RabbitConnection(object):

    _REACTION_MAP = REACTION_MAP

    def __init__(self, logger, args=None):
        self._args = args
        self.logger = logger

    def _connect_rabbit(self):
        rabbit_server = self._args.rabbit_server
        rabbit_port = self._args.rabbit_port
        rabbit_user = self._args.rabbit_user
        rabbit_password = self._args.rabbit_password
        rabbit_vhost = self._args.rabbit_vhost
        rabbit_ha_mode = self._args.rabbit_ha_mode

        self._db_resync_done = gevent.event.Event()

        q_name = 'svc_mon.%s' % (socket.gethostname())
        self._vnc_kombu = VncKombuClient(rabbit_server, rabbit_port,
            rabbit_user, rabbit_password,
            rabbit_vhost, rabbit_ha_mode,
            q_name, self._vnc_subscribe_callback,
            self.logger.log)

    def _vnc_subscribe_callback(self, oper_info):
        self._db_resync_done.wait()
        try:
            self._vnc_subscribe_actions(oper_info)
        except Exception:
            svc_monitor.cgitb_error_log(self)

    def _vnc_subscribe_actions(self, oper_info):
        msg = "Notification Message: %s" % (pformat(oper_info))
        self.logger.debug(msg)
        obj_type = oper_info['type'].replace('-', '_')
        obj_class = DBBaseSM.get_obj_type_map().get(obj_type)
        if obj_class is None:
            return

        if oper_info['oper'] == 'CREATE':
            obj_dict = oper_info['obj_dict']
            obj_id = oper_info['uuid']
            obj = obj_class.locate(obj_id)
            dependency_tracker = DependencyTracker(
                DBBaseSM.get_obj_type_map(), self._REACTION_MAP)
            dependency_tracker.evaluate(obj_type, obj)
        elif oper_info['oper'] == 'UPDATE':
            obj_id = oper_info['uuid']
            obj = obj_class.get(obj_id)
            old_dt = None
            if obj is not None:
                old_dt = DependencyTracker(
                    DBBaseSM.get_obj_type_map(), self._REACTION_MAP)
                old_dt.evaluate(obj_type, obj)
            else:
                obj = obj_class.locate(obj_id)
            obj.update()
            dependency_tracker = DependencyTracker(
                DBBaseSM.get_obj_type_map(), self._REACTION_MAP)
            dependency_tracker.evaluate(obj_type, obj)
            if old_dt:
                for resource, ids in old_dt.resources.items():
                    if resource not in dependency_tracker.resources:
                        dependency_tracker.resources[resource] = ids
                    else:
                        dependency_tracker.resources[resource] = list(
                            set(dependency_tracker.resources[resource]) |
                            set(ids))
        elif oper_info['oper'] == 'DELETE':
            obj_id = oper_info['uuid']
            obj = obj_class.get(obj_id)
            if obj is None:
                return
            dependency_tracker = DependencyTracker(
                DBBaseSM.get_obj_type_map(), self._REACTION_MAP)
            dependency_tracker.evaluate(obj_type, obj)
            obj_class.delete(obj_id)
        else:
            # unknown operation
            self.logger.error('Unknown operation %s' % oper_info['oper'])
            return

        if obj is None:
            self.logger.error('Error while accessing %s uuid %s' % (
                obj_type, obj_id))
            return

        for res_type, res_id_list in dependency_tracker.resources.items():
            if not res_id_list:
                continue
            cls = DBBaseSM.get_obj_type_map().get(res_type)
            if cls is None:
                continue
            for res_id in res_id_list:
                res_obj = cls.get(res_id)
                if res_obj is not None:
                    res_obj.evaluate()
