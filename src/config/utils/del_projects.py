#!/usr/bin/python
#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

from builtins import object
import argparse
import json
import re
import requests
from requests.auth import HTTPBasicAuth
import sys
import logging

logger = logging.getLogger(__name__)

class DeleteProjects(object):

    def __init__(self, args_str = None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        self._server_ip = self._args.ip or 'localhost'
        self._server_port = self._args.port or 8095

        self._username = self._args.username or ''
        self._password = self._args.password or ''

        if self._args.debug:
            logger.setLevel(logging.DEBUG)
        elif self._args.verbose:
            logger.setLevel(logging.INFO)

        self._proj_uuid = self._args.proj_uuid

    def _parse_args(self, args_str):
        '''
        Eg. python del_projects.py --username admin --password contrail123 
                --proj-uuid a98559a9-b8a8-4888-a1b6-ffb24b360bf4 --debug
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
                             help = "IP Address of the controller")
        parser.add_argument("--port", type=str,
                             help = "Port of the controller")

        parser.add_argument('--username', type=str,
            help='Username with admin role')
        parser.add_argument('--password', type=str,
            help='Password of user with admin role')

        parser.add_argument(
            "--verbose", help="Run in verbose/INFO mode, default False",
            action='store_true', default=False)
        parser.add_argument(
            "--debug", help="Run in debug mode, default False",
            action='store_true', default=False)

        parser.add_argument('--proj-uuid', type=str,
            help='The project uuid in dashed format to delete', required=True)

        self._args = parser.parse_args(remaining_argv)

    #end _parse_args

    def _delete_ref_object(self, parent_url):
        auth = HTTPBasicAuth(self._username, self._password)
        logger.info("Deleting: %s" %(parent_url))
        ret = requests.delete(parent_url, auth=auth)
        if ret.status_code == 200:
            logger.info("Deleted: %s" %(parent_url))
        elif ret.status_code == 404:
            logger.debug("Ignoring 404 for %s" %(parent_url))
        elif ret.status_code == 409:
            ret_content = ret.content.replace(parent_url, '')
            ret_content = ret_content.replace(',', '')
            child_urls = re.findall('http[s]?://(?:[a-zA-Z]|[0-9]|[$-_@.&+]|[!*\(\),]|(?:%[0-9a-fA-F][0-9a-fA-F]))+', ret_content)
            if child_urls:
                for child_url in child_urls:
                    self._delete_ref_object(child_url)

                # Now try deleting the parent again
                self._delete_ref_object(parent_url)
        else:
            logger.error("Delete gave un-handled status code %s",
                ret.status_code)

    def _delete_project(self):
        proj_url = "http://%s:%s/project/%s" %(
            self._server_ip, self._server_port, self._proj_uuid)
        self._delete_ref_object(proj_url)
        
def main(args_str=None):
    dp = DeleteProjects(args_str)
    dp._delete_project()
#end main

if __name__ == "__main__":
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)
    logger.setLevel(logging.ERROR)
    main() 
