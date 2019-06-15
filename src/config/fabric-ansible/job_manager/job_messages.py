class MsgBundle(object):

    JOB_TEMPLATE_MISSING = 1,
    JOB_EXECUTION_ID_MISSING = 2,
    JOB_SUMMARY_MESSAGE_HDR = 3,
    JOB_RESULT_STATUS_NONE = 4,
    JOB_MULTI_DEVICE_FAILED_MESSAGE_HDR = 5,
    JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR = 6,
    PLAYBOOK_RESULTS_MESSAGE = 7,
    PLAYBOOK_EXIT_WITH_ERROR = 8,
    PLAYBOOK_RETURN_WITH_ERROR = 9,
    NO_PLAYBOOK_INPUT_DATA = 10,
    SANDESH_INITIALIZATION_TIMEOUT_ERROR = 11,
    INPUT_SCHEMA_INPUT_NOT_FOUND = 12,
    DEVICE_JSON_NOT_FOUND = 13,
    NO_DEVICE_DATA_FOUND = 14,
    NO_CREDENTIALS_FOUND = 15,
    INVALID_SCHEMA = 16,
    SEND_JOB_LOG_ERROR = 17,
    SEND_JOB_EXC_UVE_ERROR = 18,
    PLAYBOOK_INPUT_PARSING_ERROR = 19,
    PLAYBOOK_EXECUTE_ERROR = 20,
    CREATE_JOB_SUMMARY_ERROR = 21,
    DEVICE_VENDOR_FAMILY_MISSING = 22,
    READ_JOB_TEMPLATE_ERROR = 23,
    GET_PLAYBOOK_INFO_ERROR = 24,
    PLAYBOOK_NOT_FOUND = 25,
    PLAYBOOK_INFO_DEVICE_MISMATCH = 26,
    RUN_PLAYBOOK_PROCESS_ERROR = 27,
    RUN_PLAYBOOK_ERROR = 28,
    SEND_PROUTER_OBJECT_LOG_ERROR = 29,
    CLOSE_SANDESH_EXCEPTION = 30,
    RUN_PLAYBOOK_PROCESS_TIMEOUT = 31,
    PLAYBOOK_EXECUTION_COMPLETE = 32,
    START_JOB_MESSAGE = 33,
    VNC_INITIALIZATION_ERROR = 34,
    JOB_ERROR = 35,
    JOB_EXECUTION_COMPLETE = 36,
    START_EXE_PB_MSG = 37,
    STOP_EXE_PB_MSG = 38,
    JOB_EXC_REC_HDR = 39,
    EXC_JOB_ERR_HDR = 40,
    PLAYBOOK_STATUS_FAILED = 41,
    PLAYBOOK_OUTPUT_MISSING = 42,
    EMPTY_DEVICE_LIST = 43,
    PRODUCT_NAME_MISSING = 44,
    DEVICE_LOCK_FAILURE = 45,
    ZK_INIT_FAILURE = 46,
    RUN_EXECUTABLE_PROCESS_TIMEOUT = 47,
    EXECUTABLE_RETURN_WITH_ERROR = 48,
    _msgs = {
        'en': {
            JOB_TEMPLATE_MISSING: 'job_template_id is missing '
                                  'in the job input',
            JOB_EXECUTION_ID_MISSING: 'job_execution_id is missing'
                                      ' in the job input',
            JOB_SUMMARY_MESSAGE_HDR: 'Job summary: ',
            JOB_RESULT_STATUS_NONE: 'Error in getting the '
                                    'job completion '
                                    'status after job execution. \n',
            JOB_MULTI_DEVICE_FAILED_MESSAGE_HDR: 'Job failed with '
                                                 'for devices: ',
            JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR: 'Job failed. \n',
            PLAYBOOK_RESULTS_MESSAGE: 'Detailed job results: \n',
            PLAYBOOK_EXIT_WITH_ERROR: 'Playbook "{playbook_uri}" exited'
                                      ' with error.',
            PLAYBOOK_RETURN_WITH_ERROR: 'Playbook returned '
                                        'with error',
            PLAYBOOK_STATUS_FAILED: 'Playbook completed with status Failure.',
            PLAYBOOK_OUTPUT_MISSING: 'Playbook completed without sending the'
                                     'output with status details.',
            NO_PLAYBOOK_INPUT_DATA: 'Playbook input data'
                                    ' is not passed. '
                                    'Aborting execution.',
            SANDESH_INITIALIZATION_TIMEOUT_ERROR: 'Sandesh '
                                                  'initialization '
                                                  'timeout after 15s',
            INPUT_SCHEMA_INPUT_NOT_FOUND: 'Required: input paramater'
                                          ' in execute-job',
            DEVICE_JSON_NOT_FOUND: 'No Device details found for'
                                   ' any device',
            NO_DEVICE_DATA_FOUND: 'Device details for the device '
                                  '"{device_id}" not found',
            NO_CREDENTIALS_FOUND: 'Discovered device "{device_id}" '
                                  'does not have credentials',
            INVALID_SCHEMA: 'Error while validating input schema'
                            ' for job template "{job_template_id}" '
                            ': {exc_obj.message}',
            SEND_JOB_LOG_ERROR: 'Error while creating the job'
                                ' log for job template '
                                '"{job_template_fqname}" '
                                'and execution id "{job_execution_id}"'
                                ' : {exc_msg}',
            SEND_JOB_EXC_UVE_ERROR: 'Error while sending the job'
                                    ' execution UVE for job '
                                    'template "{job_template_fqname}"'
                                    ' and execution id '
                                    '"{job_execution_id}" : {exc_msg}',
            PLAYBOOK_INPUT_PARSING_ERROR: 'Exiting due playbook'
                                          ' input parsing error:'
                                          ' {exc_msg}',
            PLAYBOOK_EXECUTE_ERROR: 'Exception in playbook process'
                                    ' for playbook "{playbook_uri}" '
                                    '(exec_id: {execution_id}): {exc_msg} ',
            CREATE_JOB_SUMMARY_ERROR: 'Error while generating the'
                                      ' job summary message'
                                      ' : {exc_msg}',
            DEVICE_VENDOR_FAMILY_MISSING: 'device_vendor or '
                                          'device_family not found'
                                          ' for "{device_id}"',
            PRODUCT_NAME_MISSING: 'device_product name not found '
                                  ' for "{device_id}"',
            READ_JOB_TEMPLATE_ERROR: 'Error while reading the '
                                     'job template "{job_template_id}"'
                                     ' from database',
            GET_PLAYBOOK_INFO_ERROR: 'Error while getting the playbook'
                                     ' information from the job'
                                     ' template "{job_template_id}"'
                                     ' : {exc_msg}',
            PLAYBOOK_NOT_FOUND: 'Playbook "{playbook_uri}" '
                                'does not exist',
            PLAYBOOK_INFO_DEVICE_MISMATCH: 'Playbook info not found'
                                           ' in the job template'
                                           ' for "{device_vendor}"'
                                           ' and "{device_family}"',
            RUN_PLAYBOOK_PROCESS_ERROR: 'Exception in executing '
                                        'the playbook '
                                        'for "{playbook_uri}"'
                                        ' : {exc_msg}',
            RUN_PLAYBOOK_ERROR: 'Error while executing the playbook'
                                ' "{playbook_uri}" : {exc_msg}',
            SEND_PROUTER_OBJECT_LOG_ERROR: 'Error while creating '
                                           'prouter object log'
                                           ' for router '
                                           '"{prouter_fqname}" '
                                           'and execution id '
                                           '"{job_execution_id}"'
                                           ' : {exc_msg}',
            CLOSE_SANDESH_EXCEPTION: 'Error in confirming the'
                                     ' SANDESH message send operation.'
                                     ' The Job Logs might '
                                     'not be complete.',
            RUN_PLAYBOOK_PROCESS_TIMEOUT: 'Timeout while executing'
                                          ' the playbook '
                                          'for "{playbook_uri}" : '
                                          '{exc_msg}. Playbook'
                                          ' process is aborted.',
            PLAYBOOK_EXECUTION_COMPLETE: 'Completed playbook execution'
                                         ' for job template '
                                         '"{job_template_name}" with '
                                         'execution'
                                         ' id "{job_execution_id}"',
            START_JOB_MESSAGE: 'Starting execution for job '
                               'template "{job_template_name}"'
                               ' and execution id "{job_execution_id}"',
            VNC_INITIALIZATION_ERROR: 'Exiting due to vnc api '
                                      'initialization error: {exc_msg}',
            JOB_ERROR: 'Exiting job due to error: {exc_msg} ',
            JOB_EXECUTION_COMPLETE: 'Job execution completed '
                                    'successfully.',
            START_EXE_PB_MSG: 'Starting to execute the '
                              'playbook "{playbook_name}"',
            STOP_EXE_PB_MSG: 'Finished executing the '
                             'playbook "{playbook_name}"',
            JOB_EXC_REC_HDR: 'Job Exception recieved: ',
            EXC_JOB_ERR_HDR: 'Error while executing job ',
            EMPTY_DEVICE_LIST: 'Need to pass a valid device list ',
            DEVICE_LOCK_FAILURE: 'Failed to acquire device level lock ',
            ZK_INIT_FAILURE: 'Failed to initialize zoo keeper client',
            RUN_EXECUTABLE_PROCESS_TIMEOUT: 'Timeout while executing'
                                          ' the playbook for "{exec_path}" : '
                                          '{exc_msg}. Execution is aborted.',
            EXECUTABLE_RETURN_WITH_ERROR: 'Playbook returned with error',
        }
    }

    @classmethod
    def getMessage(cls, msg_id, locale='en', *args, **kwargs):
        if locale not in MsgBundle._msgs:
            return 'Failed to construct job message due to invalid '\
                   'locale: %s' % locale
        if msg_id not in MsgBundle._msgs[locale]:
            return 'Failed to construct job message due to invalid '\
                   'message id: %s' % msg_id
        try:
            return MsgBundle._msgs[locale][msg_id].format(*args, **kwargs)
        except KeyError as ex:
            return 'Failed to construct job message due to missing message '\
                   'arguments: %s' % ex.message
