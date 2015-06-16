#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import gevent
import sys
import select
from supervisor import childutils


class EventListenerProtocolNodeMgr(childutils.EventListenerProtocol):
    def wait(self, stdin=sys.stdin, stdout=sys.stdout):
        self.ready(stdout)
        while 1:
            if select.select([sys.stdin], [], [])[0]:
                line = stdin.readline()
                if line is not None:
                    sys.stderr.write("wokeup and found a line\n")
                    break
                else:
                    sys.stderr.write("wokeup from select just like that\n")
        headers = childutils.get_headers(line)
        payload = stdin.read(int(headers['len']))
        return headers, payload
