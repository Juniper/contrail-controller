#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
from ContrailCli.main import ContrailCliApp
from commandlist import commands_list
import sys, os

def contrail_control_cli(argv=sys.argv[1:]):
    app_name = os.path.splitext(os.path.basename(sys.argv[0]))[0]
    if app_name in commands_list.keys():
        cliapp = ContrailCliApp(commands_list[app_name])
        return cliapp.run(argv)
    else:
        print "commands list is empty, exiting"
        return
