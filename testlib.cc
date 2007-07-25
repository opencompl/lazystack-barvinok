#include <sstream>
#include <barvinok/set.h>
#include <barvinok/options.h>
#include <barvinok/evalue.h>
#include <barvinok/util.h>
#include "conversion.h"
#include "evalue_read.h"
#include "dpoly.h"
#include "lattice_point.h"
#include "tcounter.h"
#include "bernoulli.h"

using std::cout;
using std::cerr;
using std::endl;

template <typename T>
void set_from_string(T& v, const char *s)
{
    std::istringstream str(s);
    str >> v;
}

int test_evalue(struct barvinok_options *options)
{
    unsigned nvar, nparam;
    char **all_vars;
    evalue *poly1, poly2;

    poly1 = evalue_read_from_str("(1/4 * n^4 + 1/2 * n^3 + 1/4 * n^2)",
				 NULL, &all_vars, &nvar, &nparam,
				 options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);

    value_init(poly2.d);
    evalue_copy(&poly2, poly1);
    evalue_negate(poly1);
    eadd(&poly2, poly1);
    reduce_evalue(poly1);
    assert(EVALUE_IS_ZERO(*poly1));
    free_evalue_refs(poly1);
    free_evalue_refs(&poly2);
    free(poly1);
}

int test_split_periods(struct barvinok_options *options)
{
    unsigned nvar, nparam;
    char **all_vars;
    evalue *e;

    e = evalue_read_from_str("U + 2V + 3 >= 0\n- U -2V  >= 0\n- U  10 >= 0\n"
			     "U  >= 0\n\n({( 1/3 * U + ( 2/3 * V + 0 ))})",
			     NULL, &all_vars, &nvar, &nparam,
			     options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);

    evalue_split_periods(e, 2, options->MaxRays);
    assert(value_zero_p(e->d));
    assert(e->x.p->type == partition);
    assert(e->x.p->size == 4);
    assert(value_zero_p(e->x.p->arr[1].d));
    assert(e->x.p->arr[1].x.p->type == polynomial);
    assert(value_zero_p(e->x.p->arr[3].d));
    assert(e->x.p->arr[3].x.p->type == polynomial);
    free_evalue_refs(e);
    free(e);
}

int test_specialization(struct barvinok_options *options)
{
    Value v;
    value_init(v);
    mpq_t count;
    mpq_init(count);
    ZZ sign;

    value_set_si(v, 5);
    dpoly n(2, v);
    assert(value_cmp_si(n.coeff->p[0], 1) == 0);
    assert(value_cmp_si(n.coeff->p[1], 5) == 0);
    assert(value_cmp_si(n.coeff->p[2], 10) == 0);

    value_set_si(v, 1);
    dpoly d(2, v, 1);
    value_set_si(v, 2);
    dpoly d2(2, v, 1);
    d *= d2;
    assert(value_cmp_si(d.coeff->p[0], 2) == 0);
    assert(value_cmp_si(d.coeff->p[1], 1) == 0);
    assert(value_cmp_si(d.coeff->p[2], 0) == 0);

    sign = 1;
    n.div(d, count, sign);
    mpq_canonicalize(count);
    assert(value_cmp_si(mpq_numref(count), 31) == 0);
    assert(value_cmp_si(mpq_denref(count), 8) == 0);

    value_set_si(v, -2);
    dpoly n2(2, v);
    assert(value_cmp_si(n2.coeff->p[0], 1) == 0);
    assert(value_cmp_si(n2.coeff->p[1], -2) == 0);
    assert(value_cmp_si(n2.coeff->p[2], 3) == 0);

    n2.div(d, count, sign);
    mpq_canonicalize(count);
    assert(value_cmp_si(mpq_numref(count), 6) == 0);
    assert(value_cmp_si(mpq_denref(count), 1) == 0);

    value_clear(v);
    mpq_clear(count);
}

int test_lattice_points(struct barvinok_options *options)
{
    Param_Vertices V;
    mat_ZZ tmp;
    set_from_string(tmp, "[[0 0 0 0 4][0 0 0 0 4][-1 0 1 0 4]]");
    V.Vertex = zz2matrix(tmp);
    vec_ZZ lambda;
    set_from_string(lambda, "[3 5 7]");
    mat_ZZ rays;
    set_from_string(rays, "[[0 1 0][4 0 1][0 0 -1]]");
    term_info num;
    evalue *point[4];

    unsigned nvar, nparam;
    char **all_vars;
    point[0] = evalue_read_from_str("( -7/4 * a + ( 7/4 * c + "
		    "( 7 * {( 1/4 * a + ( 3/4 * c + 3/4 ) ) } + -21/4 ) ) )",
		    "a,b,c", &all_vars, &nvar, &nparam, options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);
    point[1] = evalue_read_from_str("( -7/4 * a + ( 7/4 * c + "
		    "( 7 * {( 1/4 * a + ( 3/4 * c + 1/2 ) ) } + -1/2 ) ) )",
		    "a,b,c", &all_vars, &nvar, &nparam, options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);
    point[2] = evalue_read_from_str("( -7/4 * a + ( 7/4 * c + "
		    "( 7 * {( 1/4 * a + ( 3/4 * c + 1/4 ) ) } + 17/4 ) ) )",
		    "a,b,c", &all_vars, &nvar, &nparam, options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);
    point[3] = evalue_read_from_str("( -7/4 * a + ( 7/4 * c + "
		    "( 7 * {( 1/4 * a + ( 3/4 * c + 0 ) ) } + 9 ) ) )",
		    "a,b,c", &all_vars, &nvar, &nparam, options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);

    lattice_point(&V, rays, lambda, &num, 4, NULL, options);
    Matrix_Free(V.Vertex);

    for (int i = 0; i < 4; ++i) {
	assert(eequal(num.E[i], point[i]));
	free_evalue_refs(point[i]);
	free(point[i]);
	free_evalue_refs(num.E[i]);
	delete num.E[i];
    }
    delete [] num.E; 
}

int test_todd(struct barvinok_options *options)
{
    tcounter t(2, options->max_index);
    assert(value_cmp_si(t.todd.coeff->p[0], 1) == 0);
    assert(value_cmp_si(t.todd.coeff->p[1], -3) == 0);
    assert(value_cmp_si(t.todd.coeff->p[2], 3) == 0);
    assert(value_cmp_si(t.todd_denom->p[0], 1) == 0);
    assert(value_cmp_si(t.todd_denom->p[1], 6) == 0);
    assert(value_cmp_si(t.todd_denom->p[2], 36) == 0);

    vec_ZZ lambda;
    set_from_string(lambda, "[1 -1]");
    zz2values(lambda, t.lambda->p);

    mat_ZZ rays;
    set_from_string(rays, "[[-1 0][-1 1]]");

    QQ one(1, 1);

    vec_ZZ v;
    set_from_string(v, "[2 0 1]");
    Vector *vertex = Vector_Alloc(3);
    zz2values(v, vertex->p);

    t.handle(rays, vertex->p, one, 1, NULL, options);
    assert(value_cmp_si(mpq_numref(t.count), 71) == 0);
    assert(value_cmp_si(mpq_denref(t.count), 24) == 0);

    set_from_string(rays, "[[0 -1][1 -1]]");
    set_from_string(v, "[0 2 1]");
    zz2values(v, vertex->p);

    t.handle(rays, vertex->p, one, 1, NULL, options);
    assert(value_cmp_si(mpq_numref(t.count), 71) == 0);
    assert(value_cmp_si(mpq_denref(t.count), 12) == 0);

    set_from_string(rays, "[[1 0][0 1]]");
    set_from_string(v, "[0 0 1]");
    zz2values(v, vertex->p);

    t.handle(rays, vertex->p, one, 1, NULL, options);
    assert(value_cmp_si(mpq_numref(t.count), 6) == 0);
    assert(value_cmp_si(mpq_denref(t.count), 1) == 0);

    Vector_Free(vertex);
}

int test_bernoulli(struct barvinok_options *options)
{
    struct bernoulli *bernoulli;
    bernoulli = bernoulli_compute(2);
    bernoulli = bernoulli_compute(4);
    bernoulli = bernoulli_compute(8);
    assert(value_cmp_si(bernoulli->b_num->p[6], 1) == 0);
    assert(value_cmp_si(bernoulli->b_den->p[6], 42) == 0);
    assert(value_cmp_si(bernoulli->sum[3]->p[0], 0) == 0);
    assert(value_cmp_si(bernoulli->sum[3]->p[1], 0) == 0);
    assert(value_cmp_si(bernoulli->sum[3]->p[2], 1) == 0);
    assert(value_cmp_si(bernoulli->sum[3]->p[3], -2) == 0);
    assert(value_cmp_si(bernoulli->sum[3]->p[4], 1) == 0);
    assert(value_cmp_si(bernoulli->sum[3]->p[5], 4) == 0);

    unsigned nvar, nparam;
    char **all_vars;
    evalue *base, *sum1, *sum2;
    base = evalue_read_from_str("(1 * n + 1)", NULL, &all_vars, &nvar, &nparam,
				options->MaxRays);

    sum1 = evalue_polynomial(bernoulli->sum[3], base);
    Free_ParamNames(all_vars, nvar+nparam);

    sum2 = evalue_read_from_str("(1/4 * n^4 + 1/2 * n^3 + 1/4 * n^2)",
				NULL, &all_vars, &nvar, &nparam,
				options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);
    assert(eequal(sum1, sum2));
    free_evalue_refs(base);
    free_evalue_refs(sum1);
    free_evalue_refs(sum2);
    free(base);
    free(sum1);
    free(sum2);
}

int test_bernoulli_sum(struct barvinok_options *options)
{
    unsigned nvar, nparam;
    char **all_vars;
    evalue *e, *sum1, *sum2;
    e = evalue_read_from_str("i + -1 >= 0\n -i + n >= 0\n\n 1 + (-1 *i) + i^2",
			     "i", &all_vars, &nvar, &nparam,
				options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);

    sum1 = Bernoulli_sum_evalue(e, 1, options);
    sum2 = evalue_read_from_str("n -1 >= 0\n\n (1/3 * n^3 + 2/3 * n)",
				NULL, &all_vars, &nvar, &nparam,
				options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);
    evalue_negate(sum1);
    eadd(sum2, sum1);
    reduce_evalue(sum1);
    assert(EVALUE_IS_ZERO(*sum1));
    free_evalue_refs(e);
    free_evalue_refs(sum1);
    free(e);
    free(sum1);

    e = evalue_read_from_str("-i + -1 >= 0\n i + n >= 0\n\n 1 + i + i^2",
			     "i", &all_vars, &nvar, &nparam,
				options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);
    sum1 = Bernoulli_sum_evalue(e, 1, options);
    evalue_negate(sum1);
    eadd(sum2, sum1);
    reduce_evalue(sum1);
    assert(EVALUE_IS_ZERO(*sum1));
    free_evalue_refs(e);
    free_evalue_refs(sum1);
    free(e);
    free(sum1);

    free_evalue_refs(sum2);
    free(sum2);

    e = evalue_read_from_str("i + 4 >= 0\n -i + n >= 0\n\n i",
			     "i", &all_vars, &nvar, &nparam,
				options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);
    sum1 = Bernoulli_sum_evalue(e, 1, options);
    sum2 = evalue_read_from_str("n + 0 >= 0\n\n (1/2 * n^2 + 1/2 * n + (-10))\n"
		    "n + 4 >= 0\n -n -1 >= 0\n\n (1/2 * n^2 + 1/2 * n + (-10))",
				NULL, &all_vars, &nvar, &nparam,
				options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);
    evalue_negate(sum1);
    eadd(sum2, sum1);
    reduce_evalue(sum1);
    assert(EVALUE_IS_ZERO(*sum1));
    free_evalue_refs(e);
    free_evalue_refs(sum1);
    free(e);
    free(sum1);
    free_evalue_refs(sum2);
    free(sum2);

    e = evalue_read_from_str("i -5 >= 0\n -i + n >= 0\n j -1 >= 0\n i -j >= 0\n"
			     "k -1 >= 0\n j -k >= 0\n\n1",
			     "i,j,k", &all_vars, &nvar, &nparam,
				options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);
    sum1 = Bernoulli_sum_evalue(e, 3, options);
    sum2 = evalue_read_from_str("n -5 >= 0\n\n"
				"1/6 * n^3 + 1/2 * n^2 + 1/3 * n + -20",
				NULL, &all_vars, &nvar, &nparam,
				options->MaxRays);
    Free_ParamNames(all_vars, nvar+nparam);
    evalue_negate(sum1);
    eadd(sum2, sum1);
    reduce_evalue(sum1);
    assert(EVALUE_IS_ZERO(*sum1));
    free_evalue_refs(e);
    free_evalue_refs(sum1);
    free(e);
    free(sum1);
    free_evalue_refs(sum2);
    free(sum2);
}

int main(int argc, char **argv)
{
    struct barvinok_options *options = barvinok_options_new_with_defaults();
    test_evalue(options);
    test_split_periods(options);
    test_specialization(options);
    test_lattice_points(options);
    test_todd(options);
    test_bernoulli(options);
    test_bernoulli_sum(options);
    barvinok_options_free(options);
}