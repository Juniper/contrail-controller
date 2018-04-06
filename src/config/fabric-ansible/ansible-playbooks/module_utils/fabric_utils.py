from ansible.module_utils.basic import AnsibleModule
from job_manager.fabric_display import fabric_ansible_logger


class FabricAnsibleModule(AnsibleModule):
    def __init__(self, argument_spec={}, **kwargs):
        super(
            FabricAnsibleModule,
            self).__init__(
            argument_spec=argument_spec,
            **kwargs)
        self.module_name = self._name
        self.ctx = self.params.get('job_ctx')
        self.logger = fabric_ansible_logger(self.module_name)
        self.logger.warning("Module params: {}".format(self.params))
        