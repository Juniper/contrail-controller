#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
VNC management for Mesos
"""

import gevent
from gevent.queue import Empty

import requests

from cfgm_common import importutils
from cfgm_common import vnc_cgitb
from cfgm_common.vnc_amqp import VncAmqpHandle
from vnc_api.vnc_api import *
from config_db import *
import db
from reaction_map import REACTION_MAP

class VncMesos(object):

    def __init__(self, args=None, logger=None, q=None):
        self.args = args
        self.logger = logger
        self.q = q

        # init vnc connection
        self.vnc_lib = self._vnc_connect()

        # init access to db
        self._db = db.MesosNetworkManagerDB(self.args, self.logger)
        DBBaseMM.init(self, self.logger, self._db)

        # init rabbit connection
        self.rabbit = VncAmqpHandle(self.logger, DBBaseMM,
            REACTION_MAP, 'mesos_manager', args=self.args)
        self.rabbit.establish()

        # sync api server db in local cache
        self._sync_sm()
        self.rabbit._db_resync_done.set()


    def _vnc_connect(self):
        # Retry till API server connection is up
        connected = False
        while not connected:
            try:
                vnc_lib = VncApi(self.args.admin_user,
                    self.args.admin_password, self.args.admin_tenant,
                    self.args.vnc_endpoint_ip, self.args.vnc_endpoint_port)
                connected = True
            except requests.exceptions.ConnectionError as e:
                time.sleep(3)
            except ResourceExhaustionError:
                time.sleep(3)
        return vnc_lib

    def _sync_sm(self):
        for cls in DBBaseMM.get_obj_type_map().values():
            for obj in cls.list_obj():
                cls.locate(obj['uuid'], obj)

    @staticmethod
    def reset():
        for cls in DBBaseMM.get_obj_type_map().values():
            cls.reset()

    def vnc_process(self):
        while True:
            try:
                event = self.q.get()
            except Empty:
                gevent.sleep(0)
