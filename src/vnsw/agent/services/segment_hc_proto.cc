
SegmentHCProto::SegmentHCProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, "Agent::Services", PktHandler::DIAG, io),
    msg_(new PktInfo(agent, BFD_TX_BUFF_LEN, PktHandler::DIAG, 0)),
    handler_(agent, msg_, io) {

    // limit the number of entries in the workqueue
    work_queue_.SetSize(agent->params()->services_queue_limit());
    work_queue_.SetBounded(true);

    agent->health_check_table()->RegisterHealthCheckCallback(
        boost::bind(&SegmentHCProto::BfdSessionControl, this, _1, _2));
}

SegmentHCProto::~SegmentHCProto() {
}

ProtoHandler *SegmentHCProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                          boost::asio::io_service &io) {
    return new BfdHandler(agent(), info, io);
}

bool SegmentHCProto::BfdSessionControl(
               HealthCheckTable::HealthCheckServiceAction action,
               HealthCheckInstanceService *service) {

    switch (action) {
        case HealthCheckTable::CREATE_SERVICE:
            {

            }

        case HealthCheckTable::DELETE_SERVICE:
            break;

        case HealthCheckTable::RUN_SERVICE:
            break;

        case HealthCheckTable::STOP_SERVICE:
            break;

        default:
            assert(0);
    }

    return true;
}
