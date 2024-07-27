/*++
Copyright (c) 2020 Microsoft Corporation

Module Name:

    sls_arith_base.h

Abstract:

    Theory plugin for arithmetic local search

Author:

    Nikolaj Bjorner (nbjorner) 2020-09-08

--*/
#pragma once

#include "util/obj_pair_set.h"
#include "util/checked_int64.h"
#include "util/optional.h"
#include "ast/ast_trail.h"
#include "ast/arith_decl_plugin.h"
#include "ast/sls/sls_context.h"

namespace sls {

    using theory_var = int;

    // local search portion for arithmetic
    template<typename num_t>
    class arith_base : public plugin {
        enum class ineq_kind { EQ, LE, LT};
        enum class var_sort { INT, REAL };
        struct bound { bool is_strict = false; num_t value; };
        typedef unsigned var_t;
        typedef unsigned atom_t;

        struct config {
            double cb = 0.0;
            unsigned L = 20;
            unsigned t = 45;
            unsigned max_no_improve = 500000;
            double sp = 0.0003;
        };

        struct stats {
            unsigned m_num_flips = 0;
        };

    public:
        struct linear_term {
            vector<std::pair<num_t, var_t>> m_args;
            num_t      m_coeff{ 0 };
        };
        // encode args <= bound, args = bound, args < bound
        struct ineq : public linear_term {            
            ineq_kind  m_op = ineq_kind::LE;            
            num_t      m_args_value;
            unsigned   m_var_to_flip = UINT_MAX;

            bool is_true() const;
            std::ostream& display(std::ostream& out) const;
        };
    private:

        struct var_info {
            var_info(expr* e, var_sort k): m_expr(e), m_sort(k) {}
            expr*        m_expr;
            num_t        m_value{ 0 };
            num_t        m_best_value{ 0 };
            var_sort     m_sort;
            arith_op_kind m_op = arith_op_kind::LAST_ARITH_OP;
            unsigned     m_def_idx = UINT_MAX;
            vector<std::pair<num_t, sat::bool_var>> m_bool_vars;
            unsigned_vector m_muls;
            unsigned_vector m_adds;
            optional<bound> m_lo, m_hi;
        };

        struct mul_def {
            unsigned        m_var;
            num_t           m_coeff;
            unsigned_vector m_monomial;
        };

        struct add_def : public linear_term {
            unsigned        m_var;
        };

        struct op_def {
            unsigned m_var = UINT_MAX;
            arith_op_kind m_op = LAST_ARITH_OP;
            unsigned m_arg1, m_arg2;
        };
       
        stats                        m_stats;
        config                       m_config;
        scoped_ptr_vector<ineq>      m_bool_vars;
        vector<var_info>             m_vars;
        vector<mul_def>              m_muls;
        vector<add_def>              m_adds;
        vector<op_def>               m_ops;
        unsigned_vector              m_expr2var;
        svector<double>              m_probs;
        bool                         m_dscore_mode = false;
        arith_util                   a;

        void invariant();
        void invariant(ineq const& i);

        unsigned get_num_vars() const { return m_vars.size(); }

        bool repair_mul1(mul_def const& md);
        bool repair_square(mul_def const& md);
        bool repair_mul(mul_def const& md);
        bool repair_add(add_def const& ad);
        bool repair_mod(op_def const& od);
        bool repair_idiv(op_def const& od);
        bool repair_div(op_def const& od);
        bool repair_rem(op_def const& od);
        bool repair_power(op_def const& od);
        bool repair_abs(op_def const& od);
        bool repair_to_int(op_def const& od);
        bool repair_to_real(op_def const& od);
        void repair(sat::literal lit, ineq const& ineq);
        bool repair_eq(sat::literal lit, ineq const& ineq);
        bool in_bounds(var_t v, num_t const& value);
        bool is_fixed(var_t v);

        vector<num_t> m_factors;
        vector<num_t> const& factor(num_t n);
        num_t sqrt(num_t n);

        double reward(sat::literal lit);

        bool sign(sat::bool_var v) const { return !ctx.is_true(sat::literal(v, false)); }
        ineq* atom(sat::bool_var bv) const { return m_bool_vars.get(bv, nullptr); }        
        num_t dtt(bool sign, ineq const& ineq) const { return dtt(sign, ineq.m_args_value, ineq); }
        num_t dtt(bool sign, num_t const& args_value, ineq const& ineq) const;
        num_t dtt(bool sign, ineq const& ineq, var_t v, num_t const& new_value) const;
        num_t dtt(bool sign, ineq const& ineq, num_t const& coeff, num_t const& old_value, num_t const& new_value) const;
        num_t dts(unsigned cl, var_t v, num_t const& new_value) const;
        num_t compute_dts(unsigned cl) const;
        bool cm(ineq const& ineq, var_t v, num_t& new_value);
        bool cm(ineq const& ineq, var_t v, num_t const& coeff, num_t& new_value);
        int cm_score(var_t v, num_t const& new_value);
        bool update(var_t v, num_t const& new_value);
        double dscore_reward(sat::bool_var v);
        double dtt_reward(sat::literal lit);
        double dscore(var_t v, num_t const& new_value) const;
        void save_best_values();
        bool solve_eq_pairs(ineq const& ineq);
        bool solve_eq_pairs(num_t const& a, var_t x, num_t const& b, var_t y, num_t const& r);

        var_t mk_var(expr* e);
        var_t mk_term(expr* e);
        var_t mk_op(arith_op_kind k, expr* e, expr* x, expr* y);
        void add_arg(linear_term& term, num_t const& c, var_t v);
        void add_args(linear_term& term, expr* e, num_t const& sign);
        ineq& new_ineq(ineq_kind op, num_t const& bound);
        void init_ineq(sat::bool_var bv, ineq& i);
        num_t divide(var_t v, num_t const& delta, num_t const& coeff);
        
        void init_bool_var_assignment(sat::bool_var v);

        bool is_int(var_t v) const { return m_vars[v].m_sort == var_sort::INT; }

        num_t value(var_t v) const { return m_vars[v].m_value; }
        bool is_num(expr* e, num_t& i);
        expr_ref from_num(sort* s, num_t const& n);
        void check_ineqs();
        void init_bool_var(sat::bool_var bv);
        void initialize(sat::literal lit);
        void add_le(var_t v, num_t const& n);
        void add_ge(var_t v, num_t const& n);
        void add_lt(var_t v, num_t const& n);
        void add_gt(var_t v, num_t const& n);
        std::ostream& display(std::ostream& out, var_t v) const;
    public:
        arith_base(context& ctx);
        ~arith_base() override {}        
        void register_term(expr* e) override;
        void set_value(expr* e, expr* v) override;
        expr_ref get_value(expr* e) override;
        void initialize() override;
        void propagate_literal(sat::literal lit) override;
        bool propagate() override;
        void repair_up(app* e) override;
        bool repair_down(app* e) override;
        void repair_literal(sat::literal lit) override;
        bool is_sat() override;
        void on_rescale() override;
        void on_restart() override;
        std::ostream& display(std::ostream& out) const override;
        void mk_model(model& mdl) override;
    };


    inline std::ostream& operator<<(std::ostream& out, typename arith_base<checked_int64<true>>::ineq const& ineq) {
        return ineq.display(out);
    }

    inline std::ostream& operator<<(std::ostream& out, typename arith_base<rational>::ineq const& ineq) {
        return ineq.display(out);
    }
}
