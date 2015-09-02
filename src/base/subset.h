/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_subset_h
#define ctrlplane_subset_h

#include <cassert>
#include <vector>

// Generates all the possible subset permutations of a particular set.
template <typename Container>
class SubsetGenerator {
public:
    SubsetGenerator(const Container &container)
        : container_(container) {
        stack_.push_back(0);
    }

    bool HasNext() const {
        if (container_.size() <= 1) {
            return false;
        }
        return (stack_.front() < 1);
    }

    // generate the next permutation.
    // rhs is the complement of lhs in container.
    void Next(Container *lhs, Container *rhs) {
        lhs->clear();
        rhs->clear();

        int prev = 0;
        for (unsigned int i = 0; i < stack_.size(); i++) {
            int index = stack_[i];
            assert((std::size_t)index < container_.size());

            for (int n = prev; n < index; n++) {
                rhs->push_back(container_[n]);
            }

            lhs->push_back(container_[index]);
            prev = index + 1;
        }
        for (unsigned int n = prev; n < container_.size(); n++) {
            rhs->push_back(container_[n]);
        }        
        int last = stack_.back();
        if (stack_.size() < container_.size() - 1) {
            last++;
            if ((std::size_t) last < container_.size()) {
                stack_.push_back(last);
                return;
            }
        }
        while (true) {
            stack_.back() += 1;
            if ((std::size_t) stack_.back() < container_.size()) {
                break;
            }
            if (stack_.size() == 1) {
                break;
            }
            stack_.pop_back();
        }
    }

private:
    const Container &container_;
    std::vector<int> stack_;
};

#endif
