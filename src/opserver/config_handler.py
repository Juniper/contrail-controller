#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#


import json

from cfgm_common.vnc_amqp import VncAmqpHandle
from cfgm_common.vnc_object_db import VncObjectDBClient
from analytics_logger import AnalyticsLogger


class ConfigHandler(object):

    def __init__(self, sandesh, service_id, rabbitmq_cfg, cassandra_cfg,
                 db_cls, reaction_map):
        self._sandesh = sandesh
        self._logger = AnalyticsLogger(self._sandesh)
        self._service_id = service_id
        self._rabbitmq_cfg = rabbitmq_cfg
        self._cassandra_cfg = cassandra_cfg
        self._db_cls = db_cls
        self._reaction_map = reaction_map
        self._vnc_amqp = None
        self._vnc_db = None
    # end __init__

    # Public methods

    def start(self):
        # Connect to rabbitmq for config update notifications
        rabbitmq_qname = self._service_id
        self._vnc_amqp = VncAmqpHandle(self._sandesh, self._logger,
            self._db_cls, self._reaction_map, self._service_id,
            self._rabbitmq_cfg)
        self._vnc_amqp.establish()
        cassandra_credential = {
            'username': self._cassandra_cfg['user'],
            'password': self._cassandra_cfg['password']
        }
        if not all(cassandra_credential.values()):
            cassandra_credential = None
        self._vnc_db = VncObjectDBClient(self._cassandra_cfg['servers'],
            self._cassandra_cfg['cluster_id'], logger=self._logger.log,
            credential=cassandra_credential)
        self._db_cls.init(self, self._logger, self._vnc_db)
        self._sync_config_db()
    # end start

    def stop(self):
        self._vnc_amqp.close()
        self._vnc_db = None
        self._db_cls.clear()
    # end stop

    def obj_to_dict(self, obj):
        def to_json(obj):
            if hasattr(obj, 'serialize_to_json'):
                return obj.serialize_to_json()
            else:
                return dict((k, v) for k, v in obj.__dict__.iteritems())

        return json.loads(json.dumps(obj, default=to_json))
    # end obj_to_dict

    # Private methods

    def _fqname_to_str(self, fq_name):
        return ':'.join(fq_name)
    # end _fqname_to_str

    def _sync_config_db(self):
        for cls in self._db_cls.get_obj_type_map().values():
            cls.reinit()
        self._handle_config_sync()
        self._vnc_amqp._db_resync_done.set()
    # end _sync_config_db

    # Should be overridden by the derived class
    def _handle_config_sync(self):
        pass
    # end _handle_config_sync


# end class ConfigHandler
