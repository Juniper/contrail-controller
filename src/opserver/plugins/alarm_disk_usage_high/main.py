from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class DiskUsageHigh(AlarmBase):
    """Disk Usage crosses high threshold level.
       NodeMgr reports disk usage in NodeStatus.disk_usage_info"""

    _RULES = 'NodeStatus.disk_usage_info.percentage_partition_space_used range [70, 90]'

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)
        self._threshold = [70, 90]

    def __call__(self, uve_key, uve_data):
        or_list = []
        node_status = uve_data.get("NodeStatus", None)
        if node_status is not None:
            disk_usage = node_status.get("disk_usage_info", None)
            if disk_usage is not None:
                for partition_info in disk_usage:
                    partition_space_used = partition_info['percentage_partition_space_used']
                    if self._threshold[0] <= partition_space_used <= self._threshold[1]:
                        or_list.append(AllOf(all_of=[AlarmElement(
                            rule=AlarmTemplate(oper="range",
                                operand1=Operand1(
                                    keys=["NodeStatus", "disk_usage_info",
                                        "percentage_partition_space_used"]),
                                operand2=Operand2(json_value=json.dumps(
                                    self._threshold))),
                            json_operand1_value=json.dumps(
                                partition_info['percentage_partition_space_used']),
                            json_vars={"NodeStatus.disk_usage_info.partition_name":
                                json.dumps(partition_info["partition_name"])})]))
        if len(or_list):
            return or_list
        else:
            return None
