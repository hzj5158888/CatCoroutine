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
// A btree implementation of the STL set and map interfaces. A btree is both
// smaller and faster than STL set/map. The red-black tree implementation of
// STL set/map has an overhead of 3 pointers (left, right and parent) plus the
// node color information for each stored value. So a set<int32> consumes 20
// bytes for each value stored. This btree implementation stores multiple
// values on fixed size nodes (usually 256 bytes) and doesn't store child
// pointers for leaf nodes. The result is that a set<int32> may use much
// less memory per stored value. For the random insertion benchmark in
// btree_test.cc, a set<int32> with node-size of 256 uses 4.9 bytes per
// stored value.
//
// The packing of multiple values on to each node of a btree has another effect
// besides better space utilization: better cache locality due to fewer cache
// lines being accessed. Better cache locality translates into faster
// operations.
//
// CAVEATS
//
// Insertions and deletions on a btree can cause splitting, merging or
// rebalancing of btree nodes. And even without these operations, insertions
// and deletions on a btree will move values around within a node. In both
// cases, the result is that insertions and deletions can invalidate iterators
// pointing to values other than the one being inserted/deleted. This is
// notably different from STL set/map which takes care to not invalidate
// iterators on insert/erase except, of course, for iterators pointing to the
// value being erased.  A partial workaround when erasing is available:
// erase() returns an iterator pointing to the item just after the one that was
// erased (or end() if none exists).  See also safe_btree.

// PERFORMANCE
//
//   btree_bench --benchmarks=. 2>&1 | ./benchmarks.awk
//
// Run on pmattis-warp.nyc (4 X 2200 MHz CPUs); 2010/03/04-15:23:06
// Benchmark                 STL(ns) B-Tree(ns) @    <size>
// --------------------------------------------------------
// BM_set_int32_insert        1516      608  +59.89%  <256>    [40.0,  5.2]
// BM_set_int32_lookup        1160      414  +64.31%  <256>    [40.0,  5.2]
// BM_set_int32_fulllookup     960      410  +57.29%  <256>    [40.0,  4.4]
// BM_set_int32_delete        1741      528  +69.67%  <256>    [40.0,  5.2]
// BM_set_int32_queueaddrem   3078     1046  +66.02%  <256>    [40.0,  5.5]
// BM_set_int32_mixedaddrem   3600     1384  +61.56%  <256>    [40.0,  5.3]
// BM_set_int32_fifo           227      113  +50.22%  <256>    [40.0,  4.4]
// BM_set_int32_fwditer        158       26  +83.54%  <256>    [40.0,  5.2]
// BM_map_int32_insert        1551      636  +58.99%  <256>    [48.0, 10.5]
// BM_map_int32_lookup        1200      508  +57.67%  <256>    [48.0, 10.5]
// BM_map_int32_fulllookup     989      487  +50.76%  <256>    [48.0,  8.8]
// BM_map_int32_delete        1794      628  +64.99%  <256>    [48.0, 10.5]
// BM_map_int32_queueaddrem   3189     1266  +60.30%  <256>    [48.0, 11.6]
// BM_map_int32_mixedaddrem   3822     1623  +57.54%  <256>    [48.0, 10.9]
// BM_map_int32_fifo           151      134  +11.26%  <256>    [48.0,  8.8]
// BM_map_int32_fwditer        161       32  +80.12%  <256>    [48.0, 10.5]
// BM_set_int64_insert        1546      636  +58.86%  <256>    [40.0, 10.5]
// BM_set_int64_lookup        1200      512  +57.33%  <256>    [40.0, 10.5]
// BM_set_int64_fulllookup     971      487  +49.85%  <256>    [40.0,  8.8]
// BM_set_int64_delete        1745      616  +64.70%  <256>    [40.0, 10.5]
// BM_set_int64_queueaddrem   3163     1195  +62.22%  <256>    [40.0, 11.6]
// BM_set_int64_mixedaddrem   3760     1564  +58.40%  <256>    [40.0, 10.9]
// BM_set_int64_fifo           146      103  +29.45%  <256>    [40.0,  8.8]
// BM_set_int64_fwditer        162       31  +80.86%  <256>    [40.0, 10.5]
// BM_map_int64_insert        1551      720  +53.58%  <256>    [48.0, 20.7]
// BM_map_int64_lookup        1214      612  +49.59%  <256>    [48.0, 20.7]
// BM_map_int64_fulllookup     994      592  +40.44%  <256>    [48.0, 17.2]
// BM_map_int64_delete        1778      764  +57.03%  <256>    [48.0, 20.7]
// BM_map_int64_queueaddrem   3189     1547  +51.49%  <256>    [48.0, 20.9]
// BM_map_int64_mixedaddrem   3779     1887  +50.07%  <256>    [48.0, 21.6]
// BM_map_int64_fifo           147      145   +1.36%  <256>    [48.0, 17.2]
// BM_map_int64_fwditer        162       41  +74.69%  <256>    [48.0, 20.7]
// BM_set_string_insert       1989     1966   +1.16%  <256>    [64.0, 44.5]
// BM_set_string_lookup       1709     1600   +6.38%  <256>    [64.0, 44.5]
// BM_set_string_fulllookup   1573     1529   +2.80%  <256>    [64.0, 35.4]
// BM_set_string_delete       2520     1920  +23.81%  <256>    [64.0, 44.5]
// BM_set_string_queueaddrem  4706     4309   +8.44%  <256>    [64.0, 48.3]
// BM_set_string_mixedaddrem  5080     4654   +8.39%  <256>    [64.0, 46.7]
// BM_set_string_fifo          318      512  -61.01%  <256>    [64.0, 35.4]
// BM_set_string_fwditer       182       93  +48.90%  <256>    [64.0, 44.5]
// BM_map_string_insert       2600     2227  +14.35%  <256>    [72.0, 55.8]
// BM_map_string_lookup       2068     1730  +16.34%  <256>    [72.0, 55.8]
// BM_map_string_fulllookup   1859     1618  +12.96%  <256>    [72.0, 44.0]
// BM_map_string_delete       3168     2080  +34.34%  <256>    [72.0, 55.8]
// BM_map_string_queueaddrem  5840     4701  +19.50%  <256>    [72.0, 59.4]
// BM_map_string_mixedaddrem  6400     5200  +18.75%  <256>    [72.0, 57.8]
// BM_map_string_fifo          398      596  -49.75%  <256>    [72.0, 44.0]
// BM_map_string_fwditer       243      113  +53.50%  <256>    [72.0, 55.8]

#ifndef UTIL_BTREE_BTREE_H__
#define UTIL_BTREE_BTREE_H__

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <algorithm>
#include <functional>
#include <iterator>
#include <limits>
#include <type_traits>
#include <new>
#include <string>
#include <utility>

#ifndef BTREE_NO_IOSTREAM
#include <iostream>
#include <ostream>
#endif

namespace btree {

// Inside a btree method, if we just call swap(), it will choose the
// btree::swap method, which we don't want. And we can't say ::swap
// because then MSVC won't pickup any std::swap() implementations. We
// can't just use std::swap() directly because then we don't get the
// specialization for types outside the std namespace. So the solution
// is to have a special swap helper function whose name doesn't
// collide with other swap functions defined by the btree classes.
    template <typename T>
    inline void btree_swap_helper(T &a, T &b) {
        using std::swap;
        swap(a, b);
    }

// A template helper used to select A or B based on a condition.
    template<bool cond, typename A, typename B>
    struct if_{
        typedef A type;
    };

    template<typename A, typename B>
    struct if_<false, A, B> {
        typedef B type;
    };

// Types small_ and big_ are promise that sizeof(small_) < sizeof(big_)
    typedef char small_;

    struct big_ {
        char dummy[2];
    };

// A helper type used to indicate that a key-compare-to functor has been
// provided. A user can specify a key-compare-to functor by doing:
//
//  struct MyStringComparer
//      : public util::btree::btree_key_compare_to_tag {
//    int operator()(const string &a, const string &b) const {
//      return a.compare(b);
//    }
//  };
//
// Note that the return type is an int and not a bool. There is a
// static_assert which enforces this return type.
    struct btree_key_compare_to_tag {
    };

// A helper class that indicates if the Compare parameter is derived from
// btree_key_compare_to_tag.
    template <typename Compare>
    struct btree_is_key_compare_to
            : public std::is_convertible<Compare, btree_key_compare_to_tag> {
    };

// A helper class to convert a boolean comparison into a three-way
// "compare-to" comparison that returns a negative value to indicate
// less-than, zero to indicate equality and a positive value to
// indicate greater-than. This helper class is specialized for
// less<string> and greater<string>. The btree_key_compare_to_adapter
// class is provided so that btree users automatically get the more
// efficient compare-to code when using common google string types
// with common comparison functors.
    template <typename Compare>
    struct btree_key_compare_to_adapter : Compare {
        btree_key_compare_to_adapter() { }
        btree_key_compare_to_adapter(const Compare &c) : Compare(c) { }
        btree_key_compare_to_adapter(const btree_key_compare_to_adapter<Compare> &c)
                : Compare(c) {
        }
        btree_key_compare_to_adapter &operator=(const btree_key_compare_to_adapter &) = default;
    };

    template <>
    struct btree_key_compare_to_adapter<std::less<std::string> >
            : public btree_key_compare_to_tag {
        btree_key_compare_to_adapter() {}
        btree_key_compare_to_adapter(const std::less<std::string>&) {}
        btree_key_compare_to_adapter(
                const btree_key_compare_to_adapter<std::less<std::string> >&) {}
        int operator()(const std::string &a, const std::string &b) const {
            return a.compare(b);
        }
        btree_key_compare_to_adapter &operator=(const btree_key_compare_to_adapter &) = default;
    };

    template <>
    struct btree_key_compare_to_adapter<std::greater<std::string> >
            : public btree_key_compare_to_tag {
        btree_key_compare_to_adapter() {}
        btree_key_compare_to_adapter(const std::greater<std::string>&) {}
        btree_key_compare_to_adapter(
                const btree_key_compare_to_adapter<std::greater<std::string> >&) {}
        int operator()(const std::string &a, const std::string &b) const {
            return b.compare(a);
        }
        btree_key_compare_to_adapter &operator=(const btree_key_compare_to_adapter &) = default;
    };

// A helper class that allows a compare-to functor to behave like a plain
// compare functor. This specialization is used when we do not have a
// compare-to functor.
    template <typename Key, typename Compare, bool HaveCompareTo>
    struct btree_key_comparer {
        btree_key_comparer() {}
        btree_key_comparer(Compare c) : comp(c) {}
        static bool bool_compare(const Compare &comp, const Key &x, const Key &y) {
            return comp(x, y);
        }
        bool operator()(const Key &x, const Key &y) const {
            return bool_compare(comp, x, y);
        }
        Compare comp;
    };

// A specialization of btree_key_comparer when a compare-to functor is
// present. We need a plain (boolean) comparison in some parts of the btree
// code, such as insert-with-hint.
    template <typename Key, typename Compare>
    struct btree_key_comparer<Key, Compare, true> {
        btree_key_comparer() {}
        btree_key_comparer(Compare c) : comp(c) {}
        static bool bool_compare(const Compare &comp, const Key &x, const Key &y) {
            return comp(x, y) < 0;
        }
        bool operator()(const Key &x, const Key &y) const {
            return bool_compare(comp, x, y);
        }
        Compare comp;
    };

// A helper function to compare to keys using the specified compare
// functor. This dispatches to the appropriate btree_key_comparer comparison,
// depending on whether we have a compare-to functor or not (which depends on
// whether Compare is derived from btree_key_compare_to_tag).
    template <typename Key, typename Compare>
    static bool btree_compare_keys(
            const Compare &comp, const Key &x, const Key &y) {
        typedef btree_key_comparer<Key, Compare,
                btree_is_key_compare_to<Compare>::value> key_comparer;
        return key_comparer::bool_compare(comp, x, y);
    }

    template <typename Key, typename Compare,
            typename Alloc, int TargetNodeSize, int ValueSize>
    struct btree_common_params {
        // If Compare is derived from btree_key_compare_to_tag then use it as the
        // key_compare type. Otherwise, use btree_key_compare_to_adapter<> which will
        // fall-back to Compare if we don't have an appropriate specialization.
        typedef typename if_<
                btree_is_key_compare_to<Compare>::value,
                Compare, btree_key_compare_to_adapter<Compare> >::type key_compare;
        // A type which indicates if we have a key-compare-to functor or a plain old
        // key-compare functor.
        typedef btree_is_key_compare_to<key_compare> is_key_compare_to;

        typedef Alloc allocator_type;
        typedef Key key_type;
        typedef size_t size_type;
        typedef ptrdiff_t difference_type;

        enum {
            kTargetNodeSize = TargetNodeSize,

            // Available space for values.  This is largest for leaf nodes,
            // which has overhead no fewer than two pointers.
            kNodeValueSpace = TargetNodeSize - 2 * sizeof(void*),
        };

        // This is an integral type large enough to hold as many
        // ValueSize-values as will fit a node of TargetNodeSize bytes.
        typedef typename if_<
                (kNodeValueSpace / ValueSize) >= 256,
                uint16_t,
                uint8_t>::type node_count_type;
    };

// A parameters structure for holding the type parameters for a btree_map.
    template <typename Key, typename Data, typename Compare,
            typename Alloc, int TargetNodeSize>
    struct btree_map_params
            : public btree_common_params<Key, Compare, Alloc, TargetNodeSize,
                    sizeof(Key) + sizeof(Data)> {
        typedef Data data_type;
        typedef Data mapped_type;
        typedef std::pair<const Key, data_type> value_type;
        typedef value_type* pointer;
        typedef const value_type* const_pointer;
        typedef value_type& reference;
        typedef const value_type& const_reference;

        enum {
            kValueSize = sizeof(value_type),
        };

        struct mutable_value_ref_wrapper {
            value_type &target;

            mutable_value_ref_wrapper(value_type &target) : target(target) {}

            Key &key() { return const_cast<Key &>(target.first); }
            data_type &value() { return target.second; }
        };

        static const Key& key(const value_type &x) { return x.first; }
        static const Key& key(mutable_value_ref_wrapper x) { return x.key(); }
        static void swap(mutable_value_ref_wrapper a, mutable_value_ref_wrapper b) {
            btree_swap_helper(a.key(), b.key());
            btree_swap_helper(a.value(), b.value());
        }
        static void move_assign(mutable_value_ref_wrapper to, mutable_value_ref_wrapper from) {
            to.key() = std::move(from.key());
            to.value() = std::move(from.value());
        }
        static void move_construct_at(mutable_value_ref_wrapper at, mutable_value_ref_wrapper from) {
            new (&at.target) value_type(std::move(from.key()), std::move(from.value()));
        }
    };

// A parameters structure for holding the type parameters for a set.
    template <typename Key, typename Compare, typename Alloc, int TargetNodeSize>
    struct btree_set_params
            : public btree_common_params<Key, Compare, Alloc, TargetNodeSize,
                    sizeof(Key)> {
        typedef std::false_type data_type;
        typedef std::false_type mapped_type;
        typedef Key value_type;
        typedef value_type* pointer;
        typedef const value_type* const_pointer;
        typedef value_type& reference;
        typedef const value_type& const_reference;

        enum {
            kValueSize = sizeof(Key),
        };

        struct mutable_value_ref_wrapper {
            value_type &target;

            mutable_value_ref_wrapper(value_type &target) : target(target) {}
        };

        static const Key& key(const value_type &x) { return x; }
        static void swap(mutable_value_ref_wrapper a, mutable_value_ref_wrapper b) {
            btree_swap_helper<value_type>(a.target, b.target);
        }
        static void move_assign(mutable_value_ref_wrapper to, mutable_value_ref_wrapper from) {
            to.target = std::move(from.target);
        }
        static void move_construct_at(mutable_value_ref_wrapper at, mutable_value_ref_wrapper from) {
            new (&at.target) value_type(std::move(from.target));
        }
    };

// An adapter class that converts a lower-bound compare into an upper-bound
// compare.
    template <typename Key, typename Compare>
    struct btree_upper_bound_adapter : public Compare {
        btree_upper_bound_adapter(Compare c) : Compare(c) {}
        bool operator()(const Key &a, const Key &b) const {
            return !static_cast<const Compare&>(*this)(b, a);
        }
    };

    template <typename Key, typename CompareTo>
    struct btree_upper_bound_compare_to_adapter : public CompareTo {
        btree_upper_bound_compare_to_adapter(CompareTo c) : CompareTo(c) {}
        int operator()(const Key &a, const Key &b) const {
            return static_cast<const CompareTo&>(*this)(b, a);
        }
    };

// Dispatch helper class for using linear search with plain compare.
    template <typename K, typename N, typename Compare>
    struct btree_linear_search_plain_compare {
        static int lower_bound(const K &k, const N &n, Compare comp)  {
            return n.linear_search_plain_compare(k, 0, n.count(), comp);
        }
        static int upper_bound(const K &k, const N &n, Compare comp)  {
            typedef btree_upper_bound_adapter<K, Compare> upper_compare;
            return n.linear_search_plain_compare(k, 0, n.count(), upper_compare(comp));
        }
    };

// Dispatch helper class for using linear search with compare-to
    template <typename K, typename N, typename CompareTo>
    struct btree_linear_search_compare_to {
        static int lower_bound(const K &k, const N &n, CompareTo comp)  {
            return n.linear_search_compare_to(k, 0, n.count(), comp);
        }
        static int upper_bound(const K &k, const N &n, CompareTo comp)  {
            typedef btree_upper_bound_adapter<K,
                    btree_key_comparer<K, CompareTo, true> > upper_compare;
            return n.linear_search_plain_compare(k, 0, n.count(), upper_compare(comp));
        }
    };

// Dispatch helper class for using binary search with plain compare.
    template <typename K, typename N, typename Compare>
    struct btree_binary_search_plain_compare {
        static int lower_bound(const K &k, const N &n, Compare comp)  {
            return n.binary_search_plain_compare(k, 0, n.count(), comp);
        }
        static int upper_bound(const K &k, const N &n, Compare comp)  {
            typedef btree_upper_bound_adapter<K, Compare> upper_compare;
            return n.binary_search_plain_compare(k, 0, n.count(), upper_compare(comp));
        }
    };

// Dispatch helper class for using binary search with compare-to.
    template <typename K, typename N, typename CompareTo>
    struct btree_binary_search_compare_to {
        static int lower_bound(const K &k, const N &n, CompareTo comp)  {
            return n.binary_search_compare_to(k, 0, n.count(), CompareTo());
        }
        static int upper_bound(const K &k, const N &n, CompareTo comp)  {
            typedef btree_upper_bound_adapter<K,
                    btree_key_comparer<K, CompareTo, true> > upper_compare;
            return n.linear_search_plain_compare(k, 0, n.count(), upper_compare(comp));
        }
    };

// A node in the btree holding. The same node type is used for both internal
// and leaf nodes in the btree, though the nodes are allocated in such a way
// that the children array is only valid in internal nodes.
    template <typename Params>
    class btree_node {
    public:
        typedef Params params_type;
        typedef btree_node<Params> self_type;
        typedef typename Params::key_type key_type;
        typedef typename Params::data_type data_type;
        typedef typename Params::value_type value_type;
        typedef typename Params::mutable_value_ref_wrapper mutable_value_ref_wrapper;
        typedef typename Params::pointer pointer;
        typedef typename Params::const_pointer const_pointer;
        typedef typename Params::reference reference;
        typedef typename Params::const_reference const_reference;
        typedef typename Params::key_compare key_compare;
        typedef typename Params::size_type size_type;
        typedef typename Params::difference_type difference_type;
        // Typedefs for the various types of node searches.
        typedef btree_linear_search_plain_compare<
                key_type, self_type, key_compare> linear_search_plain_compare_type;
        typedef btree_linear_search_compare_to<
                key_type, self_type, key_compare> linear_search_compare_to_type;
        typedef btree_binary_search_plain_compare<
                key_type, self_type, key_compare> binary_search_plain_compare_type;
        typedef btree_binary_search_compare_to<
                key_type, self_type, key_compare> binary_search_compare_to_type;
        // If we have a valid key-compare-to type, use linear_search_compare_to,
        // otherwise use linear_search_plain_compare.
        typedef typename if_<
                Params::is_key_compare_to::value,
                linear_search_compare_to_type,
                linear_search_plain_compare_type>::type linear_search_type;
        // If we have a valid key-compare-to type, use binary_search_compare_to,
        // otherwise use binary_search_plain_compare.
        typedef typename if_<
                Params::is_key_compare_to::value,
                binary_search_compare_to_type,
                binary_search_plain_compare_type>::type binary_search_type;
        // If the key is an integral or floating point type, use linear search which
        // is faster than binary search for such types. Might be wise to also
        // configure linear search based on node-size.
        typedef typename if_<
                std::is_integral<key_type>::value ||
                std::is_floating_point<key_type>::value,
                linear_search_type, binary_search_type>::type search_type;

        struct base_fields {
            typedef typename Params::node_count_type field_type;

            // A boolean indicating whether the node is a leaf or not.
            bool leaf;
            // The position of the node in the node's parent.
            field_type position;
            // The maximum number of values the node can hold.
            field_type max_count;
            // The count of the number of values in the node.
            field_type count;
            // A pointer to the node's parent.
            btree_node *parent;
        };

        constexpr static size_t align_size(size_t size, size_t alignment) {
            return (size + alignment - 1) & ~(alignment - 1);
        }

        enum {
            kValueSize = params_type::kValueSize,
            kValueOffset = align_size(sizeof(base_fields), alignof(value_type)),
            kTargetNodeSize = params_type::kTargetNodeSize,

            // Compute how many values we can fit onto a leaf node.
            kNodeTargetValues = (kTargetNodeSize - kValueOffset) / kValueSize,
            // We need a minimum of 3 values per internal node in order to perform
            // splitting (1 value for the two nodes involved in the split and 1 value
            // propagated to the parent as the delimiter for the split).
            kNodeValues = kNodeTargetValues >= 3 ? kNodeTargetValues : 3,

            kExactMatch = 1 << 30,
            kMatchMask = kExactMatch - 1,
        };

        struct root_fields {
            btree_node *rightmost;
            size_type size;
        };

        /*
         * A btree node is variable size depending on which fields are required for a specific node type.
         * struct btree_node only includes base_fields, as these are always required, and to avoid UB when the allocation would be smaller than the struct.
         * Further fields are laid out sequentially in memory as required.
         *
         *                                                    + + +
         * base_fields                                        | | |
         *                                                    | | | <--- leaf node
         * value_type values[kNodeValues]                     | | |
         *                                                    | | +
         * btree_node *children[kNodeValues + 1]              | | <--- internal node
         *                                                    | +
         * root_fields                                        | <--- root node
         *                                                    +
         *
         * values may have fewer than kNodeValues entries for nodes created using new_leaf_root_node().
         * the number of allocated entries for values is stored in base_fields::max_count.
         */

        enum {
            kFullLeafNodeSize = kValueOffset + (sizeof(value_type) * kNodeValues),
            kChildrenOffset = align_size(kFullLeafNodeSize, alignof(btree_node *)),
            kChildrenValues = kNodeValues + 1,
            kInternalNodeSize = kChildrenOffset + (sizeof(btree_node *) * kChildrenValues),
            kRootFieldsOffset = align_size(kInternalNodeSize, alignof(root_fields)),
            kRootNodeSize = kRootFieldsOffset + sizeof(root_fields),
        };

    public:
        ~btree_node() = delete;

        btree_node **get_children_ptr() {
            return reinterpret_cast<btree_node **>(reinterpret_cast<char *>(this) + kChildrenOffset);
        }
        btree_node * const *get_children_ptr() const {
            return reinterpret_cast<btree_node * const *>(reinterpret_cast<const char *>(this) + kChildrenOffset);
        }

        value_type *get_values_ptr() {
            return reinterpret_cast<value_type *>(reinterpret_cast<char *>(this) + kValueOffset);
        }
        const value_type *get_values_ptr() const {
            return reinterpret_cast<const value_type *>(reinterpret_cast<const char *>(this) + kValueOffset);
        }

        root_fields &get_root_fields() {
            return *reinterpret_cast<root_fields *>(reinterpret_cast<char *>(this) + kRootFieldsOffset);
        }
        const root_fields &get_root_fields() const {
            return *reinterpret_cast<const root_fields *>(reinterpret_cast<const char *>(this) + kRootFieldsOffset);
        }

        // Getter/setter for whether this is a leaf node or not. This value doesn't
        // change after the node is created.
        bool leaf() const { return fields_.leaf; }

        // Getter for the position of this node in its parent.
        int position() const { return fields_.position; }
        void set_position(int v) { fields_.position = static_cast<typename Params::node_count_type>(v); }

        // Getter/setter for the number of values stored in this node.
        int count() const { return fields_.count; }
        void set_count(int v) { fields_.count = static_cast<typename Params::node_count_type>(v); }
        int max_count() const { return fields_.max_count; }

        // Getter for the parent of this node.
        btree_node* parent() const { return fields_.parent; }
        // Getter for whether the node is the root of the tree. The parent of the
        // root of the tree is the leftmost node in the tree which is guaranteed to
        // be a leaf.
        bool is_root() const { return parent()->leaf(); }
        void make_root() {
            assert(parent()->is_root());
            fields_.parent = fields_.parent->parent();
        }

        // Getter for the rightmost root node field. Only valid on the root node.
        btree_node* rightmost() const { return get_root_fields().rightmost; }
        btree_node** mutable_rightmost() { return &get_root_fields().rightmost; }

        // Getter for the size root node field. Only valid on the root node.
        size_type size() const { return get_root_fields().size; }
        size_type* mutable_size() { return &get_root_fields().size; }

        // Getters for the key/value at position i in the node.
        const key_type& key(int i) const {
            return params_type::key(get_values_ptr()[i]);
        }
        reference value(int i) {
            return get_values_ptr()[i];
        }
        const_reference value(int i) const {
            return get_values_ptr()[i];
        }
        mutable_value_ref_wrapper mutable_value(int i) {
            return mutable_value_ref_wrapper(value(i));
        }

        // Swap value i in this node with value j in node x.
        void value_swap(int i, btree_node *x, int j) {
            params_type::swap(mutable_value(i), x->mutable_value(j));
        }

        // Move assign value i in this node from value j in node x.
        void value_move(int i, btree_node *x, int j) {
            params_type::move_assign(mutable_value(i), x->mutable_value(j));
        }

        // Move construct value i in this node from value j in node x.
        void value_move_construct(int i, btree_node *x, int j) {
            params_type::move_construct_at(mutable_value(i), x->mutable_value(j));
        }

        // Getters/setter for the child at position i in the node.
        btree_node* child(int i) const { return get_children_ptr()[i]; }
        btree_node** mutable_child(int i) { return get_children_ptr() + i; }
        void set_child(int i, btree_node *c) {
            *mutable_child(i) = c;
            c->fields_.parent = this;
            c->fields_.position = static_cast<typename Params::node_count_type>(i);
        }

        // Returns the position of the first value whose key is not less than k.
        template <typename Compare>
        int lower_bound(const key_type &k, const Compare &comp) const {
            return search_type::lower_bound(k, *this, comp);
        }
        // Returns the position of the first value whose key is greater than k.
        template <typename Compare>
        int upper_bound(const key_type &k, const Compare &comp) const {
            return search_type::upper_bound(k, *this, comp);
        }

        // Returns the position of the first value whose key is not less than k using
        // linear search performed using plain compare.
        template <typename Compare>
        int linear_search_plain_compare(
                const key_type &k, int s, int e, const Compare &comp) const {
            while (s < e) {
                if (!btree_compare_keys(comp, key(s), k)) {
                    break;
                }
                ++s;
            }
            return s;
        }

        // Returns the position of the first value whose key is not less than k using
        // linear search performed using compare-to.
        template <typename Compare>
        int linear_search_compare_to(
                const key_type &k, int s, int e, const Compare &comp) const {
            while (s < e) {
                int c = comp(key(s), k);
                if (c == 0) {
                    return s | kExactMatch;
                } else if (c > 0) {
                    break;
                }
                ++s;
            }
            return s;
        }

        // Returns the position of the first value whose key is not less than k using
        // binary search performed using plain compare.
        template <typename Compare>
        int binary_search_plain_compare(
                const key_type &k, int s, int e, const Compare &comp) const {
            while (s != e) {
                int mid = (s + e) / 2;
                if (btree_compare_keys(comp, key(mid), k)) {
                    s = mid + 1;
                } else {
                    e = mid;
                }
            }
            return s;
        }

        // Returns the position of the first value whose key is not less than k using
        // binary search performed using compare-to.
        template <typename CompareTo>
        int binary_search_compare_to(
                const key_type &k, int s, int e, const CompareTo &comp) const {
            while (s != e) {
                int mid = (s + e) / 2;
                int c = comp(key(mid), k);
                if (c < 0) {
                    s = mid + 1;
                } else if (c > 0) {
                    e = mid;
                } else {
                    // Need to return the first value whose key is not less than k, which
                    // requires continuing the binary search. Note that we are guaranteed
                    // that the result is an exact match because if "key(mid-1) < k" the
                    // call to binary_search_compare_to() will return "mid".
                    s = binary_search_compare_to(k, s, mid, comp);
                    return s | kExactMatch;
                }
            }
            return s;
        }

    private:
        void insert_value_common(int i);

    public:
        // Inserts the value x at position i, shifting all existing values and
        // children at positions >= i to the right by 1.
        template <typename... Args>
        void insert_value(int i, Args&&... args);

        // Removes the value at position i, shifting all existing values and children
        // at positions > i to the left by 1.
        void remove_value(int i);

        // Rebalances a node with its right sibling.
        void rebalance_right_to_left(btree_node *sibling, int to_move);
        void rebalance_left_to_right(btree_node *sibling, int to_move);

        // Splits a node, moving a portion of the node's values to its right sibling.
        void split(btree_node *sibling, int insert_position);

        // Merges a node with its right sibling, moving all of the values and the
        // delimiting key in the parent node onto itself.
        void merge(btree_node *sibling);

        // Swap the contents of "this" and "src".
        void swap(btree_node *src);

        // Node allocation/deletion routines.
        static void init_leaf(
                btree_node *f, btree_node *parent, int max_count) {
            f->fields_.leaf = 1;
            f->fields_.position = 0;
            f->fields_.max_count = static_cast<typename base_fields::field_type>(max_count);
            f->fields_.count = 0;
            f->fields_.parent = parent;
#ifdef BTREE_DEBUG
            memset(f->get_values_ptr(), 0, max_count * sizeof(value_type));
#endif
        }
        static void init_internal(btree_node *f, btree_node *parent) {
            init_leaf(f, parent, kNodeValues);
            f->fields_.leaf = 0;
#ifdef BTREE_DEBUG
            memset(f->get_children_ptr(), 0, sizeof(btree_node *) * kChildrenValues);
#endif
        }
        static void init_root(btree_node *f, btree_node *parent) {
            init_internal(f, parent);
            root_fields &root = f->get_root_fields();
            root.rightmost = parent;
            root.size = parent->count();
        }
        void destroy() {
            for (int i = 0; i < count(); ++i) {
                value_destroy(i);
            }
        }

    private:
        template <typename... Args>
        void value_init_args(int i, Args&&... args) {
            new (get_values_ptr() + i) value_type(std::forward<Args>(args)...);
        }
        void value_destroy(int i) {
            get_values_ptr()[i].~value_type();
        }

    private:
        base_fields fields_;

    private:
        btree_node(const btree_node&);
        void operator=(const btree_node&);
    };

    template <typename Node, typename Reference, typename Pointer>
    struct btree_iterator {
        typedef typename Node::key_type key_type;
        typedef typename Node::size_type size_type;
        typedef typename Node::difference_type difference_type;
        typedef typename Node::params_type params_type;

        typedef Node node_type;
        typedef typename std::remove_const<Node>::type normal_node;
        typedef const Node const_node;
        typedef typename params_type::value_type value_type;
        typedef typename params_type::pointer normal_pointer;
        typedef typename params_type::reference normal_reference;
        typedef typename params_type::const_pointer const_pointer;
        typedef typename params_type::const_reference const_reference;

        typedef Pointer pointer;
        typedef Reference reference;
        typedef std::bidirectional_iterator_tag iterator_category;

        typedef btree_iterator<
                normal_node, normal_reference, normal_pointer> iterator;
        typedef btree_iterator<
                const_node, const_reference, const_pointer> const_iterator;
        typedef btree_iterator<Node, Reference, Pointer> self_type;

        btree_iterator()
                : node(NULL),
                  position(-1) {
        }
        btree_iterator(Node *n, int p)
                : node(n),
                  position(p) {
        }
        btree_iterator(const iterator &x)
                : node(x.node),
                  position(x.position) {
        }

        btree_iterator &operator=(const btree_iterator &) = default;

        // Increment/decrement the iterator.
        void increment() {
            if (node->leaf() && ++position < node->count()) {
                return;
            }
            increment_slow();
        }
        void increment_by(int count);
        void increment_slow();

        void decrement() {
            if (node->leaf() && --position >= 0) {
                return;
            }
            decrement_slow();
        }
        void decrement_slow();

#if defined(__cplusplus) && __cplusplus >= 202002L
        friend bool operator==(const btree_iterator &a, const btree_iterator &b) noexcept {
            return a.node == b.node && a.position == b.position;
        }
#else
        bool operator==(const const_iterator &x) const {
    return node == x.node && position == x.position;
  }
  bool operator!=(const const_iterator &x) const {
    return node != x.node || position != x.position;
  }
#endif

        // Accessors for the key/value the iterator is pointing at.
        const key_type& key() const {
            return node->key(position);
        }
        reference operator*() const {
            return node->value(position);
        }
        pointer operator->() const {
            return &node->value(position);
        }

        self_type& operator++() {
            increment();
            return *this;
        }
        self_type& operator--() {
            decrement();
            return *this;
        }
        self_type operator++(int) {
            self_type tmp = *this;
            ++*this;
            return tmp;
        }
        self_type operator--(int) {
            self_type tmp = *this;
            --*this;
            return tmp;
        }

        // The node in the tree the iterator is pointing at.
        Node *node;
        // The position within the node of the tree the iterator is pointing at.
        int position;
    };

// Dispatch helper class for using btree::internal_locate with plain compare.
    struct btree_internal_locate_plain_compare {
        template <typename K, typename T, typename Iter>
        static std::pair<Iter, int> dispatch(const K &k, const T &t, Iter iter) {
            return t.internal_locate_plain_compare(k, iter);
        }
    };

// Dispatch helper class for using btree::internal_locate with compare-to.
    struct btree_internal_locate_compare_to {
        template <typename K, typename T, typename Iter>
        static std::pair<Iter, int> dispatch(const K &k, const T &t, Iter iter) {
            return t.internal_locate_compare_to(k, iter);
        }
    };

    template <typename Params>
    class btree : public Params::key_compare {
        typedef btree<Params> self_type;
        typedef btree_node<Params> node_type;
        typedef typename node_type::base_fields base_fields;
        typedef typename Params::is_key_compare_to is_key_compare_to;

        friend struct btree_internal_locate_plain_compare;
        friend struct btree_internal_locate_compare_to;
        typedef typename if_<
                is_key_compare_to::value,
                btree_internal_locate_compare_to,
                btree_internal_locate_plain_compare>::type internal_locate_type;

        enum {
            kNodeValues = node_type::kNodeValues,
            kMinNodeValues = kNodeValues / 2,
            kValueSize = node_type::kValueSize,
            kExactMatch = node_type::kExactMatch,
            kMatchMask = node_type::kMatchMask,

            kValueOffset = node_type::kValueOffset,
            kFullLeafNodeSize = node_type::kFullLeafNodeSize,
            kInternalNodeSize = node_type::kInternalNodeSize,
            kRootNodeSize = node_type::kRootNodeSize,
        };

        // A helper class to get the empty base class optimization for 0-size
        // allocators. Base is internal_allocator_type.
        // (e.g. empty_base_handle<internal_allocator_type, node_type*>). If Base is
        // 0-size, the compiler doesn't have to reserve any space for it and
        // sizeof(empty_base_handle) will simply be sizeof(Data). Google [empty base
        // class optimization] for more details.
        template <typename Base, typename Data>
        struct empty_base_handle : public Base {
            empty_base_handle(const Base &b, const Data &d)
                    : Base(b),
                      data(d) {
            }
            Data data;
        };

        struct node_stats {
            node_stats(size_t l, size_t i)
                    : leaf_nodes(l),
                      internal_nodes(i) {
            }

            node_stats& operator+=(const node_stats &x) {
                leaf_nodes += x.leaf_nodes;
                internal_nodes += x.internal_nodes;
                return *this;
            }

            size_t leaf_nodes;
            size_t internal_nodes;
        };

    public:
        typedef Params params_type;
        typedef typename Params::key_type key_type;
        typedef typename Params::data_type data_type;
        typedef typename Params::mapped_type mapped_type;
        typedef typename Params::value_type value_type;
        typedef typename Params::key_compare key_compare;
        typedef typename Params::pointer pointer;
        typedef typename Params::const_pointer const_pointer;
        typedef typename Params::reference reference;
        typedef typename Params::const_reference const_reference;
        typedef typename Params::size_type size_type;
        typedef typename Params::difference_type difference_type;
        typedef btree_iterator<node_type, reference, pointer> iterator;
        typedef typename iterator::const_iterator const_iterator;
        typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
        typedef std::reverse_iterator<iterator> reverse_iterator;

        typedef typename Params::allocator_type allocator_type;
        using internal_allocator_type = typename std::allocator_traits<allocator_type>::template rebind_alloc<char>;

    public:
        // Default constructor.
        btree(const key_compare &comp, const allocator_type &alloc);

        // Copy constructor.
        btree(const self_type &x);

        // Destructor.
        ~btree() {
            clear();
        }

        // Iterator routines.
        iterator begin() {
            return iterator(leftmost(), 0);
        }
        const_iterator begin() const {
            return const_iterator(leftmost(), 0);
        }
        iterator end() {
            node_type* const right = rightmost();
            return iterator(right, right ? right->count() : 0);
        }
        const_iterator end() const {
            const node_type* const right = rightmost();
            return const_iterator(right, right ? right->count() : 0);
        }
        reverse_iterator rbegin() {
            return reverse_iterator(end());
        }
        const_reverse_iterator rbegin() const {
            return const_reverse_iterator(end());
        }
        reverse_iterator rend() {
            return reverse_iterator(begin());
        }
        const_reverse_iterator rend() const {
            return const_reverse_iterator(begin());
        }

        // Finds the first element whose key is not less than key.
        iterator lower_bound(const key_type &key) {
            return internal_end(
                    internal_lower_bound(key, iterator(root(), 0)));
        }
        const_iterator lower_bound(const key_type &key) const {
            return internal_end(
                    internal_lower_bound(key, const_iterator(root(), 0)));
        }

        // Finds the first element whose key is greater than key.
        iterator upper_bound(const key_type &key) {
            return internal_end(
                    internal_upper_bound(key, iterator(root(), 0)));
        }
        const_iterator upper_bound(const key_type &key) const {
            return internal_end(
                    internal_upper_bound(key, const_iterator(root(), 0)));
        }

        // Finds the range of values which compare equal to key. The first member of
        // the returned pair is equal to lower_bound(key). The second member pair of
        // the pair is equal to upper_bound(key).
        std::pair<iterator,iterator> equal_range(const key_type &key) {
            return std::make_pair(lower_bound(key), upper_bound(key));
        }
        std::pair<const_iterator,const_iterator> equal_range(const key_type &key) const {
            return std::make_pair(lower_bound(key), upper_bound(key));
        }

        // Inserts a value into the btree only if it does not already exist. The
        // boolean return value indicates whether insertion succeeded or failed. The
        // Args&&... type is used to avoid instatiating the value unless the key
        // is being inserted. See btree_map::operator[].
        template <typename... Args>
        std::pair<iterator,bool> insert_unique_args(const key_type &key, Args&&... args);

        // Inserts a value into the btree only if it does not already exist. The
        // boolean return value indicates whether insertion succeeded or failed.
        std::pair<iterator,bool> insert_unique(const value_type &v) {
            return insert_unique_args(params_type::key(v), v);
        }
        std::pair<iterator,bool> insert_unique(value_type &&v) {
            return insert_unique_args(params_type::key(v), std::move(v));
        }

        template <typename... Args>
        iterator insert_unique_hint_args(iterator position, const key_type &key, Args&&... args);

        // Insert with hint. Check to see if the value should be placed immediately
        // before position in the tree. If it does, then the insertion will take
        // amortized constant time. If not, the insertion will take amortized
        // logarithmic time as if a call to insert_unique(v) were made.
        iterator insert_unique(iterator position, const value_type &v) {
            return insert_unique_hint_args(position, params_type::key(v), v);
        }
        iterator insert_unique(iterator position, value_type &&v) {
            return insert_unique_hint_args(position, params_type::key(v), std::move(v));
        }

        // Insert a range of values into the btree.
        template <typename InputIterator>
        void insert_unique(InputIterator b, InputIterator e);

        // Inserts a value into the btree. The Args&&... type is used to avoid
        // instatiating the value unless the key is being inserted. See
        // btree_map::operator[].
        template <typename... Args>
        iterator insert_multi_args(const key_type &key, Args&&... args);

        // Inserts a value into the btree.
        iterator insert_multi(const value_type &v) {
            return insert_multi_args(params_type::key(v), v);
        }
        iterator insert_multi(value_type &&v) {
            return insert_multi_args(params_type::key(v), std::move(v));
        }

        template <typename... Args>
        iterator insert_multi_hint_args(iterator position, const key_type &key, Args&&... args);

        // Insert with hint. Check to see if the value should be placed immediately
        // before position in the tree. If it does, then the insertion will take
        // amortized constant time. If not, the insertion will take amortized
        // logarithmic time as if a call to insert_multi(v) were made.
        iterator insert_multi(iterator position, const value_type &v) {
            return insert_multi_hint_args(position, params_type::key(v), v);
        }
        iterator insert_multi(iterator position, value_type &&v) {
            return insert_multi_hint_args(position, params_type::key(v), std::move(v));
        }

        // Insert a range of values into the btree.
        template <typename InputIterator>
        void insert_multi(InputIterator b, InputIterator e);

        void assign(const self_type &x);

        // Erase the specified iterator from the btree. The iterator must be valid
        // (i.e. not equal to end()).  Return an iterator pointing to the node after
        // the one that was erased (or end() if none exists).
        iterator erase(iterator iter);

        // Erases range. Returns the number of keys erased.
        int erase(iterator begin, iterator end);

        // Erases the specified key from the btree. Returns 1 if an element was
        // erased and 0 otherwise.
        int erase_unique(const key_type &key);

        // Erases all of the entries matching the specified key from the
        // btree. Returns the number of elements erased.
        int erase_multi(const key_type &key);

        // Finds the iterator corresponding to a key or returns end() if the key is
        // not present.
        iterator find_unique(const key_type &key) {
            return internal_end(
                    internal_find_unique(key, iterator(root(), 0)));
        }
        const_iterator find_unique(const key_type &key) const {
            return internal_end(
                    internal_find_unique(key, const_iterator(root(), 0)));
        }
        iterator find_multi(const key_type &key) {
            return internal_end(
                    internal_find_multi(key, iterator(root(), 0)));
        }
        const_iterator find_multi(const key_type &key) const {
            return internal_end(
                    internal_find_multi(key, const_iterator(root(), 0)));
        }

        // Returns a count of the number of times the key appears in the btree.
        size_type count_unique(const key_type &key) const {
            const_iterator begin = internal_find_unique(
                    key, const_iterator(root(), 0));
            if (!begin.node) {
                // The key doesn't exist in the tree.
                return 0;
            }
            return 1;
        }
        // Returns a count of the number of times the key appears in the btree.
        size_type count_multi(const key_type &key) const {
            return distance(lower_bound(key), upper_bound(key));
        }

        // Clear the btree, deleting all of the values it contains.
        void clear();

        // Swap the contents of *this and x.
        void swap(self_type &x);

        // Assign the contents of x to *this.
        self_type& operator=(const self_type &x) {
            if (&x == this) {
                // Don't copy onto ourselves.
                return *this;
            }
            assign(x);
            return *this;
        }

        key_compare* mutable_key_comp() {
            return this;
        }
        const key_compare& key_comp() const {
            return *this;
        }
        bool compare_keys(const key_type &x, const key_type &y) const {
            return btree_compare_keys(key_comp(), x, y);
        }

#ifndef BTREE_NO_IOSTREAM
        // Dump the btree to the specified ostream. Requires that operator<< is
        // defined for Key and Value.
        void dump(std::ostream &os) const {
            if (root() != NULL) {
                internal_dump(os, root(), 0);
            }
        }
#endif

        // Verifies the structure of the btree.
        void verify() const;

        // Size routines. Note that empty() is slightly faster than doing size()==0.
        size_type size() const {
            if (empty()) return 0;
            if (root()->leaf()) return root()->count();
            return root()->size();
        }
        size_type max_size() const { return std::numeric_limits<size_type>::max(); }
        bool empty() const { return root() == NULL; }

        // The height of the btree. An empty tree will have height 0.
        size_type height() const {
            size_type h = 0;
            if (root()) {
                // Count the length of the chain from the leftmost node up to the
                // root. We actually count from the root back around to the level below
                // the root, but the calculation is the same because of the circularity
                // of that traversal.
                const node_type *n = root();
                do {
                    ++h;
                    n = n->parent();
                } while (n != root());
            }
            return h;
        }

        // The number of internal, leaf and total nodes used by the btree.
        size_type leaf_nodes() const {
            return internal_stats(root()).leaf_nodes;
        }
        size_type internal_nodes() const {
            return internal_stats(root()).internal_nodes;
        }
        size_type nodes() const {
            node_stats stats = internal_stats(root());
            return stats.leaf_nodes + stats.internal_nodes;
        }

        // The total number of bytes used by the btree.
        size_type bytes_used() const {
            node_stats stats = internal_stats(root());
            if (stats.leaf_nodes == 1 && stats.internal_nodes == 0) {
                return sizeof(*this) +
                       kValueOffset + root()->max_count() * sizeof(value_type);
            } else {
                return sizeof(*this) +
                       kRootNodeSize - kInternalNodeSize +
                       stats.leaf_nodes * kFullLeafNodeSize +
                       stats.internal_nodes * kInternalNodeSize;
            }
        }

        // The average number of bytes used per value stored in the btree.
        static double average_bytes_per_value() {
            // Returns the number of bytes per value on a leaf node that is 75%
            // full. Experimentally, this matches up nicely with the computed number of
            // bytes per value in trees that had their values inserted in random order.
            return kFullLeafNodeSize / (kNodeValues * 0.75);
        }

        // The fullness of the btree. Computed as the number of elements in the btree
        // divided by the maximum number of elements a tree with the current number
        // of nodes could hold. A value of 1 indicates perfect space
        // utilization. Smaller values indicate space wastage.
        double fullness() const {
            return double(size()) / (nodes() * kNodeValues);
        }
        // The overhead of the btree structure in bytes per node. Computed as the
        // total number of bytes used by the btree minus the number of bytes used for
        // storing elements divided by the number of elements.
        double overhead() const {
            if (empty()) {
                return 0.0;
            }
            return (bytes_used() - size() * kValueSize) / double(size());
        }

    private:
        // Internal accessor routines.
        node_type* root() { return root_.data; }
        const node_type* root() const { return root_.data; }
        node_type** mutable_root() { return &root_.data; }

        // The rightmost node is stored in the root node.
        node_type* rightmost() {
            return (!root() || root()->leaf()) ? root() : root()->rightmost();
        }
        const node_type* rightmost() const {
            return (!root() || root()->leaf()) ? root() : root()->rightmost();
        }
        node_type** mutable_rightmost() { return root()->mutable_rightmost(); }

        // The leftmost node is stored as the parent of the root node.
        node_type* leftmost() { return root() ? root()->parent() : NULL; }
        const node_type* leftmost() const { return root() ? root()->parent() : NULL; }

        // The size of the tree is stored in the root node.
        size_type* mutable_size() { return root()->mutable_size(); }

        // Allocator routines.
        internal_allocator_type* mutable_internal_allocator() {
            return static_cast<internal_allocator_type*>(&root_);
        }
        const internal_allocator_type& internal_allocator() const {
            return *static_cast<const internal_allocator_type*>(&root_);
        }

        // Node creation/deletion routines.
        node_type* new_internal_node(node_type *parent) {
            node_type *p = reinterpret_cast<node_type*>(
                    mutable_internal_allocator()->allocate(kInternalNodeSize));
            node_type::init_internal(p, parent);
            return p;
        }
        node_type* new_internal_root_node() {
            node_type *p = reinterpret_cast<node_type*>(
                    mutable_internal_allocator()->allocate(kRootNodeSize));
            node_type::init_root(p, root()->parent());
            return p;
        }
        node_type* new_leaf_node(node_type *parent) {
            node_type *p = reinterpret_cast<node_type*>(
                    mutable_internal_allocator()->allocate(
                            kValueOffset + kNodeValues * sizeof(value_type)));
            node_type::init_leaf(p, parent, kNodeValues);
            return p;
        }
        node_type* new_leaf_root_node(int max_count) {
            node_type *p = reinterpret_cast<node_type*>(
                    mutable_internal_allocator()->allocate(
                            kValueOffset + max_count * sizeof(value_type)));
            node_type::init_leaf(p, p, max_count);
            return p;
        }
        void delete_internal_node(node_type *node) {
            node->destroy();
            assert(node != root());
            mutable_internal_allocator()->deallocate(
                    reinterpret_cast<char*>(node), kInternalNodeSize);
        }
        void delete_internal_root_node() {
            root()->destroy();
            mutable_internal_allocator()->deallocate(
                    reinterpret_cast<char*>(root()), kRootNodeSize);
        }
        void delete_leaf_node(node_type *node) {
            node->destroy();
            mutable_internal_allocator()->deallocate(
                    reinterpret_cast<char*>(node),
                    kValueOffset + node->max_count() * sizeof(value_type));
        }

        // Rebalances or splits the node iter points to.
        void rebalance_or_split(iterator *iter);

        // Merges the values of left, right and the delimiting key on their parent
        // onto left, removing the delimiting key and deleting right.
        void merge_nodes(node_type *left, node_type *right);

        // Tries to merge node with its left or right sibling, and failing that,
        // rebalance with its left or right sibling. Returns true if a merge
        // occurred, at which point it is no longer valid to access node. Returns
        // false if no merging took place.
        bool try_merge_or_rebalance(iterator *iter);

        // Tries to shrink the height of the tree by 1.
        void try_shrink();

        iterator internal_end(iterator iter) {
            return iter.node ? iter : end();
        }
        const_iterator internal_end(const_iterator iter) const {
            return iter.node ? iter : end();
        }

        iterator internal_insert_common(iterator iter);

        // Inserts a value into the btree immediately before iter. Requires that
        // key(v) <= iter.key() and (--iter).key() <= key(v).
        template <typename... Args>
        iterator internal_insert(iterator iter, Args&&... args);

        // Returns an iterator pointing to the first value >= the value "iter" is
        // pointing at. Note that "iter" might be pointing to an invalid location as
        // iter.position == iter.node->count(). This routine simply moves iter up in
        // the tree to a valid location.
        template <typename IterType>
        static IterType internal_last(IterType iter);

        // Returns an iterator pointing to the leaf position at which key would
        // reside in the tree. We provide 2 versions of internal_locate. The first
        // version (internal_locate_plain_compare) always returns 0 for the second
        // field of the pair. The second version (internal_locate_compare_to) is for
        // the key-compare-to specialization and returns either kExactMatch (if the
        // key was found in the tree) or -kExactMatch (if it wasn't) in the second
        // field of the pair. The compare_to specialization allows the caller to
        // avoid a subsequent comparison to determine if an exact match was made,
        // speeding up string keys.
        template <typename IterType>
        std::pair<IterType, int> internal_locate(
                const key_type &key, IterType iter) const;
        template <typename IterType>
        std::pair<IterType, int> internal_locate_plain_compare(
                const key_type &key, IterType iter) const;
        template <typename IterType>
        std::pair<IterType, int> internal_locate_compare_to(
                const key_type &key, IterType iter) const;

        // Internal routine which implements lower_bound().
        template <typename IterType>
        IterType internal_lower_bound(
                const key_type &key, IterType iter) const;

        // Internal routine which implements upper_bound().
        template <typename IterType>
        IterType internal_upper_bound(
                const key_type &key, IterType iter) const;

        // Internal routine which implements find_unique().
        template <typename IterType>
        IterType internal_find_unique(
                const key_type &key, IterType iter) const;

        // Internal routine which implements find_multi().
        template <typename IterType>
        IterType internal_find_multi(
                const key_type &key, IterType iter) const;

        // Deletes a node and all of its children.
        void internal_clear(node_type *node);

#ifndef BTREE_NO_IOSTREAM
        // Dumps a node and all of its children to the specified ostream.
        void internal_dump(std::ostream &os, const node_type *node, int level) const;
#endif

        // Verifies the tree structure of node.
        int internal_verify(const node_type *node,
                            const key_type *lo, const key_type *hi) const;

        node_stats internal_stats(const node_type *node) const {
            if (!node) {
                return node_stats(0, 0);
            }
            if (node->leaf()) {
                return node_stats(1, 0);
            }
            node_stats res(0, 1);
            for (int i = 0; i <= node->count(); ++i) {
                res += internal_stats(node->child(i));
            }
            return res;
        }

    private:
        empty_base_handle<internal_allocator_type, node_type*> root_;

    private:
        // A never instantiated helper function that returns big_ if we have a
        // key-compare-to functor or if R is bool and small_ otherwise.
        template <typename R>
        static typename if_<
                if_<is_key_compare_to::value,
                        std::is_same<R, int>,
                        std::is_same<R, bool> >::type::value,
                big_, small_>::type key_compare_checker(R);

        // A never instantiated helper function that returns the key comparison
        // functor.
        static key_compare key_compare_helper();
        // A never instantiated helper function that returns a const key_type &.
        static const key_type &key_type_helper();

        // Verify that key_compare returns a bool. This is similar to the way
        // is_convertible in base/type_traits.h works. Note that key_compare_checker
        // is never actually invoked. The compiler will select which
        // key_compare_checker() to instantiate and then figure out the size of the
        // return type of key_compare_checker() at compile time which we then check
        // against the sizeof of big_.
        static_assert(
                sizeof(key_compare_checker(key_compare_helper()(key_type_helper(), key_type_helper()))) ==
                sizeof(big_),
                "key_comparison_function_must_return_bool");

        // Note: We insist on kTargetValues, which is computed from
        // Params::kTargetNodeSize, must fit the base_fields::field_type.
        static_assert(kNodeValues <
                      (1 << (8 * sizeof(typename base_fields::field_type))),
                      "target_node_size_too_large");

        // Test the assumption made in setting kNodeValueSpace.
        static_assert(sizeof(base_fields) >= 2 * sizeof(void*),
                      "node_space_assumption_incorrect");
    };

////
// btree_node methods
    template <typename P> template <typename... Args>
    inline void btree_node<P>::insert_value(int i, Args&&... args) {
        assert(i <= count());
        insert_value_common(i);
        value_init_args(i, std::forward<Args>(args)...);
    }

    template <typename P>
    inline void btree_node<P>::insert_value_common(int i) {
        if (i < count()) {
            // invariant: count() > 0 because i >= 0
            value_move_construct(count(), this, count() - 1);
            for (int j = count() - 1; j > i; --j) {
                value_move(j, this, j - 1);
            }
            value_destroy(i);
        }
        set_count(count() + 1);

        if (!leaf()) {
            ++i;
            for (int j = count(); j > i; --j) {
                *mutable_child(j) = child(j - 1);
                child(j)->set_position(j);
            }
            *mutable_child(i) = NULL;
        }
    }

    template <typename P>
    inline void btree_node<P>::remove_value(int i) {
        if (!leaf()) {
            assert(child(i + 1)->count() == 0);
            for (int j = i + 1; j < count(); ++j) {
                *mutable_child(j) = child(j + 1);
                child(j)->set_position(j);
            }
            *mutable_child(count()) = NULL;
        }

        set_count(count() - 1);
        for (; i < count(); ++i) {
            value_move(i, this, i + 1);
        }
        value_destroy(i);
    }

    template <typename P>
    void btree_node<P>::rebalance_right_to_left(btree_node *src, int to_move) {
        assert(parent() == src->parent());
        assert(position() + 1 == src->position());
        assert(src->count() >= count());
        assert(to_move >= 1);
        assert(to_move <= src->count());

        // Move the delimiting value to the left node and the new delimiting value
        // from the right node.
        value_move_construct(count(), parent(), position());
        parent()->value_move(position(), src, to_move - 1);

        // Move the values from the right to the left node.
        for (int i = 1; i < to_move; ++i) {
            value_move_construct(count() + i, src, i - 1);
        }
        // Shift the values in the right node to their correct position.
        for (int i = to_move; i < src->count(); ++i) {
            src->value_move(i - to_move, src, i);
        }
        for (int i = 1; i <= to_move; ++i) {
            src->value_destroy(src->count() - i);
        }

        if (!leaf()) {
            // Move the child pointers from the right to the left node.
            for (int i = 0; i < to_move; ++i) {
                set_child(1 + count() + i, src->child(i));
            }
            for (int i = 0; i <= src->count() - to_move; ++i) {
                assert(i + to_move <= src->max_count());
                src->set_child(i, src->child(i + to_move));
                *src->mutable_child(i + to_move) = NULL;
            }
        }

        // Fixup the counts on the src and dest nodes.
        set_count(count() + to_move);
        src->set_count(src->count() - to_move);
    }

    template <typename P>
    void btree_node<P>::rebalance_left_to_right(btree_node *dest, int to_move) {
        assert(parent() == dest->parent());
        assert(position() + 1 == dest->position());
        assert(count() >= dest->count());
        assert(to_move >= 1);
        assert(to_move <= count());

        // Make room in the right node for the new values.
        for (int i = dest->count() - 1; i >= 0; --i) {
            dest->value_move_construct(i + to_move, dest, i);
            dest->value_destroy(i);
        }

        // Move the delimiting value to the right node and the new delimiting value
        // from the left node.
        dest->value_move_construct(to_move - 1, parent(), position());
        parent()->value_move(position(), this, count() - to_move);
        value_destroy(count() - to_move);

        // Move the values from the left to the right node.
        for (int i = 1; i < to_move; ++i) {
            dest->value_move_construct(i - 1, this, count() - to_move + i);
            value_destroy(count() - to_move + i);
        }

        if (!leaf()) {
            // Move the child pointers from the left to the right node.
            for (int i = dest->count(); i >= 0; --i) {
                dest->set_child(i + to_move, dest->child(i));
                *dest->mutable_child(i) = NULL;
            }
            for (int i = 1; i <= to_move; ++i) {
                dest->set_child(i - 1, child(count() - to_move + i));
                *mutable_child(count() - to_move + i) = NULL;
            }
        }

        // Fixup the counts on the src and dest nodes.
        set_count(count() - to_move);
        dest->set_count(dest->count() + to_move);
    }

    template <typename P>
    void btree_node<P>::split(btree_node *dest, int insert_position) {
        assert(dest->count() == 0);

        // We bias the split based on the position being inserted. If we're
        // inserting at the beginning of the left node then bias the split to put
        // more values on the right node. If we're inserting at the end of the
        // right node then bias the split to put more values on the left node.
        if (insert_position == 0) {
            dest->set_count(count() - 1);
        } else if (insert_position == max_count()) {
            dest->set_count(0);
        } else {
            dest->set_count(count() / 2);
        }
        set_count(count() - dest->count());
        assert(count() >= 1);

        // Move values from the left sibling to the right sibling.
        for (int i = 0; i < dest->count(); ++i) {
            dest->value_move_construct(i, this, count() + i);
            value_destroy(count() + i);
        }

        // The split key is the largest value in the left sibling.
        set_count(count() - 1);

        parent()->insert_value_common(position());
        parent()->value_move_construct(position(), this, count());
        value_destroy(count());

        parent()->set_child(position() + 1, dest);

        if (!leaf()) {
            for (int i = 0; i <= dest->count(); ++i) {
                assert(child(count() + i + 1) != NULL);
                dest->set_child(i, child(count() + i + 1));
                *mutable_child(count() + i + 1) = NULL;
            }
        }
    }

    template <typename P>
    void btree_node<P>::merge(btree_node *src) {
        assert(parent() == src->parent());
        assert(position() + 1 == src->position());

        // Move the delimiting value to the left node.
        value_move_construct(count(), parent(), position());

        // Move the values from the right to the left node.
        for (int i = 0; i < src->count(); ++i) {
            value_move_construct(1 + count() + i, src, i);
            src->value_destroy(i);
        }

        if (!leaf()) {
            // Move the child pointers from the right to the left node.
            for (int i = 0; i <= src->count(); ++i) {
                set_child(1 + count() + i, src->child(i));
                *src->mutable_child(i) = NULL;
            }
        }

        // Fixup the counts on the src and dest nodes.
        set_count(1 + count() + src->count());
        src->set_count(0);

        // Remove the value on the parent node.
        parent()->remove_value(position());
    }

    template <typename P>
    void btree_node<P>::swap(btree_node *x) {
        assert(leaf() == x->leaf());

        btree_node *y = this;
        if (x->count() > y->count()) btree_swap_helper(x, y);

        // invariant: x->count() <= y->count()

        // Swap the values.
        for (int i = 0; i < x->count(); ++i) {
            x->value_swap(i, y, i);
        }
        for (int i = x->count(); i < y->count(); ++i) {
            x->value_move_construct(i, y, i);
            y->value_destroy(i);
        }

        if (!x->leaf()) {
            // Swap the child pointers.
            int n = std::max(x->count(), y->count());
            for (int i = 0; i <= n; ++i) {
                btree_swap_helper(*x->mutable_child(i), *y->mutable_child(i));
            }
            for (int i = 0; i <= y->count(); ++i) {
                x->child(i)->fields_.parent = x;
            }
            for (int i = 0; i <= x->count(); ++i) {
                y->child(i)->fields_.parent = y;
            }
        }

        // Swap the counts.
        btree_swap_helper(y->fields_.count, x->fields_.count);
    }

////
// btree_iterator methods
    template <typename N, typename R, typename P>
    void btree_iterator<N, R, P>::increment_slow() {
        if (node->leaf()) {
            assert(position >= node->count());
            self_type save(*this);
            while (position == node->count() && !node->is_root()) {
                assert(node->parent()->child(node->position()) == node);
                position = node->position();
                node = node->parent();
            }
            if (position == node->count()) {
                *this = save;
            }
        } else {
            assert(position < node->count());
            node = node->child(position + 1);
            while (!node->leaf()) {
                node = node->child(0);
            }
            position = 0;
        }
    }

    template <typename N, typename R, typename P>
    void btree_iterator<N, R, P>::increment_by(int count) {
        while (count > 0) {
            if (node->leaf()) {
                int rest = node->count() - position;
                position += std::min(rest, count);
                count = count - rest;
                if (position < node->count()) {
                    return;
                }
            } else {
                --count;
            }
            increment_slow();
        }
    }

    template <typename N, typename R, typename P>
    void btree_iterator<N, R, P>::decrement_slow() {
        if (node->leaf()) {
            assert(position <= -1);
            self_type save(*this);
            while (position < 0 && !node->is_root()) {
                assert(node->parent()->child(node->position()) == node);
                position = node->position() - 1;
                node = node->parent();
            }
            if (position < 0) {
                *this = save;
            }
        } else {
            assert(position >= 0);
            node = node->child(position);
            while (!node->leaf()) {
                node = node->child(node->count());
            }
            position = node->count() - 1;
        }
    }

////
// btree methods
    template <typename P>
    btree<P>::btree(const key_compare &comp, const allocator_type &alloc)
            : key_compare(comp),
              root_(alloc, NULL) {
    }

    template <typename P>
    btree<P>::btree(const self_type &x)
            : key_compare(x.key_comp()),
              root_(x.internal_allocator(), NULL) {
        assign(x);
    }

    template <typename P> template <typename... Args>
    std::pair<typename btree<P>::iterator, bool>
    btree<P>::insert_unique_args(const key_type &key, Args&&... args) {
        if (empty()) {
            *mutable_root() = new_leaf_root_node(1);
        }

        std::pair<iterator, int> res = internal_locate(key, iterator(root(), 0));
        iterator &iter = res.first;
        if (res.second == kExactMatch) {
            // The key already exists in the tree, do nothing.
            return std::make_pair(internal_last(iter), false);
        } else if (!res.second) {
            iterator last = internal_last(iter);
            if (last.node && !compare_keys(key, last.key())) {
                // The key already exists in the tree, do nothing.
                return std::make_pair(last, false);
            }
        }

        return std::make_pair(internal_insert(iter, std::forward<Args>(args)...), true);
    }

    template <typename P> template <typename... Args>
    inline typename btree<P>::iterator
    btree<P>::insert_unique_hint_args(iterator position, const key_type &key, Args&&... args) {
        if (!empty()) {
            const iterator end = this->end();
            if (position == end || compare_keys(key, position.key())) {
                iterator prev = position;
                if (position == begin() || compare_keys((--prev).key(), key)) {
                    // prev.key() < key < position.key()
                    return internal_insert(position, std::forward<Args>(args)...);
                }
            } else if (compare_keys(position.key(), key)) {
                iterator next = position;
                ++next;
                if (next == end || compare_keys(key, next.key())) {
                    // position.key() < key < next.key()
                    return internal_insert(next, std::forward<Args>(args)...);
                }
            } else {
                // position.key() == key
                return position;
            }
        }
        return insert_unique_args(key, std::forward<Args>(args)...).first;
    }

    template <typename P> template <typename InputIterator>
    void btree<P>::insert_unique(InputIterator b, InputIterator e) {
        for (; b != e; ++b) {
            insert_unique(end(), *b);
        }
    }

    template <typename P> template <typename... Args>
    typename btree<P>::iterator
    btree<P>::insert_multi_args(const key_type &key, Args&&... args) {
        if (empty()) {
            *mutable_root() = new_leaf_root_node(1);
        }

        iterator iter = internal_upper_bound(key, iterator(root(), 0));
        if (!iter.node) {
            iter = end();
        }
        return internal_insert(iter, std::forward<Args>(args)...);
    }


    template <typename P> template <typename... Args>
    typename btree<P>::iterator
    btree<P>::insert_multi_hint_args(iterator position, const key_type &key, Args&&... args) {
        if (!empty()) {
            const iterator end = this->end();
            if (position == end || !compare_keys(position.key(), key)) {
                iterator prev = position;
                if (position == begin() || !compare_keys(key, (--prev).key())) {
                    // prev.key() <= key <= position.key()
                    return internal_insert(position, std::forward<Args>(args)...);
                }
            } else {
                iterator next = position;
                ++next;
                if (next == end || !compare_keys(next.key(), key)) {
                    // position.key() < key <= next.key()
                    return internal_insert(next, std::forward<Args>(args)...);
                }
            }
        }
        return insert_multi_args(key, std::forward<Args>(args)...);
    }

    template <typename P> template <typename InputIterator>
    void btree<P>::insert_multi(InputIterator b, InputIterator e) {
        for (; b != e; ++b) {
            insert_multi(end(), *b);
        }
    }

    template <typename P>
    void btree<P>::assign(const self_type &x) {
        clear();

        *mutable_key_comp() = x.key_comp();
        *mutable_internal_allocator() = x.internal_allocator();

        // Assignment can avoid key comparisons because we know the order of the
        // values is the same order we'll store them in.
        for (const_iterator iter = x.begin(), xend = x.end(); iter != xend; ++iter) {
            if (empty()) {
                insert_multi(*iter);
            } else {
                // If the btree is not empty, we can just insert the new value at the end
                // of the tree!
                internal_insert(end(), *iter);
            }
        }
    }

    template <typename P>
    typename btree<P>::iterator btree<P>::erase(iterator iter) {
        bool internal_delete = false;
        if (!iter.node->leaf()) {
            // Deletion of a value on an internal node. Swap the key with the largest
            // value of our left child. This is easy, we just decrement iter.
            iterator tmp_iter(iter--);
            assert(iter.node->leaf());
            assert(!compare_keys(tmp_iter.key(), iter.key()));
            tmp_iter.node->value_move(tmp_iter.position, iter.node, iter.position);
            internal_delete = true;
            --*mutable_size();
        } else if (!root()->leaf()) {
            --*mutable_size();
        }

        // Delete the key from the leaf.
        iter.node->remove_value(iter.position);

        // We want to return the next value after the one we just erased. If we
        // erased from an internal node (internal_delete == true), then the next
        // value is ++(++iter). If we erased from a leaf node (internal_delete ==
        // false) then the next value is ++iter. Note that ++iter may point to an
        // internal node and the value in the internal node may move to a leaf node
        // (iter.node) when rebalancing is performed at the leaf level.

        // Merge/rebalance as we walk back up the tree.
        iterator res(iter);
        for (;;) {
            if (iter.node == root()) {
                try_shrink();
                if (empty()) {
                    return end();
                }
                break;
            }
            if (iter.node->count() >= kMinNodeValues) {
                break;
            }
            bool merged = try_merge_or_rebalance(&iter);
            if (iter.node->leaf()) {
                res = iter;
            }
            if (!merged) {
                break;
            }
            iter.node = iter.node->parent();
        }

        // Adjust our return value. If we're pointing at the end of a node, advance
        // the iterator.
        if (res.position == res.node->count()) {
            res.position = res.node->count() - 1;
            ++res;
        }
        // If we erased from an internal node, advance the iterator.
        if (internal_delete) {
            ++res;
        }
        return res;
    }

    template <typename P>
    int btree<P>::erase(iterator begin, iterator end) {
        int count = distance(begin, end);
        for (int i = 0; i < count; i++) {
            begin = erase(begin);
        }
        return count;
    }

    template <typename P>
    int btree<P>::erase_unique(const key_type &key) {
        iterator iter = internal_find_unique(key, iterator(root(), 0));
        if (!iter.node) {
            // The key doesn't exist in the tree, return nothing done.
            return 0;
        }
        erase(iter);
        return 1;
    }

    template <typename P>
    int btree<P>::erase_multi(const key_type &key) {
        iterator begin = internal_lower_bound(key, iterator(root(), 0));
        if (!begin.node) {
            // The key doesn't exist in the tree, return nothing done.
            return 0;
        }
        // Delete all of the keys between begin and upper_bound(key).
        iterator end = internal_end(
                internal_upper_bound(key, iterator(root(), 0)));
        return erase(begin, end);
    }

    template <typename P>
    void btree<P>::clear() {
        if (root() != NULL) {
            internal_clear(root());
        }
        *mutable_root() = NULL;
    }

    template <typename P>
    void btree<P>::swap(self_type &x) {
        std::swap(static_cast<key_compare&>(*this), static_cast<key_compare&>(x));
        std::swap(root_, x.root_);
    }

    template <typename P>
    void btree<P>::verify() const {
        if (root() != NULL) {
            assert(size() == internal_verify(root(), NULL, NULL));
            assert(leftmost() == (++const_iterator(root(), -1)).node);
            assert(rightmost() == (--const_iterator(root(), root()->count())).node);
            assert(leftmost()->leaf());
            assert(rightmost()->leaf());
        } else {
            assert(size() == 0);
            assert(leftmost() == NULL);
            assert(rightmost() == NULL);
        }
    }

    template <typename P>
    void btree<P>::rebalance_or_split(iterator *iter) {
        node_type *&node = iter->node;
        int &insert_position = iter->position;
        assert(node->count() == node->max_count());

        // First try to make room on the node by rebalancing.
        node_type *parent = node->parent();
        if (node != root()) {
            if (node->position() > 0) {
                // Try rebalancing with our left sibling.
                node_type *left = parent->child(node->position() - 1);
                if (left->count() < left->max_count()) {
                    // We bias rebalancing based on the position being inserted. If we're
                    // inserting at the end of the right node then we bias rebalancing to
                    // fill up the left node.
                    int to_move = (left->max_count() - left->count()) /
                                  (1 + (insert_position < left->max_count()));
                    to_move = std::max(1, to_move);

                    if (((insert_position - to_move) >= 0) ||
                        ((left->count() + to_move) < left->max_count())) {
                        left->rebalance_right_to_left(node, to_move);

                        assert(node->max_count() - node->count() == to_move);
                        insert_position = insert_position - to_move;
                        if (insert_position < 0) {
                            insert_position = insert_position + left->count() + 1;
                            node = left;
                        }

                        assert(node->count() < node->max_count());
                        return;
                    }
                }
            }

            if (node->position() < parent->count()) {
                // Try rebalancing with our right sibling.
                node_type *right = parent->child(node->position() + 1);
                if (right->count() < right->max_count()) {
                    // We bias rebalancing based on the position being inserted. If we're
                    // inserting at the beginning of the left node then we bias rebalancing
                    // to fill up the right node.
                    int to_move = (right->max_count() - right->count()) /
                                  (1 + (insert_position > 0));
                    to_move = std::max(1, to_move);

                    if ((insert_position <= (node->count() - to_move)) ||
                        ((right->count() + to_move) < right->max_count())) {
                        node->rebalance_left_to_right(right, to_move);

                        if (insert_position > node->count()) {
                            insert_position = insert_position - node->count() - 1;
                            node = right;
                        }

                        assert(node->count() < node->max_count());
                        return;
                    }
                }
            }

            // Rebalancing failed, make sure there is room on the parent node for a new
            // value.
            if (parent->count() == parent->max_count()) {
                iterator parent_iter(node->parent(), node->position());
                rebalance_or_split(&parent_iter);
            }
        } else {
            // Rebalancing not possible because this is the root node.
            if (root()->leaf()) {
                // The root node is currently a leaf node: create a new root node and set
                // the current root node as the child of the new root.
                parent = new_internal_root_node();
                parent->set_child(0, root());
                *mutable_root() = parent;
                assert(*mutable_rightmost() == parent->child(0));
            } else {
                // The root node is an internal node. We do not want to create a new root
                // node because the root node is special and holds the size of the tree
                // and a pointer to the rightmost node. So we create a new internal node
                // and move all of the items on the current root into the new node.
                parent = new_internal_node(parent);
                parent->set_child(0, parent);
                parent->swap(root());
                node = parent;
            }
        }

        // Split the node.
        node_type *split_node;
        if (node->leaf()) {
            split_node = new_leaf_node(parent);
            node->split(split_node, insert_position);
            if (rightmost() == node) {
                *mutable_rightmost() = split_node;
            }
        } else {
            split_node = new_internal_node(parent);
            node->split(split_node, insert_position);
        }

        if (insert_position > node->count()) {
            insert_position = insert_position - node->count() - 1;
            node = split_node;
        }
    }

    template <typename P>
    void btree<P>::merge_nodes(node_type *left, node_type *right) {
        left->merge(right);
        if (right->leaf()) {
            if (rightmost() == right) {
                *mutable_rightmost() = left;
            }
            delete_leaf_node(right);
        } else {
            delete_internal_node(right);
        }
    }

    template <typename P>
    bool btree<P>::try_merge_or_rebalance(iterator *iter) {
        node_type *parent = iter->node->parent();
        if (iter->node->position() > 0) {
            // Try merging with our left sibling.
            node_type *left = parent->child(iter->node->position() - 1);
            if ((1 + left->count() + iter->node->count()) <= left->max_count()) {
                iter->position += 1 + left->count();
                merge_nodes(left, iter->node);
                iter->node = left;
                return true;
            }
        }
        if (iter->node->position() < parent->count()) {
            // Try merging with our right sibling.
            node_type *right = parent->child(iter->node->position() + 1);
            if ((1 + iter->node->count() + right->count()) <= right->max_count()) {
                merge_nodes(iter->node, right);
                return true;
            }
            // Try rebalancing with our right sibling. We don't perform rebalancing if
            // we deleted the first element from iter->node and the node is not
            // empty. This is a small optimization for the common pattern of deleting
            // from the front of the tree.
            if ((right->count() > kMinNodeValues) &&
                ((iter->node->count() == 0) ||
                 (iter->position > 0))) {
                int to_move = (right->count() - iter->node->count()) / 2;
                to_move = std::min(to_move, right->count() - 1);
                iter->node->rebalance_right_to_left(right, to_move);
                return false;
            }
        }
        if (iter->node->position() > 0) {
            // Try rebalancing with our left sibling. We don't perform rebalancing if
            // we deleted the last element from iter->node and the node is not
            // empty. This is a small optimization for the common pattern of deleting
            // from the back of the tree.
            node_type *left = parent->child(iter->node->position() - 1);
            if ((left->count() > kMinNodeValues) &&
                ((iter->node->count() == 0) ||
                 (iter->position < iter->node->count()))) {
                int to_move = (left->count() - iter->node->count()) / 2;
                to_move = std::min(to_move, left->count() - 1);
                left->rebalance_left_to_right(iter->node, to_move);
                iter->position += to_move;
                return false;
            }
        }
        return false;
    }

    template <typename P>
    void btree<P>::try_shrink() {
        if (root()->count() > 0) {
            return;
        }
        // Deleted the last item on the root node, shrink the height of the tree.
        if (root()->leaf()) {
            assert(size() == 0);
            delete_leaf_node(root());
            *mutable_root() = NULL;
        } else {
            node_type *child = root()->child(0);
            if (child->leaf()) {
                // The child is a leaf node so simply make it the root node in the tree.
                child->make_root();
                delete_internal_root_node();
                *mutable_root() = child;
            } else {
                // The child is an internal node. We want to keep the existing root node
                // so we move all of the values from the child node into the existing
                // (empty) root node.
                child->swap(root());
                delete_internal_node(child);
            }
        }
    }

    template <typename P> template <typename IterType>
    inline IterType btree<P>::internal_last(IterType iter) {
        while (iter.node && iter.position == iter.node->count()) {
            iter.position = iter.node->position();
            iter.node = iter.node->parent();
            if (iter.node->leaf()) {
                iter.node = NULL;
            }
        }
        return iter;
    }

    template <typename P>
    inline typename btree<P>::iterator
    btree<P>::internal_insert_common(iterator iter) {
        if (!iter.node->leaf()) {
            // We can't insert on an internal node. Instead, we'll insert after the
            // previous value which is guaranteed to be on a leaf node.
            --iter;
            ++iter.position;
        }
        if (iter.node->count() == iter.node->max_count()) {
            // Make room in the leaf for the new item.
            if (iter.node->max_count() < kNodeValues) {
                // Insertion into the root where the root is smaller that the full node
                // size. Simply grow the size of the root node.
                assert(iter.node == root());
                iter.node = new_leaf_root_node(
                        std::min<int>(kNodeValues, 2 * iter.node->max_count()));
                iter.node->swap(root());
                delete_leaf_node(root());
                *mutable_root() = iter.node;
            } else {
                rebalance_or_split(&iter);
                ++*mutable_size();
            }
        } else if (!root()->leaf()) {
            ++*mutable_size();
        }
        return iter;
    }

    template <typename P> template <typename... Args>
    inline typename btree<P>::iterator
    btree<P>::internal_insert(iterator iter, Args&&... args) {
        iter = internal_insert_common(iter);
        iter.node->insert_value(iter.position, std::forward<Args>(args)...);
        return iter;
    }

    template <typename P> template <typename IterType>
    inline std::pair<IterType, int> btree<P>::internal_locate(
            const key_type &key, IterType iter) const {
        return internal_locate_type::dispatch(key, *this, iter);
    }

    template <typename P> template <typename IterType>
    inline std::pair<IterType, int> btree<P>::internal_locate_plain_compare(
            const key_type &key, IterType iter) const {
        for (;;) {
            iter.position = iter.node->lower_bound(key, key_comp());
            if (iter.node->leaf()) {
                break;
            }
            iter.node = iter.node->child(iter.position);
        }
        return std::make_pair(iter, 0);
    }

    template <typename P> template <typename IterType>
    inline std::pair<IterType, int> btree<P>::internal_locate_compare_to(
            const key_type &key, IterType iter) const {
        for (;;) {
            int res = iter.node->lower_bound(key, key_comp());
            iter.position = res & kMatchMask;
            if (res & kExactMatch) {
                return std::make_pair(iter, static_cast<int>(kExactMatch));
            }
            if (iter.node->leaf()) {
                break;
            }
            iter.node = iter.node->child(iter.position);
        }
        return std::make_pair(iter, -kExactMatch);
    }

    template <typename P> template <typename IterType>
    IterType btree<P>::internal_lower_bound(
            const key_type &key, IterType iter) const {
        if (iter.node) {
            for (;;) {
                iter.position =
                        iter.node->lower_bound(key, key_comp()) & kMatchMask;
                if (iter.node->leaf()) {
                    break;
                }
                iter.node = iter.node->child(iter.position);
            }
            iter = internal_last(iter);
        }
        return iter;
    }

    template <typename P> template <typename IterType>
    IterType btree<P>::internal_upper_bound(
            const key_type &key, IterType iter) const {
        if (iter.node) {
            for (;;) {
                iter.position = iter.node->upper_bound(key, key_comp());
                if (iter.node->leaf()) {
                    break;
                }
                iter.node = iter.node->child(iter.position);
            }
            iter = internal_last(iter);
        }
        return iter;
    }

    template <typename P> template <typename IterType>
    IterType btree<P>::internal_find_unique(
            const key_type &key, IterType iter) const {
        if (iter.node) {
            std::pair<IterType, int> res = internal_locate(key, iter);
            if (res.second == kExactMatch) {
                return res.first;
            }
            if (!res.second) {
                iter = internal_last(res.first);
                if (iter.node && !compare_keys(key, iter.key())) {
                    return iter;
                }
            }
        }
        return IterType(NULL, 0);
    }

    template <typename P> template <typename IterType>
    IterType btree<P>::internal_find_multi(
            const key_type &key, IterType iter) const {
        if (iter.node) {
            iter = internal_lower_bound(key, iter);
            if (iter.node) {
                iter = internal_last(iter);
                if (iter.node && !compare_keys(key, iter.key())) {
                    return iter;
                }
            }
        }
        return IterType(NULL, 0);
    }

    template <typename P>
    void btree<P>::internal_clear(node_type *node) {
        if (!node->leaf()) {
            for (int i = 0; i <= node->count(); ++i) {
                internal_clear(node->child(i));
            }
            if (node == root()) {
                delete_internal_root_node();
            } else {
                delete_internal_node(node);
            }
        } else {
            delete_leaf_node(node);
        }
    }

#ifndef BTREE_NO_IOSTREAM
    template <typename P>
    void btree<P>::internal_dump(
            std::ostream &os, const node_type *node, int level) const {
        for (int i = 0; i < node->count(); ++i) {
            if (!node->leaf()) {
                internal_dump(os, node->child(i), level + 1);
            }
            for (int j = 0; j < level; ++j) {
                os << "  ";
            }
            os << node->key(i) << " [" << level << "]\n";
        }
        if (!node->leaf()) {
            internal_dump(os, node->child(node->count()), level + 1);
        }
    }
#endif

    template <typename P>
    int btree<P>::internal_verify(
            const node_type *node, const key_type *lo, const key_type *hi) const {
        assert(node->count() > 0);
        assert(node->count() <= node->max_count());
        if (lo) {
            assert(!compare_keys(node->key(0), *lo));
        }
        if (hi) {
            assert(!compare_keys(*hi, node->key(node->count() - 1)));
        }
        for (int i = 1; i < node->count(); ++i) {
            assert(!compare_keys(node->key(i), node->key(i - 1)));
        }
        int count = node->count();
        if (!node->leaf()) {
            for (int i = 0; i <= node->count(); ++i) {
                assert(node->child(i) != NULL);
                assert(node->child(i)->parent() == node);
                assert(node->child(i)->position() == i);
                count += internal_verify(
                        node->child(i),
                        (i == 0) ? lo : &node->key(i - 1),
                        (i == node->count()) ? hi : &node->key(i));
            }
        }
        return count;
    }

} // namespace btree

#endif  // UTIL_BTREE_BTREE_H__