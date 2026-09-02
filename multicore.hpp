#pragma once

// multicore.hpp - one run queue per core, no lock, no shared tree.
// cores drift apart on their own, so work stealing pulls tasks back level.

#include "scheduler.hpp"
#include <memory>
#include <string>
#include <climits>

enum class Balance {
    None,           // per-core queues, no migration
    IdleOnly,       // steal only when this core has nothing to run
    Imbalance,      // steal when it actually helps the balance
    ImbalanceRaw    // same, but skip re-timing (broken on purpose)
};

inline const char* balanceName(Balance b) {
    switch(b) {
        case Balance::None:         return "no stealing";
        case Balance::IdleOnly:     return "steal when idle";
        case Balance::Imbalance:    return "steal on imbalance";
        case Balance::ImbalanceRaw: return "imbalance, raw vrt";
    }
    return "?";
}

struct Core {
    RunQueue  rq;
    int       id        = 0;
    long long idleTicks = 0;
    long long busyTicks = 0;
    explicit Core(int i, uint64_t seed) : rq(seed), id(i) {}
};

class MultiCore {
public:
    MultiCore(int numCores, long long quantum_, Balance policy_,
              uint64_t seed = 0xDEADBEEFCAFEull)
        : quantum(quantum_), policy(policy_) {
        for(int i = 0; i < numCores; i++)
            cores.push_back(std::make_unique<Core>(i, seed + 0x1000 * i));
    }

    // new task starts level with whoever's least served on the
    // target queue, so it neither starves nor hogs on arrival
    void place(Task t, int core) {
        t.homeCore = core;
        cores[core]->rq.admit(t, 0);
        live++;
    }

    // one global tick: rebalance, then every core runs one slice
    bool tick() {
        if(live == 0) return false;

        for(auto& c : cores) rebalance(*c);

        for(auto& c : cores) {
            TickResult r = runOneTick(c->rq, quantum, ticks);
            if(!r.ran) { c->idleTicks++; continue; }
            c->busyTicks++;
            if(r.finished) {
                finished.push_back(r.task);
                live--;
            }
        }
        ticks++;

        // how far the per-core clocks have drifted - the exact
        // size of the mistake an un-re-timed migration would make
        long long lo = LLONG_MAX, hi = LLONG_MIN;
        for(auto& c : cores) {
            long long f = c->rq.floorVruntime();
            lo = std::min(lo, f); hi = std::max(hi, f);
        }
        maxFloorSpread = std::max(maxFloorSpread, hi - lo);

        return true;
    }

    void runUntilDone(long long maxTicks = 100000000LL) {
        while(ticks < maxTicks && tick()) {}
    }

    void runFor(long long n) {
        for(long long i = 0; i < n; i++) if(!tick()) break;
    }

    // finished tasks plus whatever's still queued
    std::vector<Task> allTasks() const {
        std::vector<Task> out = finished;
        for(auto& c : cores) c->rq.collect(out);
        return out;
    }

    int  coreCount() const { return (int)cores.size(); }
    Core& core(int i)      { return *cores[i]; }

    long long ticksRun()   const { return ticks; }
    long long stealCount() const { return steals; }
    long long floorSpread() const { return maxFloorSpread; }
    long long idleTotal()  const {
        long long s = 0;
        for(auto& c : cores) s += c->idleTicks;
        return s;
    }
    long long visitTotal() const {
        long long s = 0;
        for(auto& c : cores) s += c->rq.stats.visits;
        return s;
    }
    int liveCount() const { return live; }

private:
    std::vector<std::unique_ptr<Core>> cores;
    std::vector<Task> finished;
    long long quantum;
    Balance   policy;
    long long ticks  = 0;
    long long steals = 0;
    long long maxFloorSpread = 0;
    int       live   = 0;

    // IdleOnly fires only at zero tasks, so 7-vs-1 never triggers.
    // Imbalance fires at a gap of 2+, right when stealing helps both sides.
    void rebalance(Core& thief) {
        if(policy == Balance::None) return;

        int mine = thief.rq.size();
        if(policy == Balance::IdleOnly && mine > 0) return;

        Core* victim = nullptr;
        int best = mine;
        for(auto& c : cores) {
            if(c->id == thief.id) continue;
            if(c->rq.size() > best) { best = c->rq.size(); victim = c.get(); }
        }
        if(!victim) return;

        if(victim->rq.size() < mine + 2) return;   // not worth it yet

        Task t;
        long long victimFloor = victim->rq.floorVruntime();
        if(!victim->rq.popMax(t)) return;

        t.migrations++;
        steals++;

        if(policy == Balance::ImbalanceRaw) {
            // wrong on purpose: this vruntime was measured on a
            // clock that doesn't exist on this core
            thief.rq.admitRaw(t);
        } else {
            // carry over how far ahead of its old floor it was
            long long lag = t.vruntime - victimFloor;
            thief.rq.admit(t, lag);
        }
    }
};
