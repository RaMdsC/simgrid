/* Copyright (c) 2009-2010, 2012-2014. The SimGrid Team.
 * All rights reserved.                                                     */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#ifndef SMPI_OP_HPP
#define SMPI_OP_HPP

#include <xbt/base.h>

#include "private.h"

namespace simgrid{
namespace smpi{

class Op {
  private:
    MPI_User_function *func_;
    bool is_commutative_;
    bool is_fortran_op_;
  public:
    Op(MPI_User_function * function, bool commutative);
    bool is_commutative();
    bool is_fortran_op();
    void set_fortran_op();
    void apply(void *invec, void *inoutvec, int *len, MPI_Datatype datatype);
};

}
}

#endif