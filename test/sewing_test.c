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


#include "sewing.h"

#include <stdlib.h>
#include <stdio.h>
// printf, stdout, fflush

#include <stddef.h>
// size_t

#include <stdint.h>
// intptr_t

#include <inttypes.h>
// for PRId64

#ifndef SEW_DEMO_USE_HWLOC
#define SEW_DEMO_USE_HWLOC 0
#endif

#if SEW_DEMO_USE_HWLOC
#include <hwloc.h>
#endif

#define COUNT_EMPTY_JOBS (1024 * 10)

#define COUNT_SORT_ITEMS (1024u * 1024u * 2u)
#define COUNT_SORT_CACHE ((256u * 1024u) / sizeof(uint32_t))
#define COUNT_CACHE_GROUPS (COUNT_SORT_ITEMS / COUNT_SORT_CACHE)

#define LOG2_QUEUE_SIZE 12
#define QUEUE_SIZE (1 << LOG2_QUEUE_SIZE)

// -----------------------------------------------------------------------------
// Timing Code
// -----------------------------------------------------------------------------
#if __linux__

#include <time.h>

typedef struct timespec Sew_Time;

Sew_Time sew_test_now()
{
    struct timespec start_time;
    clock_gettime(CLOCK_REALTIME, &start_time);
    return start_time;
}

int64_t sew_test_delta
(
    Sew_Time* then
)
{
    Sew_Time time_sew_test_now = sew_test_now();

    int64_t diff_us = (time_sew_test_now.tv_sec - then->tv_sec) * 1000000;
    diff_us += (time_sew_test_now.tv_nsec - then->tv_nsec) / 1000;

    return diff_us;
}

#elif __APPLE__

#include <sys/time.h>

#include <mach/clock.h>
#include <mach/mach.h>

typedef struct timespec Sew_Time;

Sew_Time sew_test_now()
{
    clock_serv_t cclock;
    mach_timespec_t mts;

    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);

    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);

    struct timespec start_time;

    start_time.tv_sec  = mts.tv_sec;
    start_time.tv_nsec = mts.tv_nsec;

    return start_time;
}

int64_t sew_test_delta
(
    Sew_Time* then
)
{
    Sew_Time time_sew_test_now = sew_test_now();

    int64_t diff_us = (time_sew_test_now.tv_sec - then->tv_sec) * 1000000;
    diff_us += (time_sew_test_now.tv_nsec - then->tv_nsec) / 1000;

    return diff_us;
}

#elif _WIN32

#include <Windows.h>

typedef LARGE_INTEGER Sew_Time;

Sew_Time sew_test_now()
{
    Sew_Time result;
    QueryPerformanceCounter(&result);
    return result;
}

int64_t sew_test_delta
(
    Sew_Time* then
)
{
    Sew_Time time_sew_test_now = sew_test_now();

    Sew_Time one_second;
    QueryPerformanceFrequency(&one_second);

    Sew_Time us;

    us.QuadPart = (one_second.QuadPart / 1000000);

    return (time_sew_test_now.QuadPart - then->QuadPart) / us.QuadPart;
}

#else

#error Need a high resolution clock source for this platform.

#endif

// -----------------------------------------------------------------------------
// Thread Affinity
// -----------------------------------------------------------------------------
void Sew_Test_Set_hwloc_thread_affinity
(
    Sew_Thread* threads
    , size_t      thread_count
)
{
#if SEW_DEMO_USE_HWLOC
    {
        hwloc_topology_t t;

        hwloc_topology_init(&t);
        hwloc_topology_load(t);

        int    core_depth = hwloc_get_type_or_below_depth(t, HWLOC_OBJ_CORE);
        size_t core_count = hwloc_get_nbobjs_by_depth(t, core_depth);

        size_t threads_left = thread_count;

        while (threads_left)
        {
            for (size_t i = 0; i < core_count; i++)
            {
                hwloc_obj_t core = hwloc_get_obj_by_depth(t, core_depth, i);

                if (core)
                {
                    hwloc_cpuset_t cpu = hwloc_bitmap_dup(core->cpuset);

                    // NO HYPERTHREADING
                    // Even when threads > cores, turning off hyperthreading
                    // is still better (tested on i7 with hyperthreading).
                    hwloc_bitmap_singlify(cpu);

                    if
                        (
                            !hwloc_set_thread_cpubind
                            (
                                t
#if (__linux__ || __APPLE__)
                                , threads[threads_left - 1]
#elif defined(_WIN32)
                                , threads[threads_left - 1].handle
#else
#error Platform not supported, sorry.
#endif
                                , cpu
                                , HWLOC_CPUBIND_THREAD
                            )
                            )
                    {
                        threads_left--;

                        if (!threads_left)
                        {
                            break;
                        }
                    }

                    hwloc_bitmap_free(cpu);
                }
            }
        }

        hwloc_topology_destroy(t);
    }
#else
    (void) threads;
    (void) thread_count;
#endif
}

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

void sew_test_main_bringup_shutdown
(
      struct Sewing*         sewing
    , Sew_Thread*            threads
    , size_t                 thread_count
    , Sew_Procedure_Argument procedure_argument
)
{
    (void) sewing;
    (void) threads;
    (void) thread_count;
    (void) procedure_argument;

    // Do nothing on purpose.
}

// -----------------------------------------------------------------------------

void sew_test_10k_empty_jobs_count(Sew_Procedure_Argument raw)
{
    (void) raw;
}

void sew_test_main_10k_empty_jobs
(
      struct Sewing*         sewing
    , Sew_Thread*            threads
    , size_t                 thread_count
    , Sew_Procedure_Argument procedure_argument
)
{
    (void) procedure_argument;

    Sew_Test_Set_hwloc_thread_affinity(threads, thread_count);

    intptr_t lots_o_jobs_raw = (intptr_t)
        malloc((COUNT_EMPTY_JOBS * sizeof(Sew_Stitch)) + 15);

    if (lots_o_jobs_raw)
    {
        Sew_Stitch* lots_o_jobs = (Sew_Stitch*) ((lots_o_jobs_raw + 15) & ~15);

        for (size_t i = 0; i < COUNT_EMPTY_JOBS; i++)
        {
            lots_o_jobs[i].procedure = sew_test_10k_empty_jobs_count;
            lots_o_jobs[i].argument  = NULL;
            lots_o_jobs[i].name      = "sew_test_10k_empty_jobs_count";
        }

        Sew_Time start = sew_test_now();

        for (size_t i = 0; i < COUNT_EMPTY_JOBS; i += (QUEUE_SIZE - 16))
        {
            size_t queue_count_raw = i + (QUEUE_SIZE - 16);
            size_t queue_count =
                (queue_count_raw <= COUNT_EMPTY_JOBS)
                ? (size_t) (QUEUE_SIZE - 16)
                : (size_t) (COUNT_EMPTY_JOBS - i);

            sew_stitches_and_wait(sewing, &lots_o_jobs[i], queue_count);
        }

        int64_t ns = sew_test_delta(&start);

        printf("%14" PRId64 " ", ns);

        free((void*) lots_o_jobs_raw);
    }
    else
    {
        printf("%14s", "No Mem");
    }
}

// -----------------------------------------------------------------------------

#define SEW_TREE_WIDTH 8

typedef struct Sew_Test_Wait_Tree
{
    struct Sewing* sewing;
    size_t         depth;
}
Sew_Test_Wait_Tree;

void sew_test_dependency_tree(Sew_Procedure_Argument raw)
{
    Sew_Test_Wait_Tree* info = (Sew_Test_Wait_Tree*) raw;

    size_t count = SEW_TREE_WIDTH / info->depth;

    if (count > 1)
    {
        Sew_Stitch          jobs[SEW_TREE_WIDTH];
        Sew_Test_Wait_Tree  infos[SEW_TREE_WIDTH];

        for (size_t i = 0; i < count; i++)
        {
            infos[i].sewing = info->sewing;
            infos[i].depth  = info->depth + 1;

            jobs[i].procedure = sew_test_dependency_tree;
            jobs[i].argument  = &infos[i];
            jobs[i].name      = "sew_test_dependency_tree";
        }

        sew_stitches_and_wait(info->sewing, jobs, count);
    }
}

void sew_test_main_dependency_tree
(
    struct Sewing*         sewing
    , Sew_Thread*            threads
    , size_t                 thread_count
    , Sew_Procedure_Argument procedure_argument
)
{
    Sew_Test_Set_hwloc_thread_affinity(threads, thread_count);
    (void) procedure_argument;

    Sew_Time start = sew_test_now();

    Sew_Test_Wait_Tree tree;

    tree.sewing = sewing;
    tree.depth  = 1;

    sew_test_dependency_tree((Sew_Procedure_Argument) &tree);

    int64_t ns = sew_test_delta(&start);

    printf("%9" PRId64 " ", ns);
}

// -----------------------------------------------------------------------------

// Job based test code. Lets insertion sort a partially random array.
// First use a hash and a counter to generate a partially sorted buffer
// of 1 million 32 bit ints. Then sort it.

// http://burtleburtle.net/bob/hash/integer.html
uint32_t sew_test_hash(uint32_t a)
{
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);
    return a;
}

void sew_test_insertion_sort(uint32_t* b, size_t count)
{
    for (size_t i = 1; i < count; i++)
    {
        uint32_t current = b[i];
        size_t   j = i - 1;

        // since j is unsigned, I test for -ve by checking against wraparound.
        while (j < count && b[j] > current)
        {
            b[j + 1] = b[j];
            j--;
        }

        b[j + 1] = current;
    }
}

void sew_test_fill(uint32_t* b, size_t offset, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        uint32_t io = (uint32_t) (i + offset);
        uint32_t r  = sew_test_hash((uint32_t)io);
        uint32_t d  = (r & 0xFFFF) ? (r & 511) : (r & 0xFFFF);

        if ((io + d) < 256)
        {
            d += 256;
        }

        uint32_t v = io + d - 256;

        b[io] = v;
    }
}

void sew_test_prepare(uint32_t* b)
{
    for (size_t i = 0; i < COUNT_CACHE_GROUPS; i++)
    {
        sew_test_fill(b, i * COUNT_SORT_CACHE, COUNT_SORT_CACHE);
    }
}

uint32_t sew_test_sort_edge(uint32_t* b, size_t start_edge, size_t count)
{
    uint32_t bad = 0;
    uint32_t h = (uint32_t) (count / 2);

    size_t start_incl = start_edge - count;
    size_t end_encl = start_edge + count - 1;

    uint32_t v = b[start_edge];

    if (v > b[start_incl])
    {
        for (size_t i = 1; i < h; i++)
        {
            if (v >= b[start_edge - i])
            {
                start_incl = 1 + start_edge - i;
                break;
            }
        }
    }
    else
    {
        bad |= 1;
    }

    v = b[start_edge - 1];

    if (v < b[end_encl])
    {
        for (size_t i = 0; i < count; i++)
        {
            if (v <= b[start_edge + i])
            {
                end_encl = start_edge + i;
                break;
            }
        }
    }
    else
    {
        bad |= 2;
    }

    if (start_incl != end_encl)
    {
        sew_test_insertion_sort(&b[start_incl], (end_encl - start_incl) + 1);
    }

    return bad;
}

int sew_test_validate(uint32_t* b)
{
    for (size_t i = 1; i < COUNT_SORT_ITEMS; i++)
    {
        if (b[i - 1] > b[i])
        {
            printf
            (
                  "INVALID at %u (%u)\n"
                , (unsigned) i
                , (unsigned) (i / COUNT_SORT_CACHE)
            );

            printf("%u %u %u %u\n", b[i - 1], b[i], b[i + 1], b[i + 2]);
            return 0;
        }
    }

    return 1;
}

typedef struct Sew_Test_Set
{
    uint32_t* b;
    size_t    offset;
    size_t    count;
    size_t    needs_sorting;
}
Sew_Test_Set;

void sew_test_sort_wrapper(Sew_Procedure_Argument raw_set)
{
    Sew_Test_Set* set = (Sew_Test_Set*) raw_set;

    sew_test_insertion_sort(&set->b[set->offset], set->count);
}

void sew_test_fill_wrapper(Sew_Procedure_Argument raw_set)
{
    Sew_Test_Set* set = (Sew_Test_Set*) raw_set;

    sew_test_fill(set->b, set->offset, set->count);
}

void sew_test_fill_all(struct Sewing* sewing, Sew_Test_Set* data)
{
    Sew_Stitch jobs[COUNT_CACHE_GROUPS];

    for (size_t i = 0; i < COUNT_CACHE_GROUPS; i++)
    {
        jobs[i].procedure = sew_test_fill_wrapper;
        jobs[i].argument  = (Sew_Procedure_Argument) &data[i];
        jobs[i].name      = "sew_test_fill_wrapper";
    }

    sew_stitches_and_wait(sewing, jobs, COUNT_CACHE_GROUPS);
}

void sew_test_sort_all(struct Sewing* sewing, Sew_Test_Set* data)
{
    Sew_Stitch jobs[COUNT_CACHE_GROUPS];

    size_t actual_stitches = 0;
    for (size_t i = 0; i < COUNT_CACHE_GROUPS; i++)
    {
        uint32_t sort = data[i].needs_sorting & 1;
        sort |= (i) ? (data[i - 1].needs_sorting & 3) : 0;

        if (sort)
        {
            jobs[actual_stitches].procedure = sew_test_sort_wrapper;
            jobs[actual_stitches].argument  = (Sew_Procedure_Argument) &data[i];
            jobs[actual_stitches].name      = "sew_test_sort_wrapper";

            actual_stitches++;
        }
    }

    sew_stitches_and_wait(sewing, jobs, actual_stitches);
}

void sew_test_edge(Sew_Procedure_Argument raw_set)
{
    Sew_Test_Set* set = (Sew_Test_Set*) raw_set;

    set->needs_sorting = sew_test_sort_edge(set->b, set->offset, set->count);
}

void sew_test_edge_all(struct Sewing* sewing, Sew_Test_Set* data)
{
    Sew_Stitch jobs[COUNT_CACHE_GROUPS];

    for (size_t i = 0; i < (COUNT_CACHE_GROUPS - 1); i++)
    {
        jobs[i].procedure = sew_test_edge;
        jobs[i].argument  = (Sew_Procedure_Argument) &data[i + 1];
        jobs[i].name      = "sew_test_edge";
    }

    sew_stitches_and_wait(sewing, jobs, COUNT_CACHE_GROUPS - 1);
}

void sew_test_main_parallel_insertion_sort
(
      struct Sewing*         sewing
    , Sew_Thread*            threads
    , size_t                 thread_count
    , Sew_Procedure_Argument procedure_argument
)
{
    Sew_Test_Set_hwloc_thread_affinity(threads, thread_count);
    (void) procedure_argument;

    // make 16 byte aligned buffer
    intptr_t  b_raw =
        (intptr_t) malloc((COUNT_SORT_ITEMS * sizeof(uint32_t)) + 15);

    uint32_t* b = (uint32_t*) ((b_raw + 15) & ~15);

    Sew_Test_Set data[COUNT_CACHE_GROUPS];

    for (size_t i = 0; i < COUNT_CACHE_GROUPS; i++)
    {
        data[i].b             = b;
        data[i].offset        = i * COUNT_SORT_CACHE;
        data[i].count         = COUNT_SORT_CACHE;
        data[i].needs_sorting = 1;
    }

    {
        Sew_Time start = sew_test_now();

        sew_test_fill_all(sewing, data);

        int64_t ns = sew_test_delta(&start);

        printf("%9" PRId64 " ", ns);
    }

    {
        Sew_Time start = sew_test_now();
        int needs_sorting = 1;

        while (needs_sorting)
        {
            needs_sorting = 0;

            sew_test_sort_all(sewing, data);

            for (size_t i = 0; i < COUNT_CACHE_GROUPS; i++)
            {
                data[i].needs_sorting = 0;
            }

            sew_test_edge_all(sewing, data);

            for (size_t i = 0; i < COUNT_CACHE_GROUPS; i++)
            {
                if (data[i].needs_sorting)
                {
                    needs_sorting = 1;
                    break;
                }
            }
        }

        int64_t ns = sew_test_delta(&start);

        printf("%10" PRId64 "\n", ns);

        if (!sew_test_validate(b))
        {
            printf("***INVALID***\n");
        }
    }

    free((void*) b_raw);
}

// -----------------------------------------------------------------------------

void sew_test_run
(
      size_t             fibers
    , size_t             stack_bytes
    , size_t             log2_queue_size
    , size_t             threads
    , Sew_Procedure_Main procedure
)
{
    size_t bytes = sew_it
    (
          NULL
        , stack_bytes
        , threads
        , log2_queue_size
        , fibers
        , procedure
        , NULL
    );

    void* sewing = malloc(bytes);

    sew_it
    (
          sewing
        , stack_bytes
        , threads
        , log2_queue_size
        , fibers
        , procedure
        , NULL
    );

    free(sewing);
}

// -----------------------------------------------------------------------------

int main(int argv, char** argc)
{
    (void) argc;
    (void) argv;

    static const size_t FIBERS      = 128;
    static const size_t STACK_BYTES = 1024 * 32;

    printf("Sewing with fibers:\n\n");

    printf("Sewing with fibers:\n");
    printf("    %u fibers\n", (unsigned) FIBERS);
    printf("    %uk stack\n", (unsigned) (STACK_BYTES / 1024));
    printf("    %uk empty jobs,\n", COUNT_EMPTY_JOBS / 1024);
    printf("    dependency chain empty jobs,\n");
    printf("    setup for sort test,\n");

    printf
    (
          "    parallel insertion sort test (%u groups, %uk items/group).\n\n"
        , (unsigned) COUNT_CACHE_GROUPS
        , (unsigned) (COUNT_SORT_CACHE / 1024)
    );

    printf("Threads  Bringup  Empty Jobs(us)  Tree(us)  Setup(us)  Sort(us)\n");

    size_t f = FIBERS;
    size_t s = STACK_BYTES;
    size_t q = LOG2_QUEUE_SIZE;

    for (size_t t = 1; t < 16; t++)
    {
        printf("%-10zd ", t);

        sew_test_run(f, s, q, t, sew_test_main_bringup_shutdown);
        printf("   Ok  ");

        sew_test_run(f, s, q, t, sew_test_main_10k_empty_jobs);
        sew_test_run(f, s, q, t, sew_test_main_dependency_tree);
        sew_test_run(f, s, q, t, sew_test_main_parallel_insertion_sort);

        fflush(stdout);
    }

    return 0;
}
