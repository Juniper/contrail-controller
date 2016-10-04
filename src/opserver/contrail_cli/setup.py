#!/usr/bin/env python

PROJECT = 'ContrailCli'

VERSION = '0.1'

from setuptools import setup, find_packages

import ConfigParser                                                                     
import subprocess
import os
import sys
config = ConfigParser.ConfigParser()
config.read('setup.cfg')

config_dict = dict(config._sections)
for k in config_dict:
    config_dict[k] = dict(config._defaults, **config_dict[k])
    config_dict[k].pop('__name__', None)

entry_points_dict = dict()
entry_points_dict['console_scripts'] = []
commands = dict()

def _get_mapping_files(svc_name):
    names = svc_name.split(':')
    svc_name = names[0]
    if svc_name in mapping_files.keys():
	return mapping_files[svc_name]
    return []

if "install" in sys.argv:
    from sandesh_common.vns.constants import ServiceHttpPortMap

    import json

    topdir = '/usr/share/doc/contrail-docs/html/messages/'
    extn = '_introspect_cli.json'
    mapping_files = dict()
    for dirs in os.listdir(topdir):
        mapping_files[dirs] = []
        for dirpath, dirnames, files in os.walk(topdir+dirs):
            for name in files:
                if name.lower().endswith(extn):
                    mapping_files[dirs].append(os.path.join(dirpath, name))
    for svc_name in ServiceHttpPortMap:
        http_port = ServiceHttpPortMap[svc_name]
        svc_mapping_files = _get_mapping_files(svc_name)
        if len(svc_mapping_files) == 0:
	    continue
        data = ""
        print svc_mapping_files
        list_commands = []
        for mapping_file in svc_mapping_files:
            with open(mapping_file) as data_file:
                data = json.load(data_file)
            for cmd in data.keys():
	        list_commands.append((cmd, data[cmd]))

        console_command = str(svc_name) + "-cli"
        console_script_list = []
        console_script_list.append("{0} = {1}".format(console_command,
            "contrailCli.main:{0}_cli".format(str(svc_name.replace('-', '_')))))
        entry_points_dict['console_scripts'].append(console_script_list)
        entry_points_dict["contrailCli"] = []

        entry_points = [key for key in (config_dict['entry_points']) if key != "console_scripts"]
        for entry_point in entry_points:
            command_list = [str(x) for x in (config_dict['entry_points'][str(entry_point)]).splitlines() if x and x[0] != "#"]
            entry_points_dict[str(entry_point)] = command_list

        entry_point = console_command
        commands[entry_point] = []
        for command in list_commands:
            if command not in commands[entry_point]:
	        commands[entry_point].append(command)

    for name in commands:
        for command in commands[name]:
    	    if "ContrailCli_{0}".format(command[0]) not in \
            entry_points_dict["contrailCli"]:
	        entry_points_dict["contrailCli"].append("{0} = \
                    contrailCli.contrailCli:ContrailCli_{1}".format(command[1].keys()[0], command[0]))

    myfile = open("contrailCli/main.py", "r+")
    lines = myfile.readlines()
    myfile.seek(0)
    data = myfile.read()
    if "commands_list = " not in data:
        myfile.write("\ncommands_list = "+str(commands)+"\n\n")
    else:
        myfile.seek(0)
        for line in lines:
            if "commands_list = " in line:
                myfile.write("commands_list = "+str(commands)+"\n")
	        continue
            myfile.write(line)
    for svc_name in ServiceHttpPortMap:
        http_port = ServiceHttpPortMap[svc_name]
        svc_mapping_files = _get_mapping_files(svc_name)
        if len(svc_mapping_files) == 0:
	    continue
        if "def {0}_cli".format(str(svc_name.replace('-', '_'))) in data:
	    continue
        myfile.write("\ndef {0}_cli(argv=sys.argv[1:]):\n".format(str(svc_name.replace('-', '_'))))
        myfile.write("    myapp = ContrailCliApp({0})\n".format(str(http_port)))
        myfile.write("    return myapp.run(argv)\n")
    myfile.close()

    myfile = open("contrailCli/contrailCli.py", "r+")
    lines = myfile.readlines()
    myfile.seek(0)
    data = myfile.read()
    if "commands_list = " not in data:
        myfile.write("commands_list = "+str(commands)+"\n\n")
    else:
        myfile.seek(0)
        for line in lines:
            if "commands_list = " in line:
                myfile.write("commands_list = "+str(commands)+"\n")
	        continue
            myfile.write(line)

    for name in commands:
        for command in commands[name]:
    	    if "class ContrailCli_{0}".format(command[0]) not in data:
                myfile.write("\nclass ContrailCli_"+str(command[0])+"(ContrailCli):\n")
                myfile.write("\t\"\"\"\n")
                myfile.write("\t"+command[1].values()[0].keys()[0]+"\n")
                myfile.write("\t\"\"\"\n\n")

    myfile.close()

print entry_points_dict
setup(
        name=PROJECT,
        version=VERSION,
        description='Contrail Command Line Interfae',
        platforms=['Any'],
        install_requires=['cliff'],
        packages=find_packages(),
        package_data={'': ['*.html', '*.css', '*.xml']},
        include_package_data=True,
        entry_points=entry_points_dict,
        zip_safe=False,
    )

if "install" in sys.argv:
    cmd = 'rm -rf /etc/bash_completion\.d/bashrc_contrail_cli'
    output = None
    try:
        subprocess.call(cmd, shell=True)
    except Exception:
        pass

    for svc_name in ServiceHttpPortMap:
        svc_mapping_files = _get_mapping_files(svc_name)
        if len(svc_mapping_files) == 0:
	    continue
        cmd = svc_name + '-cli complete >> /etc/bash_completion\.d/bashrc_contrail_cli'
        output = None
        try:
            output = subprocess.check_output(cmd, shell=True)
        except Exception:
            output = None
        
    myfile = open("/root/.bashrc", "r+")
    lines = myfile.readlines()
    myfile.seek(0)
    data = myfile.read()
    bash_completion_enabled = False
    for line in lines:
        li=line.strip()
        if not li.startswith("#"):
            if "if [ -f /etc/bash_completion ] && ! shopt -oq posix; then" in line:
                bash_completion_enabled = True

    if bash_completion_enabled == False:
        myfile.write("if [ -f /etc/bash_completion ] && ! shopt -oq posix; then\n")
        myfile.write("    . /etc/bash_completion\n")
        myfile.write("fi\n")
