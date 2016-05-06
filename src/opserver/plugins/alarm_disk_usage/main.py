from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class DiskUsage(AlarmBase):
    """Disk Usage crosses a threshold.
       NodeMgr reports disk usage in DatabaseUsageInfo.database_usage"""

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.SYS_ERR)
        self._threshold = 50

    def __call__(self, uve_key, uve_data):
        or_list = []
        db_usage_info = uve_data.get("DatabaseUsageInfo", None)
        if db_usage_info is None:
	        return None
        db_usage_list = db_usage_info.get("database_usage", None)
        if db_usage_list is None:
            return None

        db_usage = db_usage_list[0]
        if db_usage["percentage_disk_space_used"] > self._threshold:
            and_list = []
            and_list.append(AlarmConditionMatch(
                condition=AlarmCondition(operation=">",
                    operand1="DatabaseUsageInfo.database_usage."+\
                        "percentage_disk_space_used",
                    operand2=json.dumps(self._threshold),
                    vars=["DatabaseUsageInfo.database_usage.disk_space_used_1k",
                        "DatabaseUsageInfo.database_usage.disk_space_available_1k"]),
                match=[AlarmMatch(json_operand1_value=json.dumps(
                    db_usage["percentage_disk_space_used"]), json_vars={
                    "DatabaseUsageInfo.database_usage.disk_space_used_1k":\
                        json.dumps(db_usage["disk_space_used_1k"]),
                    "DatabaseUsageInfo.database_usage.disk_space_available_1k":\
                        json.dumps(db_usage["disk_space_available_1k"])})]))
            or_list.append(AlarmRuleMatch(rule=and_list))
        if len(or_list):
            return or_list
        else:
            return None

