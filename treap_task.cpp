#include <bits/stdc++.h>
using namespace std;


struct Task {
    int pid;
    long long vruntime;

    Task(int p, long long v) : pid(p), vruntime(v) {}
};


// NODE
// treap has one more field: vruntime

struct Node {
    Task task;
    int priority;
    Node* left;
    Node* right;

    Node(Task t, int p) : task(t), priority(p) {
        left = nullptr;
        right = nullptr;
    }
};

class Scheduler {

private:
    Node* root = nullptr;
    mt19937 rng{random_device{}()};

 
    static bool keyLess(const Task& a, const Task& b) {
        if(a.vruntime != b.vruntime)
            return a.vruntime < b.vruntime;
        return a.pid < b.pid;
    }

    static bool keyEqual(const Task& a, const Task& b) {
        return a.vruntime == b.vruntime && a.pid == b.pid;
    }


    // ROTATIONS
    //
    // A rotation rearranges a parent and one child while
    // PRESERVING BST ORDER. It is the only tool needed to
    // fix a broken heap property.

    Node* rotateRight(Node* y) {
        Node* x = y->left;
        y->left = x->right;
        x->right = y;
        return x;
    }

    Node* rotateLeft(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        y->left = x;
        return y;
    }

    // INSERT
    // same as BST, the only
    // addition is the rotation check on the way back up:
    // if the child we just inserted into has a higher
    // priority than us, rotate it above us.

    Node* insert(Node* root, Task task, int priority) {

        if(root == nullptr)
            return new Node(task, priority);

        if(keyLess(task, root->task)) {

            root->left = insert(root->left, task, priority);

            if(root->left->priority > root->priority)
                root = rotateRight(root);
        }
        else {

            root->right = insert(root->right, task, priority);

            if(root->right->priority > root->priority)
                root = rotateLeft(root);
        }

        return root;
    }

   
    Node* findMin(Node* root) {
        while(root && root->left)
            root = root->left;
        return root;
    }

  
    // ERASE
    //
    // Your BST erase had three cases for the found node
    // (leaf / one child / two children + successor copy).
    //
    // A treap replaces the messy two-children case with something simpler: 
    // rotate the node DOWN (always pulling up whichever child has higher priority)
    // until it becomes a leaf or has one child, then just unlink it. 
    // No successor copying at all.

    Node* erase(Node* root, const Task& task) {

        if(root == nullptr)
            return nullptr;

        if(keyLess(task, root->task)) {
            root->left = erase(root->left, task);
        }
        else if(keyLess(root->task, task)) {
            root->right = erase(root->right, task);
        }
        else {
            // found it

            if(root->left == nullptr) {
                Node* temp = root->right;
                delete root;
                return temp;
            }

            if(root->right == nullptr) {
                Node* temp = root->left;
                delete root;
                return temp;
            }

            // two children: rotate the higher-priority child
            // up, pushing this node down, then keep erasing.
            if(root->left->priority > root->right->priority) {
                root = rotateRight(root);
                root->right = erase(root->right, task);
            }
            else {
                root = rotateLeft(root);
                root->left = erase(root->left, task);
            }
        }

        return root;
    }

    void inorder(Node* root) {
        if(root == nullptr)
            return;
        inorder(root->left);
        cout << "PID=" << root->task.pid
             << " VRT=" << root->task.vruntime << '\n';
        inorder(root->right);
    }

    int height(Node* n) {
        if(!n) return 0;
        return 1 + max(height(n->left), height(n->right));
    }

    int count(Node* n) {
        if(!n) return 0;
        return 1 + count(n->left) + count(n->right);
    }

    void destroy(Node* n) {
        if(!n) return;
        destroy(n->left);
        destroy(n->right);
        delete n;
    }

public:
    ~Scheduler() { destroy(root); }

    void addTask(int pid, long long vruntime) {
        int priority = uniform_int_distribution<int>(0, 1000000000)(rng);
        root = insert(root, Task(pid, vruntime), priority);
    }

 
    // (findMin returns nullptr, then node->task derefs it).
    bool hasTask() const { return root != nullptr; }

    Task getNextTask() {
        Node* node = findMin(root);
        if(node == nullptr)
            throw runtime_error("getNextTask called on empty scheduler");
        return node->task;
    }

    void runNextTask(long long quantum) {

        if(root == nullptr)
            return;

        Task current = getNextTask();

        root = erase(root, current);

        current.vruntime += quantum;

        int priority = uniform_int_distribution<int>(0, 1000000000)(rng);
        root = insert(root, current, priority);
    }

    void display() {
        cout << "\nCurrent Scheduler State\n";
        cout << "-----------------------\n";
        inorder(root);
        cout << '\n';
    }

    int getHeight() { return height(root); }
    int getCount()  { return count(root); }
};



int main() {

    const int NUM_TASKS = 64;

    Scheduler sched;
    for(int i = 1; i <= NUM_TASKS; i++)
        sched.addTask(i, 0);

    cout << "tasks: " << NUM_TASKS << "\n";
    cout << "ideal balanced height is about "
         << (int)ceil(log2(NUM_TASKS + 1)) << "\n";
    cout << "plain BST measured height was " << NUM_TASKS
         << " (a linked list)\n\n";

    cout << left << setw(14) << "after ticks"
         << setw(10) << "height"
         << setw(10) << "count" << "\n";
    cout << string(34, '-') << "\n";

    for(int tick = 1; tick <= 200; tick++) {

        sched.runNextTask(4);

        if(tick % 25 == 0) {
            cout << left << setw(14) << tick
                 << setw(10) << sched.getHeight()
                 << setw(10) << sched.getCount() << "\n";
        }
    }

    return 0;
}