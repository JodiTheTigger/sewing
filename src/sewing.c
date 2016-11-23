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

// TODO:
// -single thread will deadlock if the queue is full - fix it.
// -Remove use of size_t where I can. use unsigned or uint32_t instead.
// -Define hard limits (# of fibers, count, etc).
// -Turn the free and waiting arrays into bitflags, but do a benchmark first.
// -test on windows
// -For loops use unsigned, then do a static assert on max size of
//  unsigned > TASKS_MAX_*
//
// Reading:
// http://www.boost.org/doc/libs/1_61_0/libs/context/doc/html/context/rationale/other_apis_.html
// http://www.1024cores.net/home/lock-free-algorithms/tricks/fibers

// -----------------------------------------------------------------------------
// Common includes
// -----------------------------------------------------------------------------

#include "fcontext.h"

#include <sewing.h>

#include <stdint.h>
// intptr_t, SIZE_MAX


// -----------------------------------------------------------------------------
// Defines
// -----------------------------------------------------------------------------

#if !defined(SEW_ASSERTS)
#define SEW_ASSERTS     1
#endif

#if !defined(SEW_STACK_GUARD)
#define SEW_STACK_GUARD 0
#endif

#define A16(x) (((x) + 15) & ~15)

// -----------------------------------------------------------------------------
// Asserts
// -----------------------------------------------------------------------------

#if (SEW_ASSERTS)

#include <assert.h>
#define SEW_ASSERT(x) assert(x)
#else
#define SEW_ASSERT(x)
#endif

#define SEW_UNREACHABLE() \
    SEW_ASSERT(!"Unreachable code reached - wtf?"); \
    for (;;) {}

// -----------------------------------------------------------------------------
// Valgrind
// -----------------------------------------------------------------------------
#ifndef SEW_VALGRIND
#define SEW_VALGRIND 0
#endif

#if SEW_VALGRIND

#include <valgrind/valgrind.h>

#define SEW_VALGRIND_ID unsigned stack_id

#define SEW_VALGRIND_REGISTER(x, s, e) \
    x->stack_id = VALGRIND_STACK_REGISTER(s, e)

#define SEW_VALGRIND_DEREGISTER(x) VALGRIND_STACK_DEREGISTER(x.stack_id)

#else

#define SEW_VALGRIND_ID
#define SEW_VALGRIND_REGISTER(x, s, e)
#define SEW_VALGRIND_DEREGISTER(x)

#endif

// -----------------------------------------------------------------------------
// Atomic Code
// -----------------------------------------------------------------------------
#ifdef __cplusplus

#if (                                                       \
           (!defined(_MSC_VER) && (__cplusplus < 201103L))  \
        || ( defined(_MSC_VER) && (_MSC_VER < 1900))        \
    )
#error C++11 Is required
#endif

#include <atomic>

typedef std::atomic<size_t> atomic_size_t;

using std::memory_order_relaxed;
using std::memory_order_release;
using std::memory_order_acquire;
using std::atomic_store_explicit;
using std::atomic_load_explicit;
using std::atomic_compare_exchange_weak_explicit;

#else

#if (__STDC_VERSION__ < 201112L)
#error C11 Is required
#endif

#if defined(__STDC_NO_ATOMICS__)
#error This C11 doesnt support atomics!
#endif

#include <stdatomic.h>

#endif

typedef atomic_size_t Sew_Count;

// -----------------------------------------------------------------------------
// Platform Code
// -----------------------------------------------------------------------------

#if (__linux__ || __APPLE__)

#include <pthread.h>

Sew_Thread sew_thread_current()
{
    return pthread_self();
}

void sew_thread_join(Sew_Thread leaver)
{
    pthread_join(leaver, NULL);
}

void sew_thread_make
(
      Sew_Thread* thread
    , void*     (*procedure)(void*)
    , void*       argument
)
{
    pthread_create(thread, NULL, procedure, argument);
}

size_t sew_thread_index(Sew_Thread* threads, size_t size)
{
    pthread_t me = pthread_self();

    for (;;)
    {
        for (size_t i = 0; i < size; i++)
        {
            if (pthread_equal(me, threads[i]))
            {
                return i;
            }
        }
    }
}

#elif defined(_WIN32)

#include <process.h>

size_t sew_thread_index(Sew_Thread* threads, size_t size)
{
    DWORD me = GetCurrentThreadId();

    for (;;)
    {
        for (size_t i = 0; i < size; i++)
        {
            if (me == threads[i].id)
            {
                return i;
            }
        }
    }
}

Sew_Thread sew_thread_current()
{
    Sew_Thread result =
    {
          GetCurrentThread()
        , GetCurrentThreadId()
    };

    return result;
}

void sew_thread_join(Sew_Thread leaver)
{
    WaitForSingleObject(leaver.handle, INFINITE);
    CloseHandle(leaver.handle);
}

void sew_thread_make
(
      Sew_Thread* thread
    , void* (*procedure)(void*)
    , void* argument
)
{
    HANDLE thread_handle = (HANDLE) _beginthreadex
    (
          NULL
        , 0
        , (_beginthreadex_proc_type) procedure
        , argument
        , 0
        , NULL
    );

    *thread =
    {
          thread_handle
        , GetThreadId(thread_handle)
    };
}

#else

#error Platform not supported, sorry.

#endif

// -----------------------------------------------------------------------------

typedef fcontext_t Sew_Context;
struct Sew_Thread_Local;

static_assert
(
      sizeof(intptr_t) == sizeof(struct Sewing*)
    , "boost context argument size assumption failed, jump_fcontext will crash."
);

void sew_context_make
(
      Sew_Context* context
    , void       (*procedure)(intptr_t)
    , void*        stack
    , size_t       stack_bytes
)
{
    *context = make_fcontext(stack, stack_bytes, procedure);
}

intptr_t sew_context_switch
(
      struct Sew_Thread_Local* tls
    , Sew_Context*             from
    , Sew_Context*             to
)
{
    SEW_ASSERT(*from != *to);
    return jump_fcontext(from, *to, (intptr_t) tls, 1);
}

// -----------------------------------------------------------------------------

#if (SEW_STACK_GUARD > 0)

#if __linux__

#include <sys/mman.h>

void sew_memory_guard(void* memory, size_t bytes)
{
    int result = mprotect(memory, bytes, PROT_NONE);
    SEW_ASSERT(!result);
}

void sew_memory_guard_stop(void* memory, size_t bytes)
{
    int result = mprotect(memory, bytes, PROT_READ | PROT_WRITE);
    SEW_ASSERT(!result);
}

#elif defined(_WIN32)

void sew_memory_guard(void* memory, size_t bytes)
{
    DWORD ignored;

    BOOL result = VirtualProtect(memory, bytes, PAGE_NOACCESS, &ignored);
    SEW_ASSERT(result);
}

void sew_memory_guard_stop(void* memory, size_t bytes)
{
    DWORD ignored;

    BOOL result = VirtualProtect(memory, bytes, PAGE_READWRITE, &ignored);
    SEW_ASSERT(result);
}

#else

#error "Need way to protect memory for this platform".

#endif

#else

void sew_memory_guard(void* memory, size_t bytes)
{
    (void) memory;
    (void) bytes;
}

void sew_memory_guard_stop(void* memory, size_t bytes)
{
    (void) memory;
    (void) bytes;
}

#endif

// -----------------------------------------------------------------------------
// Internal Stitches.
// -----------------------------------------------------------------------------

typedef struct Sew_Stitch_Internal
{
    Sew_Stitch  stitch;
    Sew_Count*  stitches_left;
}
Sew_Stitch_Internal;

// -----------------------------------------------------------------------------
// MPMC lockless Queue
// -----------------------------------------------------------------------------
#define SEW_CACHELINE_SIZE 64

struct Sew_Mpmc_Cell;

typedef struct Sew_Mpmc_Queue
{
    char                  pad0[SEW_CACHELINE_SIZE];

    struct Sew_Mpmc_Cell* buffer;
    size_t                buffer_mask;
    char                  pad1[SEW_CACHELINE_SIZE];

    atomic_size_t         enqueue_pos;
    char                  pad2[SEW_CACHELINE_SIZE];

    atomic_size_t         dequeue_pos;
    char                  pad3[SEW_CACHELINE_SIZE];
}
Sew_Mpmc_Queue;

typedef struct Sew_Mpmc_Cell
{
    atomic_size_t       sequence;
    Sew_Stitch_Internal data;
}
Sew_Mpmc_Cell;

void sew_mpmc_queue_init
(
      Sew_Mpmc_Queue* q
    , Sew_Mpmc_Cell*  cells
    , size_t          cell_count
)
{
    if (!(cell_count && !(cell_count & (cell_count - 1))))
    {
        SEW_ASSERT(!"cell_count not a power of 2");

        q->buffer = NULL;

        return;
    }

    q->buffer      = cells;
    q->buffer_mask = cell_count - 1;

    for (size_t i = 0; i != (q->buffer_mask + 1); ++i)
    {
        atomic_store_explicit
        (
              &q->buffer[i].sequence
            , i
            , memory_order_relaxed
        );
    }

    atomic_store_explicit(&q->enqueue_pos, (size_t) 0, memory_order_relaxed);
    atomic_store_explicit(&q->dequeue_pos, (size_t) 0, memory_order_relaxed);
}

typedef struct Sew_Mpmc_Work
{
    Sew_Mpmc_Cell* cell;
    size_t         position;
}
Sew_Mpmc_Work;

Sew_Mpmc_Work sew_mpmc_queue_work
(
      Sew_Mpmc_Queue* queue
    , atomic_size_t*  in_or_out
    , const unsigned  pos_delta
)
{
    Sew_Mpmc_Cell* cell;

    size_t position = atomic_load_explicit(in_or_out, memory_order_relaxed);

    for (;;)
    {
        cell = &queue->buffer[position & queue->buffer_mask];

        size_t sequence =
            atomic_load_explicit(&cell->sequence, memory_order_acquire);

        intptr_t difference =
              (intptr_t) sequence
            - (intptr_t) (position + pos_delta);

        if (!difference)
        {
            if
            (
                atomic_compare_exchange_weak_explicit
                (
                      in_or_out
                    , &position
                    , position + 1
                    , memory_order_relaxed
                    , memory_order_relaxed
                )
            )
            {
                Sew_Mpmc_Work result =
                {
                      cell
                    , position
                };

                return result;
            }
        }
        else
        {
            if (difference < 0)
            {
                Sew_Mpmc_Work result =
                {
                      NULL
                    , 0
                };

                return result;
            }
            else
            {
                position =
                    atomic_load_explicit(in_or_out, memory_order_relaxed);
            }
        }
    }
}

int sew_mpmc_enqueue(Sew_Mpmc_Queue* mpmc_q, Sew_Stitch_Internal* data)
{
    Sew_Mpmc_Work get = sew_mpmc_queue_work(mpmc_q, &mpmc_q->enqueue_pos, 0);

    if (get.cell)
    {
        get.cell->data = *data;
        atomic_store_explicit
        (
              &get.cell->sequence
            , get.position + 1
            , memory_order_release
        );

        return 1;
    }

    return 0;
}

int sew_mpmc_dequeue(Sew_Mpmc_Queue* mpmc_q, Sew_Stitch_Internal* data)
{
    Sew_Mpmc_Work get = sew_mpmc_queue_work(mpmc_q, &mpmc_q->dequeue_pos, 1);

    if (get.cell)
    {
        *data = get.cell->data;
        atomic_store_explicit
        (
              &get.cell->sequence
            , get.position + mpmc_q->buffer_mask + 1
            , memory_order_release
        );

        return 1;
    }

    return 0;
}

// -----------------------------------------------------------------------------
// Sewing Types
// -----------------------------------------------------------------------------

static_assert(sizeof(size_t) >= sizeof(void*), "size_t cannot hold pointers!");

#define SEW_INVALID SIZE_MAX

typedef struct Sew_Fiber
{
    Sew_Stitch_Internal stitch_internal;
    Sew_Context         context;

    Sew_Count*          wait_counter;

    SEW_VALGRIND_ID;
}
Sew_Fiber;

typedef enum Sew_Thread_Local_Flags
{
      Sew_In_Use  = 0
    , Sew_To_Free = 0x40000000
    , Sew_To_Wait = 0x80000000
    , Sew_Mask    = ~(Sew_To_Free | Sew_To_Wait)
}
Sew_Thread_Local_Flags;

typedef struct Sew_Thread_Local
{
    struct Sewing*         sewing;
    Sew_Context            home_context;

    uint32_t               fiber_in_use;
    uint32_t               fiber_old;
}
Sew_Thread_Local;

typedef struct Sew_Main_Data
{
    struct Sewing*      sewing;
    Sew_Procedure_Main  proceduer;
    void*               data;
}
Sew_Main_Data;

typedef struct Sewing
{
    Sew_Mpmc_Queue      jobs;
    Sew_Mpmc_Cell*      cells;

    Sew_Thread_Local*   tls;
    atomic_size_t       tls_sync;

    Sew_Fiber*          fibers;
    atomic_size_t*      waiting;
    atomic_size_t*      free;

    atomic_size_t*      locks;

    Sew_Thread*         threads;
    Sew_Stitch*         ends;

    char*               stack;
    size_t              stack_bytes;

    size_t              thread_count;
    // only needs to be a byte, but meh.

    size_t              fiber_count;

}
Sewing;

// -----------------------------------------------------------------------------
// Sewing Code
// -----------------------------------------------------------------------------

Sew_Thread_Local* get_thread_local(Sewing* sew)
{
    size_t index = sew_thread_index(sew->threads, sew->thread_count);

    return &sew->tls[index];
}

size_t sew_get_free_fiber(Sewing* sew)
{
    for (size_t i = 0; i < sew->fiber_count; i++)
    {
        size_t fiber =
            atomic_load_explicit
            (
                  &sew->free[i]
                , memory_order_relaxed
            );

        if (fiber == SEW_INVALID)
        {
            continue;
        }

        fiber =
            atomic_load_explicit
            (
                  &sew->free[i]
                , memory_order_acquire
            );

        if (fiber == SEW_INVALID)
        {
            continue;
        }

        size_t expected = fiber;

        if
        (
            atomic_compare_exchange_weak_explicit
            (
                  &sew->free[i]
                , &expected
                , SEW_INVALID
                , memory_order_release
                , memory_order_relaxed
            )
        )
        {
            return fiber;
        }
    }

    return SEW_INVALID;
}

void sew_thread_update_free_and_waiting(Sew_Thread_Local* tls)
{
    Sewing* sewing = tls->sewing;

    if (tls->fiber_old == (uint32_t) SEW_INVALID)
    {
        return;
    }

    const size_t fiber_index = tls->fiber_old & Sew_Mask;

    if (tls->fiber_old & Sew_To_Free)
    {
        SEW_VALGRIND_DEREGISTER(sewing->fibers[fiber_index]);

        // Thread that added fiber to free list is the same as the one freeing
        // it, so I don't need to care about memory order.
        atomic_store_explicit
        (
              &sewing->free[fiber_index]
            , (size_t) fiber_index
            , memory_order_relaxed
        );
    }

    if (tls->fiber_old & Sew_To_Wait)
    {
        // Wait threshold needs to be thread synced, hence the need for release.
        atomic_store_explicit
        (
              &sewing->waiting[fiber_index]
            , (size_t) fiber_index
            , memory_order_release
        );
    }

    tls->fiber_old = (uint32_t) SEW_INVALID;
}

void sew_weave(intptr_t raw_sew);

size_t sew_get_next_fiber(Sewing* sew)
{
    size_t fiber_index = SEW_INVALID;

    for (size_t i = 0; i < sew->fiber_count; i++)
    {
        // Double lock helps cpus that have a weak memory model. (eg arm
        // goes 2x to 3x faster using double lock when its the bottleneck).
        size_t fiber_waiting =
            atomic_load_explicit
            (
                  &sew->waiting[i]
                , memory_order_relaxed
            );

        if (fiber_waiting == SEW_INVALID)
        {
            continue;
        }

        fiber_waiting =
            atomic_load_explicit
            (
                  &sew->waiting[i]
                , memory_order_acquire
            );

        if (fiber_waiting == SEW_INVALID)
        {
            continue;
        }

        Sew_Fiber* fiber = &sew->fibers[fiber_waiting];

        size_t     finished = 1;
        Sew_Count* counter  = fiber->wait_counter;

        if (counter)
        {
            size_t left = atomic_load_explicit(counter, memory_order_relaxed);

            finished = (!left);
        }

        if (!finished)
        {
            continue;
        }

        size_t expected = fiber_waiting;

        if
        (
            atomic_compare_exchange_weak_explicit
            (
                  &sew->waiting[i]
                , &expected
                , SEW_INVALID
                , memory_order_release
                , memory_order_relaxed
            )
        )
        {
            fiber_index = i;
            break;
        }
    }

    // -------------------------------------------------------------------------

    if (fiber_index == SEW_INVALID)
    {
        Sew_Stitch_Internal job;

        if (sew_mpmc_dequeue(&sew->jobs, &job))
        {
            while (fiber_index == SEW_INVALID)
            {
                fiber_index = sew_get_free_fiber(sew);
            }

            Sew_Fiber* fiber = &sew->fibers[fiber_index];

            fiber->stitch_internal = job;

            const size_t bytes  = sew->stack_bytes + SEW_STACK_GUARD;
            char*        stack  = &sew->stack[(fiber_index * bytes) + bytes];
            // sew_context_make requires top of stack as it grows down.

            sew_context_make
            (
                  &fiber->context
                , sew_weave
                , stack
                , sew->stack_bytes
            );

            SEW_VALGRIND_REGISTER(fiber, stack, stack - bytes);
        }
    }

    return fiber_index;
}

Sew_Thread_Local* sew_next_fiber
(
      Sewing*           sewing
    , Sew_Thread_Local* tls
    , Sew_Context*      context
)
{
    Sew_Count* wait_counter = NULL;

    if
    (
           (tls->fiber_old != (uint32_t) SEW_INVALID)
        && (tls->fiber_old & Sew_To_Wait)
    )
    {
        const size_t fiber_index = tls->fiber_old & Sew_Mask;
        Sew_Fiber* fiber         = &sewing->fibers[fiber_index];

        wait_counter = fiber->wait_counter;
    }

    for (;;)
    {
        size_t fiber_index = sew_get_next_fiber(sewing);

        if (fiber_index != SEW_INVALID)
        {
            Sew_Fiber* fiber = &sewing->fibers[fiber_index];

            tls->fiber_in_use = (uint32_t) fiber_index;

            return (Sew_Thread_Local*)
                sew_context_switch(tls, context, &fiber->context);
        }

        // Race condition fix.
        // 1) context needs to wait until a set of jobs are done, so starts
        //    swapping to do a new job
        // 2) the jobs finish _before_ a new context to swap to is found
        // 3) there are no new jobs
        // 4) the context swap code deadlocks looking for a new job to swap to,
        //    but none will arrive. Meanwhile the 'to be swapped' context is
        //    waiting to be run, but cannot as it hasn't been swapped out yet
        //    (in order to be picked up by the wait list)
        if (wait_counter)
        {
            size_t count =
                atomic_load_explicit(wait_counter, memory_order_relaxed);

            if (!count)
            {
                // tls->fiber_in_use still points to the 'to waitlist' fiber
                tls->fiber_old = (uint32_t) SEW_INVALID;
                return tls;
            }
        }
    }
}

void sew_weave(intptr_t raw_tls)
{
    Sew_Thread_Local* tls    = (Sew_Thread_Local*) raw_tls;
    Sewing*           sewing = tls->sewing;

    sew_thread_update_free_and_waiting(tls);

    // Do the job
    {
        Sew_Fiber* fiber = &sewing->fibers[tls->fiber_in_use];

        fiber->stitch_internal.stitch.procedure
        (
            fiber->stitch_internal.stitch.argument
        );
        // threads can switch inside the procedure.

        if (fiber->stitch_internal.stitches_left)
        {
            size_t last = atomic_fetch_sub_explicit
            (
                  fiber->stitch_internal.stitches_left
                , (size_t) 1u
                , memory_order_relaxed
            );

            if (!last)
            {
                SEW_ASSERT(last > 0);
            }
        }
    }

    tls                  = get_thread_local(sewing);
    Sew_Fiber* old_fiber = &sewing->fibers[tls->fiber_in_use];

    tls->fiber_old = tls->fiber_in_use | Sew_To_Free;

    sew_next_fiber(sewing, tls, &old_fiber->context);

    SEW_UNREACHABLE();
}

void sew_wait(Sewing* sewing, Sew_Chain chain)
{
    Sew_Count* counter = (Sew_Count*) chain;
    size_t wait_value  = 0;

    if (counter)
    {
        wait_value = atomic_load_explicit(counter, memory_order_relaxed);

        SEW_ASSERT(wait_value != SEW_INVALID);
    }

    if (wait_value)
    {
        Sew_Thread_Local* tls       = get_thread_local(sewing);
        Sew_Fiber*        old_fiber = &sewing->fibers[tls->fiber_in_use];

        old_fiber->wait_counter = counter;

        tls->fiber_old = tls->fiber_in_use | Sew_To_Wait;

        tls = sew_next_fiber(sewing, tls, &old_fiber->context);

        sew_thread_update_free_and_waiting(tls);
    }

    if (counter)
    {
        atomic_store_explicit(counter, SEW_INVALID, memory_order_release);
    }
}

atomic_size_t* sew_get_lock(Sewing* sewing, size_t initial_value)
{
    for (;;)
    {
        for (size_t i = 0; i < sewing->fiber_count; i++)
        {
            atomic_size_t* lock = &sewing->locks[i];

            if (atomic_load_explicit(lock, memory_order_relaxed) == SEW_INVALID)
            {
                size_t expected = SEW_INVALID;

                if
                (
                    atomic_compare_exchange_weak_explicit
                    (
                          lock
                        , &expected
                        , initial_value
                        , memory_order_relaxed
                        , memory_order_relaxed
                    )
                )
                {
                    return lock;
                }
            }
        }
    }
}

void sew_external(Sewing* sewing, Sew_Chain* chain)
{
    Sew_Count** counter = (Sew_Count**) chain;

    *counter = sew_get_lock(sewing, 1);
}

void sew_external_finished(Sew_Chain chain)
{
    Sew_Count* counter = (Sew_Count*) chain;
    atomic_store_explicit(counter, 0ul, memory_order_release);
}

void sew_stitches_and_wait
(
      Sewing*     sewing
    , Sew_Stitch* stitches
    , size_t      stitch_count
)
{
    Sew_Chain chain;

    sew_stitches(sewing, stitches, stitch_count, &chain);
    sew_wait(sewing, chain);
}

void sew_stitches
(
      Sewing*     sewing
    , Sew_Stitch* stitches
    , size_t      stitch_count
    , Sew_Chain*  chain
)
{
    Sew_Count** counter       = (Sew_Count**) chain;
    Sew_Count* counter_to_use = NULL;

    if (counter)
    {
        *counter       = sew_get_lock(sewing, stitch_count);
        counter_to_use = *counter;
    }

    for (size_t i = 0; i < stitch_count; i++)
    {
        Sew_Stitch_Internal stitch =
        {
              stitches[i]
            , counter_to_use
        };

        while (!sew_mpmc_enqueue(&sewing->jobs, &stitch))
        {
        }
    }
}

void* sew_thread_start(void* raw_tls)
{
    Sew_Thread_Local* tls    = (Sew_Thread_Local*) raw_tls;
    Sewing*           sewing = tls->sewing;

    while (!atomic_load_explicit(&sewing->tls_sync, memory_order_acquire))
    {
    }

    tls->fiber_old = (uint32_t) SEW_INVALID;

    tls = sew_next_fiber(sewing, tls, &tls->home_context);

    sew_thread_update_free_and_waiting(tls);

    return NULL;
}

void sew_job_quit(void* raw_sew)
{
    Sewing* sewing = (Sewing*) raw_sew;

    Sew_Thread_Local* tls = get_thread_local(sewing);
    Sew_Fiber* old_fiber  = &sewing->fibers[tls->fiber_in_use];

    if (tls == &sewing->tls[0])
    {
        for (size_t i = 1; i < sewing->thread_count; i++)
        {
            sew_thread_join(sewing->threads[i]);
        }
    }

    tls->fiber_old = tls->fiber_in_use | Sew_To_Free;

    sew_context_switch(tls, &old_fiber->context, &tls->home_context);

    SEW_UNREACHABLE();
}

// -----------------------------------------------------------------------------

void sew_main_procedure(void* raw_main_data)
{
    Sew_Main_Data* main_data = (Sew_Main_Data*) raw_main_data;
    Sewing*        sewing    = main_data->sewing;
    const size_t threads     = sewing->thread_count;

    main_data->proceduer
    (
          sewing
        , sewing->threads
        , threads
        , main_data->data
    );

    // -------------------------------------------------------------------------
    // tell everyone to quit please.
    // -------------------------------------------------------------------------

    for (size_t i = 0; i < threads; i++)
    {
        sewing->ends[i].procedure = sew_job_quit;
        sewing->ends[i].argument  = (void*) sewing;
        sewing->ends[i].name      = "sew_job_quit";
    }

    sew_stitches_and_wait(sewing, sewing->ends, threads);
    // Don't expect to return from here.

    SEW_UNREACHABLE();
}

size_t sew_it
(
      void*                  sewing_memory
    , size_t                 stack_bytes
    , size_t                 thread_count
    , size_t                 log_2_job_count
    , size_t                 fiber_count
    , Sew_Procedure_Main     main_procedure
    , Sew_Procedure_Argument main_procedure_argument
)
{
    // -------------------------------------------------------------------------
    // Why can't I just have a sew_start() and sew_stop() method instead?
    //
    // Because I can't find an easy way of saving the context of the caller to
    // sew_start() so it'll be used the first time sew_wait() is called.
    // I don't have a covert_context() function, only a make_context(), which
    // needs a function pointer.
    // -------------------------------------------------------------------------

    // Deal with memory
    Sewing* sewing = NULL;
    {
        size_t sew_bytes = A16(sizeof(Sewing));

        size_t sew_cells_count   = 1ull << log_2_job_count;
        size_t sew_cells_bytes   = A16(sizeof(Sew_Mpmc_Cell) * sew_cells_count);
        size_t tls_bytes         = A16(sizeof(Sew_Thread_Local)) * thread_count;
        size_t sew_fibers_bytes  = A16(sizeof(Sew_Fiber) * fiber_count);
        size_t sew_waiting_bytes = A16(sizeof(atomic_size_t) * fiber_count);
        size_t sew_free_bytes    = A16(sizeof(atomic_size_t) * fiber_count);
        size_t sew_lock_bytes    = A16(sizeof(atomic_size_t) * fiber_count);

        size_t sew_thread_bytes  = A16(sizeof(Sew_Thread) * thread_count);
        size_t sew_end_bytes        = A16(sizeof(Sew_Stitch) * thread_count);
        size_t sew_stack_bytes      = A16(stack_bytes);
        size_t sew_stack_heap_bytes = sew_stack_bytes * fiber_count;
        size_t sew_bytes_alignment  = 15 + (SEW_STACK_GUARD);

        sew_stack_heap_bytes += SEW_STACK_GUARD * (fiber_count + 1);

        size_t total_bytes =
              sew_bytes
            + sew_cells_bytes
            + tls_bytes
            + sew_fibers_bytes
            + sew_waiting_bytes
            + sew_free_bytes
            + sew_lock_bytes
            + sew_thread_bytes
            + sew_end_bytes
            + sew_stack_heap_bytes
            + sew_bytes_alignment;

        if (!sewing_memory)
        {
            return total_bytes;
        }

        sewing = (Sewing*) A16((intptr_t) sewing_memory);

        {
            uint8_t* z = (uint8_t*) sewing;
            for (size_t i = 0; i < total_bytes; i++)
            {
                z[i] = 0;
            }
        }

        size_t o  = sew_bytes;
        char* raw = (char*) sewing;

        sewing->cells   = (Sew_Mpmc_Cell*)    &raw[o]; o += sew_cells_bytes;
        sewing->tls     = (Sew_Thread_Local*) &raw[o]; o += tls_bytes;
        sewing->fibers  = (Sew_Fiber*)        &raw[o]; o += sew_fibers_bytes;
        sewing->waiting = (atomic_size_t*)    &raw[o]; o += sew_waiting_bytes;
        sewing->free    = (atomic_size_t*)    &raw[o]; o += sew_free_bytes;
        sewing->locks   = (atomic_size_t*)    &raw[o]; o += sew_lock_bytes;
        sewing->threads = (Sew_Thread*)       &raw[o]; o += sew_thread_bytes;
        sewing->ends    = (Sew_Stitch*)       &raw[o]; o += sew_end_bytes;

#if (SEW_STACK_GUARD > 0)
        sewing->stack = (char*)
        (
              (((intptr_t) &raw[o]) + (SEW_STACK_GUARD - 1))
            & ~(SEW_STACK_GUARD - 1)
        );
#else
        sewing->stack = (char*) &raw[o];
#endif

        sewing->stack_bytes  = sew_stack_bytes;

        sewing->thread_count = thread_count;
        sewing->fiber_count  = fiber_count;

        SEW_ASSERT(!(((intptr_t) sewing->cells  ) & 15));
        SEW_ASSERT(!(((intptr_t) sewing->tls    ) & 15));
        SEW_ASSERT(!(((intptr_t) sewing->fibers ) & 15));
        SEW_ASSERT(!(((intptr_t) sewing->waiting) & 15));
        SEW_ASSERT(!(((intptr_t) sewing->free   ) & 15));
        SEW_ASSERT(!(((intptr_t) sewing->locks  ) & 15));
        SEW_ASSERT(!(((intptr_t) sewing->threads) & 15));
        SEW_ASSERT(!(((intptr_t) sewing->ends   ) & 15));
        SEW_ASSERT(!(((intptr_t) sewing->stack  ) & 15));
    }

    sew_mpmc_queue_init
    (
          &sewing->jobs
        , sewing->cells
        , 1ull << log_2_job_count
    );

    // <= instead of < as I'm adding a guard page at the end as well.
    for (size_t i = 0; i <= sewing->fiber_count; i++)
    {
        sew_memory_guard
        (
              &sewing->stack[i * (sewing->stack_bytes + SEW_STACK_GUARD)]
            , SEW_STACK_GUARD
        );
    }

    for (size_t i = 0; i < sewing->fiber_count; i++)
    {
        atomic_store_explicit
        (
              &sewing->free[i]
            , (size_t) i
            , memory_order_release
        );

        atomic_store_explicit
        (
              &sewing->waiting[i]
            , SEW_INVALID
            , memory_order_release
        );

        atomic_store_explicit
        (
              &sewing->locks[i]
            , SEW_INVALID
            , memory_order_release
        );
    }

    {
        // Setup thread 0
        sewing->tls[0].sewing       = sewing;
        sewing->tls[0].fiber_in_use = (uint32_t) sew_get_free_fiber(sewing);
        sewing->threads[0]          = sew_thread_current();
    }

    atomic_store_explicit(&sewing->tls_sync, (size_t) 0, memory_order_release);

    for (size_t i = 1; i < thread_count; i++)
    {
        Sew_Thread_Local* tls = &sewing->tls[i];

        tls->sewing       = sewing;
        tls->fiber_in_use = (uint32_t) SEW_INVALID;

        sew_thread_make(&sewing->threads[i], sew_thread_start, (void*) tls);
    }

    atomic_store_explicit(&sewing->tls_sync, (size_t) 1, memory_order_release);

    // -------------------------------------------------------------------------

    Sew_Main_Data main_data =
    {
          sewing
        , main_procedure
        , main_procedure_argument
    };

    Sew_Stitch main_job =
    {
          sew_main_procedure
        , (void*) &main_data
        , "sew_main_procedure"
    };

    sew_stitches(sewing, &main_job, 1, NULL);
    sew_thread_start((void*) &sewing->tls[0]);

    for (size_t i = 0; i <= sewing->fiber_count; i++)
    {
        sew_memory_guard_stop
        (
              &sewing->stack[i * (sewing->stack_bytes + SEW_STACK_GUARD)]
            , SEW_STACK_GUARD
        );
    }

    return 0;
}
