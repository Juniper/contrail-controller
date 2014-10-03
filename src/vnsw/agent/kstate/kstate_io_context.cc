/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <kstate/kstate.h>
#include "kstate_io_context.h"
#include "ksync/ksync_types.h"

void KStateIoContext::Handler() {

    SandeshContext *ctx = GetSandeshContext();
    KState *kctx = static_cast<KState *>(ctx);

    kctx->Handler();

    if (kctx->more_context() == NULL)
        delete kctx;
}

void KStateIoContext::ErrorHandler(int err) {
    KSYNC_ERROR(VRouterError, "VRouter query operation failed. Error <", err,
                ":", strerror(err), ">. Object <", "N/A", ">. State <", "N/A",
                ">. Message number :", GetSeqno());
    LOG(ERROR, "Error reading kstate. Error <" << err << ": " 
        << strerror(err) << ": Sequence No : " << GetSeqno());

}

