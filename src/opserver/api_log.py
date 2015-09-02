import argparse
import commands

class LogQuerier(object):

    def __init__(self):
        self._args = None

    def parse_args(self):
        parser = argparse.ArgumentParser(
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.add_argument(
            "--start-time", help="Logs start time (format now-10m, now-1h)",
            default = "now-10m")
        parser.add_argument("--end-time", help="Logs end time",
            default = "now")
        parser.add_argument("--object-type", help="object-type")
        parser.add_argument("--identifier-name", help="identifier-name")
        self._args = parser.parse_args()
        return 0

    def validate_query(self):
        if self._args.identifier_name is not None and self._args.object_type is None:
            print "object-type is required for identifier-name"
            return None
        return True

    def query(self):
        start_time, end_time = self._args.start_time, self._args.end_time
        options = ""
        if self._args.object_type is not None:
            options += " --object-id " + self._args.object_type
            if self._args.identifier_name is not None:
                options += ":" + self._args.identifier_name
            else:
                options += ":*"
        command_str = ("contrail-logs --object-type config" +
            " --start-time " + str(start_time) +
            " --end-time " + str(end_time) +
            options)
        res = commands.getoutput(command_str)
        print res

def main():
    try:
        querier = LogQuerier()
        if querier.parse_args() != 0:
            return
        if querier.validate_query() is None:
            return
        querier.query()
    except KeyboardInterrupt:
        return

if __name__ == "__main__":
    main()
