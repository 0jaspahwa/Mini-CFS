// experiments.cpp - every number here is measured fresh, not typed in.
// build: g++ -O2 -std=c++17 experiments.cpp -o experiments

#include <bits/stdc++.h>
#include "scheduler.hpp"
#include "bst_scheduler.hpp"
#include "multicore.hpp"

using namespace std;
using namespace std::chrono;

static const long long MS      = 1000000LL;   // ns
static const long long QUANTUM = 4 * MS;      // 4 ms slice

// ideal share is proportional to weight, so this covers both
// the equal-weight and mixed-nice runs
struct Fairness {
    double jain      = 0;   // 1.0 is perfect
    double maxDevPct = 0;   // worst task, % off its ideal share
};

static Fairness measureFairness(const vector<Task>& ts) {
    Fairness f;
    if(ts.empty()) return f;
    double totalW = 0, totalC = 0;
    for(const auto& t : ts) { totalW += t.weight; totalC += (double)t.cpuTime; }
    if(totalC == 0 || totalW == 0) return f;

    double s = 0, s2 = 0;
    for(const auto& t : ts) {
        double ideal = totalC * (t.weight / totalW);
        double r     = (double)t.cpuTime / ideal;
        s += r; s2 += r * r;
        f.maxDevPct = max(f.maxDevPct, fabs(r - 1.0) * 100.0);
    }
    f.jain = (s * s) / (ts.size() * s2);
    return f;
}

static void banner(const string& title) {
    cout << "\n" << string(66, '=') << "\n" << title << "\n"
         << string(66, '=') << "\n";
}

// experiment 0 - broken invariants don't crash, they just make
// wrong picks silently. checked under churn: tasks join and leave.
static void experiment0() {
    banner("0.  invariants under churn");

    mt19937 rng(12345);
    RunQueue rq;
    int nextPid = 1;
    long long checks = 0;
    bool ok = true;

    for(int i = 0; i < 40; i++) rq.admit(Task(nextPid++), 0);

    for(int step = 0; step < 20000 && ok; step++) {
        runOneTick(rq, QUANTUM);

        if(rng() % 8 == 0) rq.admit(Task(nextPid++), 0);
        if(rng() % 8 == 0 && rq.size() > 4) { Task junk; rq.popMax(junk); }

        if(step % 25 == 0) { ok = rq.validate(); checks++; }
    }

    cout << "  ticks simulated      : 20000\n";
    cout << "  full validations     : " << checks << "\n";
    cout << "  bst order / heap / cached-leftmost / count : "
         << (ok ? "all hold" : "BROKEN") << "\n";
}

// experiment 1 - vruntime only goes up, so every insert lands
// far right. that's the bst worst case, and it's the only case here.
static void experiment1() {
    banner("1.  tree shape under monotonically increasing keys");

    cout << left
         << setw(8)  << "tasks"
         << setw(10) << "log2(n)"
         << setw(12) << "BST h"
         << setw(16) << "treap h (mean)"
         << setw(12) << "treap h max"
         << setw(14) << "BST vis/tick"
         << setw(14) << "treap vis/tick" << "\n";
    cout << string(86, '-') << "\n";

    for(int n = 8; n <= 1024; n *= 2) {
        long long ticks  = 20LL * n;
        long long sample = max(1LL, ticks / 50);

        BstRunQueue bst;
        RunQueue    treap;
        for(int i = 1; i <= n; i++) { bst.admit(Task(i), 0); treap.admit(Task(i), 0); }

        bst.stats.reset();
        treap.stats.reset();

        // sampled through the run - one snapshot of a random
        // tree is mostly luck, not signal
        double hSum = 0; long long hN = 0; int hMax = 0;
        for(long long t = 0; t < ticks; t++) {
            Task a;
            bst.popMin(a);
            a.cpuTime  += QUANTUM;
            a.vruntime += QUANTUM;
            bst.enqueue(a);
            bst.advanceFloor();

            runOneTick(treap, QUANTUM);

            if(t % sample == 0) {
                int h = treap.height();
                hSum += h; hN++; hMax = max(hMax, h);
            }
        }

        cout << left
             << setw(8)  << n
             << setw(10) << fixed << setprecision(1) << log2((double)n)
             << setw(12) << bst.height()
             << setw(16) << setprecision(1) << hSum / hN
             << setw(12) << hMax
             << setw(14) << setprecision(1) << (double)bst.stats.visits / ticks
             << setw(14) << (double)treap.stats.visits / ticks << "\n";
    }

    cout << "\n  BST height = task count. It's a linked list, not the worst\n"
            "  case it hits sometimes - the only case it has.\n";
}

// EXPERIMENT 2 - "who's next" gets asked far more than the tree
// changes. Without a cache, each ask walks the left spine.
static const uint64_t E2_SEED = 0xA5A5A5A5DEADBEEFull;

// mode 0 ticks only, 1 walking peeks, 2 cached peeks. same seed
// and warmup for all three. K=32 batches peeks above timer noise.
static const int E2_K = 32;

static double timeLoop(int n, long long T, int mode) {
    RunQueue rq(E2_SEED);
    for(int i = 1; i <= n; i++) rq.admit(Task(i), 0);
    for(int t = 0; t < 5 * n; t++) runOneTick(rq, QUANTUM);

    volatile long long sink = 0;
    auto t0 = steady_clock::now();
    for(long long t = 0; t < T; t++) {
        if(mode == 1)      for(int k = 0; k < E2_K; k++) sink += rq.peekWalk()->pid;
        else if(mode == 2) for(int k = 0; k < E2_K; k++) sink += rq.peek()->pid;
        runOneTick(rq, QUANTUM);
    }
    auto t1 = steady_clock::now();
    (void)sink;
    return duration<double, nano>(t1 - t0).count() / (double)T;
}

// best of 3: the min is the run least disturbed by anything else
static double bestOf3(int n, long long T, int mode) {
    double b = timeLoop(n, T, mode);
    for(int i = 0; i < 2; i++) b = min(b, timeLoop(n, T, mode));
    return b;
}

static void experiment2() {
    banner("2.  cost of the most-asked question");

    const long long T = 200000;

    cout << "  cost of one \"who runs next\", averaged over " << T
         << " tree states.\n"
            "  a single static tree is a bad measurement - leftmost node depth\n"
            "  is randomised, so one snapshot mostly reports luck.\n\n";

    cout << left
         << setw(8)  << "tasks"
         << setw(18) << "walk visits/query"
         << setw(20) << "cached visits/query"
         << setw(16) << "walk ns/query"
         << setw(16) << "cached ns/query" << "\n";
    cout << string(78, '-') << "\n";

    for(int n = 16; n <= 4096; n *= 4) {
        RunQueue rq(E2_SEED);
        for(int i = 1; i <= n; i++) rq.admit(Task(i), 0);
        for(int t = 0; t < 5 * n; t++) runOneTick(rq, QUANTUM);

        long long walkVis = 0, cachVis = 0;
        for(long long t = 0; t < T; t++) {
            long long b = rq.stats.visits;
            rq.peekWalk();
            walkVis += rq.stats.visits - b;

            b = rq.stats.visits;
            rq.peek();
            cachVis += rq.stats.visits - b;

            runOneTick(rq, QUANTUM);
        }

        double base = bestOf3(n, T, 0);
        double walk = bestOf3(n, T, 1);
        double cach = bestOf3(n, T, 2);

        cout << left << setw(8) << n
             << setw(18) << fixed << setprecision(2) << (double)walkVis / T
             << setw(20) << (double)cachVis / T
             << setw(16) << max(0.0, walk - base) / E2_K
             << setw(16) << max(0.0, cach - base) / E2_K << "\n";
    }

    // the other half of the trade: keeping the cache correct
    // has to cost less than the queries it saves
    cout << "\n  per full tick (pick + charge + reinsert), cache kept correct:\n\n";
    cout << left << setw(8) << "tasks" << setw(16) << "visits/tick"
         << setw(16) << "ns/tick" << setw(14) << "2*ln(n)" << "\n";
    cout << string(54, '-') << "\n";

    for(int n = 16; n <= 4096; n *= 4) {
        RunQueue rq(E2_SEED);
        for(int i = 1; i <= n; i++) rq.admit(Task(i), 0);
        for(int t = 0; t < 5 * n; t++) runOneTick(rq, QUANTUM);

        rq.stats.reset();
        auto t0 = steady_clock::now();
        for(long long t = 0; t < T; t++) runOneTick(rq, QUANTUM);
        auto t1 = steady_clock::now();

        cout << left << setw(8) << n
             << setw(16) << fixed << setprecision(2) << (double)rq.stats.visits / T
             << setw(16) << duration<double, nano>(t1 - t0).count() / T
             << setw(14) << setprecision(1) << 2.0 * log((double)n) << "\n";
    }

    cout << "\n  maintaining the cache is basically free: insert updates it with\n"
            "  one comparison, and popMin finds the next leftmost in the same\n"
            "  descent it already makes. nothing walks the tree twice.\n";
}

// EXPERIMENT 3 - nothing here takes turns or tracks whose go it
// is. one comparison, run in a loop. this is what it produces.
static void experiment3() {
    banner("3.  fairness, from one comparison");

    // 3a: what the picks look like
    {
        RunQueue rq;
        for(int i = 1; i <= 4; i++) rq.admit(Task(i), 0);
        cout << "  4 equal tasks, first 20 picks:\n    ";
        for(int t = 0; t < 20; t++) {
            TickResult r = runOneTick(rq, QUANTUM);
            cout << r.task.pid << " ";
        }
        cout << "\n  round robin. no code asked for it.\n";
    }

    // 3b: equal weights, long run
    {
        const int N = 8;
        const long long TICKS = 200000;
        RunQueue rq;
        for(int i = 1; i <= N; i++) rq.admit(Task(i), 0);
        for(long long t = 0; t < TICKS; t++) runOneTick(rq, QUANTUM);

        vector<Task> ts; rq.collect(ts);
        Fairness f = measureFairness(ts);

        long long lo = LLONG_MAX, hi = LLONG_MIN;
        long long cLo = LLONG_MAX, cHi = LLONG_MIN;
        for(const auto& t : ts) {
            lo = min(lo, t.vruntime); hi = max(hi, t.vruntime);
            cLo = min(cLo, t.cpuTime); cHi = max(cHi, t.cpuTime);
        }

        cout << "\n  8 equal tasks, " << TICKS << " ticks:\n";
        cout << "    cpu time, min .. max   : " << cLo / MS << " ms .. "
             << cHi / MS << " ms\n";
        cout << "    ticks apart            : " << (cHi - cLo) / QUANTUM << "\n";
        cout << "    vruntime spread        : " << (hi - lo) / MS
             << " ms  (one slice is " << QUANTUM / MS << " ms)\n";
        cout << "    worst share deviation  : " << fixed << setprecision(4)
             << f.maxDevPct << " %\n";
        cout << "    jain fairness index    : " << setprecision(6) << f.jain << "\n";
    }

    // 3c: mixed nice levels
    {
        const long long TICKS = 400000;
        vector<int> nices = {-5, 0, 0, 5, 10};
        RunQueue rq;
        for(size_t i = 0; i < nices.size(); i++)
            rq.admit(Task((int)i + 1, nices[i]), 0);
        for(long long t = 0; t < TICKS; t++) runOneTick(rq, QUANTUM);

        vector<Task> ts; rq.collect(ts);
        sort(ts.begin(), ts.end(), [](const Task& a, const Task& b){ return a.pid < b.pid; });

        double totalW = 0, totalC = 0;
        for(const auto& t : ts) { totalW += t.weight; totalC += (double)t.cpuTime; }

        cout << "\n  mixed nice levels, " << TICKS << " ticks:\n\n";
        cout << left << setw(6) << "pid" << setw(8) << "nice" << setw(10) << "weight"
             << setw(14) << "ideal share" << setw(14) << "actual share"
             << setw(10) << "error" << "\n";
        cout << string(60, '-') << "\n";
        for(const auto& t : ts) {
            double ideal  = t.weight / totalW;
            double actual = (double)t.cpuTime / totalC;
            cout << left << setw(6) << t.pid << setw(8) << t.nice
                 << setw(10) << t.weight
                 << setw(14) << fixed << setprecision(4) << ideal
                 << setw(14) << actual
                 << setw(10) << setprecision(4) << (actual - ideal) / ideal * 100.0
                 << "\n";
        }
        Fairness f = measureFairness(ts);
        cout << "\n    worst share deviation  : " << setprecision(4) << f.maxDevPct << " %\n";
        cout << "    jain fairness index    : " << setprecision(6) << f.jain << "\n";
        cout << "  priority is one division in the vruntime update. no separate\n"
                "  priority code.\n";
    }
}

// experiment 4 - per-core queues and work stealing. A is
// throughput, B is fairness under churn, C isolates migration.
static void scenarioA(Balance policy) {
    const int  CORES = 4;
    const int  TASKS = 32;
    const long long WORK = 100 * QUANTUM;   // every task wants the same

    MultiCore mc(CORES, QUANTUM, policy);
    for(int i = 1; i <= TASKS; i++)
        mc.place(Task(i, 0, WORK), 0);       // all on core 0

    mc.runUntilDone(200000);

    long long ideal = (long long)TASKS * 100 / CORES;   // perfectly packed
    vector<Task> ts = mc.allTasks();
    long long maxMig = 0;
    for(const auto& t : ts) maxMig = max<long long>(maxMig, t.migrations);

    cout << left << setw(22) << balanceName(policy)
         << setw(12) << mc.ticksRun()
         << setw(10) << ideal
         << setw(14) << fixed << setprecision(2)
         << (double)mc.ticksRun() / ideal
         << setw(14) << mc.idleTotal()
         << setw(10) << mc.stealCount() << "\n";
}

// migrations need to happen late - at tick zero every clock
// matches, so a bad migration and a good one look the same.
static void scenarioB(Balance policy) {
    const int  CORES  = 4;
    const int  CANARIES = 4;
    const long long TICKS  = 200000;
    const long long BURST_EVERY = 8000;
    const int  BURST_SIZE = 40;
    const long long BURST_WORK = 400 * QUANTUM;

    MultiCore mc(CORES, QUANTUM, policy);
    for(int i = 0; i < CANARIES; i++) mc.place(Task(i + 1), i);

    int pid = CANARIES + 1;
    for(long long t = 0; t < TICKS; t++) {
        if(t % BURST_EVERY == 0 && t > 0)
            for(int i = 0; i < BURST_SIZE; i++)
                mc.place(Task(pid++, 0, BURST_WORK), 0);
        mc.tick();
    }

    vector<Task> ts = mc.allTasks();

    // canaries are the measurement - counting everything would
    // be dominated by short tasks that arrived at random times
    vector<Task> canaries;
    for(const auto& t : ts) if(t.pid <= CANARIES) canaries.push_back(t);

    long long lo = LLONG_MAX, hi = 0;
    for(const auto& t : canaries) { lo = min(lo, t.cpuTime); hi = max(hi, t.cpuTime); }

    long long migs = 0, maxWait = 0, done = 0;
    for(const auto& t : ts) {
        migs += t.migrations;
        if(t.remaining == 0) done++;
        long long w = t.maxWaitTicks;
        if(t.remaining != 0 && t.lastRanTick >= 0)          // still queued at the end
            w = max(w, mc.ticksRun() - t.lastRanTick - 1);
        maxWait = max(maxWait, w);
    }

    Fairness f = measureFairness(canaries);

    cout << left << setw(22) << balanceName(policy)
         << setw(11) << lo / MS
         << setw(11) << hi / MS
         << setw(10) << fixed << setprecision(2)
         << (lo == 0 ? 0.0 : (double)hi / (double)lo)
         << setw(11) << setprecision(4) << f.jain
         << setw(9)  << done
         << setw(12) << migs
         << setw(12) << maxWait
         << setw(12) << mc.floorSpread() / MS << "\n";
}

// migration in isolation. vruntime is a position in one queue's
// history, not a time - so drift two clocks apart, then migrate.
static void scenarioC(bool normalize) {
    const int  CROWD   = 20;
    const long long DIVERGE = 10000;
    const long long MEASURE = 20000;

    RunQueue fast;    // 1 task, clock runs at full speed
    RunQueue busy;    // 20 tasks, clock crawls

    fast.admit(Task(1), 0);
    for(int i = 2; i <= CROWD + 1; i++) busy.admit(Task(i), 0);

    for(long long t = 0; t < DIVERGE; t++) {
        runOneTick(fast, QUANTUM, t);
        runOneTick(busy, QUANTUM, t);
    }

    long long drift = fast.floorVruntime() - busy.floorVruntime();

    Task mig;
    long long busyFloor = busy.floorVruntime();
    busy.popMax(mig);
    if(normalize) fast.admit(mig, mig.vruntime - busyFloor);
    else          fast.admitRaw(mig);

    long long residentCpu = 0, migrantCpu = 0, worstWait = 0;
    for(long long t = 0; t < MEASURE; t++) {
        TickResult r = runOneTick(fast, QUANTUM, DIVERGE + t);
        if(!r.ran) continue;
        if(r.task.pid == 1) { residentCpu += r.slice; worstWait = max(worstWait, r.task.maxWaitTicks); }
        else                  migrantCpu  += r.slice;
    }

    double total = (double)(residentCpu + migrantCpu);
    cout << left << setw(16) << (normalize ? "re-timed" : "raw vruntime")
         << setw(14) << drift / MS
         << setw(16) << fixed << setprecision(1) << 100.0 * residentCpu / total
         << setw(16) << 100.0 * migrantCpu / total
         << setw(16) << worstWait << "\n";
}

static void experiment4() {
    banner("4.  per-core queues and work stealing");

    cout << "  A. throughput: 32 equal tasks, all dumped on core 0 of 4\n\n";
    cout << left << setw(22) << "policy" << setw(12) << "ticks"
         << setw(10) << "ideal" << setw(14) << "vs ideal"
         << setw(14) << "idle core-ticks" << setw(10) << "steals" << "\n";
    cout << string(82, '-') << "\n";
    scenarioA(Balance::None);
    scenarioA(Balance::IdleOnly);
    scenarioA(Balance::Imbalance);
    scenarioA(Balance::ImbalanceRaw);

    cout << "\n  B. fairness under churn: 4 never-ending tasks, one per core,\n"
            "     plus 40 short tasks dumped on core 0 every 8000 ticks. the 4\n"
            "     are identical, so they should end with identical CPU time.\n\n";
    cout << left << setw(22) << "policy" << setw(11) << "min ms"
         << setw(11) << "max ms" << setw(10) << "max/min"
         << setw(11) << "jain" << setw(9) << "done"
         << setw(12) << "migrations"
         << setw(12) << "worst wait" << setw(12) << "drift ms" << "\n";
    cout << string(110, '-') << "\n";
    scenarioB(Balance::None);
    scenarioB(Balance::IdleOnly);
    scenarioB(Balance::Imbalance);
    scenarioB(Balance::ImbalanceRaw);

    cout << "\n     idle-only never fires here - every core always has its own\n"
            "     task, so none is ever empty, and core 0 drowns anyway. same\n"
            "     score as no stealing at all.\n"
            "     raw vruntime is NOT broken in this test: a balancer that keeps\n"
            "     counts level also keeps clocks close, so re-timing has nothing\n"
            "     to fix. that's a real result, and it's why C exists.\n";

    cout << "\n  C. one migration, across cores whose clocks drifted apart.\n"
            "     core A ran 1 task for 10k ticks, core B ran 20. then one task\n"
            "     moves from B to A. watch A's resident task.\n\n";
    cout << left << setw(16) << "on arrival" << setw(14) << "drift ms"
         << setw(16) << "resident cpu%" << setw(16) << "migrant cpu%"
         << setw(16) << "resident wait" << "\n";
    cout << string(76, '-') << "\n";
    scenarioC(true);
    scenarioC(false);
    cout << "\n     same steal, same task. only difference: one line placing the\n"
            "     arrival against the local floor.\n";

    cout << "\n  D. known gap: balancer moves tasks by count, not weight.\n"
            "     same setup, mixed nice levels, best policy:\n\n";
    {
        MultiCore mc(4, QUANTUM, Balance::Imbalance);
        int pid = 1;
        for(int i = 0; i < 9; i++) mc.place(Task(pid++, (i % 3) * 5 - 5), 0);
        for(int i = 0; i < 3; i++) mc.place(Task(pid++, 0), 1);
        mc.runFor(100000);
        vector<Task> ts = mc.allTasks();
        Fairness f = measureFairness(ts);
        cout << "    worst share deviation  : " << fixed << setprecision(2)
             << f.maxDevPct << " %\n";
        cout << "    jain fairness index    : " << setprecision(5) << f.jain << "\n";
        cout << "    per core it's still exact. globally it's not - a core of\n"
                "    heavy tasks and a core of light ones can have the same task\n"
                "    count. fix is balancing on summed weight. not done yet.\n";
    }
}

int main() {
    cout << "preemptive vruntime scheduler: measurements\n";
    cout << "quantum " << QUANTUM / MS << " ms, nice-0 weight " << NICE_0_WEIGHT << "\n";

    experiment0();
    experiment1();
    experiment2();
    experiment3();
    experiment4();

    cout << "\n";
    return 0;
}
