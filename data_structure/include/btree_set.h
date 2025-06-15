// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// A set<> implements the STL unique sorted associative container
// interface (a.k.a set<>) using a btree. A multiset<> implements the STL
// multiple sorted associative container interface (a.k.a multiset<>) using a
// btree. See btree.h for details of the btree implementation and caveats.

#ifndef UTIL_BTREE_BTREE_SET_H__
#define UTIL_BTREE_BTREE_SET_H__

#include <functional>
#include <memory>
#include <string>

#include "btree.h"
#include "btree_container.h"

namespace btree {

// The set class is needed mainly for its constructors.
    template <typename Key,
            typename Compare = std::less<Key>,
            typename Alloc = std::allocator<Key>,
            int TargetNodeSize = 256>
    class set : public btree_unique_container<
            btree<btree_set_params<Key, Compare, Alloc, TargetNodeSize> > > {

        typedef set<Key, Compare, Alloc, TargetNodeSize> self_type;
        typedef btree_set_params<Key, Compare, Alloc, TargetNodeSize> params_type;
        typedef btree<params_type> btree_type;
        typedef btree_unique_container<btree_type> super_type;

    public:
        typedef typename btree_type::key_compare key_compare;
        typedef typename btree_type::allocator_type allocator_type;

    public:
        // Default constructor.
        set(const key_compare &comp = key_compare(),
            const allocator_type &alloc = allocator_type())
                : super_type(comp, alloc) {
        }

        // Copy constructor.
        set(const self_type &x)
                : super_type(x) {
        }

        // Move constructor.
        set(self_type &&x) noexcept
                : super_type() {
            this->swap(x);
        }

        // Copy/move assignment
        self_type& operator=(self_type x) noexcept {
            this->swap(x);
            return *this;
        }

        // Range constructor.
        template <class InputIterator>
        set(InputIterator b, InputIterator e,
            const key_compare &comp = key_compare(),
            const allocator_type &alloc = allocator_type())
                : super_type(b, e, comp, alloc) {
        }
    };

    template <typename K, typename C, typename A, int N>
    inline void swap(set<K, C, A, N> &x, set<K, C, A, N> &y) {
        x.swap(y);
    }

// The multiset class is needed mainly for its constructors.
    template <typename Key,
            typename Compare = std::less<Key>,
            typename Alloc = std::allocator<Key>,
            int TargetNodeSize = 256>
    class multiset : public btree_multi_container<
            btree<btree_set_params<Key, Compare, Alloc, TargetNodeSize> > > {

        typedef multiset<Key, Compare, Alloc, TargetNodeSize> self_type;
        typedef btree_set_params<Key, Compare, Alloc, TargetNodeSize> params_type;
        typedef btree<params_type> btree_type;
        typedef btree_multi_container<btree_type> super_type;

    public:
        typedef typename btree_type::key_compare key_compare;
        typedef typename btree_type::allocator_type allocator_type;

    public:
        // Default constructor.
        multiset(const key_compare &comp = key_compare(),
                 const allocator_type &alloc = allocator_type())
                : super_type(comp, alloc) {
        }

        // Copy constructor.
        multiset(const self_type &x)
                : super_type(x) {
        }

        // Move constructor.
        multiset(self_type &&x) noexcept
                : super_type() {
            this->swap(x);
        }

        // Copy/move assignment
        self_type& operator=(self_type x) noexcept {
            this->swap(x);
            return *this;
        }

        // Range constructor.
        template <class InputIterator>
        multiset(InputIterator b, InputIterator e,
                 const key_compare &comp = key_compare(),
                 const allocator_type &alloc = allocator_type())
                : super_type(b, e, comp, alloc) {
        }
    };

    template <typename K, typename C, typename A, int N>
    inline void swap(multiset<K, C, A, N> &x,
                     multiset<K, C, A, N> &y) {
        x.swap(y);
    }

} // namespace btree

#endif  // UTIL_BTREE_BTREE_SET_H__