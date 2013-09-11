#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import functools

import stevedore

class ApiHookManager(stevedore.hook.HookManager):
    def __init__(self, namespace, hook_name):
        super(ApiHookManager, self).__init__(namespace, hook_name, invoke_on_load = True)
    #end __init__

    def run_pre(self, hook_name, args, kwargs):
        for e in self.extensions:
            obj = e.obj
            pre = getattr(obj, 'pre', None)
            if pre:
                pre(*args, **kwargs)
    #end run_pre

    def run_post(self, hook_name, rv, args, kwargs):
        for e in reversed(self.extensions):
            obj = e.obj
            post = getattr(obj, 'post', None)
            if post:
                post(rv, *args, **kwargs)
    #end run_post
#end class ApiHookManager


def add_api_hook(hook_manager, hook_name):
    def outer(f):
        @functools.wraps(f)
        def inner(*args, **kwargs):
            hook_manager.run_pre(hook_name, args, kwargs)
            rv = f(*args, **kwargs)
            hook_manager.run_post(hook_name, rv, args, kwargs)

            return rv

        return inner
        #end inner
    #end outer

    return outer
#end add_api_hook

class ExtensionManager(stevedore.extension.ExtensionManager):
    def __init__(self, namespace, api_server_ip, api_server_port, conf_sections):
        super(ExtensionManager, self).__init__(namespace, invoke_on_load = True,
                                               invoke_kwds = {
                                                   'api_server_ip': api_server_ip,
                                                   'api_server_port': api_server_port,
                                                   'conf_sections': conf_sections,
                                               })
    #end __init__

#end class ExtensionManager
