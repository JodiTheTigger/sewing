Sewing
======

Multiplatform multithreaded fiber based job system based on ideas presented
by Naughty Dog in the talk 'Parallelizing the Naughty Dog Engine'.

[Video](http://gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine)  
[Slides](http://twvideo01.ubm-us.net/o1/vault/gdc2015/presentations/Gyrling_Christian_Parallelizing_The_Naughty.pdf)

Example
=======

Make a `Sew_Procedure`:

```
    void hello_something(Sew_Procedure_Argument argument)
    {
        unsigned world = (unsigned) (size_t) argument;

        printf("Hello world %d\n", world);
    }
```

Then make a couple of jobs (stitches) that call that procedure:

```
    Sew_Stitch jobs[10];

    for (unsigned i = 0; i < 10; i++)
    {
        jobs[i].procedure = hello_something;
        jobs[i].argument  = (Sew_Procedure_Argument) (size_t) i;
        jobs[i].name      = "hello_something";
    }

    sew_stiches_and_finish(sewing, jobs, 10);
    // Run the 10 jobs pointed to by 'jobs'. The call will wait until all ten
    // jobs are finsihed.
```

This will result in output similar to:

```
    Hello world 0
    Hello world 1
    Hello world 2
    Hello world 7
    Hello world 8
    Hello world 9
    Hello world 3
    Hello world 4
    Hello world 6
    Hello world 5
```

since the jobs system is multithreaded, the order of job execution is not
guaraneteed to be the order the jobs were submitted.

For a full tutorial, see the end of this readme file.

How is this Different from Normal Thread Job Systems?
=====================================================

'sew_stiches_and_finish' doesn't "block" in the traditional sense. while the
code is waiting for the jobs to finish in `sew_stiches_and_finish` the current
context is swapped with a new fiber containing a new job. Only when all the
jobs have been finished will the context be swapped back and
`sew_stiches_and_finish` returns.

That is, each thread is always doing work, and not "blocking" on a job that is
stuck waiting in `sew_stiches_and_finish` (or `sew_finish`).

Support
=======

```
    YES     = Tested Working
    c       = Compiles
    t       = In Theory. Compile code not written yet
    <empty> = Not supported
```

| Arch    | Linux | Windows | MacOS X | iOS |
| --------| ----- | --------| ------- | --- |
| arm     | YES   | t       |         | t   |
| arm_64  | t     |         |         | t   |
| x86     | YES   | YES     | t       |     |
| x86_64  | YES   | YES     | t       |     |
| mips    | t     |         |         |     |
| ppc     | t     |         | t       |     |
| ppc_64  | t     |         | t       |     |

Building
========

The build system uses CMake for building. This can be used to generate Make,
Ninja, MSVC or XCode projects easily.

Requirements
------------

 * CMake version 3.3.0 or higher
 * C11 or C++11 with atomics support
 * `pthread` on linux
 * `hwloc` for the test code (optional)

CMake with Make
---------------

```
    cd Sewing
    mkdir build
    cd build
    cmake ..
    make
```

Special defines and CMake options
---------------------------------

### Build C++11 instead of C11

(Default: Off)

The library uses C11 atomics, however it can also be built using a C++11
compiler and C++11 atomics instead if this option is set.

### SEW_DEMO_USE_HWLOC

(Default: 0)

The test code can use the hwloc platform library to bind threads to a logical
CPU core as opposed to letting the OS shuffle them around as it see fit.

Can be set using the CMake option "Test: Bind threads to cores (hwloc)."

### SEW_VALGRIND

(Default: 0)

if `SEW_VALGRIND` is set to 1, then code to add valgrind tracking to the fiber's
stacks are added. Prevents excessive false positive warnings when running the
code under valgrind.

Can be set using the CMake option "Build valgrind friendly library."

### SEW_ASSERTS

(Default: 1)

if `SEW_ASSERTS` is set to 1, then the code will be compiled using `<assert.h>`
and `assert(test)` for it's internal assertion checks.

### SEW_STACK_GUARD

(Default: 0)

For Linux or Windows, you can enable a stack guard by setting the define
`SEW_STACK_GUARD` to a multiple of your target system's page size value
(4096 bytes on my PC). If the stack gets touched out of bounds in the guard area
then it should crash (or raise a signal) there and then, resulting in a useful
stacktrace.

Can be set using the CMake option "Stack guard page size (bytes)."

Gotchas
=======

 * 'sew_stiches' and 'sew_stiches_and_finish' will block until all jobs are
  queued. If you are only running one thread, and the queue is full, this will
  cause a deadlock.

 * The system will deadlock if all fibers are used up. You can put a breakpoint
   in `sew_get_free_fiber` on the line `return SEW_INVALID;` if you suspect this
   is happening to you.

 * When `Sew_Procedure_Main` exits, all threads and fibers are destroyed. If
  there are any outstanding fibers waiting to finish, they will NOT be called.
  This could cause a resource leak if those fibers needed to free memory,
  close handles, call destructors or run cleanup code.

 * Make sure you know what your OS's min and max stack sizes are.


License
=======

The Sewing library is under the MIT license, except the contents of the
`src/asm` directory, which are under the Boost license.

Technical Notes
===============

### Why?

Normal multithread job produce and consume systems don't have the ability for a
job to wait for another job to finish without blocking the thread running the
waiting job.

### Atomics

Jeff Preshing has written excellent articles about atomics operations, and his
guides were instrumental in the creation of this library.

[Atomics: Introduction to Lock-Free Programming](http://preshing.com/20120612/an-introduction-to-lock-free-programming/)

### Job Queue

The job queue was implemented from the multiple producer, multiple consumer
(MPMC) queue described by Dmitry Vyuko on 1024cores.

[High Speed Atomic MPMC Queue](http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue)

### Fibers

Fiber based context switching was originally supported in POSIX using
`getcontext` and `setcontext`. However that API was depreciated in the latest
POSIX standard. Secondly, because it had to handle POSIX signals, it needed
to cross the user/kernel boundary for each context switch, significantly slowing
down its raw speed.

For windows there is the `CreateFiber` API. However that doesn't let you
specifiy the stack used by the fiber. This means that creating Fibers can be
a costly operation due to the required memory allocation (for the stack) per
fiber.

libmill (and libdill) looked like a wonderful solution to the problem, however
they don't support windows, so were a no-go.

Boost::Context seemed like the logical solution as it supported multiple
platforms, ABIs and compilers. It was also lightweight as described in the
original Naugty Dog video. However it was C++ only ABI. Luckly it also
exported the C ABI `fcontext`. That is until Boost version 1.61, where the API
was made private. So I just copied the boost 1.60 assembler code and included
in this project.

### Stack Safety

As the fiber stacks are created as a large flat array, there are no protections
on stack overflow. An overflow will just walk into the neighbouring fiber's
stack and corrupt it.

Set `SEW_STACK_GUARD` if you suspect that's happening.

### Thread Local Storage

Fundemental to the fiber code is thread local storage (TLS). This is used as a
way for the system to communicate between jobs. However I don't use an OS
provided TLS, but instead roll my own by using a lookup based on the current
thread id. Why?

`__thread` on GCC is a GCC specific extension.
`__declspec( thread )` on windows only supports statically linked libraries.
`thread_local` in C++11 is, well, C++11 only, and not supported in C.
`TlsAlloc` and friends is just too much work for me to support, I'm lazy.
`_Thread_local` in C11 would be nice, except thread support in C11 is optional.

### How Fast

Good question, I don't know.

On my i7-4790 CPU @ 3.60GHz, Running on linux with`-g -O2` using the
'Empty Jobs' benchmark, I get between 100ns to 150ns per empty job called.
I think in the scheme of things thats pretty slow for a context based library.

### Make It Faster

Ideas for making it faster:

Don't swap fibers to find a new job. If there are no old fiber to resume then
just run another job, instead of putting the fiber on the free list and making
a new fiber. For the empty job benchmark this can do a 30% speed inprovment.
The reason I didn't implement this, is that it prevents using a dual-stack
job system as described in the 'Naughty Dog Engine' (which I intend to implement
eventually).

The free and wait list implementations are pretty naive. I'm pretty sure there
is a better lockless way of accessing them as opposed to looping over them. I
suspect there is a lot of cache churn and false sharing around index 0 between
lots of threads.

Have someone else look at the code, point out any stupids that can easly be
fixed.

Tutorial
========

Hello World
-----------

### Setup

The sewing library is malloc free. That means it will tell you how much
memory it needs and it's your job to allocate it.

```
    #include "sewing.h"

    #include <malloc.h>
    // malloc, free

    #include <stdio.h>
    // printf

    int main(int argc, char** argv)
    {
        (void) argc;
        (void) argv;

        size_t bytes = sew_it
        (
              NULL               // Set to null to get the required memory size
            , 32 * 1024          // 32kb stack per fiber
            , 4                  // 4 threads (including this one)
            , 10                 // Job queue will be (1 << 10) entires large
            , 128                // we can have 128 fibers on the go at once
            , main_procedure     // User defined entry for the sewing system.
            , NULL               // User defined argument for 'main_procedure'
        );

        void* sewing = malloc(bytes);

        sew_it
        (
              sewing            // Memory the sewing system will use internally.
            , 32 * 1024
            , 4
            , 10
            , 128
            , main_procedure
            , NULL
        );
        // This will call 'main_procedure' and block until it has finished.
        // once finished the user is free to release memory allocated for the sewing
        // system.

        free(sewing);

        return 0;
    }
```

The `main_procedure` will need a function signature as follows:

```
    void main_procedure
    (
          struct Sewing*         sewing
        , Sew_Thread*            threads
        , size_t                 thread_count
        , Sew_Procedure_Argument procedure_argument
    )
    {
        // 'sewing' is the internal state required by any call to the sewing
        // API.

        // 'threads[thread_count]' are OS specific thread handles passed back to
        // the user. This gives the user the ability to adjust thread affinity,
        // priority, security, or any other such thread based tweaks.

        // 'procedure_argument' is the value passed to the call to 'sew_it'.

        // When this function exits, all threads will be destroyed and
        // 'sew_it' will return.
    }
```

### Jobs

Jobs (refered to as `stitches`) are given their own fiber and stack, and can
call, and wait on any other jobs. Lets reuse the one in the example.

```
    void hello_something(Sew_Procedure_Argument argument)
    {
        unsigned world = (unsigned) (size_t) argument;

        printf("Hello world %d\n", world);
    }
```

Now lets modify `main_procedure` to call it.

```
    void main_procedure
    (
          struct Sewing*         sewing
        , Sew_Thread*            threads
        , size_t                 thread_count
        , Sew_Procedure_Argument procedure_argument
    )
    {
        // 'sewing' is the internal state required by any call to the sewing
        // API.

        // 'threads[thread_count]' are OS specific thread handles passed back to
        // the user. This gives the user the ability to adjust thread affinity,
        // priority, security, or any other such thread based tweaks.

        // 'procedure_argument' is the value passed to the call to 'sew_it'.

        // ---- NEW ------

        // remove Unused variable warnings
        (void) threads;
        (void) thread_count;
        (void) procedure_argument;

        Sew_Stitch jobs[10];

        for (unsigned i = 0; i < 10; i++)
        {
            jobs[i].procedure = hello_something;
            jobs[i].argument  = (Sew_Procedure_Argument) (size_t) i;
            jobs[i].name      = "hello_something";
        }

        sew_stiches_and_finish(sewing, jobs, 10);
        // call will wait until all ten jobs are finsihed.

        // ---- NEW ------

        // When this function exits, all threads will be destroyed and
        // 'sew_it' will return.
    }
```

Asynchronously
--------------

How about doing it asynchronously?

If you use `sew_stiches` (instead of`sew_stiches_and_finish`), then you can
keep working while the jobs are run in the background until you wait for them to
finish using `sew_finish`. Just like an asynchronously system.

So lets modify the `main_procedure`

```
    void main_procedure
    (
          struct Sewing*         sewing
        , Sew_Thread*            threads
        , size_t                 thread_count
        , Sew_Procedure_Argument procedure_argument
    )
    {
        // 'sewing' is the internal state required by any call to the sewing
        // API.

        // 'threads[thread_count]' are OS specific thread handles passed back to
        // the user. This gives the user the ability to adjust thread affinity,
        // priority, security, or any other such thread based tweaks.

        // 'procedure_argument' is the value passed to the call to 'sew_it'.

        // remove Unused variable warnings
        (void) threads;
        (void) thread_count;
        (void) procedure_argument;

        Sew_Stitch jobs[10];

        for (unsigned i = 0; i < 10; i++)
        {
            jobs[i].procedure = hello_something;
            jobs[i].argument  = (Sew_Procedure_Argument) (size_t) i;
            jobs[i].name      = "hello_something";
        }

        // ---- NEW ------

        //sew_stiches_and_finish(sewing, jobs, 10);
        // call will wait until all ten jobs are finsihed.

        Sew_Chain future;

        sew_stiches(sewing, jobs, 10, &future);
        // This returns straight away, jobs run in background. 'future' is
        // updated with a handle that we can wait on later.
        // If it's set to NULL, then this is a fire and forget.

        Sew_Stitch jobs_100[100];

        for (unsigned i = 0; i < 100; i++)
        {
            jobs_100[i].procedure = hello_something;
            jobs_100[i].argument  = (Sew_Procedure_Argument) (size_t) (100 + i);
            jobs_100[i].name      = "hello_something";
        }

        sew_stiches_and_finish(sewing, jobs_100, 100);

        sew_finish(sewing, future);
        // NOW we wait for the first set of jobs to finish.

        // ---- NEW ------

        // When this function exits, all threads will be destroyed and
        // 'sew_it' will return.
    }
```
