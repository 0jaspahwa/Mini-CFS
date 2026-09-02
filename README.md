# Preemptive CPU scheduler (vruntime / CFS style)

A simulator of how Linux picks the next task to run. C++.

Each task has a **vruntime** - how much CPU it has used. One rule:

> run the smallest vruntime for one quantum, add that time to it, put it back, repeat.

No code takes turns. Fair sharing falls out of that rule.

## Run it

```bash
g++ -O2 -std=c++17 experiments.cpp -o experiments
```

```bash
./experiments
```

Full output is in `results.txt`. All numbers below come from it.

## 1. Plain BST vs treap

vruntime only goes up, so every insert lands on the far right and the tree
becomes a straight line. The treap's random priority fixes the shape.

| tasks | BST height | treap height | BST work/tick | treap work/tick |
| --- | --- | --- | --- | --- |
| 64 | 64 | 12 | 65 | 10 |
| 256 | 256 | 17 | 257 | 13 |
| 1024 | 1024 | 22 | 1025 | 16 |

BST height = task count. That is a linked list, not a tree.

## 2. Caching "who runs next"

That question is asked far more often than the tree changes. Cache a pointer
to the smallest node instead of walking down every time.

| tasks | nodes touched, no cache | nodes touched, cached |
| --- | --- | --- |
| 16 | 3.4 | 0 |
| 256 | 6.1 | 0 |
| 4096 | 8.9 | 0 |

The cache is free to keep correct. Insert checks it with one comparison, and
removing the smallest finds the next one on the same trip down.

(Wall clock timing here is mostly noise, so node counts are the honest number.)

## 3. Fair share

8 same tasks, 200000 ticks. 800000 ms of CPU split 8 ways = 100000 ms each.

| | |
| --- | --- |
| lowest task got | 100000 ms |
| highest task got | 100000 ms |

Priority is not separate code. When a task runs, its vruntime goes up by
`time / weight`. Heavy tasks grow slower, so they look smallest more often
and get picked more. Share should be weight / total weight.

| nice | weight | should get | got |
| --- | --- | --- | --- |
| -5 | 3121 | 55.59% | 55.59% |
| 0 | 1024 | 18.24% | 18.24% |
| +5 | 335 | 5.97% | 5.97% |
| +10 | 110 | 1.96% | 1.96% |

Worst error: 0.0058%.

## 4. Four cores

Each core gets its own tree, no shared lock. But tasks stay on the core that
made them, so work piles up. Stealing fixes it. Two versions:

- **steal when idle** - take work only if this core has nothing to run
- **steal on imbalance** - take work if another core has 2+ more tasks

**Test 1.** 32 tasks x 100 quanta, all made on core 0. Best possible is
32 x 100 / 4 = 800 ticks.

| policy | finished in | vs best | idle core-ticks |
| --- | --- | --- | --- |
| no stealing | 3200 | 4.00x | 9600 |
| steal when idle | 812 | 1.01x | 48 |
| steal on imbalance | 800 | 1.00x | 0 |

No stealing means core 0 does all 3200 quanta alone while 3 cores sit idle.

**Test 2.** 4 same never-ending tasks, one per core, plus 40 short tasks
dumped on core 0 every 8000 ticks. The 4 should end up equal.

| policy | least CPU | most CPU | gap | longest wait |
| --- | --- | --- | --- | --- |
| no stealing | 35264 ms | 800000 ms | 22.69x | 800 ticks |
| steal when idle | 35264 ms | 800000 ms | 22.69x | 800 ticks |
| steal on imbalance | 415904 ms | 416096 ms | 1.00x | 18 ticks |

Row 1: the task on core 0 fights all the dumped work. The task on core 3 has
its core to itself. Same task, 22x apart, just because of where it started.

"Steal when idle" scores the same as no stealing. Every core already has a
task, so no core is ever empty and it never fires.

## 5. Moving a task between cores

vruntime is a position in one queue, not a clock. Two cores, 10000 ticks:

- **Core A**, 1 task: runs every tick, vruntime hits 40000 ms
- **Core B**, 20 tasks: each runs 1 tick in 20, vruntime hits 2000 ms

Both cores worked the same. Their numbers are 38000 ms apart anyway.

Move one task from B to A. Keeping the raw number, it arrives at 2000 while
the resident sits at 40000. Smallest always wins, so the newcomer runs
38000 / 4 = 9500 ticks straight. Re-timing puts it at core A's floor instead,
so both start level.

| on arrival | resident got | migrant got | resident waited |
| --- | --- | --- | --- |
| re-timed | 50.0% | 50.0% | 1 tick |
| raw vruntime | 26.2% | 73.8% | 9500 ticks |

One line of difference. Nothing crashes, nothing gets logged. The rule is
followed correctly on numbers that mean the wrong thing.

## Known gap

The balancer compares task count between cores, not weight. Per core the
sharing is exact. Across cores it is not, because a core of heavy tasks and
a core of light tasks look the same to it.

Worst share error with mixed nice levels: **73.23%**. With equal weights: 0%.

Fix is to balance on total weight. Not done yet.

## Files

| file | what it is |
| --- | --- |
| `scheduler.hpp` | treap run queue, the cache, one tick |
| `bst_scheduler.hpp` | same queue without the treap, for comparison |
| `multicore.hpp` | per-core queues and work stealing |
| `experiments.cpp` | the tests behind every number above |
| `bst.cpp`, `task.cpp`, `treap_task.cpp` | earlier steps |
| `ARCHITECTURE.md` | longer writeup: decisions and rejected options |
