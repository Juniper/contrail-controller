#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
from gevent.queue import Queue as GQueue
from gevent.coros import Semaphore
import os, json, sys, subprocess, time, gevent, socket
from tempfile import NamedTemporaryFile, mkdtemp
import cPickle as pickle
from snmpuve import SnmpUve
from opserver.consistent_schdlr import ConsistentScheduler


class Controller(object):
    def __init__(self, config):
        self._config = config
        self._me = socket.gethostname() + ':' + str(os.getpid())
        self.uve = SnmpUve(self._config)
        self._logger = self.uve.logger()
        self.sleep_time()
        self._keep_running = True
        self.last = set()
        self._sem = None
        self._config.set_cb(self.notify)
        self._state = 'full_scan' # replace it w/ fsm
        self._factor = 10 # replace it w/ fsm
        self._curr_idx = 0 # replace it w/ fsm
        self._if_data = None # replace it w/ fsm
        self._if_ccnt = 0 # replace it w/ fsm
        self._if_ccnt_max = 3 # replace it w/ fsm

    def _make_if_cdata(self, data):
        if_cdata = {}
        for dev in data:
            if 'snmp' in data[dev]:
                if 'ifMib' in data[dev]['snmp']:
                    if 'ifTable' in data[dev]['snmp']['ifMib']:
                        if_cdata[dev] = dict(map(lambda x: (
                                x['ifDescr'], x['ifOperStatus']), filter(
                                        lambda x: 'ifOperStatus' in x and \
                                        'ifDescr' in x, data[dev]['snmp'][
                                                'ifMib']['ifTable'])))
        return if_cdata

    def _chk_if_change(self, data):
        if_cdata = self._make_if_cdata(data)
        if if_cdata == self._if_data:
            return False
        self._if_ccnt += 1
        if self._if_ccnt == self._if_ccnt_max:
            self._if_data = if_cdata
            self._if_ccnt = 0
            return True
        return False

    def _extra_call_params(self):
        if self._state != 'full_scan':
            return dict(restrict='ifOperStatus')
        return {}

    def _analyze(self, data):
        chngd = self._chk_if_change(data)
        if self._state != 'full_scan':
            self._curr_idx += 1
            if self._curr_idx == self._factor or chngd:
                self._state = 'full_scan'
            ret = False, self._sleep_time/self._factor
        else:
            self._state = 'fast_scan'
            self._curr_idx = 0
            ret = True, self._sleep_time
        self._logger.debug('@do_work(analyze):State %s(%d)->%s!' % (self._state,
                    len(data), str(ret)))
        return ret

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

    def _setup_io(self):
        cdir = mkdtemp()
        input_file = os.path.join(cdir, 'in.data')
        output_file = os.path.join(cdir, 'out.data')
        return cdir, input_file, output_file

    def _create_input(self, input_file, output_file, devices, i, restrict=None):
        with open(input_file, 'wb') as f:
            data = dict(out=output_file,
                        netdev=devices,
                        instance=i)
            if restrict:
                data['restrict'] = restrict
            pickle.dump(data, f)
            f.flush()

    def _run_scanner(self, input_file, output_file, i):
        proc = subprocess.Popen('contrail-snmp-scanner --input %s' % (
                    input_file), shell=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        o,e = proc.communicate()
        self._logger.debug('@run_scanner(%d): scan done with %d\nstdout:' \
                '\n%s\nstderr:\n%s\n' % (i, proc.returncode, o, e))
        with open(output_file, 'rb') as f:
            d = pickle.load(f)
        self._logger.debug('@run_scanner(%d): loaded %s' % (i, output_file))
        return d

    def _cleanup_io(self, cdir, input_file, output_file):
        os.unlink(input_file)
        os.unlink(output_file)
        os.rmdir(cdir)

    def _send_uve(self, d):
        for dev, data in d.items():
            self.uve.send(data['snmp'])
            self.uve.send_flow_uve({'name': dev,
                'flow_export_source_ip': data['flow_export_source_ip']})
            self.find_fix_name(data['name'], dev)
        self._logger.debug('@send_uve:Processed %d!' % (len(d)))

    def _del_uves(self, l):
        with self._sem:
            for dev in l:
                self.uve.delete(dev)

    def do_work(self, i, devices):
        self._logger.debug('@do_work(%d):started (%d)...' % (i, len(devices)))
        sleep_time = self._sleep_time
        if devices:
            with self._sem:
                self._work_set = devices
                cdir, input_file, output_file = self._setup_io()
                self._create_input(input_file, output_file, devices,
                                   i, **self._extra_call_params())
                data = self._run_scanner(input_file, output_file, i)
                self._cleanup_io(cdir, input_file, output_file)
                do_send, sleep_time = self._analyze(data)
                if do_send:
                    self._send_uve(data)
                    gevent.sleep(0)
                del self._work_set
        self._logger.debug('@do_work(%d):Processed %d!' % (i, len(devices)))
        return sleep_time

    def find_fix_name(self, cfg_name, snmp_name):
        if snmp_name != cfg_name:
            self._logger.debug('@find_fix_name: snmp name %s differs from ' \
                    'configured name %s, fixed for this run' % (
                            snmp_name, cfg_name))
            for d in self._work_set:
                if d.name == cfg_name:
                    d.name = snmp_name
                    return

    def run(self):
        i = 0
        self._sem = Semaphore()
        constnt_schdlr = ConsistentScheduler(
                            self._config._name,
                            zookeeper=self._config.zookeeper_server(),
                            delete_hndlr=self._del_uves,
                            logger=self._logger)
        while self._keep_running:
            self._logger.debug('@run: ittr(%d)' % i)
            if constnt_schdlr.schedule(self._config.devices()):
                sleep_time = self.do_work(i, constnt_schdlr.work_items())
                self._logger.debug('done work %s' % str(
                            map(lambda x: x.name,
                            constnt_schdlr.work_items())))
                i += 1
                gevent.sleep(sleep_time)
            else:
                gevent.sleep(1)
        constnt_schdlr.finish()
