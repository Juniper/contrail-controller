
JOB_TEMPLATE_MISSING = "job_template_id is missing in the job input"

JOB_EXECUTION_ID_MISSING = "job_execution_id is missing in the job input"

JOB_SUMMARY_MESSAGE_HDR = "Job summary: \n"

JOB_RESULT_STATUS_NONE = "Error in getting the job completion " \
                         "status after job execution. \n"

JOB_MULTI_DEVICE_FAILED_MESSAGE_HDR = "Job failed with for devices: "

JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR = "Job failed. \n"

PLAYBOOK_RESULTS_MESSAGE = "Detailed job results: \n"

PLAYBOOK_EXIT_WITH_ERROR = "Playbook exited with error."

PLAYBOOK_RETURN_WITH_ERROR = "Playbook returned with error"

NO_PLAYBOOK_INPUT_DATA = "Playbook input data is not passed. "\
                         "Aborting execution."

SANDESH_INITIALIZATION_TIMEOUT_ERROR = "Sandesh initialization "\
                                       "timeout after 15s"

INPUT_SCHEMA_INPUT_NOT_FOUND = "Required: input paramater in execute-job"

DEVICE_JSON_NOT_FOUND = "No Device details found for any device"


def get_no_device_data_error_message(device_id):
    return "Device details for the device '%s' not found" \
           % device_id


def get_missing_credentials_error_message(device_id):
    return "Discovered device '%s' does not have credentials" \
           % device_id


def get_validate_input_schema_error_message(job_template_id, exc_obj):
    return "Error while validating input schema for job template '%s'"\
                  " : %s" % (job_template_id, exc_obj.message)


def get_send_job_log_error_message(job_template_fqname,
                                   job_execution_id, exc_obj):
    return "Error while creating the job log for job template " \
           "'%s' and execution id '%s' : %s" % (job_template_fqname,
                                                job_execution_id,
                                                repr(exc_obj))


def get_send_job_exc_uve_error_message(job_template_fqname,
                                       job_execution_id, exc_obj):
    return "Error while sending the job execution UVE for job " \
           "template '%s' and execution id '%s' : %s" % \
            (job_template_fqname, job_execution_id, repr(exc_obj))


def get_playbook_input_parsing_error_message(exc_obj):
    return "Exiting due playbook input parsing error: %s" % repr(exc_obj)


def get_playbook_execute_exception_message(exc_obj):
    return "Exception in playbook process : %s " % repr(exc_obj)


def get_create_job_summary_error_message(exc_obj):
    return "Error while generating the job summary " \
           "message : %s" % repr(exc_obj)


def get_no_dev_fmly_no_vendor_error_message(device_id):
    return "device_vendor or device_family not found for '%s'"\
           % device_id


def get_job_template_read_error_message(job_template_id):
    return "Error while reading the job template '%s' from " \
                  "database" % job_template_id


def get_playbook_info_error_message(job_template_id, exc_obj):
    return "Error while getting the playbook information from the " \
                  "job template '%s' : %s" % (job_template_id, repr(exc_obj))


def get_pb_not_found_message(playbook_uri):
    return "Playbook '%s' does not "\
                  "exist" % playbook_uri


def get_playbook_info_device_mismatch_message(device_vendor, device_family):
    return "Playbook info not found in the job template for " \
           "'%s' and '%s'" % (device_vendor, device_family)


def get_run_playbook_process_error_message(playbook_uri, exc_obj):
    return "Exception in creating a playbook process " \
           "for '%s' : %s" % (playbook_uri, repr(exc_obj))


def get_run_playbook_error_message(playbook_uri, exc_obj):
    return "Error while executing the playbook '%s' : %s" % \
                  (playbook_uri, repr(exc_obj))


def get_send_prouter_object_log_error_message(
        name, job_execution_id, e):
    return "Error while creating prouter object log for router " \
           "'%s' and execution id '%s' : %s" % (name,
                                                job_execution_id,
                                                repr(e))
