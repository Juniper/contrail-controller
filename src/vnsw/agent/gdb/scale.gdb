define pksync_sock
    print KSyncSock::sock_
end

define pproto
    print /x Agent::singleton_->pkt_->pkt_handler_.px->proto_list_
    print KSyncSock::sock_
end

define scale_queue
   set $__wq=scale_queue_->queue_
   printf "Scale Queue        : %6d %6d %6d\n", $__wq.enqueues_, $__wq.task_starts_, $__wq.max_queue_len_
   set $__wq=scale_queue_->shared_queue_
   printf "Shared Scale Queue : %6d %6d %6d\n", $__wq.enqueues_, $__wq.task_starts_, $__wq.max_queue_len_
   printf "Count              : %6d\n", scale_queue_->count_
   printf "Schedule           : %6d\n", scale_queue_->schedule_
   printf "Delay              : %6d\n", scale_queue_->delay_
   printf "PktHandler delay   : %6d\n", pkt_time_delay_
   printf "PktHandler count   : %6d\n", pkt_time_count_
end

define pqs
    if $argc != 2
       help dump_uc_v4_route_entries
    else
       set $__flow_table = Agent::singleton_->pkt_->flow_table_.px
       set $__pkt = Agent::singleton_->pkt_
       set $__flow_mgmt = $__pkt->flow_mgmt_manager_.px
       set $__wq=((Proto *) $arg0)->work_queue_
       set $__pwq=$__wq
       printf "PktFlow Queue Starts/Max Len       : %d %d\n", $__wq.task_starts_, $__wq.max_queue_len_
       set $__wq = $__flow_table->request_queue_
       printf "FlowTable Queue Starts/Max Len     : %d %d\n", $__wq.task_starts_, $__wq.max_queue_len_
       set $__ksync_sock=((KSyncSock *) $arg1)
       set $__wq = $__ksync_sock->async_send_queue_
       printf "KSyncSend Queue Starts/Max Len     : %d %d\n", $__wq.task_starts_, $__wq.max_queue_len_
       set $__wq = $__ksync_sock->receive_work_queue[0]
       printf "KSyncRecv Queue Starts/Max Len     : %d %d\n", $__wq.task_starts_, $__wq.max_queue_len_
       set $__wq = $__flow_mgmt->request_queue_
       printf "FlowMgmtReq Queue Starts/Max Len   : %d %d\n", $__wq.task_starts_, $__wq.max_queue_len_
       set $__wq = $__flow_mgmt->response_queue_
       printf "FlowMgmtResp Queue Starts/Max Len  : %d %d\n", $__wq.task_starts_, $__wq.max_queue_len_
       #printf "FlowTable Delay                    : %d\n", flow_table_time_delay_
       #printf "Pkt Delay                          : %d\n", pkt_time_delay_
       #printf "Flow Handler Delay                 : %d\n", flow_handler_time_delay_
    end
end

document pqs
     Prints work-queue enqueues
     Syntax: dump_uc_v4_route_entries <table>: Prints all route entries in UC v4 route table
end


