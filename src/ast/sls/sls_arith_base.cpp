/*++
Copyright (c) 2023 Microsoft Corporation

Module Name:

    sls_arith_base.cpp

Abstract:

    Local search dispatch for arithmetic

Author:

    Nikolaj Bjorner (nbjorner) 2023-02-07

--*/

#include "ast/sls/sls_arith_base.h"
#include "ast/ast_ll_pp.h"

namespace sls {

    template<typename num_t>
    bool arith_base<num_t>::ineq::is_true() const {
        switch (m_op) {
        case ineq_kind::LE:
            return m_args_value + this->m_coeff <= 0;
        case ineq_kind::EQ:
            return m_args_value + this->m_coeff == 0;
        default:
            return m_args_value + this->m_coeff < 0;
        }
    }



    template<typename num_t>
    std::ostream& arith_base<num_t>::ineq::display(std::ostream& out) const {
        bool first = true;
        for (auto const& [c, v] : this->m_args)
            out << (first ? "" : " + ") << c << " * v" << v, first = false;
        if (this->m_coeff != 0)
            out << " + " << this->m_coeff;
        switch (m_op) {
        case ineq_kind::LE:
            return out << " <= " << 0 << "(" << m_args_value + this->m_coeff << ")";
        case ineq_kind::EQ:
            return out << " == " << 0 << "(" << m_args_value + this->m_coeff << ")";
        default:
            return out << " < " << 0 << "(" << m_args_value + this->m_coeff << ")";
        }
    }    

    template<typename num_t>
    arith_base<num_t>::arith_base(context& ctx) :
        plugin(ctx),
        a(m) {
        m_fid = a.get_family_id();
    }

    template<typename num_t>
    void arith_base<num_t>::save_best_values() {
        for (auto& v : m_vars)
            v.m_best_value = v.m_value;
        check_ineqs();
    }

    // distance to true
    template<typename num_t>
    num_t arith_base<num_t>::dtt(bool sign, num_t const& args, ineq const& ineq) const {
        num_t zero{ 0 };
        switch (ineq.m_op) {
        case ineq_kind::LE:
            if (sign) {
                if (args + ineq.m_coeff <= 0)
                    return -ineq.m_coeff - args + 1;
                return zero;
            }
            if (args + ineq.m_coeff <= 0)
                return zero;
            return args + ineq.m_coeff;
        case ineq_kind::EQ:
            if (sign) {
                if (args + ineq.m_coeff == 0)
                    return num_t(1);
                return zero;
            }
            if (args + ineq.m_coeff == 0)
                return zero;
            return num_t(1);
        case ineq_kind::LT:
            if (sign) {
                if (args + ineq.m_coeff < 0)
                    return -ineq.m_coeff - args;
                return zero;
            }
            if (args + ineq.m_coeff < 0)
                return zero;
            return args + ineq.m_coeff + 1;
        default:
            UNREACHABLE();
            return zero;
        }
    }

    //
    // dtt is high overhead. It walks ineq.m_args
    // m_vars[w].m_value can be computed outside and shared among calls
    // different data-structures for storing coefficients
    // 
    template<typename num_t>
    num_t arith_base<num_t>::dtt(bool sign, ineq const& ineq, var_t v, num_t const& new_value) const {
        for (auto const& [coeff, w] : ineq.m_args)
            if (w == v)
                return dtt(sign, ineq.m_args_value + coeff * (new_value - m_vars[v].m_value), ineq);
        return num_t(1);
    }

    template<typename num_t>
    num_t arith_base<num_t>::dtt(bool sign, ineq const& ineq, num_t const& coeff, num_t const& old_value, num_t const& new_value) const {
        return dtt(sign, ineq.m_args_value + coeff * (new_value - old_value), ineq);
    }

    template<typename num_t>
    bool arith_base<num_t>::cm(ineq const& ineq, var_t v, num_t& new_value) {
        for (auto const& [coeff, w] : ineq.m_args)
            if (w == v)
                return cm(ineq, v, coeff, new_value);
        return false;
    }

    template<typename num_t>
    num_t arith_base<num_t>::divide(var_t v, num_t const& delta, num_t const& coeff) {
        if (is_int(v))
            return div(delta + abs(coeff) - 1, coeff);
        else
            return delta / coeff;        
    }

    template<typename num_t>
    bool arith_base<num_t>::cm(ineq const& ineq, var_t v, num_t const& coeff, num_t& new_value) {
        auto bound = -ineq.m_coeff;
        auto argsv = ineq.m_args_value;
        bool solved = false;
        num_t delta = argsv - bound;
        auto const& lo = m_vars[v].m_lo;
        auto const& hi = m_vars[v].m_hi;

        if (is_fixed(v))
            return false;

        auto well_formed = [&]() {
            num_t new_args = argsv + coeff * (new_value - value(v));
            if (ineq.is_true()) {
                switch (ineq.m_op) {
                case ineq_kind::LE: return new_args > bound;
                case ineq_kind::LT: return new_args >= bound;
                case ineq_kind::EQ: return new_args != bound;
                }
            }
            else {
                switch (ineq.m_op) {
                case ineq_kind::LE: return new_args <= bound;
                case ineq_kind::LT: return new_args < bound;
                case ineq_kind::EQ: return new_args == bound;
                }
            }
            return false;
        };

        auto move_to_bounds = [&]() {
            VERIFY(well_formed());
            if (!in_bounds(v, value(v)))
                return true;
            if (in_bounds(v, new_value))
                return true;
            if (lo && lo->value > new_value) {
                new_value = lo->value;
                if (!well_formed())
                    new_value += 1;
            }
            if (hi && hi->value < new_value) {
                new_value = hi->value;
                if (!well_formed())
                    new_value -= 1;
            }
            return well_formed() && in_bounds(v, new_value);
        };

        if (ineq.is_true()) {
            switch (ineq.m_op) {
            case ineq_kind::LE:
                // args <= bound -> args > bound
                SASSERT(argsv <= bound);
                SASSERT(delta <= 0);
                delta -= 1;
                new_value = value(v) + divide(v, abs(delta - ctx.rand(3)), coeff);
                return move_to_bounds();  
            case ineq_kind::LT:
                // args < bound -> args >= bound
                SASSERT(argsv <= bound);
                SASSERT(delta <= 0);
                delta = abs(delta);
                new_value = value(v) + divide(v, delta + ctx.rand(3), coeff);
                VERIFY(argsv + coeff * (new_value - value(v)) >= bound);
                return move_to_bounds();
            case ineq_kind::EQ: {
                delta = abs(delta) + 1 + ctx.rand(10);
                int sign = ctx.rand(2) == 0 ? 1 : -1;
                new_value = value(v) + sign * divide(v, abs(delta), coeff);
                return move_to_bounds();             
            }
            default:
                UNREACHABLE();
                break;
            }
        }
        else {
            switch (ineq.m_op) {
            case ineq_kind::LE:
                SASSERT(argsv > bound);
                SASSERT(delta > 0);
                delta += ctx.rand(10);
                new_value = value(v) - divide(v, delta + ctx.rand(3), coeff);
                return move_to_bounds();
            case ineq_kind::LT:
                SASSERT(argsv >= bound);
                SASSERT(delta >= 0);
                delta += 1 + ctx.rand(10);
                new_value = value(v) - divide(v, delta + ctx.rand(3), coeff);
                return move_to_bounds();
            case ineq_kind::EQ:
                SASSERT(delta != 0);
                if (delta < 0)
                    new_value = value(v) + divide(v, abs(delta), coeff);
                else
                    new_value = value(v) - divide(v, delta, coeff);
                solved = argsv + coeff * (new_value - value(v)) == bound;
                return solved && move_to_bounds();
            default:
                UNREACHABLE();
                break;
            }
        }
        return false;
    }

    template<typename num_t>
    bool arith_base<num_t>::solve_eq_pairs(ineq const& ineq) {
        SASSERT(ineq.m_op == ineq_kind::EQ);
        auto v = ineq.m_var_to_flip;
        if (is_fixed(v))
            return false;
        auto bound = -ineq.m_coeff;
        auto argsv = ineq.m_args_value;
        num_t a;
        for (auto const& [c, w] : ineq.m_args)
            if (v == w) {
                a = c;
                argsv -= value(v) * c;
            }
        if (abs(a) == 1)
            return false;
        verbose_stream() << "solve_eq_pairs " << ineq << " for v" << v << "\n";
        unsigned start = ctx.rand();
        for (unsigned i = 0; i < ineq.m_args.size(); ++i) {
            unsigned j = (start + i) % ineq.m_args.size();
            auto const& [b, w] = ineq.m_args[j];
            if (w == v)
                continue;
            if (b == 1 || b == -1)
                continue;
            argsv -= value(w) * b;
            if (solve_eq_pairs(a, v, b, w, bound - argsv))
                return true;
            argsv += value(w) * b;
        }
        return false;
    }

    // ax0 + by0 = r
    // (x, y) = (x0 - k*b/g, y0 + k*a/g)
    // find the min x1 >= x0 satisfying progression and where x1 >= lo(x)
    // k*ab/g - k*ab/g = 0
    template<typename num_t>
    bool arith_base<num_t>::solve_eq_pairs(num_t const& _a, var_t x, num_t const& _b, var_t y, num_t const& r) {
        if (is_fixed(y))
            return false;
        num_t x0, y0;
        num_t a = _a, b = _b;
        num_t g = gcd(a, b, x0, y0);
        SASSERT(g >= 1);
        SASSERT(g == a * x0 + b * y0);
        if (!divides(g, r))
            return false;
        //verbose_stream() << g << " == " << a << "*" << x0 << " + " << b << "*" << y0  << "\n";
        x0 *= div(r, g);
        y0 *= div(r, g); 

        //verbose_stream() << r << " == " << a << "*" << x0 << " + " << b << "*" << y0 << "\n";



        auto adjust_lo = [&](num_t& x0, num_t& y0, num_t a, num_t b, optional<bound> const& lo, optional<bound> const& hi) {
            if (!lo || lo->value <= x0)
                return true;
            // x0 + k*b/g >= lo
            // k*(b/g) >= lo - x0
            // k >= (lo - x0)/(b/g)
            // x1 := x0 + k*b/g
            auto delta = lo->value - x0;
            auto bg = abs(div(b, g));
            verbose_stream() << g << " " << bg << " " << " " << delta << "\n";
            auto k = divide(x, delta, bg);
            auto x1 = x0 + k * bg;
            if (hi && hi->value < x1)
                return false;
            x0 = x1;
            y0 = y0 + k * (div(b, g) > 0 ? -div(a, g) : div(a, g));
            SASSERT(r == a * x0 + b * y0);    
            return true;
        };
        auto adjust_hi = [&](num_t& x0, num_t& y0, num_t a, num_t b, optional<bound> const& lo, optional<bound> const& hi) {
            if (!hi || hi->value >= x0)
                return true;
            // x0 + k*b/g <= hi
            // k <= (x0 - hi)/(b/g)
            auto delta = x0 - hi->value;
            auto bg = abs(div(b, g));
            auto k = div(delta, bg);
            auto x1 = x0 - k * bg;
            if (lo && lo->value < x1)
                return false;
            x0 = x1;
            y0 = y0 - k * (div(b, g) > 0 ? -div(a, g) : div(a, g));
            SASSERT(r == a * x0 + b * y0);
            return true;
        };
        auto const& lo_x = m_vars[x].m_lo;
        auto const& hi_x = m_vars[x].m_hi;

        if (!adjust_lo(x0, y0, a, b, lo_x, hi_x))
            return false;
        if (!adjust_hi(x0, y0, a, b, lo_x, hi_x))
            return false;
       
        auto const& lo_y = m_vars[y].m_lo;
        auto const& hi_y = m_vars[y].m_hi;

        if (!adjust_lo(y0, x0, b, a, lo_y, hi_y))
            return false;
        if (!adjust_hi(y0, x0, b, a, lo_y, hi_y))
            return false;

        if (lo_x && lo_x->value > x0)
            return false;
        if (hi_x && hi_x->value < x0)
            return false;

        if (x0 == value(x))
            return false;
        if (abs(value(x)) * 2 < abs(x0))
            return false;
        if (abs(value(y)) * 2 < abs(y0))
            return false;
        update(x, x0);
        update(y, y0);
        
        return true;
    }

    // flip on the first positive score
    // it could be changed to flip on maximal positive score
    // or flip on maximal non-negative score
    // or flip on first non-negative score
    template<typename num_t>
    void arith_base<num_t>::repair(sat::literal lit, ineq const& ineq) {
        num_t new_value, old_value;
        dtt_reward(lit);

        auto v = ineq.m_var_to_flip;
        
        if (v == UINT_MAX) {
            IF_VERBOSE(0, verbose_stream() << "no var to flip\n");
            return;
        }

        if (repair_eq(lit, ineq))
            return;

        if (!cm(ineq, v, new_value)) {
            display(verbose_stream(), v) << "\n";
            IF_VERBOSE(0, verbose_stream() << "no critical move for " << v << "\n");
            if (dtt(!ctx.is_true(lit), ineq) != 0)
                ctx.flip(lit.var());
            return;
        }
        verbose_stream() << "repair " << lit << ": " << ineq << " var: v" << v << " := " << value(v) << " -> " << new_value << "\n";
        //for (auto const& [coeff, w] : ineq.m_args)
        //    display(verbose_stream(), w) << "\n";
        update(v, new_value);
        invariant(ineq);
        if (dtt(!ctx.is_true(lit), ineq) != 0)
            ctx.flip(lit.var());
    }

    template<typename num_t>
    bool arith_base<num_t>::repair_eq(sat::literal lit, ineq const& ineq) {
        if (lit.sign() || ineq.m_op != ineq_kind::EQ)
            return false;
        auto v = ineq.m_var_to_flip;
        num_t new_value;
        verbose_stream() << ineq << "\n";
        for (auto const& [coeff, w] : ineq.m_args)
            display(verbose_stream(), w) << "\n";
        if (ctx.rand(10) == 0 && solve_eq_pairs(ineq)) {
            verbose_stream() << ineq << "\n";
            for (auto const& [coeff, w] : ineq.m_args)
                display(verbose_stream(), w) << "\n";
        }
        else if (cm(ineq, v, new_value) && update(v, new_value))
            ;
        else if (solve_eq_pairs(ineq)) {
            verbose_stream() << ineq << "\n";
            for (auto const& [coeff, w] : ineq.m_args)
                display(verbose_stream(), w) << "\n";
        }
        else
            return false;
        SASSERT(dtt(!ctx.is_true(lit), ineq) == 0);
        if (dtt(!ctx.is_true(lit), ineq) != 0)
            ctx.flip(lit.var());
        return true;
    }

    //
    // dscore(op) = sum_c (dts(c,alpha) - dts(c,alpha_after)) * weight(c)
    // TODO - use cached dts instead of computed dts
    // cached dts has to be updated when the score of literals are updated.
    // 
    template<typename num_t>
    double arith_base<num_t>::dscore(var_t v, num_t const& new_value) const {
        double score = 0;
        auto const& vi = m_vars[v];
        for (auto const& [coeff, bv] : vi.m_bool_vars) {
            sat::literal lit(bv, false);
            for (auto cl : ctx.get_use_list(lit))
                score += (compute_dts(cl) - dts(cl, v, new_value)).get_int64() * ctx.get_weight(cl);
            for (auto cl : ctx.get_use_list(~lit))
                score += (compute_dts(cl) - dts(cl, v, new_value)).get_int64() * ctx.get_weight(cl);
        }
        return score;
    }

    //
    // cm_score is costly. It involves several cache misses.
    // Note that
    // - get_use_list(lit).size() is "often" 1 or 2
    // - dtt_old can be saved
    //
    template<typename num_t>
    int arith_base<num_t>::cm_score(var_t v, num_t const& new_value) {
        int score = 0;
        auto& vi = m_vars[v];
        num_t old_value = vi.m_value;
        for (auto const& [coeff, bv] : vi.m_bool_vars) {
            auto const& ineq = *atom(bv);
            bool old_sign = sign(bv);
            num_t dtt_old = dtt(old_sign, ineq);
            num_t dtt_new = dtt(old_sign, ineq, coeff, old_value, new_value);
            if ((dtt_old == 0) == (dtt_new == 0))
                continue;
            sat::literal lit(bv, old_sign);
            if (dtt_old == 0)
                // flip from true to false
                lit.neg();

            // lit flips form false to true:           

            for (auto cl : ctx.get_use_list(lit)) {
                auto const& clause = ctx.get_clause(cl);
                if (!clause.is_true())
                    ++score;
            }

            // ignore the situation where clause contains multiple literals using v
            for (auto cl : ctx.get_use_list(~lit)) {
                auto const& clause = ctx.get_clause(cl);
                if (clause.m_num_trues == 1)
                    --score;
            }
        }
        return score;
    }

    template<typename num_t>
    num_t arith_base<num_t>::compute_dts(unsigned cl) const {
        num_t d(1), d2;
        bool first = true;
        for (auto a : ctx.get_clause(cl)) {
            auto const* ineq = atom(a.var());
            if (!ineq)
                continue;
            d2 = dtt(a.sign(), *ineq);
            if (first)
                d = d2, first = false;
            else
                d = std::min(d, d2);
            if (d == 0)
                break;
        }
        return d;
    }

    template<typename num_t>
    num_t arith_base<num_t>::dts(unsigned cl, var_t v, num_t const& new_value) const {
        num_t d(1), d2;
        bool first = true;
        for (auto lit : ctx.get_clause(cl)) {
            auto const* ineq = atom(lit.var());
            if (!ineq)
                continue;
            d2 = dtt(lit.sign(), *ineq, v, new_value);
            if (first)
                d = d2, first = false;
            else
                d = std::min(d, d2);
            if (d == 0)
                break;
        }
        return d;
    }


    template<typename num_t>
    bool arith_base<num_t>::in_bounds(var_t v, num_t const& value) {
        auto const& vi = m_vars[v];
        auto const& lo = vi.m_lo;
        auto const& hi = vi.m_hi;
        if (lo && value < lo->value)
            return false;
        if (lo && lo->is_strict && value <= lo->value)
            return false;
        if (hi && value > hi->value)
            return false;
        if (hi && hi->is_strict && value >= hi->value)
            return false;
        return true;
    }

    template<typename num_t>
    bool arith_base<num_t>::is_fixed(var_t v) {
        auto const& vi = m_vars[v];
        auto const& lo = vi.m_lo;
        auto const& hi = vi.m_hi;
        return lo && hi && lo->value == hi->value && lo->value == value(v);
    }

    template<typename num_t>
    bool arith_base<num_t>::update(var_t v, num_t const& new_value) {
        auto& vi = m_vars[v];
        expr* e = vi.m_expr;
        auto old_value = vi.m_value;
        if (old_value == new_value)
            return true;
        display(verbose_stream(), v) << " := " << new_value << "\n";
        if (!in_bounds(v, new_value)) {
            auto const& lo = vi.m_lo;
            auto const& hi = vi.m_hi;
            if (is_int(v) && lo && !lo->is_strict && new_value < lo->value) {
                if (lo->value != old_value) 
                    return update(v, lo->value);
                if (in_bounds(v, old_value + 1))
                    return update(v, old_value + 1);
                else
                    return false;                
            }
            if (is_int(v) && hi && !hi->is_strict && new_value > hi->value) {
                if (hi->value != old_value) 
                    return update(v, hi->value);
                else if (in_bounds(v, old_value - 1))
                    return update(v, old_value - 1);
                else
                    return false;                                
            }
            verbose_stream() << "out of bounds old value " << old_value << "\n";
            display(verbose_stream(), v) << "\n";
            SASSERT(false);
            return false;
        }
        for (auto const& [coeff, bv] : vi.m_bool_vars) {
            auto& ineq = *atom(bv);
            bool old_sign = sign(bv);
            sat::literal lit(bv, old_sign);
            SASSERT(ctx.is_true(lit));
            ineq.m_args_value += coeff * (new_value - old_value);
            num_t dtt_new = dtt(old_sign, ineq);
            // verbose_stream() << "dtt " << lit << " " << ineq << " " << dtt_new << "\n";
            if (dtt_new != 0)
                ctx.flip(bv);
            SASSERT(dtt(sign(bv), ineq) == 0);
        }
        vi.m_value = new_value;


        SASSERT(!m.is_value(e));
        verbose_stream() << "new value eh " << mk_bounded_pp(e, m) << "\n";
        ctx.new_value_eh(e);
        for (auto idx : vi.m_muls) {
            auto const& [w, coeff, monomial] = m_muls[idx];
            num_t prod(coeff);
            for (auto w : monomial)
                prod *= value(w);
            if (value(w) != prod) 
                update(w, prod);            
        }
        for (auto idx : vi.m_adds) {
            auto const& ad = m_adds[idx];
            num_t sum(ad.m_coeff);
            for (auto const& [coeff, w] : ad.m_args)
                sum += coeff * value(w);
            if (sum != ad.m_coeff) 
                update(ad.m_var, sum);
            
        }

        return true;
    }

    template<typename num_t>
    typename arith_base<num_t>::ineq& arith_base<num_t>::new_ineq(ineq_kind op, num_t const& coeff) {
        auto* i = alloc(ineq);
        i->m_coeff = coeff;
        i->m_op = op;
        return *i;
    }

    template<typename num_t>
    void arith_base<num_t>::add_arg(linear_term& ineq, num_t const& c, var_t v) {
        if (c != 0)
            ineq.m_args.push_back({ c, v });
    }

    template<>
    bool arith_base<checked_int64<true>>::is_num(expr* e, checked_int64<true>& i) {
        rational r;
        if (a.is_extended_numeral(e, r)) {            
            if (!r.is_int64())
                throw overflow_exception();
            i = r.get_int64();
            return true;
        }
        return false;
    }

    template<>
    bool arith_base<rational>::is_num(expr* e, rational& i) {
        return a.is_extended_numeral(e, i);
    }

    template<typename num_t>
    bool arith_base<num_t>::is_num(expr* e, num_t& i) {
        UNREACHABLE();
        return false;
    }

    template<>
    expr_ref arith_base<rational>::from_num(sort* s, rational const& n) {
        return expr_ref(a.mk_numeral(n, s), m);
    }

    template<>
    expr_ref arith_base<checked_int64<true>>::from_num(sort* s, checked_int64<true> const& n) {
        return expr_ref(a.mk_numeral(rational(n.get_int64(), rational::i64()), s), m);
    }

    template<typename num_t>
    expr_ref arith_base<num_t>::from_num(sort* s, num_t const& n) {
        UNREACHABLE();
        return expr_ref(m);
    }

    template<typename num_t>
    void arith_base<num_t>::add_args(linear_term& term, expr* e, num_t const& coeff) {
        auto v = m_expr2var.get(e->get_id(), UINT_MAX);
        expr* x, * y;
        num_t i;
        if (v != UINT_MAX)
            add_arg(term, coeff, v);
        else if (is_num(e, i))
            term.m_coeff += coeff * i;
        else if (a.is_add(e)) {
            for (expr* arg : *to_app(e))
                add_args(term, arg, coeff);
        }
        else if (a.is_sub(e, x, y)) {
            add_args(term, x, coeff);
            add_args(term, y, -coeff);
        }
        else if (a.is_mul(e)) {
            unsigned_vector m;
            num_t c(1);
            for (expr* arg : *to_app(e))
                if (is_num(arg, i))
                    c *= i;
                else
                    m.push_back(mk_term(arg));
            switch (m.size()) {
            case 0:
                term.m_coeff += c*coeff;
                break;
            case 1:
                add_arg(term, c*coeff, m[0]);
                break;
            default: {
                v = mk_var(e);
                unsigned idx = m_muls.size();
                m_muls.push_back({ v, c, m });
                num_t prod(c);
                for (auto w : m)
                    m_vars[w].m_muls.push_back(idx), prod *= value(w);
                m_vars[v].m_def_idx = idx;
                m_vars[v].m_op = arith_op_kind::OP_MUL;
                m_vars[v].m_value = prod;
                add_arg(term, coeff, v);
                break;
            }
            }
        }
        else if (a.is_uminus(e, x))
            add_args(term, x, -coeff);
        else if (a.is_mod(e, x, y) || a.is_mod0(e, x, y))
            add_arg(term, coeff, mk_op(arith_op_kind::OP_MOD, e, x, y));
        else if (a.is_idiv(e, x, y) || a.is_idiv0(e, x, y))
            add_arg(term, coeff, mk_op(arith_op_kind::OP_IDIV, e, x, y));
        else if (a.is_div(e, x, y) || a.is_div0(e, x, y))
            add_arg(term, coeff, mk_op(arith_op_kind::OP_DIV, e, x, y));
        else if (a.is_rem(e, x, y))
            add_arg(term, coeff, mk_op(arith_op_kind::OP_REM, e, x, y));
        else if (a.is_power(e, x, y) || a.is_power0(e, x, y))
            add_arg(term, coeff, mk_op(arith_op_kind::OP_POWER, e, x, y));
        else if (a.is_abs(e, x))
            add_arg(term, coeff, mk_op(arith_op_kind::OP_ABS, e, x, x));
        else if (a.is_to_int(e, x))
            add_arg(term, coeff, mk_op(arith_op_kind::OP_TO_INT, e, x, x));
        else if (a.is_to_real(e, x))
            add_arg(term, coeff, mk_op(arith_op_kind::OP_TO_REAL, e, x, x));       
        else if (a.is_arith_expr(e)) {
            NOT_IMPLEMENTED_YET();
        }
        else 
            add_arg(term, coeff, mk_var(e));
    }

    template<typename num_t>
    typename arith_base<num_t>::var_t arith_base<num_t>::mk_op(arith_op_kind k, expr* e, expr* x, expr* y) {
        auto v = mk_var(e);
        auto w = mk_term(x);
        unsigned idx = m_ops.size();
        num_t val;
        switch (k) {
        case arith_op_kind::OP_MOD:
            val = value(v) == 0 ? num_t(0) : mod(value(w), value(v));
            break;
        case arith_op_kind::OP_REM:
            if (value(v) == 0)
                val = 0;
            else {
                val = value(w);
                val %= value(v);
            }
            break;
        case arith_op_kind::OP_IDIV:
            val = value(v) == 0 ? num_t(0): div(value(w), value(v));
            break;
        case arith_op_kind::OP_DIV:
            val = value(v) == 0? num_t(0) : value(w) / value(v);
            break;
        case arith_op_kind::OP_ABS:
            val = abs(value(w));
            break;
        default:
            NOT_IMPLEMENTED_YET();
            break;
        }
        verbose_stream() << "mk-op " << mk_bounded_pp(e, m) << "\n";
        m_ops.push_back({v, k, v, w});
        m_vars[v].m_def_idx = idx;
        m_vars[v].m_op = k;
        m_vars[v].m_value = val;
        return v;
    }

    template<typename num_t>
    typename arith_base<num_t>::var_t arith_base<num_t>::mk_term(expr* e) {
        auto v = m_expr2var.get(e->get_id(), UINT_MAX);
        if (v != UINT_MAX)
            return v;
        linear_term t;
        add_args(t, e, num_t(1));
        if (t.m_coeff == 0 && t.m_args.size() == 1 && t.m_args[0].first == 1)
            return t.m_args[0].second;
        v = mk_var(e);
        auto idx = m_adds.size();
        num_t sum(t.m_coeff);
        m_adds.push_back({ { t.m_args, t.m_coeff }, v });
        for (auto const& [c, w] : t.m_args)
            m_vars[w].m_adds.push_back(idx), sum += c * value(w);
        m_vars[v].m_def_idx = idx;
        m_vars[v].m_op = arith_op_kind::OP_ADD;
        m_vars[v].m_value = sum;
        return v;
    }

    template<typename num_t>
    typename arith_base<num_t>::var_t arith_base<num_t>::mk_var(expr* e) {
        SASSERT(!m.is_value(e));
        var_t v = m_expr2var.get(e->get_id(), UINT_MAX);
        if (v == UINT_MAX) {
            v = m_vars.size();
            m_expr2var.setx(e->get_id(), v, UINT_MAX);
            m_vars.push_back(var_info(e, a.is_int(e) ? var_sort::INT : var_sort::REAL));            
        }
        return v;
    }

    template<typename num_t>
    void arith_base<num_t>::init_bool_var(sat::bool_var bv) {
        if (m_bool_vars.get(bv, nullptr))
            return;
        expr* e = ctx.atom(bv);
        // verbose_stream() << "bool var " << bv << " " << mk_bounded_pp(e, m) << "\n";
        if (!e)
            return;
        expr* x, * y;
        m_bool_vars.reserve(bv + 1);
        if (a.is_le(e, x, y) || a.is_ge(e, y, x)) {
            auto& ineq = new_ineq(ineq_kind::LE, num_t(0));
            add_args(ineq, x, num_t(1));
            add_args(ineq, y, num_t(-1));
            init_ineq(bv, ineq);
        }
        else if ((a.is_lt(e, x, y) || a.is_gt(e, y, x)) && a.is_int(x)) {
            auto& ineq = new_ineq(ineq_kind::LE, num_t(1));
            add_args(ineq, x, num_t(1));
            add_args(ineq, y, num_t(-1));
            init_ineq(bv, ineq);
        }
        else if ((a.is_lt(e, x, y) || a.is_gt(e, y, x)) && a.is_real(x)) {
            auto& ineq = new_ineq(ineq_kind::LT, num_t(0));
            add_args(ineq, x, num_t(1));
            add_args(ineq, y, num_t(-1));
            init_ineq(bv, ineq);
        }
        else if (m.is_eq(e, x, y) && a.is_int_real(x)) {
            auto& ineq = new_ineq(ineq_kind::EQ, num_t(0));
            add_args(ineq, x, num_t(1));
            add_args(ineq, y, num_t(-1));
            init_ineq(bv, ineq);
        }
        else if (m.is_distinct(e) && a.is_int_real(to_app(e)->get_arg(0))) {
            NOT_IMPLEMENTED_YET();
        }
        else if (a.is_is_int(e, x))
        {
            NOT_IMPLEMENTED_YET();
        }
#if 0
        else if (a.is_idivides(e, x, y))
            NOT_IMPLEMENTED_YET();
#endif
        else {
            SASSERT(!a.is_arith_expr(e));
        }
    }

    template<typename num_t>
    void arith_base<num_t>::init_ineq(sat::bool_var bv, ineq& i) {
        i.m_args_value = 0;
        for (auto const& [coeff, v] : i.m_args) {
            m_vars[v].m_bool_vars.push_back({ coeff, bv });
            i.m_args_value += coeff * value(v);
        }
        m_bool_vars.set(bv, &i);
    }

    template<typename num_t>
    void arith_base<num_t>::init_bool_var_assignment(sat::bool_var v) {
        auto* ineq = m_bool_vars.get(v, nullptr);
        if (ineq && ctx.is_true(sat::literal(v, false)) != (dtt(false, *ineq) == 0))
            ctx.flip(v);
    }

    template<typename num_t>
    void arith_base<num_t>::propagate_literal(sat::literal lit) {        
        if (!ctx.is_true(lit))
            return;
        auto const* ineq = atom(lit.var());
        if (!ineq)
            return;      
        if (ineq->is_true() != lit.sign())
            return;
        repair(lit, *ineq);
    }

    template<typename num_t>
    void arith_base<num_t>::repair_literal(sat::literal lit) {
        auto v = lit.var();
        auto const* ineq = atom(v);
        if (ineq && ineq->is_true() != ctx.is_true(v)) 
            ctx.flip(v);
    }

    template<typename num_t>
    bool arith_base<num_t>::propagate() {
        return false;
    }

    template<typename num_t>
    void arith_base<num_t>::repair_up(app* e) {        
        auto v = m_expr2var.get(e->get_id(), UINT_MAX);
        if (v == UINT_MAX)
            return;
        auto const& vi = m_vars[v];
        if (vi.m_def_idx == UINT_MAX)
            return;
        num_t v1, v2;
        switch (vi.m_op) {
        case LAST_ARITH_OP:
            break;
        case OP_ADD: {
            auto const& ad = m_adds[vi.m_def_idx];
            auto const& args = ad.m_args;
            num_t sum(ad.m_coeff);
            for (auto [c, w] : args)
                sum += c * value(w);
            update(v, sum);
            break;
        }
        case OP_MUL: {
            auto const& [w, coeff, monomial] = m_muls[vi.m_def_idx];
            num_t prod(coeff);
            for (auto w : monomial)
                prod *= value(w);
            update(v, prod);
            break;
        }
        case OP_MOD:
            v1 = value(m_ops[vi.m_def_idx].m_arg1);
            v2 = value(m_ops[vi.m_def_idx].m_arg2);
            update(v, v2 == 0 ? num_t(0) : mod(v1, v2));
            break;
        case OP_DIV:
            v1 = value(m_ops[vi.m_def_idx].m_arg1);
            v2 = value(m_ops[vi.m_def_idx].m_arg2);
            update(v, v2 == 0 ? num_t(0) : v1 / v2);
            break;
        case OP_IDIV:
            v1 = value(m_ops[vi.m_def_idx].m_arg1);
            v2 = value(m_ops[vi.m_def_idx].m_arg2);
            update(v, v2 == 0 ? num_t(0) : div(v1, v2));
            break;
        case OP_REM:
            v1 = value(m_ops[vi.m_def_idx].m_arg1);
            v2 = value(m_ops[vi.m_def_idx].m_arg2);
            update(v, v2 == 0 ? num_t(0) : v1 %= v2);
            break;
        case OP_ABS:
            update(v, abs(value(m_ops[vi.m_def_idx].m_arg1)));
            break;
        default:
            NOT_IMPLEMENTED_YET();
        }
    }

    template<typename num_t>
    bool arith_base<num_t>::repair_down(app* e) {
        auto v = m_expr2var.get(e->get_id(), UINT_MAX);
        if (v == UINT_MAX)
            return false;
        auto const& vi = m_vars[v];
        if (vi.m_def_idx == UINT_MAX)
            return false;
        TRACE("sls", tout << "repair def " << mk_bounded_pp(vi.m_expr, m) << "\n");
        switch (vi.m_op) {
        case arith_op_kind::LAST_ARITH_OP:
            break;
        case arith_op_kind::OP_ADD:
            return repair_add(m_adds[vi.m_def_idx]);
        case arith_op_kind::OP_MUL:
            return repair_mul(m_muls[vi.m_def_idx]);
        case arith_op_kind::OP_MOD:
            return repair_mod(m_ops[vi.m_def_idx]);
        case arith_op_kind::OP_REM:
            return repair_rem(m_ops[vi.m_def_idx]);
        case arith_op_kind::OP_POWER:
            return repair_power(m_ops[vi.m_def_idx]);
        case arith_op_kind::OP_IDIV:
            return repair_idiv(m_ops[vi.m_def_idx]);
        case arith_op_kind::OP_DIV:
            return repair_div(m_ops[vi.m_def_idx]);
        case arith_op_kind::OP_ABS:
            return repair_abs(m_ops[vi.m_def_idx]);
        case arith_op_kind::OP_TO_INT:
            return repair_to_int(m_ops[vi.m_def_idx]);
        case arith_op_kind::OP_TO_REAL:
            return repair_to_real(m_ops[vi.m_def_idx]);
        default:
            NOT_IMPLEMENTED_YET();
        }
        return true;
    }

    template<typename num_t>
    void arith_base<num_t>::initialize() { 
        for (auto lit : ctx.unit_literals())
            initialize(lit);
    }

    template<typename num_t>
    void arith_base<num_t>::initialize(sat::literal lit) {
        init_bool_var(lit.var());
        auto* ineq = atom(lit.var());
        if (!ineq)
            return;

        if (ineq->m_args.size() != 1)
            return;
        auto [c, v] = ineq->m_args[0];
        
        switch (ineq->m_op) {
            case ineq_kind::LE:
                if (lit.sign()) {
                    if (c == -1) // -x + c >= 0 <=> c >= x
                        add_le(v, ineq->m_coeff);
                    else if (c == 1) // x + c >= 0 <=> x >= -c
                        add_ge(v, -ineq->m_coeff);
                    else
                        verbose_stream() << "INITIALIZE " << lit << " " << *ineq << "\n";
                }
                else {
                    if (c == -1)
                        add_ge(v, ineq->m_coeff);
                    else if (c == 1)
                        add_le(v, -ineq->m_coeff);
                    else
                        verbose_stream() << "INITIALIZE " << lit << " " << *ineq << "\n";
                }
                break;
            case ineq_kind::EQ:
                if (lit.sign()) {
                    verbose_stream() << "INITIALIZE " << lit << " " << *ineq << "\n";
                }
                else {
                    if (c == -1) {
                        add_ge(v, ineq->m_coeff);
                        add_le(v, ineq->m_coeff);
                    }
                    else if (c == 1) {
                        add_ge(v, -ineq->m_coeff);
                        add_le(v, -ineq->m_coeff);
                    }
                    else
                        verbose_stream() << "INITIALIZE " << lit << " " << *ineq << "\n";
                }
                break;
            case ineq_kind::LT:

                if (lit.sign()) {
                    if (c == -1) // -x + c >= 0 <=> c >= x
                        add_le(v, ineq->m_coeff);
                    else if (c == 1) // x + c >= 0 <=> x >= -c
                        add_ge(v, -ineq->m_coeff);
                    else
                        verbose_stream() << "INITIALIZE " << lit << " " << *ineq << "\n";
                }
                else {
                    if (c == -1)
                        add_gt(v, ineq->m_coeff);
                    else if (c == 1)
                        add_lt(v, -ineq->m_coeff);
                    else
                        verbose_stream() << "INITIALIZE " << lit << " " << *ineq << "\n";
                }
                break;
        }
    }

    template<typename num_t>
    void arith_base<num_t>::add_le(var_t v, num_t const& n) {
        if (m_vars[v].m_hi && m_vars[v].m_hi->value <= n)
            return;
        m_vars[v].m_hi = { false, n };
    }

    template<typename num_t>
    void arith_base<num_t>::add_ge(var_t v, num_t const& n) {
        if (m_vars[v].m_lo && m_vars[v].m_lo->value >= n)
            return;
        m_vars[v].m_lo = { false, n };
    }

    template<typename num_t>
    void arith_base<num_t>::add_lt(var_t v, num_t const& n) {
        if (is_int(v))
            add_le(v, n - 1);
        else
            m_vars[v].m_hi = { true, n };
    }

    template<typename num_t>
    void arith_base<num_t>::add_gt(var_t v, num_t const& n) {
        if (is_int(v))
            add_ge(v, n + 1);
        else 
            m_vars[v].m_lo = { true, n };
    }

    template<typename num_t>
    bool arith_base<num_t>::repair_add(add_def const& ad) {
        auto v = ad.m_var;
        auto const& coeffs = ad.m_args;
        num_t sum(ad.m_coeff);
        num_t val = value(v);

        verbose_stream() << mk_bounded_pp(m_vars[v].m_expr, m) << " := " << value(v) << "\n";

        for (auto const& [c, w] : coeffs)
            sum += c * value(w);
        if (val == sum)
            return true;
        if (ctx.rand(20) == 0)
            return update(v, sum);
        else {
            auto const& [c, w] = coeffs[ctx.rand(coeffs.size())];
            num_t delta = sum - val;
            bool is_real = m_vars[w].m_sort == var_sort::REAL;
            bool round_down = ctx.rand(2) == 0;
            num_t new_value = value(w) + (is_real ? delta / c : round_down ? div(delta, c) : div(delta + c - 1, c));
            return update(w, new_value);
        }
    }

    template<typename num_t>
    bool arith_base<num_t>::repair_square(mul_def const& md) {
        auto const& [v, coeff, monomial] = md;
        if (!is_int(v) || monomial.size() != 2 || monomial[0] != monomial[1])
            return false;
        
        num_t val = value(v);
        val = div(val, coeff);
        var_t w = monomial[0];
        if (val < 0) 
            update(w, num_t(ctx.rand(10)));        
        else {
            num_t root = sqrt(val);
            if (ctx.rand(3) == 0)
                root = -root;
            if (root * root == val)
                update(w, root);
            else
                update(w, root + num_t(ctx.rand(3)) - 1);
        }
        verbose_stream() << "ROOT " << val << " v" << w << " := " << value(w) << "\n";
        return true;
    }

    template<typename num_t>
    bool arith_base<num_t>::repair_mul1(mul_def const& md) {
        auto const& [v, coeff, monomial] = md;
        if (!is_int(v))
            return false;
        num_t val = value(v);
        val = div(val, coeff);
        if (val == 0)
            return false;
        unsigned sz = monomial.size();
        unsigned start = ctx.rand(sz);
        for (unsigned i = 0; i < sz; ++i) {
            unsigned j = (start + i) % sz;
            auto w = monomial[j];
            num_t product(1);
            for (auto v : monomial)
                if (v != w)
                    product *= value(v);
            if (product == 0 || !divides(product, val))
                continue;
            if (update(w, div(val, product)))
                return true;
        }
        return false;
    }

    template<typename num_t>
    bool arith_base<num_t>::repair_mul(mul_def const& md) {
        auto const& [v, coeff, monomial] = md;
        num_t product(coeff);
        num_t val = value(v);
        num_t new_value;
        for (auto v : monomial)
            product *= value(v);
        if (product == val)
            return true;
        verbose_stream() << "repair mul " << mk_bounded_pp(m_vars[v].m_expr, m) << " := " << val << "(product: " << product << ")\n";
        unsigned sz = monomial.size();
        if (ctx.rand(20) == 0)
            return update(v, product);
        else if (val == 0) {
            auto v = monomial[ctx.rand(sz)];
            num_t zero(0);
            return update(v, zero);
        }
        else if (repair_square(md))
            return true;
        else if (ctx.rand(4) != 0 && repair_mul1(md)) {
#if 0
            verbose_stream() << "mul1 " << val << " " << coeff << " ";
            for (auto v : monomial)
                verbose_stream() << "v" << v << " = " << value(v) << " ";
            verbose_stream() << "\n";
#endif
            return true;
        }
        else if (is_int(v)) {
#if 0
            verbose_stream() << "repair mul2 - ";
            for (auto v : monomial)
                verbose_stream() << "v" << v << " = " << value(v) << " ";
#endif
            num_t n = div(val, coeff);
            if (!divides(coeff, val) && ctx.rand(2) == 0)
                n = div(val + coeff - 1, coeff);
            auto const& fs = factor(abs(n));
            vector<num_t> coeffs(sz, num_t(1));
            vector<num_t> gcds(sz, num_t(0));
            num_t sign(1);
            for (auto c : coeffs)
                sign *= c;
            unsigned i = 0;            
            for (auto w : monomial) {
                for (auto idx : m_vars[w].m_muls) {
                    auto const& [w1, coeff1, monomial1] = m_muls[idx];
                    gcds[i] = gcd(gcds[i], abs(value(w1)));
                }
                auto const& vi = m_vars[w];
                if (vi.m_lo && vi.m_lo->value >= 0)
                    coeffs[i] = 1;
                else if (vi.m_hi && vi.m_hi->value < 0)
                    coeffs[i] = -1;
                else 
                    coeffs[i] = num_t(ctx.rand(2) == 0 ? 1 : -1);
                ++i;
            }
            for (auto f : fs) 
                coeffs[ctx.rand(sz)] *= f;
            if ((sign == 0) != (n == 0))
                coeffs[ctx.rand(sz)] *= -1;
            verbose_stream() << "value " << val << " coeff: " << coeff << " coeffs: " << coeffs << " factors: " << fs << "\n";
            i = 0;
            for (auto w : monomial) {
                if (!update(w, coeffs[i++])) {
                    verbose_stream() << "failed to update v" << w << " to " << coeffs[i - 1] << "\n";
                    return false;
                }
            }
            verbose_stream() << "all updated for v" << v << " := " << value(v) << "\n";
            return true;
        }
        else {
            NOT_IMPLEMENTED_YET();
        }
        return false;
    }

    template<typename num_t>
    bool arith_base<num_t>::repair_rem(op_def const& od) {
        auto v1 = value(od.m_arg1);
        auto v2 = value(od.m_arg2);
        if (v2 == 0) 
            return update(od.m_var, num_t(0));
        

        IF_VERBOSE(0, verbose_stream() << "todo repair rem");
        // bail
        v1 %= v2;
        return update(od.m_var, v1);
    }

    template<typename num_t>
    bool arith_base<num_t>::repair_abs(op_def const& od) {
        auto val = value(od.m_var);
        auto v1 = value(od.m_arg1);
        if (val < 0)
            return update(od.m_var, abs(v1));
        else if (ctx.rand(2) == 0)
            return update(od.m_arg1, val);
        else
            return update(od.m_arg1, -val);        
    }

    template<typename num_t>
    bool arith_base<num_t>::repair_to_int(op_def const& od) {
        auto val = value(od.m_var);
        auto v1 = value(od.m_arg1);
        if (val - 1 < v1 && v1 <= val)
            return true;
        return update(od.m_arg1, val);        
    }

    template<typename num_t>
    bool arith_base<num_t>::repair_to_real(op_def const& od) {
        if (ctx.rand(20) == 0)
            return update(od.m_var, value(od.m_arg1));
        else 
            return update(od.m_arg1, value(od.m_arg1));
    }

    template<typename num_t>
    bool arith_base<num_t>::repair_power(op_def const& od) {
        auto v1 = value(od.m_arg1);
        auto v2 = value(od.m_arg2);
        if (v1 == 0 && v2 == 0) {
            return update(od.m_var, num_t(0));
        }
        IF_VERBOSE(0, verbose_stream() << "todo repair ^");
        NOT_IMPLEMENTED_YET();
        return false;
    }

    template<typename num_t>
    bool arith_base<num_t>::repair_mod(op_def const& od) {        
        auto val = value(od.m_var);
        auto v1 = value(od.m_arg1);
        auto v2 = value(od.m_arg2);
        // repair first argument
        if (val >= 0 && val < v2) {
            auto v3 = mod(v1, v2);
            if (v3 == val)
                return true;
            // find r, such that mod(v1 + r, v2) = val
            // v1 := v1 + val - v3 (+/- v2)
            v1 += val - v3;
            switch (ctx.rand(6)) {
            case 0:
                v1 += v2;
                break;
            case 1:
                v1 -= v2;
                break;
            default:
                break;
            }
            return update(od.m_arg1, v1);
        }
        return update(od.m_var, v2 == 0 ? num_t(0) : mod(v1, v2));
    }

    template<typename num_t>
    bool arith_base<num_t>::repair_idiv(op_def const& od) {
        auto v1 = value(od.m_arg1);
        auto v2 = value(od.m_arg2);
        IF_VERBOSE(0, verbose_stream() << "todo repair div");
        // bail
        return update(od.m_var, v2 == 0 ? num_t(0) : div(v1, v2));
    }
    
    template<typename num_t>
    bool arith_base<num_t>::repair_div(op_def const& od) {
        auto v1 = value(od.m_arg1);
        auto v2 = value(od.m_arg2);
        IF_VERBOSE(0, verbose_stream() << "todo repair /");
        // bail
        return update(od.m_var, v2 == 0 ? num_t(0) : v1 / v2);
    }

    template<typename num_t>
    double arith_base<num_t>::reward(sat::literal lit) {
        if (m_dscore_mode)
            return dscore_reward(lit.var());
        else
            return dtt_reward(lit);
    }

    template<typename num_t>
    double arith_base<num_t>::dtt_reward(sat::literal lit) {
        auto* ineq = atom(lit.var());
        if (!ineq)
            return -1;
        num_t new_value;
        double max_result = -100;
        unsigned n = 0, mult = 2;
        double sum_prob = 0;
        unsigned i = 0;
        m_probs.reserve(ineq->m_args.size());
        for (auto const& [coeff, x] : ineq->m_args) {
            double result = 0;
            double prob = 0;
            if (is_fixed(x))
                prob = 0;
            else if (!cm(*ineq, x, coeff, new_value)) 
                prob = 0.5;              
            else {

                auto old_value = m_vars[x].m_value;
                for (auto const& [coeff, bv] : m_vars[x].m_bool_vars) {
                    bool old_sign = sign(bv);
                    auto dtt_old = dtt(old_sign, *atom(bv));
                    auto dtt_new = dtt(old_sign, *atom(bv), coeff, old_value, new_value);
                    if (dtt_new == 0 && dtt_old != 0)
                        result += 1;
                    if (dtt_new != 0 && dtt_old == 0)
                        result -= 1;
                }                

                if (result > max_result || max_result == -100 || (result == max_result && (ctx.rand(++n) == 0))) 
                    max_result = result;

                if (result < 0)
                    prob = 0.1;
                else if (result == 0)
                    prob = 0.2;
                else
                    prob = result;
                    
            }
            // verbose_stream() << "prob v" << x << " " << prob << "\n";
            m_probs[i++] = prob;
            sum_prob += prob;
        }
        double lim = sum_prob * ((double)ctx.rand() / random_gen().max_value());
        do {
            lim -= m_probs[--i];
        } 
        while (lim >= 0 && i > 0);

        ineq->m_var_to_flip = ineq->m_args[i].second;

        return max_result;
    }

#if 0
    double sum_prob = 0;
    unsigned i = 0;
    clause const& c = get_clause(cls_idx);
    for (literal lit : c) {
        double prob = m_prob_break[m_breaks[lit.var()]];
        m_probs[i++] = prob;
        sum_prob += prob;
    }
    double lim = sum_prob * ((double)m_rand() / m_rand.max_value());
    do {
        lim -= m_probs[--i];
    } while (lim >= 0 && i > 0);
#endif

    // Newton function for integer square root.
    template<typename num_t>
    num_t arith_base<num_t>::sqrt(num_t n) {
        if (n <= 1)
            return n;

        auto x0 = div(n, num_t(2));

        auto x1 = div(x0 + div(n, x0), num_t(2));

        while (x1 < x0)	{
            x0 = x1;
            x1 = div(x0 + div(n, x0), num_t(2));
        }
        return x0;
    }

    template<typename num_t>
    vector<num_t> const& arith_base<num_t>::factor(num_t n) {
        m_factors.reset();
        for (auto d : { 2, 3, 5 }) {
            while (mod(n, num_t(d)) == 0) {
                m_factors.push_back(num_t(d));
                n = div(n, num_t(d));
            }
        }
        static int increments[8] = { 4, 2, 4, 2, 4, 6, 2, 6 };
        unsigned i = 0, j = 0;
        for (auto d = num_t(7); d * d <= n && j < 3; d += num_t(increments[i++]), ++j) {
            while (mod(n, d) == 0) {
                m_factors.push_back(d);
                n = div(n, d);
            }
            if (i == 8)
                i = 0;       
        }
        if (n > 1)
            m_factors.push_back(n);
        return m_factors;
    }


    template<typename num_t>
    double arith_base<num_t>::dscore_reward(sat::bool_var bv) {
        m_dscore_mode = false;
        bool old_sign = sign(bv);
        sat::literal litv(bv, old_sign);
        auto* ineq = atom(bv);
        if (!ineq)
            return 0;
        SASSERT(ineq->is_true() != old_sign);
        num_t new_value;

        for (auto const& [coeff, v] : ineq->m_args) {
            double result = 0;
            if (cm(*ineq, v, coeff, new_value))
                result = dscore(v, new_value);
            // just pick first positive, or pick a max?
            if (result > 0) {
                ineq->m_var_to_flip = v;
                return result;
            }
        }
        return 0;
    }

    // switch to dscore mode
    template<typename num_t>
    void arith_base<num_t>::on_rescale() {
        m_dscore_mode = true;
    }

    template<typename num_t>
    void arith_base<num_t>::on_restart() {
        for (unsigned v = 0; v < ctx.num_bool_vars(); ++v)
            init_bool_var_assignment(v);
        check_ineqs();
    }

    template<typename num_t>
    void arith_base<num_t>::check_ineqs() {
        auto check_bool_var = [&](sat::bool_var bv) {
            auto const* ineq = atom(bv);
            if (!ineq)
                return;
            num_t d = dtt(sign(bv), *ineq);
            sat::literal lit(bv, sign(bv));
            if (ctx.is_true(lit) != (d == 0)) {
                verbose_stream() << "invalid assignment " << bv << " " << *ineq << "\n";
            }
            VERIFY(ctx.is_true(lit) == (d == 0));
            };
        for (unsigned v = 0; v < ctx.num_bool_vars(); ++v)
            check_bool_var(v);
    }

    template<typename num_t>
    void arith_base<num_t>::register_term(expr* _e) {
        if (!is_app(_e))
            return;
        app* e = to_app(_e);
        auto v = ctx.atom2bool_var(e);
        if (v != sat::null_bool_var)
            init_bool_var(v);
        if (!a.is_arith_expr(e) && !m.is_eq(e) && !m.is_distinct(e))
            for (auto arg : *e)
                if (a.is_int_real(arg))
                    mk_term(arg);
    }

    template<typename num_t>
    void arith_base<num_t>::set_value(expr* e, expr* v) {
        if (!a.is_int_real(e))
            return;
        var_t w = m_expr2var.get(e->get_id(), UINT_MAX);
        if (w == UINT_MAX)
            w = mk_term(e);

        num_t n;
        if (!is_num(v, n))
            return;
        // verbose_stream() << "set value " << w << " " << mk_bounded_pp(e, m) << " " << n << " " << value(w) << "\n";
        if (n == value(w))
            return;
        update(w, n);        
    }

    template<typename num_t>
    expr_ref arith_base<num_t>::get_value(expr* e) {
        num_t n;
        if (is_num(e, n))
            return expr_ref(a.mk_numeral(n.to_rational(), a.is_int(e)), m);
        auto v = mk_term(e);
        return expr_ref(a.mk_numeral(m_vars[v].m_value.to_rational(), a.is_int(e)), m);
    }

    template<typename num_t>
    bool arith_base<num_t>::is_sat() {
        invariant();
        for (auto const& clause : ctx.clauses()) {
            bool sat = false;
            for (auto lit : clause.m_clause) {
                if (!ctx.is_true(lit))
                    continue;
                auto ineq = atom(lit.var());
                if (!ineq) {
                    sat = true;
                    break;
                }
                if (ineq->is_true() != lit.sign()) {
                    sat = true;
                    break;
                }
            }
            if (sat)
                continue;
            verbose_stream() << "not sat:\n";
            verbose_stream() << clause << "\n";
            for (auto lit : clause.m_clause) {
                verbose_stream() << lit << " (" << ctx.is_true(lit) << ") ";
                auto ineq = atom(lit.var());
                if (!ineq)
                    continue;
                verbose_stream() << *ineq << "\n";
                for (auto const& [coeff, v] : ineq->m_args)
                    verbose_stream() << coeff << " " << v << " " << mk_bounded_pp(m_vars[v].m_expr, m) << " := " << value(v) << "\n";
            }
            exit(0);
            if (!sat)
                return false;
        }
        return true;
    }

    template<typename num_t>
    std::ostream& arith_base<num_t>::display(std::ostream& out, var_t v) const {
        auto const& vi = m_vars[v];
        auto const& lo = vi.m_lo;
        auto const& hi = vi.m_hi;
        out << "v" << v << " := " << vi.m_value << " ";
        if (lo || hi) {
            if (lo)
                out << (lo->is_strict ? "(": "[") << lo->value;
            else
                out << "(";
            out << " ";
            if (hi)
                out << hi->value << (hi->is_strict ? ")" : "]");
            else
                out << ")";
            out << " ";
        }
        out << mk_bounded_pp(vi.m_expr, m) << " : ";
        for (auto [c, bv] : vi.m_bool_vars)
            out << c << "@" << bv << " ";
        return out;
    }

    template<typename num_t>
    std::ostream& arith_base<num_t>::display(std::ostream& out) const {
        for (unsigned v = 0; v < ctx.num_bool_vars(); ++v) {
            auto ineq = atom(v);
            if (ineq)
                out << v << ": " << *ineq << "\n";
        }
        for (unsigned v = 0; v < m_vars.size(); ++v)
            display(out, v) << "\n";
        
        for (auto md : m_muls) {
            out << "v" << md.m_var << " := ";
            for (auto w : md.m_monomial)
                out << "v" << w << " ";
            out << "\n";
        }
        for (auto ad : m_adds) {
            out << "v" << ad.m_var << " := ";
            bool first = true;
            for (auto [c, w] : ad.m_args)
                out << (first?"":" + ") << c << "* v" << w;
            if (ad.m_coeff != 0)
                out << " + " << ad.m_coeff;
            out << "\n";
        }
        for (auto od : m_ops) {
            out << "v" << od.m_var << " := ";
            out << "v" << od.m_arg1 << " op-" << od.m_op << " v" << od.m_arg2 << "\n";
        }
        return out;
    }

    template<typename num_t>
    void arith_base<num_t>::invariant() {
        for (unsigned v = 0; v < ctx.num_bool_vars(); ++v) {
            auto ineq = atom(v);
            if (ineq)
                invariant(*ineq);
        }
        auto& out = verbose_stream();
        for (auto md : m_muls) {
            auto const& [w, coeff, monomial] = md;
            num_t prod(coeff);
            for (auto v : monomial)
                prod *= value(v);
            //verbose_stream() << "check " << w << " " << monomial << "\n";
            if (prod != value(w)) {
                out << prod << " " << value(w) << "\n";
                out << "v" << w << " := ";
                for (auto w : monomial)
                    out << "v" << w << " ";
                out << "\n";
            }
            SASSERT(prod == value(w));

        }
        for (auto ad : m_adds) {
            //out << "check add " << ad.m_var << "\n";
            num_t sum(ad.m_coeff);
            for (auto [c, w] : ad.m_args)
                sum += c * value(w);
            if (sum != value(ad.m_var)) {


                out << "v" << ad.m_var << " := ";
                bool first = true;
                for (auto [c, w] : ad.m_args)
                    out << (first ? "" : " + ") << c << "* v" << w;
                if (ad.m_coeff != 0)
                    out << " + " << ad.m_coeff;
                out << "\n";
            }
            SASSERT(sum == value(ad.m_var));
        }
    }

    template<typename num_t>
    void arith_base<num_t>::invariant(ineq const& i) {
        num_t val(0);
        for (auto const& [c, v] : i.m_args)
            val += c * value(v);
        //verbose_stream() << "invariant " << i << "\n";
        if (val != i.m_args_value)
            verbose_stream() << i << "\n";
        SASSERT(val == i.m_args_value);
    }

    template<typename num_t>
    void arith_base<num_t>::mk_model(model& mdl) {
    }
}

template class sls::arith_base<checked_int64<true>>;
template class sls::arith_base<rational>;
