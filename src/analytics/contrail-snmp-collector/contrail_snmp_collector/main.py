from gevent import monkey
monkey.patch_all()

from snmpcfg import CfgParser
from snmpctrlr import Controller
import sys

def setup_controller(argv):
    config = CfgParser(argv)
    config.parse()
    return Controller(config)

def emain(args=None):
    controller = setup_controller(args or ' '.join(sys.argv[1:]))
    controller.run()

if __name__ == '__main__':
    emain()
