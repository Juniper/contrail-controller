#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import argparse, os, sys
from snmp import SnmpSession
import gevent
from gevent.queue import Queue as GQueue
import cPickle as pickle

class Controller(object):
    def __init__(self, config):
        self._cfg = config
        d = pickle.load(self._cfg.input)
        self.out_file = d['out']
        self.netdevices = d['netdev']
        self.instance = d['instance']
        self.restrict = False
        if 'restrict' in d and 'ifOperStatus' in d['restrict']:
            self.restrict = True
        self._cfg.input.close()

    def task(self, netdev, sessions):
        print('@task(%d):started...' % self.instance)
        ses = SnmpSession(netdev)
        if self.restrict:
            data = dict(name=netdev.name, ifOperStatus=ses.get_if_status())
            gevent.sleep(0)
        else:
            ses.scan_device()
            gevent.sleep(0)
            data = ses.get_data()
        if data['name'] != '__no_resp__':
            # Replace snmp name with config name
            data['name'] = netdev.name
            sessions.put_nowait({data['name']: {
                'name': netdev.name,
                'snmp': data,
                'flow_export_source_ip': netdev.get_flow_export_source_ip(),
                                           },
                            })
        print('@task(%d):%s done!' % (self.instance, data['name']))

    def poll(self):
        print ('@poll(%d):started...' % self.instance)
        print ('@poll(%d):creating GQueue.Queue...' % self.instance)
        sessions = GQueue()
        print ('@poll(%d):creating Thread...' % self.instance)
        threads = [gevent.spawn(self.task, netdev,
                sessions) for netdev in self.netdevices]
        gevent.sleep(0)
        gevent.joinall(threads)
        data = {}
        while not sessions.empty():
            data.update(sessions.get())
        self.write(data)
        print ('@poll(%d):Done!' % self.instance)

    def run(self):
        self.poll()

    def write(self, data):
        with open(self.out_file, 'wb') as f:
            pickle.dump(data, f)
            f.flush()

def setup_controller(argv):
    parser = argparse.ArgumentParser(description='Use netsnmp to get MIBs')
    parser.add_argument("--input", required=True, type=argparse.FileType(
            'rb'), help="Filename of binary config")
    return Controller(parser.parse_args(argv.split()))

def main(args=None):
    controller = setup_controller(args or ' '.join(sys.argv[1:]))
    controller.run()

if __name__ == '__main__':
    main()
