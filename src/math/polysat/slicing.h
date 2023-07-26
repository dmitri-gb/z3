/*++
Copyright (c) 2023 Microsoft Corporation

Module Name:

    polysat slicing (relating variables of different bit-widths by extraction)

Author:

    Jakob Rath 2023-06-01

Notation:

    Let x be a bit-vector of width w.
    Let l, h indices such that 0 <= l <= h < w.
    Then x[h:l] extracts h - l + 1 bits of x.
    Shorthands:
    - x[h:] stands for x[h:0], and
    - x[:l] stands for x[w-1:l].

    Example:
        0001[0:] = 1
        0001[2:0] = 001

--*/
#pragma once
#include "ast/euf/euf_egraph.h"
#include "ast/bv_decl_plugin.h"
#include "math/polysat/types.h"
#include "math/polysat/constraint.h"
#include <variant>

namespace polysat {

    class solver;

    class slicing final {

        friend class test_slicing;

        using enode = euf::enode;
        using enode_vector = euf::enode_vector;

        class dep_t {
            std::variant<std::monostate, sat::literal, unsigned> m_data;
        public:
            dep_t() { SASSERT(is_null()); }
            dep_t(sat::literal l): m_data(l) { SASSERT(l != sat::null_literal); SASSERT_EQ(l, lit()); }
            explicit dep_t(unsigned vi): m_data(vi) { SASSERT_EQ(vi, var_idx()); }
            bool is_null() const { return std::holds_alternative<std::monostate>(m_data); }
            bool is_lit()  const { return std::holds_alternative<sat::literal>(m_data); }
            bool is_var_idx()  const { return std::holds_alternative<unsigned>(m_data); }
            sat::literal lit() const { SASSERT(is_lit()); return *std::get_if<sat::literal>(&m_data); }
            unsigned var_idx() const { SASSERT(is_var_idx()); return *std::get_if<unsigned>(&m_data); }
            bool operator==(dep_t other) const { return m_data == other.m_data; }
            bool operator!=(dep_t other) const { return !operator==(other); }
            unsigned to_uint() const;
            static dep_t from_uint(unsigned x);
        };

        using dep_vector = svector<dep_t>;

        std::ostream& display(std::ostream& out, dep_t d);

        dep_t mk_var_dep(pvar v, enode* s);

        pvar_vector         m_dep_var;
        ptr_vector<enode>   m_dep_slice;
        unsigned_vector     m_dep_size_trail;

        pvar get_dep_var(dep_t d) const { return m_dep_var[d.var_idx()]; }
        enode* get_dep_slice(dep_t d) const { return m_dep_slice[d.var_idx()]; }

        static constexpr unsigned null_cut = std::numeric_limits<unsigned>::max();

        // We use the following kinds of enodes:
        // - proper slices (of variables)
        // - value slices
        // - virtual concat(...) expressions
        // - equalities between enodes (to track disequalities; currently not represented in slice_info)
        struct slice_info {
            // Cut point: if not null_cut, the slice s has been subdivided into s[|s|-1:cut+1] and s[cut:0].
            // The cut point is relative to the parent slice (rather than a root variable, which might not be unique)
            unsigned    cut     = null_cut;     // cut point, or null_cut if no subslices
            pvar        var     = null_var;     // slice is equivalent to this variable, if any (without dependencies)
            enode*      parent  = nullptr;      // parent slice, only for proper slices (if not null: s == sub_hi(parent(s)) || s == sub_lo(parent(s)))
            enode*      slice   = nullptr;      // if enode corresponds to a concat(...) expression, this field links to the represented slice.
            enode*      sub_hi  = nullptr;      // upper subslice s[|s|-1:cut+1]
            enode*      sub_lo  = nullptr;      // lower subslice s[cut:0]

            void reset() { *this = slice_info(); }
            bool has_sub() const { return !!sub_hi; }
            void set_cut(unsigned cut, enode* sub_hi, enode* sub_lo) { this->cut = cut; this->sub_hi = sub_hi; this->sub_lo = sub_lo; }
        };
        using slice_info_vector = svector<slice_info>;

        // Return true iff n is either a proper slice or a value slice
        bool is_slice(enode* n) const;

        bool is_proper_slice(enode* n) const { return !is_value(n) && is_slice(n); }
        bool is_value(enode* n) const { return n->interpreted(); }
        bool is_concat(enode* n) const;
        bool is_equality(enode* n) const { return n->is_equality(); }

        solver&                 m_solver;

        ast_manager             m_ast;
        scoped_ptr<bv_util>     m_bv;

        euf::egraph             m_egraph;
        slice_info_vector       m_info;         // indexed by enode::get_id()
        enode_vector            m_var2slice;    // pvar -> slice
        tracked_uint_set        m_needs_congruence;     // set of pvars that need updated concat(...) expressions
        enode*                  m_disequality_conflict = nullptr;

        // Add an equation v = concat(s1, ..., sn)
        // for each variable v with base slices s1, ..., sn
        void update_var_congruences();
        void add_congruence(pvar v);
        void add_congruence_if_needed(pvar v);

        func_decl* mk_concat_decl(ptr_vector<expr> const& args);
        enode* mk_concat_node(enode_vector const& slices);
        // Add s = concat(s1, ..., sn)
        void add_concat_node(enode* s, enode* concat);

        static void* encode_dep(dep_t d);
        static dep_t decode_dep(void* d);

        slice_info& info(euf::enode* n);
        slice_info const& info(euf::enode* n) const;

        enode* alloc_enode(expr* e, unsigned num_args, enode* const* args, pvar var);
        enode* find_or_alloc_enode(expr* e, unsigned num_args, enode* const* args, pvar var);
        enode* alloc_slice(unsigned width, pvar var = null_var);
        enode* find_or_alloc_disequality(enode* x, enode* y, sat::literal lit);

        // Find hi, lo such that s = a[hi:lo]
        bool find_range_in_ancestor(enode* s, enode* a, unsigned& out_hi, unsigned& out_lo);

        enode* var2slice(pvar v) const { return m_var2slice[v]; }
        pvar slice2var(enode* s) const { return info(s).var; }

        unsigned width(enode* s) const;

        enode* parent(enode* s) const { return info(s).parent; }

        bool has_sub(enode* s) const { return info(s).has_sub(); }

        /// Upper subslice (direct child, not necessarily the representative)
        enode* sub_hi(enode* s) const { return info(s).sub_hi; }

        /// Lower subslice (direct child, not necessarily the representative)
        enode* sub_lo(enode* s) const { return info(s).sub_lo; }

        // Retrieve (or create) a slice representing the given value.
        enode* mk_value_slice(rational const& val, unsigned bit_width);

        rational get_value(enode* s) const;
        bool try_get_value(enode* s, rational& val) const;

        /// Split slice s into s[|s|-1:cut+1] and s[cut:0]
        void split(enode* s, unsigned cut);
        void split_core(enode* s, unsigned cut);

        template <bool should_get_root>
        void get_base_core(enode* src, enode_vector& out_base) const;

        /// Retrieve base slices s_1,...,s_n such that src == s_1 ++ ... ++ s_n (actual descendant subslices)
        void get_base(enode* src, enode_vector& out_base) const;
        /// Retrieve base slices s_1,...,s_n such that src == s_1 ++ ... ++ s_n (representatives of subslices)
        void get_root_base(enode* src, enode_vector& out_base) const;

        /// Retrieve (or create) base slices s_1,...,s_n such that src[hi:lo] == s_1 ++ ... ++ s_n.
        /// If output_full_src is true, return the new base for src, i.e., src == s_1 ++ ... ++ s_n.
        /// If output_base is false, return coarsest intermediate slices instead of only base slices.
        void mk_slice(enode* src, unsigned hi, unsigned lo, enode_vector& out, bool output_full_src = false, bool output_base = true);

        // Extract reason why slices x and y are in the same equivalence class
        void explain_class(enode* x, enode* y, ptr_vector<void>& out_deps);

        // Extract reason why slices x and y are equal
        // (i.e., x and y have the same base, but are not necessarily in the same equivalence class)
        void explain_equal(enode* x, enode* y, ptr_vector<void>& out_deps);

        /** Extract reason for conflict */
        void explain(ptr_vector<void>& out_deps);

        /** Extract reason for x == y */
        void explain_equal(pvar x, pvar y, ptr_vector<void>& out_deps);

        void egraph_on_merge(enode* root, enode* other);
        void egraph_on_propagate(enode* lit, enode* ante);

        // Merge equivalence classes of two base slices.
        // Returns true if merge succeeded without conflict.
        [[nodiscard]] bool merge_base(enode* s1, enode* s2, dep_t dep);

        // Merge equality x_1 ++ ... ++ x_n == y_1 ++ ... ++ y_k
        //
        // Precondition:
        // - sequence of slices with equal total width
        // - ordered from msb to lsb
        //
        // The argument vectors will be cleared.
        //
        // Returns true if merge succeeded without conflict.
        [[nodiscard]] bool merge(enode_vector& xs, enode_vector& ys, dep_t dep);
        [[nodiscard]] bool merge(enode_vector& xs, enode* y, dep_t dep);
        [[nodiscard]] bool merge(enode* x, enode* y, dep_t dep);

        // Check whether two slices are known to be equal
        bool is_equal(enode* x, enode* y);

        // deduplication of extract terms
        struct extract_args {
            pvar src;
            unsigned hi;
            unsigned lo;
            bool operator==(extract_args const& other) const { return src == other.src && hi == other.hi && lo == other.lo; }
            unsigned hash() const { return mk_mix(src, hi, lo); }
        };
        using extract_args_eq = default_eq<extract_args>;
        using extract_args_hash = obj_hash<extract_args>;
        using extract_map = map<extract_args, pvar, extract_args_hash, extract_args_eq>;
        extract_map m_extract_dedup;

        enum class trail_item : std::uint8_t {
            add_var,
            split_core,
            mk_extract,
            mk_concat,
        };
        svector<trail_item> m_trail;
        enode_vector        m_split_trail;
        svector<extract_args> m_extract_trail;
        unsigned_vector     m_scopes;

        struct concat_info {
            pvar v;
            unsigned num_args;
            unsigned args_idx;
            unsigned next_args_idx() const { return args_idx + num_args; }
        };
        svector<concat_info> m_concat_trail;
        svector<pvar> m_concat_args;

        void undo_add_var();
        void undo_split_core();
        void undo_mk_extract();

        mutable enode_vector m_tmp1;
        mutable enode_vector m_tmp2;
        mutable enode_vector m_tmp3;
        ptr_vector<void>     m_tmp_deps;
        sat::literal_set     m_marked_lits;

        /** Get variable representing src[hi:lo] */
        pvar mk_extract(enode* src, unsigned hi, unsigned lo, pvar replay_var = null_var);
        /** Restore r = src[hi:lo] */
        void replay_extract(extract_args const& args, pvar r);

        pvar mk_concat(unsigned num_args, pvar const* args, pvar replay_var);
        void replay_concat(unsigned num_args, pvar const* args, pvar r);

        bool add_equation(pvar x, pdd const& body, sat::literal lit);

        bool invariant() const;
        bool invariant_needs_congruence() const;

        std::ostream& display(std::ostream& out, enode* s) const;
        std::ostream& display_tree(std::ostream& out, enode* s, unsigned indent, unsigned hi, unsigned lo) const;

    public:
        slicing(solver& s);

        void push_scope();
        void pop_scope(unsigned num_scopes = 1);

        void add_var(unsigned bit_width);

        /** Get or create variable representing x[hi:lo] */
        pvar mk_extract(pvar x, unsigned hi, unsigned lo);

        /** Get or create variable representing x1 ++ x2 ++ ... ++ xn */
        pvar mk_concat(unsigned num_args, pvar const* args) { return mk_concat(num_args, args, null_var); }
        pvar mk_concat(std::initializer_list<pvar> args);

        // Track value assignments to variables (and propagate to subslices)
        // (could generalize to fixed bits, then we need a way to merge interpreted enodes)
        void add_value(pvar v, rational const& value);
        void add_value(pvar v, unsigned value) { add_value(v, rational(value)); }
        void add_value(pvar v, int value) { add_value(v, rational(value)); }
        void add_constraint(signed_constraint c);

        bool can_propagate() const;

        // update congruences, egraph
        void propagate();

        bool is_conflict() const { return m_disequality_conflict || m_egraph.inconsistent(); }

        /** Extract conflict clause */
        clause_ref build_conflict_clause();

        /** For a given variable v, find the set of variables w such that w = v[|w|:0]. */
        void collect_simple_overlaps(pvar v, pvar_vector& out);

        /** Collect fixed portions of the variable v */
        void collect_fixed(pvar v, rational& mask, rational& value);

        std::ostream& display(std::ostream& out) const;
        std::ostream& display_tree(std::ostream& out) const;
    };

    inline std::ostream& operator<<(std::ostream& out, slicing const& s) { return s.display(out); }

}