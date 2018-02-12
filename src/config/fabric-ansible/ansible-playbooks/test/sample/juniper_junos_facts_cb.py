# This is a sample callback used for juniper_junos_facts.yaml playbook

from ansible.plugins.callback import CallbackBase

class CallbackModule(CallbackBase):
    CALLBACK_VERSION = 2.0
    CALLBACK_TYPE = ''
    CALLBACK_NAME = 'juniper_junos_facts'
    CALLBACK_NEEDS_WHITELIST = True

    def __init__(self, display=None):
        super(CallbackModule, self).__init__(display)
        self.results = None

    def v2_runner_on_ok(self, res):
        if res.task_name == "READ JUNOS CONFIG":
            self.results = res._result['facts']
