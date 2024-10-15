/*++
Copyright (c) 2024 Microsoft Corporation

Module Name:

    sls_datatype_plugin.cpp

Abstract:

    Algebraic Datatypes for SLS
    
Author:

    Nikolaj Bjorner (nbjorner) 2024-10-14

Notes:

Axioms:
   is-c(c(t))                for each c(t) in T
   f_i(c(t_i)) = t_i         for each c(..t_i..) in T
   is-c(t) => t = c(...acc_j(t)..) for each acc_j(t) in T

   sum_i is-c_i(t) = 1
   is-c(t) <=> c = t         for each 0-ary constructor c

   s = acc(...(acc(t)) => s != t   if t is recursive

   or_i t = t_i              if t is a finite sort with terms t_i


   s := acc(t)                  => s < t in P
   a := s = acc(t), a is a unit => s < t in P
   a := s = acc(t), a in Atoms  => (a => s < t) in P

   s << t if there is a path P with conditions L.
   L => s != t

   This disregards if acc is applied to non-matching constructor.
   In this case we rely on that the interpretation of acc can be
   forced.
   If this is incorrect, include is-c(t) assumptions in path axioms.

--*/

#include "ast/sls/sls_datatype_plugin.h"

namespace sls {
    
    datatype_plugin::datatype_plugin(context& c):
        plugin(c),
        dt(m) {
        m_fid = dt.get_family_id();
    }
    
    datatype_plugin::~datatype_plugin() {}

    void datatype_plugin::collect_path_axioms() {
        expr* t = nullptr, *z = nullptr;
        for (auto s : ctx.subterms()) 
            if (dt.is_accessor(s, t) && dt.is_recursive(t->get_sort()))
                add_edge(s, t, sat::null_literal);
        expr* x = nullptr, *y = nullptr;
        for (sat::bool_var v = 0; v < ctx.num_bool_vars(); ++v) {
            expr* e = ctx.atom(v);
            if (!e)
                continue;
            if (!m.is_eq(e, x, y))
                continue;
            sat::literal lp(v, false), ln(v, true);
            if (dt.is_accessor(x, z) && dt.is_recursive(z->get_sort())) {
                if (ctx.is_unit(lp))
                    add_edge(y, z, sat::null_literal);
                else if (ctx.is_unit(ln))
                    ;
                else
                    add_edge(y, z, lp);                    
            }
            if (dt.is_accessor(y, z) && dt.is_recursive(z->get_sort())) {
                if (ctx.is_unit(lp))
                    add_edge(x, z, sat::null_literal);
                else if (ctx.is_unit(ln))
                    ;
                else
                    add_edge(x, z, lp);                    
            }            
        }
        add_path_axioms();
    }

    void datatype_plugin::add_edge(expr* child, expr* parent, sat::literal lit) {
        m_parents.insert_if_not_there(child, svector<parent_t>()).push_back({parent, lit});
    }

    void datatype_plugin::add_path_axioms() {
        ptr_vector<expr> path;
        sat::literal_vector lits;
        for (auto [child, parents] : m_parents) {
            path.reset();
            lits.reset();
            path.push_back(child);
            add_path_axioms(path, lits, parents);
        }
    }

    void datatype_plugin::add_path_axioms(ptr_vector<expr>& children, sat::literal_vector& lits, svector<parent_t> const& parents) {
        for (auto const& [parent, lit] : parents) {
            if (lit != sat::null_literal)                    
                lits.push_back(~lit);
            if (children.contains(parent)) {
                ctx.add_clause(lits);
                if (lit != sat::null_literal)                    
                    lits.pop_back();
                continue;
            }
            if (children[0]->get_sort() == parent->get_sort()) {
                lits.push_back(~ctx.mk_literal(m.mk_eq(children[0], parent)));
                ctx.add_clause(lits);
                lits.pop_back();
            }
            auto child = children.back();
            if (m_parents.contains(child)) {
                children.push_back(parent);
                auto& parents2 = m_parents[child];
                add_path_axioms(children, lits, parents2);
                children.pop_back();                
            }
            if (lit != sat::null_literal)                    
                lits.pop_back();
        }
    }

    void datatype_plugin::add_axioms() {
        expr_ref_vector axioms(m);
        expr* u = nullptr;
        for (auto t : ctx.subterms()) {
            auto s = t->get_sort();
            if (dt.is_datatype(s)) 
                m_dts.insert_if_not_there(s, ptr_vector<expr>()).push_back(t);

            if (!is_app(t))
                continue;
            auto ta = to_app(t);
            auto f = ta->get_decl();
            
            if (dt.is_constructor(t)) {
                auto r = dt.get_constructor_recognizer(f);
                axioms.push_back(m.mk_app(r, t));
                auto& acc = *dt.get_constructor_accessors(f);
                for (unsigned i = 0; i < ta->get_num_args(); ++i) {
                    auto ti = ta->get_arg(i);
                    axioms.push_back(m.mk_eq(ti, m.mk_app(acc[i], t)));
                }
                auto& cns = *dt.get_datatype_constructors(s);
                for (auto c : cns) {
                    if (c != f) {
                        auto r2 = dt.get_constructor_recognizer(c);
                        axioms.push_back(m.mk_not(m.mk_app(r2, t)));
                    }
                }
                continue;
            }
            if (dt.is_accessor(t, u) && !dt.is_constructor(u)) {
                auto c = dt.get_accessor_constructor(f);
                auto r = dt.get_constructor_recognizer(c);
                auto& acc = *dt.get_constructor_accessors(f);
                expr_ref_vector args(m);
                for (auto a : acc)
                    args.push_back(m.mk_app(a, u));
                axioms.push_back(m.mk_implies(m.mk_app(r, u), m.mk_eq(u, m.mk_app(c, args))));                
            }

            if (dt.is_datatype(s)) {
                auto& cns = *dt.get_datatype_constructors(s);
                expr_ref_vector ors(m);
                for (auto c : cns) {
                    auto r = dt.get_constructor_recognizer(c);
                    ors.push_back(m.mk_app(r, t));
                }
                axioms.push_back(m.mk_or(ors));
                for (unsigned i = 0; i < cns.size(); ++i) {
                    auto r1 = dt.get_constructor_recognizer(cns[i]);
                    for (unsigned j = i + 1; j < cns.size(); ++j) {
                        auto r2 = dt.get_constructor_recognizer(cns[j]);
                        axioms.push_back(m.mk_or(m.mk_not(m.mk_app(r1, t)), m.mk_not(m.mk_app(r2, t))));
                    }
                }
                for (auto c : cns) {
                    if (c->get_arity() == 0) {
                        auto r = dt.get_constructor_recognizer(c);
                        axioms.push_back(m.mk_iff(m.mk_app(r, t), m.mk_eq(t, m.mk_const(c))));
                    }
                }

                //
                // sort_size sz = dt.et_datatype_size(s);
                // if (sz.is_finite()) {
                //     - sz.size()
                //     - enumerate instances and set t to be one of instances?
                // }
                // 
            }
        }
        collect_path_axioms();
    }

    expr_ref datatype_plugin::get_value(expr* e) { return expr_ref(m); }
    void datatype_plugin::initialize() {}
    void datatype_plugin::start_propagation() {}
    void datatype_plugin::propagate_literal(sat::literal lit) {}
    bool datatype_plugin::propagate() { return false; }       
    bool datatype_plugin::is_sat() { return true; }
    void datatype_plugin::register_term(expr* e) {}
    std::ostream& datatype_plugin::display(std::ostream& out) const {
        return out;
    }
    void datatype_plugin::mk_model(model& mdl) {}
        
    void datatype_plugin::collect_statistics(statistics& st) const {}
    void datatype_plugin::reset_statistics() {}
    
}
