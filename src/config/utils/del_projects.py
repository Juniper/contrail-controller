#!/usr/bin/python
#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import argparse
import json
import re
import requests
import sys

class DeleteProjects(object):

    def __init__(self, args_str = None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        self._server_ip = self._args.ip
        if not self._server_ip:
            self._server_ip = '127.0.0.1'

        self._server_port = self._args.port
        if not self._server_port:
            self._server_port = 8082

        self._proj_name = self._args.proj

    def _parse_args(self, args_str):
        '''
        Eg. python del_projects.py 10.1.1.1 8082 test_proj
        '''

        # Turn off help, so we print all options in response to -h
        parser = argparse.ArgumentParser(description='Add a Node to Puppet Config.')
        
        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help = False)
        
        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        parser.add_argument("--ip", type=str,
                             help = "IP Address of the controller", required=True)
        parser.add_argument("--port", type=str,
                             help = "Port of the controller", required=True)
        parser.add_argument('--proj', type=str,
                             help='The project name to delete', required=True)

        self._args = parser.parse_args(remaining_argv)

    #end _parse_args
    def _delete_ref_object(self, parent_url):

        ret = requests.delete(parent_url)
        if ret.status_code == 200:
            print "Deleted: %s" %(parent_url)
        if ret.status_code != 200:
            child_urls = re.findall('http[s]?://(?:[a-zA-Z]|[0-9]|[$-_@.&+]|[!*\(\),]|(?:%[0-9a-fA-F][0-9a-fA-F]))+', ret.content)
            if child_urls:
                for child_url in child_urls:
                    self._delete_ref_object(child_url)

                # Now try deleting the parent again
                self._delete_ref_object(parent_url)

    def _delete_project(self):
        #import pdb; pdb.set_trace()
        proj_name = self._proj_name
        proj_url = "http://%s:%s/projects" %(self._server_ip, self._server_port)
        ret = requests.get(proj_url)
        if ret.status_code != 200:
            print "Cannot get list of projects from %s:%s" %(self._server_ip, self._server_port)
            sys.exit(0)

        projs = json.loads(ret.content)['projects']
        for proj in projs:
            if proj_name in proj['fq_name']:
                # delete the project
                self._delete_ref_object(proj['href'])
        
def main(args_str=None):
    dp = DeleteProjects(args_str)
    dp._delete_project()
#end main

if __name__ == "__main__":
    main() 
