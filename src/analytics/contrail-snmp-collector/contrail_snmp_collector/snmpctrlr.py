#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
from gevent.queue import Queue as GQueue
import os, json, sys, subprocess, time, gevent, socket
from tempfile import NamedTemporaryFile, mkdtemp
import cPickle as pickle
from snmpuve import SnmpUve
import struct, hashlib
from libpartition.libpartition import PartitionClient

class Controller(object):
    def __init__(self, config):
        self._config = config
        self._me = socket.gethostname() + ':' + str(os.getpid())
        self.uve = SnmpUve(self._config)
        self._logger = self.uve.logger()
        self.sleep_time()
        self._keep_running = True
        self.last = set()
        self.bucketsize = 47
        self._config.set_cb(self.notify)

    def notify(self, svc, msg='', up=True, servers=''):
        self.uve.conn_state_notify(svc, msg, up, servers)

    def stop(self):
        self._keep_running = False

    def sleep_time(self, newtime=None):
        if newtime:
            self._sleep_time = newtime
        else:
            self._sleep_time = self._config.frequency()
        return self._sleep_time

    def device2partition(self, key):
        return struct.unpack('H', hashlib.md5(key).digest(
                    )[-2:])[0] % self.bucketsize

    def get_devices_inuse(self):
        d = []
        for i in self._dscvrd_workers:
            if i['name'] != self._me:
                d += i['devices'].split(', ')
        return d

    def get_net_devices(self, pc):
        devs = set()
        inuse = self.get_devices_inuse()
        for dev in self._config.devices():
            if dev.name not in inuse:
                part = self.device2partition(dev.name)
                if pc.own_partition(part):
                    devs.add(dev)
        return list(devs)

    def do_work(self, i, devices):
        self._logger.debug('@do_work(%d):started...' % i)
        if devices:
            cdir = mkdtemp()
            input_file = os.path.join(cdir, 'in.data')
            output_file = os.path.join(cdir, 'out.data')
            with open(input_file, 'wb') as f:
                pickle.dump(dict(out=output_file,
                            netdev=devices,
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
            current = set(d.keys())
            for dev in (self.last - current):
                self.uve.delete(dev)
            self.last = current
        self._logger.debug('@do_work(%d):Done!' % i)

    def workers(self):
        try:
            a = self._config._disc.subscribe(self._config._name, 0)
            gevent.sleep(0)
            self._dscvrd_workers = a.read()
            self._logger.debug('@workers(discovery):%s' % (' '.join(
                        map(lambda x: x['name'], self._dscvrd_workers))))
            return set(map(lambda x: x['name'], self._dscvrd_workers))
        except Exception as e:
            import traceback; traceback.print_exc()
            self._logger.exception('@workers(discovery):%s\n%s' % (str(
                        e), str(dir(e))))
            self._dscvrd_workers = []
            return set([])

    def notify_hndlr(self, p):
        self._logger.debug('@notify_hndlr: New partition %s' % str(p))

    def run(self):
        i = 0
        self._config._disc.publish(self._config._name, dict(name=self._me,
                devices=', '.join(self.last), time='0'))
        w = self.workers()
        gevent.sleep(1)
        pc = PartitionClient(self._config._name, self._me, list(w),
                self.bucketsize, self.notify_hndlr,
                self._config.zookeeper_server())
        while self._keep_running:
            w_ = self.workers()
            if w == w_:
                i += 1
                self.do_work(i, self.get_net_devices(pc))
                t = self._sleep_time
            else:
                try:
                    pc.update_cluster_list(list(w_))
                    self._logger.debug('@run(libpartition): updated %s' % (
                                str(w_)))
                    w = w_
                    t = 1
                except Exception as e:
                    import traceback; traceback.print_exc()
                    self._logger.exception('@run(libpartition):%s' % str(e))
            self._config._disc.publish(self._config._name,
                        dict(name=self._me,
                            devices=', '.join(self.last),
                            time='0'))
            gevent.sleep(t)

