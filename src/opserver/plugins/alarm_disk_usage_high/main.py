from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class DiskUsageHigh(AlarmBase):
    """Disk Usage crosses high threshold level.
       NodeMgr reports disk usage in NodeStatus.disk_usage_info"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'NodeStatus.disk_usage_info.*.' + \
                            'percentage_partition_space_used',
                        'operation': 'range',
                        'operand2': {
                            'json_value': '[70, 90]'
                        },
                        'variables': \
                            ['NodeStatus.disk_usage_info.__key']
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)
