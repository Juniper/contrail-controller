from gevent import monkey
monkey.patch_all()

from config import CfgParser
from controller import Controller
import sys

def setup_controller(argv):
    config = CfgParser(argv)
    config.parse()
    return Controller(config)

def main(args=None):
    controller = setup_controller(args or ' '.join(sys.argv[1:]))
    controller.run()

if __name__ == '__main__':
    main()
