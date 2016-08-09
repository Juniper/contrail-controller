from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class ProcessVersion(AlarmBase):
    """Running Process version and installed process version are different.
       NodeMgr reports package version in NodeStatus.package_version and installed package version in Nodestatus.installed_package_version"""
   
    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'NodeStatus.package_version',
                        'operation': '!=',
                        'operand2': 'NodeStatus.installed_package_version'
                    }
                ]
            }
         ]
    }

    def __init__(self):
	AlarmBase.__init__(self, AlarmBase.SYS_ERR)
