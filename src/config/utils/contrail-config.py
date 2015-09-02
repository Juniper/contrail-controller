#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import os
import sys
import errno
import subprocess
import time
import argparse
import re
import json
from pprint import pprint

sys.path.insert(0, os.path.realpath('/usr/lib/python2.7/site-packages'))
sys.path.insert(
    0,
    os.path.realpath('/opt/contrail/api-venv/lib/python2.7/site-packages/vnc_cfg_api_server/'))

from vnc_api.vnc_api import *
from vnc_api.common import exceptions as vnc_exceptions
import vnc_cfg_api_server


class ContrailConfigCmd(object):
    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        # connect to vnc server
        self._vnc_lib = VncApi('u', 'p',
                               api_server_host=self._args.listen_ip_addr,
                               api_server_port=self._args.listen_port)

        self.re_parser = re.compile('[ \t\n]+')
        self.final_list = []
    #end __init__

    def _parse_args(self, args_str):
        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        global_defaults = {}

        args.conf_file = '/etc/contrail/api_server.conf'
        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read([args.conf_file])
            global_defaults.update(dict(config.items("DEFAULTS")))

        # Override with CLI options
        # Don't surpress add_help here so it will handle -h
        parser = argparse.ArgumentParser(
            # Inherit options from config_parser
            parents=[conf_parser],
            # print script description with -h/--help
            description=__doc__,
            # Don't mess with format of description
            formatter_class=argparse.RawDescriptionHelpFormatter,
            )

        parser.set_defaults(**global_defaults)
        subparsers = parser.add_subparsers()

        restore_parser = subparsers.add_parser('restore')
        restore_parser.add_argument("filename",
                                    help="file name to save config")
        restore_parser.set_defaults(func=self.restore_contrail_config)

        backup_parser = subparsers.add_parser('backup')
        backup_parser.add_argument("name",
                                   help="name to backup config database")
        backup_parser.set_defaults(func=self.backup_contrail_config)

        self._args = parser.parse_args(remaining_argv)
    #end _parse_args

    def _delete_default_security_groups(self, objs, type):
        for obj in objs.values():
            if obj['type'] != type:
                continue

            fq_name = obj['data']['fq_name']
            fq_name.append('default')
            while 1:
                try:
                    self._vnc_lib.security_group_delete(fq_name=fq_name)
                    break
                except NoIdError:
                    pass
    #end _delete_default_security_groups

    def _create_objects(self, objs, type):
        for obj in objs.values():
            if obj['type'] != type:
                continue
            if obj['created']:
                continue

            #create object with these keys
            create_obj = {}
            for key, val in obj['data'].items():
                if key not in ['uuid', 'fq_name', 'id_perms',
                               'parent_type', 'virtual_network_refs',
                               'network_ipam_refs', 'network_ipam_mgmt']:
                    continue
                create_obj[key] = val

            resource = obj['type'].replace('_', '-')
            json_body = json.dumps({resource: create_obj})

            try:
                self._vnc_lib.restore_config(True, resource, json_body)
                obj['created'] = True
            except RefsExistError:
                obj['created'] = True
            except Exception as e:
                if hasattr(e, 'status_code'):
                    print("Error(%s): creating %s %s "
                          % (e.status_code, obj['type'],
                             obj['data']['fq_name']))
                else:
                    print("Error: creating %s %s %s\n"
                          % (obj['type'], obj['data']['fq_name'], e))

    #end _create_objects

    def _update_objects(self, objs, type):
        for obj in objs.values():
            if obj['type'] != type:
                continue

            resource = obj['type'].replace('_', '-')
            json_body = json.dumps({resource: obj['data']})
            try:
                self._vnc_lib.restore_config(False, resource, json_body)
            except RefsExistError:
                pass
            except Exception as e:
                if hasattr(e, 'status_code'):
                    print("Error(%s): updating %s %s "
                          % (e.status_code, obj['type'],
                             obj['data']['fq_name']))
                else:
                    print("Error: updating %s %s %s\n"
                          % (obj['type'], obj['data']['fq_name'], e))
    #end _update_objects

    #breadth first search
    def _bfs(self, tree, root):
        bfs_path = []
        index = -1
        bfs_path.append(root)
        while index != len(bfs_path):
            index += 1
            try:
                values = tree[bfs_path[index]]
                values.sort()
                for value in values:
                    if value not in bfs_path:
                        bfs_path.append(value)
            except Exception as e:
                pass
        return bfs_path
    #end _bfs

    #restore config from a file - overwrite old config with the same uuid
    def restore_contrail_config(self):
        print "Restoring config from %s" % (self._args.filename)
        objs = {}

        #scan through the file
        f = open(self._args.filename, 'r')
        while 1:
            line = f.readline()
            if not line:
                break

            line = line.strip().replace('\n', '')
            if not line.startswith('type'):
                continue

            #store objects
            type = line.split(':')[1]
            line = f.readline()
            obj = json.loads(line)
            objs[str(obj['fq_name'])] = {'created': False,
                                         'type': type.replace('_', '-'),
                                         'data': obj}
        f.close()

        #create hierarchy
        hierarchy = {}
        for obj in objs.values():
            if not 'parent_type' in obj['data'].keys():
                if obj['type'] not in hierarchy:
                    hierarchy[obj['type']] = []
            elif not obj['data']['parent_type'] in hierarchy:
                hierarchy[obj['data']['parent_type']] = []
                hierarchy[obj['data']['parent_type']].append(obj['type'])
            else:
                if obj['type'] not in hierarchy[obj['data']['parent_type']]:
                    hierarchy[obj['data']['parent_type']].append(obj['type'])

        #find top level
        top_list = []
        for key, val in hierarchy.items():
            top_level = True
            for values in hierarchy.values():
                if key in values:
                    top_level = False
                    break
            if top_level:
                top_list.append(key)

        #organize hierarchy
        for top_level in top_list:
            bfs_list = self._bfs(hierarchy, top_level)
            self.final_list.extend(bfs_list)

        #post(create) object to api server
        print self.final_list
        print ("Phase create")
        print ("------------")
        for type in self.final_list:
            print("%-64s -- start" % type)
            self._create_objects(objs, type)
            if type == 'project':
                self._delete_default_security_groups(objs, type)
            print("%-64s -- done" % '')

        #put(update) object in api server
        print ("Phase update")
        print ("------------")
        for type in self.final_list:
            print("%-64s -- start" % type)
            self._update_objects(objs, type)
            print("%-64s -- done" % '')

        print "Config restore complete %s" % (self._args.filename)
    #restore_contrail_config

    #backup config
    def backup_contrail_config(self):
        print "Snapshot config database to %s" % (self._args.name)
        f = open(self._args.name, 'w')
        records = self._vnc_lib.fetch_records()

        #store the records
        for record in records:
            f.write("\ntype:%s\n" % (record['type']))
            f.write(json.dumps(record) + '\n')

        f.close()
        print "Snapshot config database to %s done" % (self._args.name)
    #backup_contrail_config

#end class ContrailConfigCmd


def main(args_str=None):
    cfg = ContrailConfigCmd(args_str)
    cfg._args.func()
#end main

if __name__ == "__main__":
    main()
