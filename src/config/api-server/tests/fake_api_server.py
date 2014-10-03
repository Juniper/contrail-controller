#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import gevent
import socket
import logging
import argparse

#import cgitb
#cgitb.enable(format='text')
from flexmock import flexmock, Mock

import sys
sys.path.append("../common/tests")
from test_utils import *
import test_common

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


def parse_args(args_str):
        '''
        Eg. python fake_api_server.py --listen_ip 1.2.3.4 --listen_port 10882
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'listen_ip': '0.0.0.0',
            'listen_port': '10882',
            }
        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read([args.conf_file])
            defaults.update(dict(config.items("DEFAULTS")))

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
        parser.set_defaults(**defaults)

        parser.add_argument("--listen_ip",
                            help="IP address to serve requests on")
        parser.add_argument("--listen_port",
                            help="Port to serve requests on")

        ret_args = parser.parse_args(remaining_argv)
        ret_args.conf_file = args.conf_file

        return ret_args
#end parse_args


def main(args_str=None):
    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)

    test_common.setup_flexmock()
    if not args.listen_ip:
        api_server_ip = socket.gethostbyname(socket.gethostname())
    else:
        api_server_ip = args.listen_ip

    if not args.listen_port:
        api_server_port = get_free_port()
    else:
        api_server_port = args.listen_port
    http_server_port = get_free_port()

    api_srv = gevent.spawn(test_common.launch_api_server, api_server_ip,
                           api_server_port, http_server_port)
    gevent.joinall([api_srv])

# end main

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    try:
        main()
    except Exception as e:
        print "Exception %s" % (str(e))
