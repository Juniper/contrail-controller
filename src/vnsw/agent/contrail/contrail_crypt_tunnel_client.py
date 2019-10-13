#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc.
#
# author: Sanju Abraham
#
# This sceipt is called by the vrouter agent for CRUD
# of IPSEC tunnels

from __future__ import print_function
from future import standard_library
standard_library.install_aliases()
from builtins import object
import argparse
from six.moves import configparser
import json
import logging
import requests
import sys
import time
import socket
from passlib.hash import sha256_crypt


_ADMIN = 'admin'
_PASSWD = 'c0ntrailIPSEC'
# It is assumed that the server always listens
# on loopback interface on the compute host
_URL='http://127.0.0.1:10080/contrailipsec/connection'
_SUCCESS='success'
_EXISTS='exists'

class ContrailCryptTunnelClient(object):
    def __init__(self, leftIP, rightIP):
        self._conn=leftIP + '_' + rightIP
        self._jsonStr='{\"leftTunnelIP\":\"%s\", \"rightTunnelIP\":\"%s\"}' % (leftIP, rightIP)

    @classmethod
    def getAuthStr(cls):
        password=_PASSWD+socket.getfqdn()
        passwd=sha256_crypt.encrypt(password)
        return passwd

    def createCryptTunnel(self):
        err='failure'
        passwd=self.getAuthStr()
        session = requests.Session()
        session.auth = (_ADMIN, passwd)
        try:
           res=session.post(_URL,json=json.loads(self._jsonStr))
           logging.debug("Response from create request for tunnel connection %s = %s" % (self._conn, res.json()))
           ret=res.json()
           result=ret['Result']
           if (_SUCCESS in result) or (_EXISTS in result):
               print(_SUCCESS)
               return _SUCCESS
           else:
               print(err)
               return err
        except Exception as ex:
               logging.error(ex)
               print(err)
               return err

    def deleteCryptTunnel(self):
        err='failure'
        passwd=self.getAuthStr()
        session = requests.Session()
        session.auth = (_ADMIN, passwd)
        try:
           res=session.delete(_URL,json=json.loads(self._jsonStr))
           logging.debug("Response from delete request for tunnel connection %s = %s" % (self._conn, res.json()))
           ret=res.json()
           result=ret['Result']
           if _SUCCESS in result:
              print(_SUCCESS)
              return _SUCCESS
           else:
              print(err)
              return err
        except Exception as ex:
               logging.error(ex)
               print(err)
               return err

    def statusCryptTunnel(self):
        err='failure'
        passwd=self.getAuthStr()
        session = requests.Session()
        session.auth = (_ADMIN, passwd)
        try:
           res=session.get(_URL,json=json.loads(self._jsonStr))
           logging.debug("Response from status request for tunnel connection %s = %s" % (self._conn, res.json()))
           ret=res.json()
           result=ret['Result']
           if _SUCCESS in result:
              print(_SUCCESS)
              return _SUCCESS
           else:
              print(err)
              return err
        except Exception as ex:
                logging.error(ex)
                print(err)
                return err

def initialize(oper, leftIP, rightIP):
    cryptTunnel = ContrailCryptTunnelClient(leftIP, rightIP)
    if oper == "create":
       cryptTunnel.createCryptTunnel()
    if oper == "delete":
       cryptTunnel.deleteCryptTunnel()
    if oper == "status":
       cryptTunnel.statusCryptTunnel()

def parse_args(args_str):
    '''
    Eg. contrail_crypt_tunnel_client.py --oper create / delete / status --source_ip 1.1.1.3 --remote_ip 1.1.1.4
    '''
    conf_parser = argparse.ArgumentParser(add_help=False)
    args, remaining_argv = conf_parser.parse_known_args(args_str.split())
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
    parser.add_argument("--oper", help="operation - create / delete / status")
    parser.add_argument("--source_ip", help="Tunnel source IP")
    parser.add_argument("--remote_ip", help="Tunnel remote IP")
    args = parser.parse_args(remaining_argv)
    if not args.oper:
       parser.error('Missing required operation create / update or delete');
    if args.oper == "create" or args.oper == "delete" or args.oper == "status":
       if not (args.source_ip and args.remote_ip):
          parser.error('Missing API server credentials and / or the route parameters')
    else:
        print("Unsupported operaton on ContrailCryptTunnelClient object. Supported operations are create / delete / status")
        exit(1)
    return args

def main(args_str=None):
    logging.basicConfig(format='%(asctime)s %(message)s', datefmt='%m/%d/%Y %I:%M:%S %p',
                        filename='/var/log/contrail_crypt_tunnel_client.log',
                        level=logging.DEBUG)
    logging.debug(' '.join(sys.argv))
    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)
    initialize(args.oper, args.source_ip, args.remote_ip)

if __name__ == '__main__':
    main()
