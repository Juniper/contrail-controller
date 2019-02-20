import traceback
import json
import ast
import time
import gevent
import argparse
import time
import re

def _extract_marked_json(marked_output):
    retval = {}
    for marker, output in marked_output.iteritems():
        start = output.index(marker) + len(marker)
        end = output.rindex(marker)
        if start > end:
            # print("Invalid marked output")
            continue
        json_str = output[start:end]
        # print("Extracted marked output")
        try:
            retval[marker] = json.loads(json_str)
        except ValueError as e:
            # assuming failure is due to unicoding in the json string
            retval[marker] = ast.literal_eval(json_str)

    return retval
# end _extract_marked_json

def process_file_and_get_marked_output(unique_pb_id,
                                           exec_id):
    f_read = None
    marked_output = {}
    total_output = ''
    markers = [
        JobFileWrite.PLAYBOOK_OUTPUT,
        JobFileWrite.JOB_PROGRESS,
        JobFileWrite.JOB_LOG,
        JobFileWrite.PROUTER_LOG
    ]
    # read file as long as it is not a time out
    # Adding a residue timeout of 5 mins in case
    # the playbook dumps all the output towards the
    # end and it takes sometime to process and read it
    file_read_timeout = 3000
    # initialize it to the time when the subprocess
    # was spawned
    last_read_time = time.time()
    while True:
        try:
            f_read = open("/tmp/test_job_file.txt", "r")
            print("File got created for exec_id %s.. "
                              "proceeding to read contents.." % exec_id)
            current_time = time.time()
            invalid_str_list = []
            while current_time - last_read_time < file_read_timeout:
                # line_read = read_line(f_read, "JOB_LOG##")
                line_read = read_line(f_read, file_read_timeout, last_read_time)
                # line_read = f_read.readline()
                # print("Line read is %s " % line_read)
                if line_read:
                    if unique_pb_id in line_read:
                        total_output =\
                            line_read.split(unique_pb_id)[-1].strip()
                        if total_output == 'END':
                            break
                        if JobFileWrite.JOB_LOG in line_read \
                                or JobFileWrite.PROUTER_LOG in line_read:
                            total_output = line_read.strip()

                        for marker in markers:
                            if marker in total_output:
                                marked_output[marker] = total_output
                                marked_jsons = _extract_marked_json(
                                    marked_output
                                )
                                if marked_jsons == {}:
                                    print("******************* Reproduced issue")
                                    print("%s" % total_output)
                else:
                    # this sleep is essential
                    # to yield the context to
                    # sandesh uve for % update
                    gevent.sleep(0)

                current_time = time.time()
            break
        except IOError as file_not_found_err:
            print("File not yet created for exec_id %s !!" % exec_id)
            # check if the sub-process died but file is not created
            # if yes, it is old-usecase or there were no markers

            # for the case when the sub-process hangs for some
            # reason and does not write to/create file
            if time.time() - last_read_time > file_read_timeout:
                print("Sub process probably hung; "
                                  "stopping file processing ....")
                break
            time.sleep(0.5)
        finally:
            if f_read is not None:
                f_read.close()
                print("File closed successfully!")

    return marked_output
# end process_file_and_get_marked_output

def has_no_markers(total_output, markers):
    for marker in markers:
        if marker in total_output:
            return True
    return False


def is_valid_line(invalid_str_list, marker):
    line = ''.join(invalid_str_list)
    regexp = re.compile(marker+'.*'+marker)
    if regexp.search(line):
        return line
    return None


def read_line(f_read, file_read_timeout, last_read_time):
    str_list = []
    current_time = time.time()
    while True:
        if current_time - last_read_time >= file_read_timeout:
            print("Timed while attempting to read line. The line being read is:\n")
            print("%s" % ''.join(str_list))
            return None
        c = f_read.read(1)
        if c == '\n':
            break
        str_list.append(c)
        current_time = time.time()
    return ''.join(str_list)


class JobFileWrite(object):
    JOB_PROGRESS = 'JOB_PROGRESS##'
    PLAYBOOK_OUTPUT = 'PLAYBOOK_OUTPUT##'
    JOB_LOG = 'JOB_LOG##'
    PROUTER_LOG = 'PROUTER_LOG##'
    GEN_DEV_OP_RES = 'GENERIC_DEVICE##'

    def __init__(self, logger, ):
        self._logger = logger

    def write_to_file(self, pb_id, marker, msg):
        try:
            fname = '/tmp/test_job_file.txt'
            with open(fname, "a") as f:
                line_in_file = "%s%s%s%s\n" % (
                    str(pb_id), marker, msg, marker)
                f.write(line_in_file)
        except Exception as ex:
            print("Failed to write_to_file: %s\n%s\n" % (
                str(ex), traceback.format_exc()
            ))
    def write_chars_to_file(self, pb_id, marker, msg):
        try:
            fname = '/tmp/test_job_file.txt'
            with open(fname, "a") as f:
                line_in_file = "%s%s%s%s\n" % (
                    str(pb_id), marker, msg, marker)
                for c in line_in_file:
                    f.write(c)
        except Exception as ex:
            print("Failed to write_to_file: %s\n%s\n" % (
                str(ex), traceback.format_exc()
            ))


def _parse_args():
    arg_parser = argparse.ArgumentParser(description='test playbook read')
    arg_parser.add_argument('-r', '--reader',
                            action='store_true', help='Start reader')
    arg_parser.add_argument('-w', '--writer',
                            action='store_true', help='Start writer')
    return arg_parser.parse_args()
# end _parse_args

def get_message():
    big_str = "This is a very big test string" * 10000
    sample_json = {"status": "IN_PROGRESS",
            "completion_percent": 6.33,
            "result": None,
            "fabric_fq_name": "default-global-system-config:test-fabric",
            "job_template_fqname": ["default-global-system-config", "existing_fabric_onboard_template"],
            "details": big_str,
            "message": "Created physical interfaces link(s) from juniper device, dhawan:\n   - 8",
            "job_execution_id": "1551172725141_9bcfc93d-5d9e-4c43-9523-2b89384f330a",
            "device_name": "dhawan"}
    str = json.dumps(sample_json)
    return str


def __main__():
    _parse_args()
    parser = _parse_args()
    results = {}
    pb_id = "3b28290c-2a08-4bc0-bbc9-5869f1c4c5ee"
    if parser.reader:
        process_file_and_get_marked_output(pb_id,  "3b28290c-2a08-4bc0-bbc9-5869f1c4c5ef")
    elif parser.writer:
        jfw = JobFileWrite(None)
        while True:
            jfw.write_chars_to_file(pb_id, "JOB_LOG##",get_message())
            time.sleep(1)
        with open('/tmp/test_job_file.txt', "a") as f:
            f.write(pb_id + 'END' + '\n')

# end __main__


if __name__ == '__main__':
    __main__()