/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef QE_SANDESH_H_
#define QE_SANDESH_H_

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

struct QESandeshContext : public SandeshContext {
    QESandeshContext(QueryEngine *qe) :
        SandeshContext(),
        qe_(qe) {}
    QueryEngine *QE() { return qe_;}
    QueryEngine *qe_;
};

#endif /* QE_SANDESH_H_ */
