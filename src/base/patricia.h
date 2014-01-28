/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef PATRICIA_H
#define PATRICIA_H

#include <string>
#include <cstring>
#include <boost/intrusive/detail/parent_from_member.hpp>
#include <boost/iterator/iterator_facade.hpp>

#define IS_INT_NODE(node)       (node->intnode_) 

namespace Patricia {
class Node {
public:
    Node() {
        left_ = NULL;
        right_ = NULL;
        intnode_ = false;
        bitpos_ = 0;
    } 

    Node       *left_;
    Node       *right_;
    bool        intnode_;
    std::size_t bitpos_;
};

class TreeBase {
public:
    TreeBase() {
        root_ = NULL;
        nodes_ = 0;
        int_nodes_ = 0;
    }

    int     nodes_;
    int     int_nodes_;
    Node   *root_;
};

template <class D, Node D::* P, class K>
class Tree : private TreeBase {
public:
    Tree() : TreeBase() {
    }

    class Iterator : public boost::iterator_facade<Iterator,
                                                   D *,
                                                   boost::forward_traversal_tag,
                                                   D *> {
        public:
        Iterator() : data_(NULL) {}
        explicit Iterator(Tree<D, P, K> *tree, D *data) : tree_(tree), data_(data) {
        }

        private:
        friend class boost::iterator_core_access;

        void increment() {
            data_ = tree_->GetNext(data_);
        }
        bool equal(const Iterator &it) const {
            return data_ == it.data_;
        }
        D * dereference() const {
            return data_;
        }
        Tree<D, P, K> *tree_;
        D  *data_;
    };

    Iterator begin() {
        return Iterator(this, GetNext(NULL));
    }

    Iterator end() {
        return Iterator();
    }

    Iterator LowerBound(D * data) {
        return Iterator(this, FindNext(data));
    }

    std::size_t Size() {
        return nodes_;
    }

    bool Insert(D * data) {
        return InsertNode(DataToNode(data));
    }

    bool Remove(D * data) {
        return RemoveNode(DataToNode(data));
    }

    D * Find(const D * data) {
        return NodeToData(FindNode(DataToNode(data)));
    }

    D * FindNext(const D * data) {
        return NodeToData(FindNextNode(DataToNode(data)));
    }

    D * LPMFind(const D * data) {
        return NodeToData(FindBestMatchNode(DataToNode(data)));
    }

    D * GetNext(D * data) {
        return NodeToData(GetNextNode(DataToNode(data)));
    }

private:
    const Node *DataToNode (const D * data) {
        if (data) {
            return static_cast<const Node *>(&(data->*P));
        } else {
            return NULL;
        }
    }

    Node *DataToNode (D * data) {
        if (data) {
            return static_cast<Node *>(&(data->*P));
        } else {
            return NULL;
        }
    }

    const D *NodeToData (const Node * node) {
        if (node) {
            return boost::intrusive::detail::parent_from_member<D, Node>(node, P);
        } else {
            return NULL;
        }
    }

    D *NodeToData (Node * node) {
        if (node) {
            return boost::intrusive::detail::parent_from_member<D, Node>(node, P);
        } else {
            return NULL;
        }
    }

    bool InsertNode(Node *node) {
        Node * p, * x, *l;

        // Start at the root_
        p = NULL;
        x = root_;
        while (x) {
            if (x->bitpos_ >= K::Length(NodeToData(node)) && !IS_INT_NODE(x)) {
                break;
            }
            p = x;
            x = GetBit(node, x->bitpos_) ? x->right_ : x->left_;
            if (x && (p->bitpos_ >= x->bitpos_)) {
                /* no x to deal with */
                x = NULL;
                break;
            }
        }

        std::size_t i = 0;
        l = x ? x : p;
        // Find the first bit that does not match.
        if (l) {
            /* if l is internal node pick the left_ node to compare */
            if (Compare(node, l, 0, i)) {
                // The key already exists
                return false;
            }

            if (i != K::Length(NodeToData(node)) || i != l->bitpos_) {
                p = NULL;
                x = root_;
                while (x && x->bitpos_ <= i && x->bitpos_ < K::Length(NodeToData(node))) {
                    p = x;
                    x = GetBit(node, x->bitpos_) ? x->right_ : x->left_;
                    if (x && (p->bitpos_ >= x->bitpos_)) {
                        /* no x to deal with */
                        x = NULL;
                        break;
                    }
                }
            }
        }

        nodes_++;
        node->left_ = NULL;
        node->right_ = NULL;
        node->bitpos_ = K::Length(NodeToData(node));;

        if (x) {
            if (x->bitpos_ == i) {
                /* has to be an internal node */
                node->right_ = x->right_;
                node->left_ = x->left_;
                /* rightmost guy of the left_ subtree will be pointing to x that needs to point to node now. */
                //node->right_ = RewireRightMost(node, x->left_);
                RewireRightMost(node, x->left_);
                delete x;
                int_nodes_--;
                l = node;
            } else {
                /* key length of x has to be greater than node key length */
                if (i == K::Length(NodeToData(node))) {
                    if (GetBit(l, i)) {
                        node->right_ = x;
                    } else {
                        node->left_ = x;
                        /* right_ most node of the left_ sub tree should point to node */
                        node->right_ = RewireRightMost(node, x);
                    }
                    l = node;
                } else {
                    /* allocate internal node */
                    l = new Node;
                    int_nodes_++;
                    l->bitpos_ = i;
                    l->intnode_ = true;
                    if (GetBit(node, i)) {
                        l->left_ = x;
                        l->right_ = node;
                        /* right_ most node of the left_ sub tree should point to l */
                        node->right_ = RewireRightMost(l, x);
                    } else {
                        l->left_ = node;
                        l->right_ = x;
                        node->right_ = l;
                    }
                }
            }
        } else {
            if (p) {
                if (GetBit(node, p->bitpos_)) {
                    node->right_ = p->right_;
                } else {
                    node->right_ = p;
                }
            }
            l = node;
        }

        if (p) {
            if (GetBit(node, p->bitpos_)) {
                p->right_ = l;
            } else {
                p->left_ = l;
            }
        } else {
            root_ = l;
        }

        return true;
    }

    bool RemoveNode(Node * node) {
        Node * pPrev = NULL;
        Node * p = NULL;
        Node * x = root_;

        while (x) {
            if (x->bitpos_ > K::Length(NodeToData(node))) {
                x = NULL;
                break;
            } else if (x->bitpos_ == K::Length(NodeToData(node)) && !IS_INT_NODE(x)) {
                break;
            }
            pPrev = p;
            p = x;
            x = GetBit(node, x->bitpos_) ? x->right_ : x->left_;
            if (x && (p->bitpos_ >= x->bitpos_)) {
                /* no x to deal with */
                x = NULL;
                break;
            }
        }

        if(!x || !Compare(node, x)){
            return false;
        }

        Node * t = NULL;

        if (x->left_ && x->right_ && x->bitpos_ < x->right_->bitpos_) {
            /* need to allocate internal node to replace the going node */
            t = new Node;
            t->bitpos_ = x->bitpos_;
            t->intnode_ = true;
            int_nodes_++;
            t->left_ = x->left_;
            t->right_ = x->right_;
            RewireRightMost(t, x->left_);
            if (!p) {
                root_ =t ;
            } else if (GetBit(x, p->bitpos_)) {
                p->right_ = t;
            } else {
                p->left_ = t;
            }
        } else if (x->left_) {
            if (!p) {
                root_ = x->left_;
            } else if (GetBit(x, p->bitpos_)) {
                p->right_ = x->left_;
            } else {
                p->left_ = x->left_;
            }
            RewireRightMost(x->right_, x->left_);
        } else if (x->right_ && x->bitpos_ < x->right_->bitpos_) {
            if (!p) {
                root_ = x->right_;
            } else if (GetBit(x, p->bitpos_)) {
                p->right_ = x->right_;
            } else {
                p->left_ = x->right_;
            }
        } else {
            if (!p) {
                root_ = NULL;
            } else if (IS_INT_NODE(p)) {
                if (GetBit(x, p->bitpos_)){
                    t = p->left_;
                    //RewireRightMost((pPrev->left_ == p) ? pPrev : NULL, t);
                    RewireRightMost(x->right_, t);
                } else {
                    t = p->right_;
                }
                if (!pPrev) {
                    root_ = t;
                } else if (GetBit(x, pPrev->bitpos_)) {
                    pPrev->right_ = t;
                } else {
                    pPrev->left_ = t;
                }
                delete p;
                int_nodes_--;
            } else {
                if (GetBit(x, p->bitpos_)) {
                    p->right_ = x->right_;
                } else {
                    p->left_ = NULL;
                }
            }
        }

        nodes_--;
        node->left_ = NULL;
        node->right_ = NULL;
        return true;
    }

    Node * FindNode(const Node * node) {
        Node * p, * x;

        p = NULL;
        x = root_;
        while (x) {
            if (x->bitpos_ > K::Length(NodeToData(node))) {
                x = NULL;
                break;
            } else if (x->bitpos_ == K::Length(NodeToData(node)) && !IS_INT_NODE(x)) {
                break;
            }
            p = x;
            x = GetBit(node, x->bitpos_) ? x->right_ : x->left_;
            if (x && (p->bitpos_ >= x->bitpos_)) {
                /* no x to deal with */
                x = NULL;
                break;
            }
        }

        if(!x || !Compare(node, x)){
            return NULL;
        }

        return x;
    }

    Node * FindNextNode(const Node * node) {
        Node * p, * x, *l;
        std::size_t i = 0;

        p = NULL;
        l = NULL;
        x = root_;
        while (x) {
            if (!IS_INT_NODE(x)) {
                if (Compare(node, x, i, i)) {
                    return GetNextNode(x);
                }
                if (x->bitpos_ > K::Length(NodeToData(node)) || i != x->bitpos_) {
                    break;
                }
                l = x;
            }
            p = x;
            x = GetBit(node, x->bitpos_) ? x->right_ : x->left_;
            if (x && (p->bitpos_ >= x->bitpos_)) {
                break;
            }
        }

        if (l) {
            x = l;
            while (x && x->bitpos_ <= i) {
                l = x;
                x = GetBit(node, x->bitpos_) ? x->right_ : x->left_;
                if (x && (l->bitpos_ >= x->bitpos_)) {
                    break;
                }
            }
            if (K::Length(NodeToData(node)) != l->bitpos_) {
                if (GetBit(node, l->bitpos_)) {
                    if (!x) {
                        return NULL;
                    }
                    if (l->bitpos_ > x->bitpos_) {
                        while (x && l->bitpos_ > x->bitpos_) {
                            l = x;
                            x = x->right_;
                        }
                        l = x;
                    } else if (GetBit(node, i)) {
                        /* x is on left */
                        while (x->right_ &&
                               x->bitpos_ < x->right_->bitpos_) {
                            x = x->right_;
                        }
                        l = x;
                        x = x->right_;
                        while (x && l->bitpos_ > x->bitpos_) {
                            l = x;
                            x = x->right_;
                        }
                        l = x;
                    } else {
                        l = x;
                    }
                } else {
                    if (!x || GetBit(node, i)) {
                        x = l->right_;
                        while (x && l->bitpos_ > x->bitpos_) {
                            l = x;
                            x = x->right_;
                        }
                        l = x;
                    } else {
                        l = x;
                    }
                }

                if (x && !IS_INT_NODE(x)) {
                    return x;
                }
            }
            return GetNextNode(l);
        } else {
            if (!GetBit(node, i)) {
                /* all elements of the tree are on right */
                return x;
            }
        }

        return NULL;
    }

    Node * FindBestMatchNode(const Node * node) {
        Node * p, * x, *l;
        std::size_t i = 0;

        l = NULL;
        p = NULL;
        x = root_;
        while (x) {
            if (!IS_INT_NODE(x)) {
                if (Compare(node, x, i, i)) {
                    return x;
                }
                if (i == x->bitpos_) {
                    l = x;
                }
            }
            if (x->bitpos_ > K::Length(NodeToData(node))) {
                break;
            }
            p = x;
            x = GetBit(node, x->bitpos_) ? x->right_ : x->left_;
            if (x && (p->bitpos_ >= x->bitpos_)) {
                break;
            }
        }

        return l;
    }

    Node * GetNextNode(Node * node) {
        Node *x, *l;

        if (!root_) {
            return NULL;
        }

        x = root_;
        if (node || IS_INT_NODE(x)) {
            if (node) {
                x = node;
            }
            l = x;
            while (x) {
                if (x->bitpos_ < l->bitpos_) {
                    l = x;
                    x = l->right_;
                } else {
                    l = x;
                    x = l->left_ ? l->left_ : l->right_;
                }
                if (x && x->bitpos_ > l->bitpos_ &&
                    !IS_INT_NODE(x)) {
                    break;
                }
            }
        }

        return x;
    }

    bool GetBit(const Node * node, std::size_t pos) {
        const D * data = NodeToData(node);
        if (pos >= K::Length(data)) {
            return false;
        }

        return K::ByteValue(data, pos >> 3) & (0x80 >> (pos & 7));
    }

    bool Compare(const Node *node_left, const Node *node_right) {
        const D * data_left = NodeToData(node_left);
        const D * data_right = NodeToData(node_right);
        if (K::Length(data_left) != K::Length(data_right)) {
            return false;
        }

        std::size_t byteLen = K::Length(data_left) >> 3;
        std::size_t pos;

        for (pos = 0; pos < byteLen; ++pos) {
            if (K::ByteValue(data_left, pos) != K::ByteValue(data_right, pos)) {
                return false;
            }
        }

        for (pos <<= 3; pos < K::Length(data_left); ++pos) {
            if (GetBit(node_left, pos) != GetBit(node_right, pos)) {
                return false;
            }
        }

        return true;
    }

    bool Compare(const Node *node_left, const Node *node_right, std::size_t start, std::size_t& pos) {
        const D * data_left = NodeToData(node_left);
        const D * data_right = NodeToData(node_right);
        std::size_t shortLen;

        bool isEqual;

        if (K::Length(data_left) < K::Length(data_right)) {
            shortLen = K::Length(data_left);
            isEqual = false;
        } else {
            shortLen = K::Length(data_right);
            isEqual = (K::Length(data_left) == K::Length(data_right));
        }

        std::size_t byteLen = shortLen >> 3;

        for (pos = start >> 3; pos < byteLen; ++pos) {
            if (K::ByteValue(data_left, pos) != K::ByteValue(data_right, pos)) {
                break;
            }
        }

        pos <<= 3;
        if (pos < start) {
            pos = start;
        }

        for (; pos < shortLen; ++pos) {
            if (GetBit(node_left, pos) != GetBit(node_right, pos)) {
                return false;
            }
        }

        return isEqual;
    }

    Node * RewireRightMost (Node *p, Node *x) {
        Node *pRight;
        if (!x) {
            return NULL;
        }

        while (x->right_ && x->right_->bitpos_ > x->bitpos_) {
            x = x->right_;
        }
        pRight = x->right_;
        x->right_ = p;
        return pRight;
    }


};

};

#endif /* PATRICIA_H */

