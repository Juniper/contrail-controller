/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate_io_context.h"

void KStateIoContext::Handler() {

    SandeshContext *ctx = GetSandeshContext();
    KState *kctx = static_cast<KState *>(ctx);

    kctx->Handler();

    if (kctx->more_context() == NULL)
        delete kctx;
}

void KStateIoContext::ErrorHandler(int err) {
    LOG(ERROR, "Error reading kstate. Error <" << err << ": " 
        << strerror(err) << ": Sequnce No : " << GetSeqno());
}

