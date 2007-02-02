#ifndef GENFUN_H
#define GENFUN_H

#include <set>
#include <iostream>
#include <gmp.h>
#include <NTL/mat_ZZ.h>
#include <barvinok/evalue.h>
#include <barvinok/NTL_QQ.h>
#include <barvinok/options.h>

#ifdef NTL_STD_CXX
using namespace NTL;
#endif

struct short_rat {
    struct __short_rat_n {
	/* rows of power/columns of coeff: terms in numerator */
	vec_QQ	coeff;
	mat_ZZ	power;
    } n;
    struct __short_rat_d {
	/* rows: factors in denominator */
	mat_ZZ	power;
    } d;
    void add(const short_rat *rat);
    QQ coefficient(Value* params, barvinok_options *options) const;
    bool reduced();
    short_rat(const short_rat& r);
    short_rat(Value c);
    short_rat(const QQ& c, const vec_ZZ& num, const mat_ZZ& den);
    void normalize();
    void print(std::ostream& os, unsigned int nparam, char **param_name) const;
};

struct short_rat_lex_smaller_denominator {
  bool operator()(const short_rat* r1, const short_rat* r2) const;
};

typedef std::set<short_rat *, short_rat_lex_smaller_denominator > short_rat_list;

struct gen_fun {
    short_rat_list term;
    Polyhedron *context;

    void add(const QQ& c, const vec_ZZ& num, const mat_ZZ& den);
    void add(short_rat *r);
    /* add c times gf */
    void add(const QQ& c, const gen_fun *gf);
    void substitute(Matrix *CP);
    gen_fun *Hadamard_product(const gen_fun *gf, barvinok_options *options);
    void add_union(gen_fun *gf, barvinok_options *options);
    void shift(const vec_ZZ& offset);
    void divide(const vec_ZZ& power);
    void print(std::ostream& os, unsigned int nparam, char **param_name) const;
    operator evalue *() const;
    ZZ coefficient(Value* params, barvinok_options *options) const;
    void coefficient(Value* params, Value* c) const;
    gen_fun *summate(int nvar, barvinok_options *options) const;
    bool summate(Value *sum) const;

    gen_fun(const gen_fun *gf) {
	QQ one(1, 1);
	context = Polyhedron_Copy(gf->context);
	add(one, gf);
    }
    gen_fun(Value c);
    gen_fun(Polyhedron *C = NULL) : context(C) {}
    void clear_terms() {
	for (short_rat_list::iterator i = term.begin(); i != term.end(); ++i)
	    delete *i;
	term.clear();
    }
    ~gen_fun() {
	if (context)
	    Polyhedron_Free(context);
	clear_terms();
    }
};

std::ostream & operator<< (std::ostream & os, const gen_fun& gf);

#endif
