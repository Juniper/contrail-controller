#!/usr/bin/python
import logging
import argparse
import traceback
import sys


class FilterModule(object):
    @staticmethod
    def _init_logging():
        logger = logging.getLogger('WriteToFileFilter')
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.INFO)

        formatter = logging.Formatter(
            '%(asctime)s %(levelname)-8s %(message)s',
            datefmt='%Y/%m/%d %H:%M:%S'
        )
        console_handler.setFormatter(formatter)
        logger.addHandler(console_handler)

        return logger
    # end _init_logging

    def __init__(self):
        self._logger = FilterModule._init_logging()
    # end __init__

    def filters(self):
        return {
            'write_to_file': self.write_to_file,
        }

    def write_to_file(self, exec_id, line_in_file):

        write_to_file_log = "\n"

        try:
            write_to_file_log = "Attempting to create or open file.. \n"
            with open("/tmp/"+exec_id, "a") as f:
                write_to_file_log += "Opened file in /tmp ... \n"
                f.write(line_in_file + '\n')
                write_to_file_log += "Written line %s to the /tmp/exec-id" \
                                     " file \n" % line_in_file

            return {
                    'status': 'success',
                    'write_to_file_log': write_to_file_log
            }
        except Exception as ex:
            self._logger.info(write_to_file_log)
            self._logger.error(str(ex))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'write_to_file_log': write_to_file_log
            }


def _parse_args():
    parser = argparse.ArgumentParser(description='fabric filters tests')
    parser.add_argument('-w', '--write_to_file',
                        action='store_true',
                        help='write a line to a file')
    return parser.parse_args()
# end _parse_args


if __name__ == '__main__':

    results = None
    fabric_filter = FilterModule()
    parser = _parse_args()
    if parser.write_to_file:
        results = fabric_filter.write_to_file(
            "sample_exec_id_filename",
            "This is a random line to be written to the file")
    print results
