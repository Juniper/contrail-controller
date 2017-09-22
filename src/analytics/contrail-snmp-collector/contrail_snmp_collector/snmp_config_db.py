#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains config data model for contrail-snmp-collector
"""

from cfgm_common.vnc_db import DBBase


class DBBaseSC(DBBase):
    obj_type = __name__
    _indexed_by_name = True
    ref_fields = []
    prop_fields = []

    def update(self, obj=None):
        return self.update_vnc_obj(obj)
    # end update

    def evaluate(self):
        # Implement in the derived class
        pass
    # end evaluate

    @classmethod
    def reinit(cls):
        for obj in cls.list_vnc_obj():
            try:
                cls.locate(obj.get_fq_name_str(), obj)
            except Exception as e:
                cls._logger.error('Error in reinit for %s %s: %s' % (
                    cls.obj_type, obj.get_fq_name_str(), str(e)))
    # end reinit


# end class DBBaseSC


class PhysicalRouterSC(DBBaseSC):
    _dict = {}
    obj_type = 'physical_router'

    def __init__(self, name, obj=None):
        self.name = name
        self.update(obj)
    # end __init__


# end class PhysicalRouterSC
