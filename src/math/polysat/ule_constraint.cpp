/*++
Copyright (c) 2021 Microsoft Corporation

Module Name:

    polysat unsigned <= constraints

Author:

    Nikolaj Bjorner (nbjorner) 2021-03-19
    Jakob Rath 2021-04-6

Notes:

   Canonical representation of equation p == 0 is the constraint p <= 0.
   The alternatives p < 1, -1 <= q, q > -2 are eliminated.

   Rewrite rules to simplify expressions.
   In the following let k, k1, k2 be values.

   - k1 <= k2       ==>   0 <= 0 if k1 <= k2
   - k1 <= k2       ==>   1 <= 0 if k1 >  k2
   - 0 <= p         ==>   0 <= 0
   - p <= 0         ==>   1 <= 0 if p is never zero due to parity
   - p <= -1        ==>   0 <= 0
   - k <= p         ==>   p - k <= - k - 1
   - k*2^n*p <= 0   ==>   2^n*p <= 0 if k is odd, leading coeffient is always a power of 2.

   Note: the rules will rewrite alternative formulations of equations:
   - -1 <= p        ==>   p + 1 <= 0
   -  1 <= p        ==>   p - 1 <= -2

   Rewrite rules on signed constraints:

   - p > -2         ==>   p + 1 <= 0
   - p <= -2        ==>   p + 1 > 0

   At this point, all equations are in canonical form.

TODO: clause simplifications:

   - p + k <= p  ==> p + k <= k & p != 0   for k != 0
   - p*q = 0     ==> p = 0 or q = 0        applies to any factoring
   - 2*p <= 2*q  ==> (p >= 2^n-1 & q < 2^n-1) or (p >= 2^n-1 = q >= 2^n-1 & p <= q)
                 ==> (p >= 2^n-1 => q < 2^n-1 or p <= q) &
                     (p < 2^n-1 => p <= q) &
                     (p < 2^n-1 => q < 2^n-1)

   - 3*p <= 3*q ==> ?

Note:
     case p <= p + k is already covered because we test (lhs - rhs).is_val()

     It can be seen as an instance of lemma 5.2 of Supratik and John.

--*/

#include "math/polysat/constraint.h"
#include "math/polysat/solver.h"
#include "math/polysat/log.h"

namespace {
    using namespace polysat;

    // Simplify lhs <= rhs
    void simplify_impl(bool& is_positive, pdd& lhs, pdd& rhs) {
        // 0 <= p   -->   0 <= 0
        if (lhs.is_zero()) {
            rhs = 0;
            return;
        }
        // p <= -1   -->   0 <= 0
        if (rhs.is_max()) {
            lhs = 0, rhs = 0;
            return;
        }
        // p <= p   -->   0 <= 0
        if (lhs == rhs) {
            lhs = 0, rhs = 0;
            return;
        }
        // Evaluate constants
        if (lhs.is_val() && rhs.is_val()) {
            if (lhs.val() <= rhs.val())
                lhs = 0, rhs = 0;
            else
                lhs = 0, rhs = 0, is_positive = !is_positive;
            return;
        }

#if 0
        // !(k <= p) -> p <= k - 1
        
        if (!is_positive && lhs.is_val() && lhs.val() > 0) {
            pdd k = lhs;
            lhs = rhs;
            rhs = k - 1;
            is_positive = true;
        }
#endif
        // k <= p   -->   p - k <= - k - 1
        if (lhs.is_val()) {
            pdd k = lhs;
            lhs = rhs - k;
            rhs = - k - 1;
        }
        // NSB: why this?

        // p >  -2   -->   p + 1 <= 0
        // p <= -2   -->   p + 1 >  0
        if (rhs.is_val() && (rhs + 2).is_zero()) {
            lhs = lhs + 1;
            rhs = 0;
            is_positive = !is_positive;
        }
        // 2p + 1 <= 0   -->   0 < 0
        if (rhs.is_zero() && lhs.is_never_zero()) {
            lhs = 0;
            is_positive = !is_positive;
            return;
        }
        // a*p + q <= 0   -->   p + a^-1*q <= 0   for a odd
        if (rhs.is_zero() && !lhs.leading_coefficient().is_power_of_two()) {
            rational lc = lhs.leading_coefficient();
            rational x, y;
            gcd(lc, lhs.manager().two_to_N(), x, y);
            if (x.is_neg())
                x = mod(x, lhs.manager().two_to_N());
            lhs *= x;
            SASSERT(lhs.leading_coefficient().is_power_of_two());
        }
    } // simplify_impl
}


namespace polysat {

    ule_constraint::ule_constraint(pdd const& l, pdd const& r) :
        constraint(ckind_t::ule_t), m_lhs(l), m_rhs(r) {

        m_vars.append(m_lhs.free_vars());
        for (auto v : m_rhs.free_vars())
            if (!m_vars.contains(v))
                m_vars.push_back(v);
    }

    void ule_constraint::simplify(bool& is_positive, pdd& lhs, pdd& rhs) {
#ifndef NDEBUG
        bool const old_is_positive = is_positive;
        pdd const old_lhs = lhs;
        pdd const old_rhs = rhs;
#endif
        simplify_impl(is_positive, lhs, rhs);
#ifndef NDEBUG
        if (old_is_positive != is_positive || old_lhs != lhs || old_rhs != rhs) {
            ule_pp const old_ule(to_lbool(old_is_positive), old_lhs, old_rhs);
            ule_pp const new_ule(to_lbool(is_positive), lhs, rhs);
            LOG("Simplify: " << old_ule << "   -->   " << new_ule);
        }
#endif
    }

    std::ostream& ule_constraint::display(std::ostream& out, lbool status, pdd const& lhs, pdd const& rhs) {
        out << lhs;
        if (rhs.is_zero() && status == l_true) out << " == ";
        else if (rhs.is_zero() && status == l_false) out << " != ";
        else if (status == l_true) out << " <= ";
        else if (status == l_false) out << " > ";
        else out << " <=/> ";
        return out << rhs;
    }

    std::ostream& ule_constraint::display(std::ostream& out, lbool status) const {
        return display(out, status, m_lhs, m_rhs);
    }

    std::ostream& ule_constraint::display(std::ostream& out) const {
        return display(out, l_true, m_lhs, m_rhs);
    }

    void ule_constraint::narrow(solver& s, bool is_positive, bool first) {
        auto p = s.subst(lhs());
        auto q = s.subst(rhs());

        signed_constraint sc(this, is_positive);

        LOG_H3("Narrowing " << sc);
        LOG_V(10, "Assignment: " << assignments_pp(s));
        LOG_V(10, "Substituted LHS: " << lhs() << " := " << p);
        LOG_V(10, "Substituted RHS: " << rhs() << " := " << q);

        if (is_always_false(is_positive, p, q)) {
            s.set_conflict(sc);
            return;
        }
        if (p.is_val() && q.is_val()) {
            SASSERT(!is_positive || p.val() <= q.val());
            SASSERT(is_positive || p.val() > q.val());
            return;
        }

        s.m_viable.intersect(p, q, sc);

        if (first && !is_positive) {
            if (!p.is_val())
                // -1 > q
                s.add_clause(~sc, s.ult(q, -1), false);
            if (!q.is_val())
                // p > 0
                s.add_clause(~sc, s.ult(0, p), false);
        }
    }

    // Evaluate lhs <= rhs
    lbool ule_constraint::eval(pdd const& lhs, pdd const& rhs) {
        // NOTE: don't assume simplifications here because we also call this on partially substituted constraints
        if (lhs.is_zero())
            return l_true;      // 0 <= p
        if (lhs == rhs)
            return l_true;      // p <= p
        if (rhs.is_max())
            return l_true;      // p <= -1
        if (rhs.is_zero() && lhs.is_never_zero())
            return l_false;     // p <= 0 implies p == 0
        if (lhs.is_one() && rhs.is_never_zero())
            return l_true;      // 1 <= p implies p != 0
        if (lhs.is_val() && rhs.is_val())
            return to_lbool(lhs.val() <= rhs.val());
        return l_undef;
   }

    lbool ule_constraint::eval() const {
        return eval(lhs(), rhs());
    }

    lbool ule_constraint::eval(assignment const& a) const {
        return eval(a.apply_to(lhs()), a.apply_to(rhs()));
    }

    unsigned ule_constraint::hash() const {
    	return mk_mix(lhs().hash(), rhs().hash(), kind());
    }

    bool ule_constraint::operator==(constraint const& other) const {
        return other.is_ule() && lhs() == other.to_ule().lhs() && rhs() == other.to_ule().rhs();
    }

    void ule_constraint::add_to_univariate_solver(pvar v, solver& s, univariate_solver& us, unsigned dep, bool is_positive) const {
        pdd p = s.subst(lhs());
        pdd q = s.subst(rhs());
        bool p_ok = p.is_univariate_in(v);
        bool q_ok = q.is_univariate_in(v);
        if (!is_positive && !q_ok)  // add p > 0
            us.add_ugt(p.get_univariate_coefficients(), rational::zero(), false, dep);
        if (!is_positive && !p_ok)  // add -1 > q  <==>  q+1 > 0
            us.add_ugt((q + 1).get_univariate_coefficients(), rational::zero(), false, dep);
        if (p_ok && q_ok)
            us.add_ule(p.get_univariate_coefficients(), q.get_univariate_coefficients(), !is_positive, dep);
    }
}
