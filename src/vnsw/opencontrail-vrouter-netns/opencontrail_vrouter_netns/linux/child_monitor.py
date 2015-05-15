import sys
import subprocess
import os


def daemonize():
    try:
        pid = os.fork()
        if pid > 0:
            sys.exit(0)
    except OSError, e:
        sys.exit(1)

    os.chdir("/")
    os.setsid()
    os.umask(0)

    try:
        pid = os.fork()
        if pid > 0:
            sys.exit(0)
    except OSError, e:
        sys.exit(1)


def main():
    # close all the std fd
    os.close(0)
    os.close(1)
    os.close(2)
    daemonize()
    new_cmd = sys.argv[1:]
    dev_null = open("/dev/null", "rw")
    while True:
        obj = subprocess.Popen(new_cmd,
                               stdout=dev_null,
                               stderr=dev_null,
                               shell=False)
        try:
            return_code = obj.wait()
        except Exception as e:
            pass
        if return_code == 0:
            break
    sys.exit(0)

if __name__ == '__main__':
    main()
