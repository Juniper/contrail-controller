from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class DiskUsage(AlarmBase):
    """Disk Usage crosses a threshold.
       NodeMgr reports disk usage in NodeStatus.disk_usage_info"""

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.SYS_ERR)
        self._threshold = 50

    def __call__(self, uve_key, uve_data):
        or_list = []
        node_status = uve_data.get("NodeStatus", None)
        if node_status is None:
	    return None
        disk_usage_list = node_status.get("disk_usage_info", None)
        if disk_usage_list is None:
            return None

        match_list = []
        for disk_usage in disk_usage_list:
            if disk_usage["percentage_partition_space_used"] > self._threshold:
                match_list.append(AlarmMatch(json_operand1_value=json.dumps(
                    disk_usage["percentage_partition_space_used"]), json_vars={
                    "NodeStatus.disk_usage_info.partition_name":\
                        json.dumps(disk_usage["partition_name"])}))
        if len(match_list):
            and_list = [AlarmConditionMatch(
                condition=AlarmCondition(operation=">",
                    operand1="NodeStatus.disk_usage_info." +\
                        "percentage_partition_space_used",
                    operand2=json.dumps(self._threshold),
                    vars=["NodeStatus.disk_usage_info.partition_name"]),
                match=match_list)]
            or_list.append(AlarmRuleMatch(rule=and_list))
        if len(or_list):
            return or_list
        else:
            return None
