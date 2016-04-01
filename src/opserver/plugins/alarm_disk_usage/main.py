from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class DiskUsage(AlarmBase):
    """Disk Usage crosses a threshold.
       NodeMgr reports disk usage in DatabaseUsageInfo.database_usage"""

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.SYS_ERR)
        self._threshold = 0.90

    def __call__(self, uve_key, uve_data):
        or_list = []
        db_usage_info = uve_data.get("DatabaseUsageInfo",None)
        if db_usage_info is not None:
            db_usage_list = db_usage_info.get("database_usage",None)
        if db_usage_list is None:
            return None

        for db_usage in db_usage_list:
            used_space = db_usage["disk_space_used_1k"]
            available_space = db_usage["disk_space_available_1k"]
            use_space_threshold = available_space * self._threshold
            if used_space > use_space_threshold:
                or_list.append(AllOf(all_of=[AlarmElement(\
                    rule=AlarmTemplate(oper=">",
                        operand1=Operand1(\
                            keys=["DatabaseUsageInfo","database_usage","disk_space_used_1k"]),
                        operand2=Operand2(json_value=str(use_space_threshold))),
                    json_operand1_value=str(used_space),
                    json_vars={\
                        "DatabaseUsageInfo.database_usage.disk_space_used_1k":\
                            str(used_space),
                        "DatabaseUsageInfo.database_usage.disk_space_available_1k":\
                            str(available_space)})]))
        if len(or_list):
            return or_list
        else:
            return None

