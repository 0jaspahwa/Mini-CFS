#pragma once

// bst_scheduler.hpp - the first attempt, kept for measurement.
// same run queue with the treap ripped out: no priority, no rotations.

#include "scheduler.hpp"

struct BstNode {
    Task task;
    BstNode* left  = nullptr;
    BstNode* right = nullptr;
    explicit BstNode(const Task& t) : task(t) {}
};

class BstRunQueue {
public:
    ~BstRunQueue() { destroy(root); }

    BstRunQueue() = default;
    BstRunQueue(const BstRunQueue&)            = delete;
    BstRunQueue& operator=(const BstRunQueue&) = delete;

    void enqueue(const Task& t) {
        root = insert(root, t);
        count++;
        stats.inserts++;
    }

    void admit(Task t, long long lag = 0) {
        t.vruntime = minVruntime + lag;
        enqueue(t);
    }

    // no cache, so this walks the left spine every call
    const Task* peekWalk() {
        stats.peeks++;
        BstNode* n = root;
        if(!n) return nullptr;
        while(n->left) { stats.visits++; n = n->left; }
        stats.visits++;
        return &n->task;
    }

    bool popMin(Task& out) {
        if(!root) return false;
        stats.pops++;
        BstNode* n = root;
        while(n->left) { stats.visits++; n = n->left; }
        stats.visits++;
        out = n->task;
        root = erase(root, out);
        count--;
        return true;
    }

    void advanceFloor() {
        BstNode* n = root;
        if(!n) return;
        while(n->left) n = n->left;
        if(n->task.vruntime > minVruntime) minVruntime = n->task.vruntime;
    }

    long long floorVruntime() const { return minVruntime; }
    int  size()   const { return count; }
    bool empty()  const { return count == 0; }
    int  height() const { return heightOf(root); }
    void collect(std::vector<Task>& out) const { collectInto(root, out); }

    RqStats stats;

private:
    BstNode* root = nullptr;
    long long minVruntime = 0;
    int count = 0;

    static bool keyLess(const Task& a, const Task& b) {
        if(a.vruntime != b.vruntime) return a.vruntime < b.vruntime;
        return a.pid < b.pid;
    }
    static bool keyEqual(const Task& a, const Task& b) {
        return a.vruntime == b.vruntime && a.pid == b.pid;
    }

    BstNode* insert(BstNode* n, const Task& t) {
        if(!n) return new BstNode(t);
        stats.visits++;
        if(keyLess(t, n->task)) n->left  = insert(n->left,  t);
        else                    n->right = insert(n->right, t);
        return n;
    }

    // leaf, one child, two children with a successor copy.
    // correct, but has no way to rebalance anything.
    BstNode* erase(BstNode* n, const Task& t) {
        if(!n) return nullptr;
        stats.visits++;
        if(keyLess(t, n->task))      n->left  = erase(n->left,  t);
        else if(keyLess(n->task, t)) n->right = erase(n->right, t);
        else {
            if(!n->left)  { BstNode* r = n->right; delete n; return r; }
            if(!n->right) { BstNode* l = n->left;  delete n; return l; }
            BstNode* s = n->right;
            while(s->left) { stats.visits++; s = s->left; }
            n->task = s->task;
            n->right = erase(n->right, s->task);
        }
        return n;
    }

    static int heightOf(BstNode* n) {
        if(!n) return 0;
        return 1 + std::max(heightOf(n->left), heightOf(n->right));
    }
    static void collectInto(BstNode* n, std::vector<Task>& out) {
        if(!n) return;
        collectInto(n->left, out);
        out.push_back(n->task);
        collectInto(n->right, out);
    }
    static void destroy(BstNode* n) {
        if(!n) return;
        destroy(n->left); destroy(n->right); delete n;
    }
};
