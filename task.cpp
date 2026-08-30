#include <bits/stdc++.h>
using namespace std;

struct Task{
    int pid;
    int vruntime;

    Task(int p, int v) : pid(p), vruntime(v){}
};

struct Node{
    Task task;
    Node* left;
    Node* right;

    Node(Task t) : task(t){
        left = nullptr;
        right = nullptr;
    }
};

class Scheduler{
private:
    Node* root = nullptr;
    Node* insert(Node* root, Task task){
        if(root == nullptr){
            return new Node(task);
        }
        if(task.vruntime < root->task.vruntime ){
            root->left = insert(root->left, task);
        }
        else{
            root->right = insert(root->right, task);
        }
        return root;
    }

    Node* findMin(Node*root){
        while(root && root-> left){
            root = root->left;
        }
        return root;
    }

    Node* erase(Node* root, int vruntime){
        if(root == nullptr)
            return nullptr;

        if(vruntime < root->task.vruntime) {

            root->left = erase(root->left, vruntime);

        }
        else if(vruntime > root->task.vruntime) {

            root->right = erase(root->right, vruntime);

        }
        else {

            // leaf
            if(root->left == nullptr &&
               root->right == nullptr) {

                delete root;
                return nullptr;
            }

            // one child
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

            // two children
            Node* successor = findMin(root->right);

            root->task = successor->task;

            root->right =
                erase(root->right,
                      successor->task.vruntime);
        }

        return root;
    }

    //sorted by vruntime
    void inorder(Node* root){
        if(root == nullptr){
            return;
        }
        inorder(root->left);
        cout << "PID=" << root->task.pid
            << "VRT=" << root->task.vruntime
            <<'\n';
        inorder(root->right);    
    }

public:
    void addTask(int pid, int vruntime){
        root = insert(root, Task(pid,vruntime));
    }

    //get task with min vruntime
    Task getNexttask(){
        Node* node = findMin(root);
        return node->task;
    }

    //run next task
    void runNextTask(int quantum){
        if(root == nullptr){
            return;
        }
        Task current = getNexttask();
        root = erase(root,current.vruntime);
        current.vruntime += quantum;
        root = insert(root,current);
    }
    void display() {

        cout << "\nCurrent Scheduler State\n";
        cout << "-----------------------\n";

        inorder(root);

        cout << '\n';
    }
};

int main(){

    freopen("output.txt", "w", stdout);
    Scheduler sched;

    sched.addTask(1,10);
    sched.addTask(2,5);
    sched.addTask(3,15);

    sched.display();

    sched.runNextTask(4);
    sched.display();

    sched.runNextTask(6);
    sched.display();
}