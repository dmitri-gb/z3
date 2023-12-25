/*++
Copyright (c) 2021 Microsoft Corporation

Module Name:

    maintain viable domains
    It uses the interval extraction functions from forbidden intervals.
    An empty viable set corresponds directly to a conflict that does not rely on
    the non-viable variable.

Author:

    Nikolaj Bjorner (nbjorner) 2021-03-19
    Jakob Rath 2021-04-06

--*/
#pragma once

#include "util/rational.h"
#include "util/dlist.h"
#include "util/map.h"
#include "util/small_object_allocator.h"

#include "sat/smt/polysat/types.h"
#include "sat/smt/polysat/forbidden_intervals.h"
#include "sat/smt/polysat/fixed_bits.h"


namespace polysat {

    enum class find_t {
        empty,
        singleton,
        multiple,
        resource_out,
    };

    struct trailing_bits {
        unsigned length;
        rational bits;
        bool positive;
        unsigned src_idx;
    };

    struct leading_bits {
        unsigned length;
        bool positive; // either all 0 or all 1
        unsigned src_idx;
    };

    struct single_bit {
        bool positive;
        unsigned position;
        unsigned src_idx;
    };


    class core;
    class constraints;

    std::ostream& operator<<(std::ostream& out, find_t x);

    class viable {
        core& c;
        constraints& cs;
        forbidden_intervals      m_forbidden_intervals;

        struct entry final : public dll_base<entry>, public fi_record {
            /// whether the entry has been created by refinement (from constraints in 'fi_record::src')
            bool refined = false;
            /// whether the entry is part of the current set of intervals, or stashed away for backtracking
            bool active = true;
            bool valid_for_lemma = true;
            pvar var = null_var;
            unsigned constraint_index = UINT_MAX;

            void reset() {
                // dll_base<entry>::init(this);  // we never did this in alloc_entry either
                fi_record::reset();
                refined = false;
                active = true;
                valid_for_lemma = true;
                var = null_var;
                constraint_index = UINT_MAX;
            }
        };

        enum class entry_kind { unit_e, equal_e, diseq_e };

        struct layer final {
            entry* entries = nullptr;
            unsigned bit_width = 0;
            layer(unsigned bw) : bit_width(bw) {}
        };

        class layers final {
	  svector<layer> m_layers;
        public:
            svector<layer> const& get_layers() const { return m_layers; }
            layer& ensure_layer(unsigned bit_width);
            layer* get_layer(unsigned bit_width);
            layer* get_layer(entry* e) { return get_layer(e->bit_width); }
            layer const* get_layer(unsigned bit_width) const;
            layer const* get_layer(entry* e) const { return get_layer(e->bit_width); }
            entry* get_entries(unsigned bit_width) const { layer const* l = get_layer(bit_width); return l ? l->entries : nullptr; }
        };


        ptr_vector<entry>       m_alloc;
        vector<layers>          m_units;        // set of viable values based on unit multipliers, layered by bit-width in descending order
        ptr_vector<entry>       m_equal_lin;    // entries that have non-unit multipliers, but are equal
        ptr_vector<entry>       m_diseq_lin;    // entries that have distinct non-zero multipliers
        ptr_vector<entry>       m_explain;      // entries that explain the current propagation or conflict
        constraint_or_dependency_list             m_core;         // forbidden interval core
        bool m_has_core = false;

        bool well_formed(entry* e);
        bool well_formed(layers const& ls);

        entry* alloc_entry(pvar v, unsigned constraint_index);

        std::ostream& display_one(std::ostream& out, pvar v, entry const* e) const;
        std::ostream& display_all(std::ostream& out, pvar v, entry const* e, char const* delimiter = "") const;
        void log();
        void log(pvar v);

        struct pop_viable_trail;
        void pop_viable(entry* e, entry_kind k);
        struct push_viable_trail;
        void push_viable(entry* e);

        void insert(entry* e, pvar v, ptr_vector<entry>& entries, entry_kind k);

        bool intersect(pvar v, entry* e);

        lbool find_viable(pvar v, rational& lo, rational& hi);

        lbool find_on_layers(
            pvar v,
            unsigned_vector const& widths,
            offset_slices const& overlaps,
            rational const& to_cover_lo,
            rational const& to_cover_hi,
            rational& out_val);

        lbool find_on_layer(
            pvar v,
            unsigned w_idx,
            unsigned_vector const& widths,
            offset_slices const& overlaps,
            rational const& to_cover_lo,
            rational const& to_cover_hi,
            rational& out_val,
            ptr_vector<entry>& refine_todo);





        void set_conflict_by_interval(pvar v, unsigned w, ptr_vector<entry>& intervals, unsigned first_interval);
        bool set_conflict_by_interval_rec(pvar v, unsigned w, entry** intervals, unsigned num_intervals, bool& create_lemma, uint_set& vars_to_explain);

        std::pair<entry*, bool> find_value(rational const& val, entry* entries) {
            throw default_exception("fine_value nyi");
        }

        fixed_bits m_fixed_bits;
        void init_fixed_bits(pvar v);

        unsigned_vector m_widths;
        offset_slices m_overlaps;
        void init_overlays(pvar v);

    public:
        viable(core& c);

        ~viable();

        /**
         * Find a next viable value for variable.
         */
        find_t find_viable(pvar v, rational& out_val);

        /*
        * Explain the current variable is not viable or signleton.
        */
        dependency_vector explain();

        /*
        * flag whether there is a forbidden interval core
        */
        bool has_core() const { return m_has_core; }

        /*
        * Retrieve lemma corresponding to forbidden interval constraints
        */
        constraint_or_dependency_list const& get_core() { SASSERT(m_has_core);  return m_core; }

        /*
        * Register constraint at index 'idx' as unitary in v.
        */
        void add_unitary(pvar v, unsigned idx);

        /*
        * Ensure data-structures tracking variable v.
        */
        void ensure_var(pvar v);

    };

}
