# Phase 1 architecture and concurrency model

## Ownership and synchronization

The pool owns its threads, queue, mutexes, condition variable, and lifecycle state.
Every queue access and lifecycle read/write holds `queue_mutex_`. Tasks leave the
queue via move, then execute and release their captured resources outside that
mutex. No user task runs under a scheduler lock. Each successful insertion has
one matching dequeue; no worker can see the same queue entry twice.

`shutdown_mutex_` serializes external shutdown callers and protects thread joins.
The only nested lock order is shutdown mutex then queue mutex. Workers and
submitters acquire only the queue mutex. Shutdown releases the queue mutex before
joining, so workers can continue draining. The relaxed atomic failure counter
provides a standalone count, not a publication mechanism for task results.

## Wakeups and worker lifecycle

Construction rejects zero workers, reserves thread storage, then starts workers.
If creation fails midway, the constructor drains and joins the workers already
created before rethrowing. Allocation failure before the first thread needs no
cleanup. All member synchronization objects exist before threads start.

Workers wait using the predicate `state != Running || !queue.empty()` under the
queue mutex. Waiting atomically releases the mutex and sleeps; reacquisition and
predicate checking handle spurious wakeups. Submit publishes queue changes under
the same mutex then notifies one worker. Shutdown publishes Draining and notifies
all workers, including idle ones. Persistent predicate state avoids lost wakeups
even if notification happens before a worker begins waiting.

When awakened, a worker takes one queued task or exits if the queue is empty and
shutdown has started. An exception in user code increments the failure counter
and does not terminate the worker. Thread-local pool identity rejects self-join
before taking the shutdown mutex (including while another caller is joining).

## Shutdown linearization

Running -> Draining -> Stopped is monotonic. Submission racing with shutdown is
accepted exactly when its queue insertion wins the queue lock before the Draining
transition. Rejected submissions throw synchronously and are never queued.
Shutdown joins running workers as well as draining queued tasks. Return from join
synchronizes with worker completion, so callers may safely inspect task-written
state afterward if tasks did not race with each other while writing it.

The destructor is RAII cleanup, not a cure for invalid object lifetime: callers
must prevent concurrent access during destruction. Tasks must not destroy their
own pool. Infinite tasks or dependency deadlocks prevent graceful shutdown.

## Review limits

Partial worker creation cleanup is covered by code inspection, not deterministic
fault injection. Tests exercise FIFO with one worker because scheduling after
dequeue is controlled by the OS. Multithreaded stress testing and sanitizers
increase confidence but cannot prove absence of all possible races.
