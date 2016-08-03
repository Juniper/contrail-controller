from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class DiskUsage(AlarmBase):
    """Disk Usage crosses a threshold.
       NodeMgr reports disk usage in NodeStatus.disk_usage_info"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'NodeStatus.disk_usage_info.' + \
                            'percentage_partition_space_used',
                        'operation': '>=',
                        'operand2': {
                            'json_value': '90'
                        },
                        'variables': \
                            ['NodeStatus.disk_usage_info.partition_name']
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_CRITICAL)
