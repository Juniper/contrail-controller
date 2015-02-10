#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
from gevent.queue import Queue as GQueue
import os, json, sys, subprocess, time, gevent
from tempfile import NamedTemporaryFile, mkdtemp
import cPickle as pickle
from snmpuve import SnmpUve

class Controller(object):
    def __init__(self, config):
        self._config = config
        self.uve = SnmpUve(self._config)
        self._logger = self.uve.logger()
        self.sleep_time()
        self._keep_running = True

    def stop(self):
        self._keep_running = False

    def sleep_time(self, newtime=None):
        if newtime:
            self._sleep_time = newtime
        else:
            self._sleep_time = self._config.frequency()
        return self._sleep_time

    def get_net_devices(self):
        return list(self._config.devices())

    def do_work(self, i):
        self._logger.debug('@do_work(%d):started...' % i)
        cdir = mkdtemp()
        input_file = os.path.join(cdir, 'in.data')
        output_file = os.path.join(cdir, 'out.data')
        with open(input_file, 'wb') as f:
            pickle.dump(dict(out=output_file,
                        netdev=self.get_net_devices(),
                        instance=i), f)
            f.flush()
        proc = subprocess.Popen('contrail-snmp-scanner --input %s' % (
                    input_file), shell=True, 
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        o,e = proc.communicate()
        self._logger.debug('@do_work(%d): scan done with %d\nstdout:' \
                '\n%s\nstderr:\n%s\n' % (i, proc.returncode, o, e))
        with open(output_file, 'rb') as f:
            d = pickle.load(f)
        os.unlink(input_file)
        os.unlink(output_file)
        os.rmdir(cdir)
        for dev, data in d.items():
            self.uve.send(data['snmp'])
            self.uve.send_flow_uve({'name': dev,
                'flow_export_source_ip': data['flow_export_source_ip']})
        self._logger.debug('@do_work(%d):Done!' % i)

    def run(self):
        i = 0
        while self._keep_running:
            self.do_work(i)
            gevent.sleep(self._sleep_time)
            i += 1
