import unittest
import time
import signal
import sys
import os
import tempfile


class TestChildMonitor(unittest.TestCase):
    import opencontrail_vrouter_netns.linux.child_monitor as cm
    import opencontrail_vrouter_netns.linux.utils as utils
    import shlex

    def _pidof(self):
        out = self.utils.execute(self.shlex.split("pidof python"))
        self.assertTrue(out)
        out = self.shlex.split(out)
        cm_pid = this_pid = None
        for pid in out:
            with open("/proc/%s/cmdline" % pid, "r") as fd:
                cont = "".join(fd.read().split("\0"))
                cm = "".join(self._cmd.split())
                if cm == cont:
                    cm_pid = pid
                if "python%s" % __file__ == cont:
                    this_pid = pid

                if cm_pid and this_pid:
                    return (cm_pid, this_pid)

        return (None, None)

    def setUp(self):
        self.pid_cm = None
        self.pid_this = None
        self.fifo_name = os.path.join(tempfile.mkdtemp(), "fifo")
        os.mkfifo(self.fifo_name)
        self._cmd = "python %(child_monitor)s python %(this_process)s" % {
            "child_monitor": self.cm.__file__,
            "this_process": __file__
        }

    def tearDown(self):

        if self.pid_this:
            with open(self.fifo_name, "r") as fd:
                cont = fd.read()
            os.kill(int(self.pid_this), 10);

        time.sleep(0.1)
        (cmdp, thisp) = self._pidof()
        self.assertTrue(not cmdp and not thisp)

    def test_launch(self):
        ret = self.utils.execute(self.shlex.split(self._cmd),
                                 addl_env={"FIFO_NAME": self.fifo_name})
        self.assertEqual(ret, '')
        (self.pid_cm, self.pid_this) = self._pidof()
        self.assertTrue(self.pid_cm and self.pid_this)
        with open("/proc/%s/stat" % self.pid_this, "r") as fd:
            self.assertEqual(self.pid_cm, self.shlex.split(fd.read())[3])

    def test_restart(self):
        self.test_launch()
        with open(self.fifo_name, "r") as fd:
            cont = fd.read()

        # kill with another signal, and check if the child is restart or not
        os.kill(int(self.pid_this), 9)
        time.sleep(0.1)
        (_cm, _this) = self._pidof()
        self.assertEqual(_cm, self.pid_cm)
        self.assertNotEqual(_this, self.pid_this)
        self.pid_this = _this


def sig10handler(signum, frame):
    sys.exit(0)

if __name__ == "__main__":
    signal.signal(10, sig10handler)
    with open(os.environ['FIFO_NAME'], "w") as fd:
        fd.write("1")

    while True:
        time.sleep(10)
