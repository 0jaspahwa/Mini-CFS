#include<bits/stdc++.h>
using namespace std;

struct Node{
    int key;
    Node* left;
    Node* right;

    Node(int k){
        key = k;
        left = nullptr;
        right = nullptr;
    }
};

class BST {

private:
    Node* root = nullptr;

    Node* insert(Node* root, int key){
        if(root == nullptr){
            return new Node(key);
        }
        if(key < root->key){
            root->left = insert(root->left, key);
        }
        else{
            root->right = insert(root->right, key);
        }
        return root;
    }

    bool search(Node* root, int key){
        if(root == nullptr){
            return false;
        }
        if(root->key == key) return true;

        if(key < root->key){
            return search(root->left,key);
        }
        else{
            return search(root->right,key);
        }
    }

    //find min Node
    Node* findMin(Node* root){
        while(root && root->left){
            root = root->left;
        }
        return root;
    }
    

    //node erase
    //1.leaf node
    //2. one child
    //3. two children

    Node* erase(Node* root, int key){
        if(root == nullptr){
            return nullptr;
        }
        if(key < root->key){
            root->left = erase(root->left,key);
        }
        else if(key > root->key){
            root->right = erase(root->right, key);
        }
        else{
            //node found

            //no child
            if(root->left== nullptr && root->right == nullptr){
                delete root;
                return nullptr;
            }

            //one child
            if(root->left == nullptr){
                Node* temp = root->right;
                delete root;
                return temp;
            }
            if(root->right == nullptr){
                Node* temp = root->left;
                delete root;
                return temp;
            }

            //two children
            Node* successor = findMin(root->right);
            root->key = successor->key;
            root->right = erase(root->right,successor->key);
        }
        return root;
    }

    void inorder(Node* root) {

        if(root == nullptr)
            return;

        inorder(root->left);

        cout << root->key << " ";

        inorder(root->right);
    }

    // PREORDER
    // Root -> Left -> Right
    void preorder(Node* root) {

        if(root == nullptr)
            return;

        cout << root->key << " ";

        preorder(root->left);
        preorder(root->right);
    }


    // POSTORDER
    // Left -> Right -> Root
    void postorder(Node* root) {

        if(root == nullptr)
            return;

        postorder(root->left);
        postorder(root->right);

        cout << root->key << " ";
    }

public:

    // PUBLIC WRAPPERS
    void insert(int key) {
        root = insert(root, key);
    }

    void erase(int key) {
        root = erase(root, key);
    }

    bool search(int key) {
        return search(root, key);
    }

    void inorder() {
        inorder(root);
        cout << '\n';
    }

    void preorder() {
        preorder(root);
        cout << '\n';
    }

    void postorder() {
        postorder(root);
        cout << '\n';
    }

    // Get minimum element
    // Scheduler:
    // task with minimum vruntime
    int getMin() {

        Node* node = findMin(root);

        if(node == nullptr)
            return -1;

        return node->key;
    }
};
int main() {
    
    freopen("output.txt", "w",stdout);
    BST tree;

    tree.insert(20);
    tree.insert(10);
    tree.insert(30);
    tree.insert(5);
    tree.insert(15);
    tree.insert(25);
    tree.insert(35);

    cout << "Inorder: ";
    tree.inorder();

    cout << "Min = "
         << tree.getMin()
         << '\n';

    tree.erase(20);

    cout << "After deleting 20:\n";

    tree.inorder();
}