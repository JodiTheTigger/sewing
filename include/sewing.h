/*
Copyright (c) 2016 Richard Maxwell

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef SEWING_H
#define SEWING_H

#include <stddef.h>
// size_t

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------

#if (__linux__ || __APPLE__)

#include <pthread.h>
typedef pthread_t Sew_Thread;

#elif defined(_WIN32)

#include <Windows.h>

typedef struct Sew_Thread
{
    HANDLE handle;
    DWORD  id;
}
Sew_Thread;

#else

#error Platform not supported, sorry.

#endif


// -----------------------------------------------------------------------------

struct Sewing;

typedef void*  Sew_Chain;
typedef void*  Sew_Procedure_Argument;
typedef void (*Sew_Procedure)(Sew_Procedure_Argument);

typedef void (*Sew_Procedure_Main)
(
      struct Sewing*         sewing
    , Sew_Thread*            threads
    , size_t                 thread_count
    , Sew_Procedure_Argument procedure_argument
);

typedef struct Sew_Stitch
{
    Sew_Procedure          procedure;
    Sew_Procedure_Argument argument;
    const char*            name;
}
Sew_Stitch;

// -----------------------------------------------------------------------------

//! Run 'stitch_count' amount of jobs (passed in the flat array 'stitches'),
//! and wait for all of them to finish.
//!
//! This will block until all items have been queued in the job queue.
//!
//! 'sewing' is the opaque pointer passed into the call to sew_it's
//! 'main_procedure' procedure.
void sew_stitches_and_wait
(
      struct Sewing* sewing
    , Sew_Stitch*    stitches
    , size_t         stitch_count
);

// -----------------------------------------------------------------------------

//! Run 'stitch_count' amount of jobs (passed in the flat array 'stithces'), and
//! if chain is non-null, then set chain to a value that can be used to wait for
//! the jobs in a call to 'sew_wait'.
//!
//! This will block until all items have been queued in the job queue.
//!
//! 'sewing' is the opaque pointer passed into the call to sew_it's
//! 'main_procedure' procedure.
void sew_stitches
(
      struct Sewing* sewing
    , Sew_Stitch*    stitches
    , size_t         stitch_count
    , Sew_Chain*     chain
);

//! If chain is non-null, then wait for all the jobs attached to chain have
//! finished running.
//!
//! If chain is null, then the call may or may not yield to the job system
//! before returning.
void sew_wait(struct Sewing* sewing, Sew_Chain chain);

// -----------------------------------------------------------------------------

//! Used to get a chain that's not tied to any stitches. The chain is marked as
//! "in progress", and will block any call to sew_wait until it is marked
//! as finished.
void sew_external(struct Sewing* sewing, Sew_Chain* chain);

//! Used to mark a chain taken from sew_external as finished.
//
//! Once called, the chain is now invalid, and calling sew_external_finished
//! again with this invalid chain can result in undefined behaviour.
//!
//! WARNING: Do not pass in a chain from sew_stitches, as it will result in
//! undefined behaviour (fiber leaks, unrelated jobs finishing, etcetc)
void sew_external_finished(Sew_Chain chain);

// -----------------------------------------------------------------------------

//! Dual use:
//! 1) Entry point to the sewing system.
//! 2) Get the amount of memory required for sewing_memory (in bytes)
//!
//! When passed with a non-null value of 'sew_memory', then it will initilise
//! its internal state using the memory pointed to by 'sewing_memory'. It will
//! create 'fiber_count' fibers, using a stack of 'stack_bytes'. It will create
//! a threadsafe job queue with a size of (1 << log_2_job_count) jobs.
//! It will then create and initilise 'thread_count- 1' threads before calling
//! 'main_procedure' with the arguments of 'sewing', 'threads', 'thread_count'
//! and 'main_procedure_argument'. Where 'sewing' is an opaque pointer to the
//! sewing system required by any call to the sewing system. When the function
//! returns then 'sewing_memory' may be deallocated. Returns the value of 0.
//!
//! If 'sew_memory' is NULL, then sew_it will return the amount of bytes needed
//! for its internal state using 'thread_count' threads, 'fiber_count' fibers
//! using a stack size of 'stack_bytes', and a job queue of
//! (1 << log_2_job_count) jobs. Create a buffer of that size and pass that into
//! sew_it to start the sewing system.
size_t sew_it
(
      void*                  sewing_memory
    , size_t                 stack_bytes
    , size_t                 thread_count
    , size_t                 log_2_job_count
    , size_t                 fiber_count
    , Sew_Procedure_Main     main_procedure
    , Sew_Procedure_Argument main_procedure_argument
);

// -----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

// -----------------------------------------------------------------------------

#endif

