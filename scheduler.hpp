#pragma once

// scheduler.hpp - run queue: a treap keyed on (vruntime, pid),
// with a cached pointer to the smallest node.

// rule: pop smallest vruntime, run it one quantum, add the
// time back, reinsert. that's the whole scheduler.

#include <cstdint>
#include <random>
#include <vector>
#include <algorithm>

// linux's nice-to-weight table. bigger weight = vruntime grows
// slower = wins the "smallest" comparison more often.
constexpr long long NICE_0_WEIGHT = 1024;

inline int weightForNice(int nice) {
    static const int table[40] = {
        /* -20 */ 88761, 71755, 56483, 46273, 36291,
        /* -15 */ 29154, 23254, 18705, 14949, 11916,
        /* -10 */  9548,  7620,  6100,  4904,  3906,
        /*  -5 */  3121,  2501,  1991,  1586,  1277,
        /*   0 */  1024,   820,   655,   526,   423,
        /*   5 */   335,   272,   215,   172,   137,
        /*  10 */   110,    87,    70,    56,    45,
        /*  15 */    36,    29,    23,    18,    15,
    };
    if(nice < -20) nice = -20;
    if(nice >  19) nice =  19;
    return table[nice + 20];
}

// only vruntime matters for scheduling. rest is bookkeeping
// for the experiments to check fairness later.
struct Task {
    int  pid       = 0;
    long long vruntime = 0;    // weighted service, ns
    int  weight    = (int)NICE_0_WEIGHT;
    int  nice      = 0;

    long long cpuTime   = 0;   // real ns actually run
    long long remaining = -1;  // real ns of work left, -1 = never ends
    int  homeCore   = -1;
    int  migrations = 0;

    // longest a task waited to run. catches stalls averages hide.
    long long lastRanTick  = -1;
    long long maxWaitTicks = 0;

    Task() = default;
    Task(int p, int niceLevel = 0, long long work = -1)
        : pid(p), weight(weightForNice(niceLevel)), nice(niceLevel),
          remaining(work) {}
};

// treap node: normal BST fields plus a random priority, so
// sorted vruntime inserts can't build a bad shape.
struct Node {
    Task task;
    unsigned priority;
    Node* left  = nullptr;
    Node* right = nullptr;

    Node(const Task& t, unsigned p) : task(t), priority(p) {}
};

// visits = nodes touched. exact, unlike wall-clock timing.
struct RqStats {
    long long visits  = 0;
    long long inserts = 0;
    long long pops    = 0;
    long long peeks   = 0;
    void reset() { *this = RqStats{}; }
};

class RunQueue {
public:
    explicit RunQueue(uint64_t seed = 0x9E3779B97F4A7C15ull) : rng(seed) {}
    ~RunQueue() { destroy(root); }

    RunQueue(const RunQueue&)            = delete;
    RunQueue& operator=(const RunQueue&) = delete;

    // put a task back with the vruntime it already has
    void enqueue(const Task& t) { insertTask(t); }

    // new to this queue (fresh or stolen). vruntime only means
    // something here, so place it against this queue's floor.
    void admit(Task t, long long lag = 0) {
        t.vruntime = minVruntime + lag;
        insertTask(t);
    }

    // same, but wrong on purpose: keeps the raw vruntime.
    // experiment 4 uses this to show what breaks.
    void admitRaw(const Task& t) { insertTask(t); }

    // "who runs next?" O(1), no nodes touched, cached answer.
    const Task* peek() {
        stats.peeks++;
        return leftmost ? &leftmost->task : nullptr;
    }

    // same answer without the cache, walks the left spine.
    // kept so experiment 2 can price the cache.
    const Task* peekWalk() {
        stats.peeks++;
        Node* n = root;
        if(!n) return nullptr;
        while(n->left) { stats.visits++; n = n->left; }
        stats.visits++;
        return &n->task;
    }

    // removes the smallest node, no rotations needed. finds
    // the new smallest on the way down, so the cache is free.
    bool popMin(Task& out) {
        if(!root) return false;
        stats.pops++;
        Node* newLeft = nullptr;
        root = popMinNode(root, out, newLeft);
        leftmost = newLeft;
        count--;
        return true;
    }

    // removes the largest vruntime. work stealing takes this
    // one: weakest claim on the CPU, least disruptive to move.
    bool popMax(Task& out) {
        if(!root) return false;
        stats.pops++;
        root = popMaxNode(root, out);
        count--;
        if(count == 0) leftmost = nullptr;
        return true;
    }

    // floor only moves forward, arrivals are placed against it
    void advanceFloor() {
        if(leftmost && leftmost->task.vruntime > minVruntime)
            minVruntime = leftmost->task.vruntime;
    }

    long long floorVruntime() const { return minVruntime; }
    int  size()   const { return count; }
    bool empty()  const { return count == 0; }
    int  height() const { return heightOf(root); }

    void collect(std::vector<Task>& out) const { collectInto(root, out); }

    // checks bst order, heap order, count, and that the cached
    // leftmost really is leftmost. that last one fails silent.
    bool validate() const {
        int n = 0;
        const Task* prev = nullptr;
        if(!check(root, nullptr, n, prev)) return false;
        if(n != count) return false;
        Node* trueLeft = root;
        while(trueLeft && trueLeft->left) trueLeft = trueLeft->left;
        return leftmost == trueLeft;
    }

    RqStats stats;

private:
    Node* root     = nullptr;
    Node* leftmost = nullptr;      // the O(1) cache
    long long minVruntime = 0;
    int count = 0;
    std::mt19937_64 rng;

    // pid breaks ties so keys stay unique. every task starts
    // at 0, so ties are the normal case, not an edge case.
    static bool keyLess(const Task& a, const Task& b) {
        if(a.vruntime != b.vruntime) return a.vruntime < b.vruntime;
        return a.pid < b.pid;
    }

    // ordinary BST insert, plus a rotation check on the way up
    void insertTask(const Task& t) {
        Node* created = nullptr;
        root = insertNode(root, t, (unsigned)rng(), created);
        count++;
        stats.inserts++;
        if(!leftmost || keyLess(created->task, leftmost->task))
            leftmost = created;
    }

    Node* insertNode(Node* n, const Task& t, unsigned pri, Node*& created) {
        if(!n) { created = new Node(t, pri); return created; }
        stats.visits++;
        if(keyLess(t, n->task)) {
            n->left = insertNode(n->left, t, pri, created);
            if(n->left->priority > n->priority) n = rotateRight(n);
        } else {
            n->right = insertNode(n->right, t, pri, created);
            if(n->right->priority > n->priority) n = rotateLeft(n);
        }
        return n;
    }

    // rearranges parent and child, keeps BST order. node
    // addresses don't move, so the cached pointer stays valid.
    Node* rotateRight(Node* y) {
        Node* x = y->left;  y->left  = x->right; x->right = y; return x;
    }
    Node* rotateLeft(Node* x) {
        Node* y = x->right; x->right = y->left;  y->left  = x; return y;
    }

    Node* popMinNode(Node* n, Task& out, Node*& newLeft) {
        stats.visits++;
        if(!n->left) {
            out = n->task;
            Node* r = n->right;
            delete n;
            newLeft = leftmostOf(r);       // successor lives here
            return r;
        }
        n->left = popMinNode(n->left, out, newLeft);
        if(!newLeft) newLeft = n;          // no right subtree: parent is next
        return n;
    }

    Node* popMaxNode(Node* n, Task& out) {
        stats.visits++;
        if(!n->right) {
            out = n->task;
            Node* l = n->left;
            delete n;
            return l;
        }
        n->right = popMaxNode(n->right, out);
        return n;
    }

    Node* leftmostOf(Node* n) {
        if(!n) return nullptr;
        while(n->left) { stats.visits++; n = n->left; }
        return n;
    }

    // order checked against the last key seen, heap against parent
    static bool check(Node* n, Node* parent, int& n_out, const Task*& prev) {
        if(!n) return true;
        if(parent && n->priority > parent->priority) return false;
        if(!check(n->left, n, n_out, prev)) return false;
        if(prev && !keyLess(*prev, n->task)) return false;
        prev = &n->task;
        n_out++;
        return check(n->right, n, n_out, prev);
    }

    static int heightOf(Node* n) {
        if(!n) return 0;
        return 1 + std::max(heightOf(n->left), heightOf(n->right));
    }

    static void collectInto(Node* n, std::vector<Task>& out) {
        if(!n) return;
        collectInto(n->left, out);
        out.push_back(n->task);
        collectInto(n->right, out);
    }

    static void destroy(Node* n) {
        if(!n) return;
        destroy(n->left); destroy(n->right); delete n;
    }
};

// one tick, one core: pop smallest, run a slice, charge it to
// vruntime, put it back. the whole scheduler in one function.
struct TickResult {
    bool ran      = false;
    bool finished = false;
    Task task;
    long long slice = 0;
};

inline TickResult runOneTick(RunQueue& rq, long long quantum, long long now = -1) {
    TickResult r;
    Task t;
    if(!rq.popMin(t)) return r;            // idle

    long long slice = quantum;
    if(t.remaining >= 0) slice = std::min(quantum, t.remaining);

    if(now >= 0) {
        if(t.lastRanTick >= 0)
            t.maxWaitTicks = std::max(t.maxWaitTicks, now - t.lastRanTick - 1);
        t.lastRanTick = now;
    }

    t.cpuTime += slice;
    if(t.remaining >= 0) t.remaining -= slice;

    // this line is what makes priority work
    t.vruntime += slice * NICE_0_WEIGHT / t.weight;

    r.ran = true; r.slice = slice;

    if(t.remaining == 0) r.finished = true;
    else                 rq.enqueue(t);

    r.task = t;
    rq.advanceFloor();
    return r;
}
