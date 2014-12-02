from snmp import SnmpSession
from snmpuve import SnmpUve
import time
import gevent
import os, json, sys
from tempfile import NamedTemporaryFile

class Controller(object):
    def __init__(self, config):
        self._config = config
        self.uve = SnmpUve(self._config)
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

    def task(self, netdev):
        f = NamedTemporaryFile(mode='w+', delete=False)
        pid = os.fork()
        if pid == 0:
            ses = SnmpSession(netdev)
            ses.scan_device()
            json.dump(ses.get_data(), f)
            f.flush()
            f.close()
            sys.exit(0)
        pid, status = os.waitpid(pid, 0)
        f.seek(0, 0)
        data = json.load(f)
        self.uve.send(data)
        f.close()
        os.unlink(f.name)
        if netdev.get_snmp_name() is None:
            netdev.set_snmp_name(data['name'])
            self.uve.send_flow_uve({'name': netdev.get_snmp_name(),
                'flow_export_source_ip': netdev.get_flow_export_source_ip()})

    def run(self):
        while self._keep_running:
            for netdev in self._config.devices():
                self.task(netdev)
                gevent.sleep(0)
            gevent.sleep(self._sleep_time)
