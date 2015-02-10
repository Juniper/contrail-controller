#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
from snmp import SnmpSession
from snmpuve import SnmpUve
import time
import gevent
from gevent.queue import Queue as GQueue
import os, json, sys
from tempfile import NamedTemporaryFile
from multiprocessing import Process
from multiprocessing import Queue as PQueue

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

    def task(self, netdev, sessions, i):
        self._logger.debug('@task(%d):started...' % i)
        ses = SnmpSession(netdev)
        ses.scan_device()
        gevent.sleep(0)
        data = ses.get_data()
        sessions.put_nowait({data['name']: {
                'snmp': data,
                'flow_export_source_ip': netdev.get_flow_export_source_ip(),
                                           },
                            })
        self._logger.debug('@task(%d):%s done!' % (i, data['name']))

    def poll(self, f, i):
        self._logger.debug('@poll(%d):started...' % i)
        self.uve.killall()
        self._logger.debug('@poll(%d):creating GQueue.Queue...' % i)
        sessions = GQueue()
        self._logger.debug('@poll(%d):creating Thread...' % i)
        threads = [gevent.spawn(self.task, netdev, sessions,
                i) for netdev in self._config.devices()]
        gevent.sleep(0)
        gevent.joinall(threads)
        data = {}
        while not sessions.empty():
            data.update(sessions.get())
        f.put_nowait(data)
        self._logger.debug('@poll(%d):Done!' % i)
        sys.exit(0)

    def do_work(self, i):
        self._logger.debug('@do_work(%d):started...' % i)
        f = PQueue()
        p = Process(target=self.poll, args=(f, i))
        p.start()
        d = f.get() #blocks!!
        p.join()
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
