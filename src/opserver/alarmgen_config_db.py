#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains config data model for Alarm Generator
"""

from cfgm_common.vnc_db import DBBase


class DBBaseAG(DBBase):
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


# end class DBBaseAG


class GlobalSystemConfigAG(DBBaseAG):
    _dict = {}
    obj_type = 'global_system_config'

    def __init__(self, name, obj=None):
        self.name = name
        self.update(obj)
    # end __init__


# end class GlobalSystemConfigAG


class AlarmAG(DBBaseAG):
    _dict = {}
    obj_type = 'alarm'

    def __init__(self, name, obj=None):
        self.name = name
        self.update(obj)
    # end __init__

    def update(self, obj=None):
        self.update_vnc_obj(obj)
    # end update

    def evaluate(self):
        self._manager.config_update(self.obj_type, self.name, self.obj)
    # end evaluate

    @classmethod
    def delete(cls, name):
        if name not in cls._dict:
            return
        cls._manager.config_delete(cls.obj_type, name)
        del cls._dict[name]
    # end delete


# end class AlarmAG
