#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from __future__ import absolute_import

from builtins import object
from .vnc_mesos_config import VncMesosConfig as vnc_mesos_config
from vnc_api.vnc_api import (KeyValuePair,KeyValuePairs)
from .config_db import DBBaseMM

class VncCommon(object):
    """VNC mesos common functionality.
    """
    def __init__(self, mesos_obj_kind):
        self.annotations = {}
        self.annotations['kind'] = mesos_obj_kind

    def get_annotations(self):
        return self.annotations
