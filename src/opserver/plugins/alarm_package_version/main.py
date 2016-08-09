from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class PackageVersion(AlarmBase):
    """Package version mismatch.
       There is a mismatch between the installed and the running version of the software corresponding to this node."""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'NodeStatus.package_version',
                        'operation': '!=',
                        'operand2': {
                            'uve_attribute': 'NodeStatus.installed_package_version'
                        }
                    }
                ]
            }
         ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_CRITICAL)
