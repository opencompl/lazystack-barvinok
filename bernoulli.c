#include <assert.h>
#include <barvinok/options.h>
#include <barvinok/util.h>
#include "bernoulli.h"
#include "lattice_point.h"

#define ALLOC(type) (type*)malloc(sizeof(type))
#define ALLOCN(type,n) (type*)malloc((n) * sizeof(type))
#define REALLOCN(ptr,type,n) (type*)realloc(ptr, (n) * sizeof(type))

static struct bernoulli_coef bernoulli_coef;
static struct poly_list bernoulli;
static struct poly_list faulhaber;

struct bernoulli_coef *bernoulli_coef_compute(int n)
{
    int i, j;
    Value factor, tmp;

    if (n < bernoulli_coef.n)
	return &bernoulli_coef;

    if (n >= bernoulli_coef.size) {
	int size = 3*(n + 5)/2;
	Vector *b;

	b = Vector_Alloc(size);
	if (bernoulli_coef.n) {
	    Vector_Copy(bernoulli_coef.num->p, b->p, bernoulli_coef.n);
	    Vector_Free(bernoulli_coef.num);
	}
	bernoulli_coef.num = b;
	b = Vector_Alloc(size);
	if (bernoulli_coef.n) {
	    Vector_Copy(bernoulli_coef.den->p, b->p, bernoulli_coef.n);
	    Vector_Free(bernoulli_coef.den);
	}
	bernoulli_coef.den = b;
	b = Vector_Alloc(size);
	if (bernoulli_coef.n) {
	    Vector_Copy(bernoulli_coef.lcm->p, b->p, bernoulli_coef.n);
	    Vector_Free(bernoulli_coef.lcm);
	}
	bernoulli_coef.lcm = b;

	bernoulli_coef.size = size;
    }
    value_init(factor);
    value_init(tmp);
    for (i = bernoulli_coef.n; i <= n; ++i) {
	if (i == 0) {
	    value_set_si(bernoulli_coef.num->p[0], 1);
	    value_set_si(bernoulli_coef.den->p[0], 1);
	    value_set_si(bernoulli_coef.lcm->p[0], 1);
	    continue;
	}
	value_set_si(bernoulli_coef.num->p[i], 0);
	value_set_si(factor, -(i+1));
	for (j = i-1; j >= 0; --j) {
	    mpz_mul_ui(factor, factor, j+1);
	    mpz_divexact_ui(factor, factor, i+1-j);
	    value_division(tmp, bernoulli_coef.lcm->p[i-1],
			   bernoulli_coef.den->p[j]);
	    value_multiply(tmp, tmp, bernoulli_coef.num->p[j]);
	    value_multiply(tmp, tmp, factor);
	    value_addto(bernoulli_coef.num->p[i], bernoulli_coef.num->p[i], tmp);
	}
	mpz_mul_ui(bernoulli_coef.den->p[i], bernoulli_coef.lcm->p[i-1], i+1);
	value_gcd(tmp, bernoulli_coef.num->p[i], bernoulli_coef.den->p[i]);
	if (value_notone_p(tmp)) {
	    value_division(bernoulli_coef.num->p[i],
			    bernoulli_coef.num->p[i], tmp);
	    value_division(bernoulli_coef.den->p[i],
			    bernoulli_coef.den->p[i], tmp);
	}
	value_lcm(bernoulli_coef.lcm->p[i],
		  bernoulli_coef.lcm->p[i-1], bernoulli_coef.den->p[i]);
    }
    bernoulli_coef.n = n+1;
    value_clear(factor);
    value_clear(tmp);

    return &bernoulli_coef;
}

/*
 * Compute either Bernoulli B_n or Faulhaber F_n polynomials.
 *
 * B_n =         sum_{k=0}^n {  n  \choose k } b_k x^{n-k}
 * F_n = 1/(n+1) sum_{k=0}^n { n+1 \choose k } b_k x^{n+1-k}
 */
static struct poly_list *bernoulli_faulhaber_compute(int n, struct poly_list *pl,
						     int faulhaber)
{
    int i, j;
    Value factor;
    struct bernoulli_coef *bc;

    if (n < pl->n)
	return pl;

    if (n >= pl->size) {
	int size = 3*(n + 5)/2;
	Vector **poly;

	poly = ALLOCN(Vector *, size);
	for (i = 0; i < pl->n; ++i)
	    poly[i] = pl->poly[i];
	free(pl->poly);
	pl->poly = poly;

	pl->size = size;
    }

    bc = bernoulli_coef_compute(n);

    value_init(factor);
    for (i = pl->n; i <= n; ++i) {
	pl->poly[i] = Vector_Alloc(i+faulhaber+2);
	value_assign(pl->poly[i]->p[i+faulhaber], bc->lcm->p[i]);
	if (faulhaber)
	    mpz_mul_ui(pl->poly[i]->p[i+2], bc->lcm->p[i], i+1);
	else
	    value_assign(pl->poly[i]->p[i+1], bc->lcm->p[i]);
	value_set_si(factor, 1);
	for (j = 1; j <= i; ++j) {
	    mpz_mul_ui(factor, factor, i+faulhaber+1-j);
	    mpz_divexact_ui(factor, factor, j);
	    value_division(pl->poly[i]->p[i+faulhaber-j],
			   bc->lcm->p[i], bc->den->p[j]);
	    value_multiply(pl->poly[i]->p[i+faulhaber-j],
			   pl->poly[i]->p[i+faulhaber-j], bc->num->p[j]);
	    value_multiply(pl->poly[i]->p[i+faulhaber-j],
			   pl->poly[i]->p[i+faulhaber-j], factor);
	}
	Vector_Normalize(pl->poly[i]->p, i+faulhaber+2);
    }
    value_clear(factor);
    pl->n = n+1;

    return pl;
}

struct poly_list *bernoulli_compute(int n)
{
    return bernoulli_faulhaber_compute(n, &bernoulli, 0);
}

struct poly_list *faulhaber_compute(int n)
{
    return bernoulli_faulhaber_compute(n, &faulhaber, 1);
}

/* shift variables in polynomial one down */
static void shift(evalue *e)
{
    int i;
    if (value_notzero_p(e->d))
	return;
    assert(e->x.p->type == polynomial || e->x.p->type == fractional);
    if (e->x.p->type == polynomial) {
	assert(e->x.p->pos > 1);
	e->x.p->pos--;
    }
    for (i = 0; i < e->x.p->size; ++i)
	shift(&e->x.p->arr[i]);
}

/* shift variables in polynomial n up */
static void unshift(evalue *e, unsigned n)
{
    int i;
    if (value_notzero_p(e->d))
	return;
    assert(e->x.p->type == polynomial || e->x.p->type == fractional);
    if (e->x.p->type == polynomial)
	e->x.p->pos += n;
    for (i = 0; i < e->x.p->size; ++i)
	unshift(&e->x.p->arr[i], n);
}

static evalue *shifted_copy(evalue *src)
{
    evalue *e = ALLOC(evalue);
    value_init(e->d);
    evalue_copy(e, src);
    shift(e);
    return e;
}

/* Computes the argument for the Faulhaber polynomial.
 * If we are computing a polynomial approximation (exact == 0),
 * then this is simply arg/denom.
 * Otherwise, if neg == 0, then we are dealing with an upper bound
 * and we need to compute floor(arg/denom) = arg/denom - { arg/denom }
 * If neg == 1, then we are dealing with a lower bound
 * and we need to compute ceil(arg/denom) = arg/denom + { -arg/denom }
 *
 * Modifies arg (if exact == 1).
 */
static evalue *power_sums_base(Vector *arg, Value denom, int neg, int exact)
{
    evalue *fract;
    evalue *base = affine2evalue(arg->p, denom, arg->Size-1);

    if (!exact || value_one_p(denom))
	return base;

    if (neg)
	Vector_Oppose(arg->p, arg->p, arg->Size);

    fract = fractional_part(arg->p, denom, arg->Size-1, NULL);
    if (!neg) {
	value_set_si(arg->p[0], -1);
	evalue_mul(fract, arg->p[0]);
    }
    eadd(fract, base);
    evalue_free(fract);

    return base;
}

static evalue *power_sums(struct poly_list *faulhaber, evalue *poly,
			  Vector *arg, Value denom, int neg, int alt_neg,
			  int exact)
{
    int i;
    evalue *base = power_sums_base(arg, denom, neg, exact);
    evalue *sum = evalue_zero();

    for (i = 1; i < poly->x.p->size; ++i) {
	evalue *term = evalue_polynomial(faulhaber->poly[i], base);
	evalue *factor = shifted_copy(&poly->x.p->arr[i]);
	emul(factor, term);
	if (alt_neg && (i % 2))
	    evalue_negate(term);
	eadd(term, sum);
	evalue_free(factor);
	evalue_free(term);
    }
    if (neg)
	evalue_negate(sum);
    evalue_free(base);

    return sum;
}

/* Given a constraint (cst_affine) a x + b y + c >= 0, compate a constraint (c)
 * +- (b y + c) +- a >=,> 0
 * ^            ^      ^
 * |            |      strict
 * sign_affine  sign_cst
 */
static void bound_constraint(Value *c, unsigned dim,
			     Value *cst_affine,
			     int sign_affine, int sign_cst, int strict)
{
    if (sign_affine == -1)
	Vector_Oppose(cst_affine+1, c, dim+1);
    else
	Vector_Copy(cst_affine+1, c, dim+1);

    if (sign_cst == -1)
	value_subtract(c[dim], c[dim], cst_affine[0]);
    else if (sign_cst == 1)
	value_addto(c[dim], c[dim], cst_affine[0]);

    if (strict)
	value_decrement(c[dim], c[dim]);
}

struct Bernoulli_data {
    struct barvinok_options *options;
    struct evalue_section *s;
    int size;
    int ns;
    evalue *e;
};

static evalue *compute_poly_u(evalue *poly_u, Value *upper, Vector *row,
			      unsigned dim, Value tmp,
			      struct poly_list *faulhaber,
			      struct Bernoulli_data *data)
{
    int exact = data->options->approximation_method == BV_APPROX_NONE;
    if (poly_u)
	return poly_u;
    Vector_Copy(upper+2, row->p, dim+1);
    value_oppose(tmp, upper[1]);
    value_addto(row->p[dim], row->p[dim], tmp);
    return power_sums(faulhaber, data->e, row, tmp, 0, 0, exact);
}

static evalue *compute_poly_l(evalue *poly_l, Value *lower, Vector *row,
			      unsigned dim,
			      struct poly_list *faulhaber,
			      struct Bernoulli_data *data)
{
    int exact = data->options->approximation_method == BV_APPROX_NONE;
    if (poly_l)
	return poly_l;
    Vector_Copy(lower+2, row->p, dim+1);
    value_addto(row->p[dim], row->p[dim], lower[1]);
    return power_sums(faulhaber, data->e, row, lower[1], 0, 1, exact);
}

/* Compute sum of constant term.
 */
static evalue *linear_term(evalue *cst, Value *lower, Value *upper,
			   Vector *row, Value tmp, int exact)
{
    evalue *linear;
    unsigned dim = row->Size - 1;

    if (EVALUE_IS_ZERO(*cst))
	return evalue_zero();

    if (!exact) {
	value_absolute(tmp, upper[1]);
	/* upper - lower */
	Vector_Combine(lower+2, upper+2, row->p, tmp, lower[1], dim+1);
	value_multiply(tmp, tmp, lower[1]);
	/* upper - lower + 1 */
	value_addto(row->p[dim], row->p[dim], tmp);

	linear = affine2evalue(row->p, tmp, dim);
    } else {
	evalue *l;

	value_absolute(tmp, upper[1]);
	Vector_Copy(upper+2, row->p, dim+1);
	value_addto(row->p[dim], row->p[dim], tmp);
	/* floor(upper+1) */
	linear = power_sums_base(row, tmp, 0, 1);

	Vector_Copy(lower+2, row->p, dim+1);
	/* floor(-lower) */
	l = power_sums_base(row, lower[1], 0, 1);

	/* floor(upper+1) + floor(-lower) = floor(upper) - ceil(lower) + 1 */
	eadd(l, linear);
	evalue_free(l);
    }

    emul(cst, linear);
    return linear;
}

static void Bernoulli_init(unsigned n, void *cb_data)
{
    struct Bernoulli_data *data = (struct Bernoulli_data *)cb_data;
    int exact = data->options->approximation_method == BV_APPROX_NONE;
    int cases = exact ? 3 : 5;

    if (cases * n <= data->size)
	return;

    data->size = cases * (n + 16);
    data->s = REALLOCN(data->s, struct evalue_section, data->size);
}

static void Bernoulli_cb(Matrix *M, Value *lower, Value *upper, void *cb_data)
{
    struct Bernoulli_data *data = (struct Bernoulli_data *)cb_data;
    Matrix *M2;
    Polyhedron *T;
    evalue *factor = NULL;
    evalue *linear = NULL;
    int constant = 0;
    Value tmp;
    unsigned dim = M->NbColumns-2;
    Vector *row;
    int exact = data->options->approximation_method == BV_APPROX_NONE;
    int cases = exact ? 3 : 5;

    assert(lower);
    assert(upper);
    assert(data->ns + cases <= data->size);

    M2 = Matrix_Copy(M);
    T = Constraints2Polyhedron(M2, data->options->MaxRays);
    Matrix_Free(M2);

    POL_ENSURE_VERTICES(T);
    if (emptyQ(T)) {
	Polyhedron_Free(T);
	return;
    }

    assert(lower != upper);

    row = Vector_Alloc(dim+1);
    value_init(tmp);
    if (value_notzero_p(data->e->d)) {
	factor = data->e;
	constant = 1;
    } else {
	if (data->e->x.p->type == polynomial && data->e->x.p->pos == 1)
	    factor = shifted_copy(&data->e->x.p->arr[0]);
	else {
	    factor = shifted_copy(data->e);
	    constant = 1;
	}
    }
    linear = linear_term(factor, lower, upper, row, tmp, exact);

    if (constant) {
	data->s[data->ns].E = linear;
	data->s[data->ns].D = T;
	++data->ns;
    } else {
	evalue *poly_u = NULL, *poly_l = NULL;
	Polyhedron *D;
	struct poly_list *faulhaber;
	assert(data->e->x.p->type == polynomial);
	assert(data->e->x.p->pos == 1);
	faulhaber = faulhaber_compute(data->e->x.p->size-1);
	/* lower is the constraint
	 *			 b i - l' >= 0		i >= l'/b = l
	 * upper is the constraint
	 *			-a i + u' >= 0		i <= u'/a = u
	 */
	M2 = Matrix_Alloc(3, 2+T->Dimension);
	value_set_si(M2->p[0][0], 1);
	value_set_si(M2->p[1][0], 1);
	value_set_si(M2->p[2][0], 1);
	/* Case 1:
	 *		1 <= l		l' - b >= 0
	 *		0 < l		l' - 1 >= 0	if exact
	 */
	if (exact)
	    bound_constraint(M2->p[0]+1, T->Dimension, lower+1, -1, 0, 1);
	else
	    bound_constraint(M2->p[0]+1, T->Dimension, lower+1, -1, -1, 0);
	D = AddConstraints(M2->p_Init, 1, T, data->options->MaxRays);
	POL_ENSURE_VERTICES(D);
	if (emptyQ2(D))
	    Polyhedron_Free(D);
	else {
	    evalue *extra;
	    poly_u = compute_poly_u(poly_u, upper, row, dim, tmp,
					faulhaber, data);
	    Vector_Oppose(lower+2, row->p, dim+1);
	    extra = power_sums(faulhaber, data->e, row, lower[1], 1, 0, exact);
	    eadd(poly_u, extra);
	    eadd(linear, extra);

	    data->s[data->ns].E = extra;
	    data->s[data->ns].D = D;
	    ++data->ns;
	}

	/* Case 2:
	 *		1 <= -u		-u' - a >= 0
	 *		0 < -u		-u' - 1 >= 0	if exact
	 */
	if (exact)
	    bound_constraint(M2->p[0]+1, T->Dimension, upper+1, -1, 0, 1);
	else
	    bound_constraint(M2->p[0]+1, T->Dimension, upper+1, -1, 1, 0);
	D = AddConstraints(M2->p_Init, 1, T, data->options->MaxRays);
	POL_ENSURE_VERTICES(D);
	if (emptyQ2(D))
	    Polyhedron_Free(D);
	else {
	    evalue *extra;
	    poly_l = compute_poly_l(poly_l, lower, row, dim, faulhaber, data);
	    Vector_Oppose(upper+2, row->p, dim+1);
	    value_oppose(tmp, upper[1]);
	    extra = power_sums(faulhaber, data->e, row, tmp, 1, 1, exact);
	    eadd(poly_l, extra);
	    eadd(linear, extra);

	    data->s[data->ns].E = extra;
	    data->s[data->ns].D = D;
	    ++data->ns;
	}

	/* Case 3:
	 *		u >= 0		u' >= 0
	 *		-l >= 0		-l' >= 0
	 */
	bound_constraint(M2->p[0]+1, T->Dimension, upper+1, 1, 0, 0);
	bound_constraint(M2->p[1]+1, T->Dimension, lower+1, 1, 0, 0);
	D = AddConstraints(M2->p_Init, 2, T, data->options->MaxRays);
	POL_ENSURE_VERTICES(D);
	if (emptyQ2(D))
	    Polyhedron_Free(D);
	else {
	    poly_l = compute_poly_l(poly_l, lower, row, dim, faulhaber, data);
	    poly_u = compute_poly_u(poly_u, upper, row, dim, tmp,
					faulhaber, data);
	
	    data->s[data->ns].E = ALLOC(evalue);
	    value_init(data->s[data->ns].E->d);
	    evalue_copy(data->s[data->ns].E, poly_u);
	    eadd(poly_l, data->s[data->ns].E);
	    eadd(linear, data->s[data->ns].E);
	    data->s[data->ns].D = D;
	    ++data->ns;
	}

	if (!exact) {
	    /* Case 4:
	     *		l < 1		-l' + b - 1 >= 0
	     *		0 < l		l' - 1 >= 0
	     *		u >= 1		u' - a >= 0
	     */
	    bound_constraint(M2->p[0]+1, T->Dimension, lower+1, 1, 1, 1);
	    bound_constraint(M2->p[1]+1, T->Dimension, lower+1, -1, 0, 1);
	    bound_constraint(M2->p[2]+1, T->Dimension, upper+1, 1, 1, 0);
	    D = AddConstraints(M2->p_Init, 3, T, data->options->MaxRays);
	    POL_ENSURE_VERTICES(D);
	    if (emptyQ2(D))
		Polyhedron_Free(D);
	    else {
		poly_u = compute_poly_u(poly_u, upper, row, dim, tmp,
					    faulhaber, data);
		eadd(linear, poly_u);
		data->s[data->ns].E = poly_u;
		poly_u = NULL;
		data->s[data->ns].D = D;
		++data->ns;
	    }

	    /* Case 5:
	     * 		-u < 1		u' + a - 1 >= 0
	     *		0 < -u		-u' - 1 >= 0
	     *		l <= -1		-l' - b >= 0
	     */
	    bound_constraint(M2->p[0]+1, T->Dimension, upper+1, 1, -1, 1);
	    bound_constraint(M2->p[1]+1, T->Dimension, upper+1, -1, 0, 1);
	    bound_constraint(M2->p[2]+1, T->Dimension, lower+1, 1, -1, 0);
	    D = AddConstraints(M2->p_Init, 3, T, data->options->MaxRays);
	    POL_ENSURE_VERTICES(D);
	    if (emptyQ2(D))
		Polyhedron_Free(D);
	    else {
		poly_l = compute_poly_l(poly_l, lower, row, dim,
					faulhaber, data);
		eadd(linear, poly_l);
		data->s[data->ns].E = poly_l;
		poly_l = NULL;
		data->s[data->ns].D = D;
		++data->ns;
	    }
	}

	Matrix_Free(M2);
	Polyhedron_Free(T);
	if (poly_l)
	    evalue_free(poly_l);
	if (poly_u)
	    evalue_free(poly_u);
	evalue_free(linear);
    }
    if (factor != data->e)
	evalue_free(factor);
    value_clear(tmp);
    Vector_Free(row);
}

/*
 * Move the variable at position pos to the front by exchanging
 * that variable with the first variable.
 */
static void more_var_to_front(Polyhedron **P_p , evalue **E_p, int pos)
{
    Polyhedron *P = *P_p;
    evalue *E = *E_p;
    unsigned dim = P->Dimension;

    assert(pos != 0);

    P = Polyhedron_Copy(P);
    Polyhedron_ExchangeColumns(P, 1, 1+pos);
    *P_p = P;

    if (value_zero_p(E->d)) {
	int j;
	evalue **subs;

	subs = ALLOCN(evalue *, dim);
	for (j = 0; j < dim; ++j)
	    subs[j] = evalue_var(j);
	E = subs[0];
	subs[0] = subs[pos];
	subs[pos] = E;
	E = evalue_dup(*E_p);
	evalue_substitute(E, subs);
	for (j = 0; j < dim; ++j)
	    evalue_free(subs[j]);
	free(subs);
	*E_p = E;
    }
}

static int type_offset(enode *p)
{
   return p->type == fractional ? 1 :
	  p->type == flooring ? 1 : 0;
}

static void adjust_periods(evalue *e, unsigned nvar, Vector *periods)
{
    for (; value_zero_p(e->d); e = &e->x.p->arr[0]) {
	int pos;
	assert(e->x.p->type == polynomial);
	assert(e->x.p->size == 2);
	assert(value_notzero_p(e->x.p->arr[1].d));

	pos = e->x.p->pos - 1;
	if (pos >= nvar)
	    break;

	value_lcm(periods->p[pos], periods->p[pos], e->x.p->arr[1].d);
    }
}

static void fractional_periods_r(evalue *e, unsigned nvar, Vector *periods)
{
    int i;

    if (value_notzero_p(e->d))
	return;

    assert(e->x.p->type == polynomial || e->x.p->type == fractional);

    if (e->x.p->type == fractional)
	adjust_periods(&e->x.p->arr[0], nvar, periods);

    for (i = type_offset(e->x.p); i < e->x.p->size; ++i)
	fractional_periods_r(&e->x.p->arr[i], nvar, periods);
}

/*
 * For each of the nvar variables, compute the lcm of the
 * denominators of the coefficients of that variable in
 * any of the fractional parts.
 */
static Vector *fractional_periods(evalue *e, unsigned nvar)
{
    int i;
    Vector *periods = Vector_Alloc(nvar);

    for (i = 0; i < nvar; ++i)
	value_set_si(periods->p[i], 1);

    fractional_periods_r(e, nvar, periods);

    return periods;
}

/* Move "best" variable to sum over into the first position,
 * possibly changing *P_p and *E_p.
 *
 * If there are any fractional parts (period != NULL), then we
 * first look for a variable with the smallest lcm of denominators
 * inside a fractional part.  This denominator is assigned to period
 * and corresponds to the number of "splinters" we need to construct
 * at this level.
 *
 * Of those with this denominator (all if period == NULL or if there
 * are no fractional parts), we select the variable with the smallest
 * maximal coefficient, as this coefficient will become a denominator
 * in new fractional parts (for an exact computation), which may
 * lead to splintering in the next step.
 */
static void move_best_to_front(Polyhedron **P_p, evalue **E_p, unsigned nvar,
			       Value *period)
{
    Polyhedron *P = *P_p;
    evalue *E = *E_p;
    int i, j, best_i = -1;
    Vector *periods;
    Value best, max;

    assert(nvar >= 1);

    if (period) {
	periods = fractional_periods(E, nvar);
	value_assign(*period, periods->p[0]);
	for (i = 1; i < nvar; ++i)
	    if (value_lt(periods->p[i], *period))
		value_assign(*period, periods->p[i]);
    }

    value_init(best);
    value_init(max);

    for (i = 0; i < nvar; ++i) {
	if (period && value_ne(*period, periods->p[i]))
	    continue;

	value_set_si(max, 0);

	for (j = 0; j < P->NbConstraints; ++j) {
	    if (value_zero_p(P->Constraint[j][1+i]))
		continue;
	    if (First_Non_Zero(P->Constraint[j]+1, i) == -1 &&
		First_Non_Zero(P->Constraint[j]+1+i+1, nvar-i-1) == -1)
		continue;
	    if (value_abs_gt(P->Constraint[j][1+i], max))
		value_absolute(max, P->Constraint[j][1+i]);
	}

	if (best_i == -1 || value_lt(max, best)) {
	    value_assign(best, max);
	    best_i = i;
	}
    }

    value_clear(best);
    value_clear(max);

    if (period)
	Vector_Free(periods);

    if (best_i != 0)
	more_var_to_front(P_p, E_p, best_i);

    return;
}

static evalue *sum_over_polytope_base(Polyhedron *P, evalue *E, unsigned nvar,
				      struct Bernoulli_data *data,
				      struct barvinok_options *options)
{
    evalue *res;

    assert(P->NbEq == 0);

    data->ns = 0;
    data->e = E;

    for_each_lower_upper_bound(P, Bernoulli_init, Bernoulli_cb, data);

    res = evalue_from_section_array(data->s, data->ns);

    if (nvar > 1) {
	evalue *tmp = Bernoulli_sum_evalue(res, nvar-1, options);
	evalue_free(res);
	res = tmp;
    }

    return res;
}

static evalue *sum_over_polytope(Polyhedron *P, evalue *E, unsigned nvar,
				 struct Bernoulli_data *data,
				 struct barvinok_options *options);

static evalue *sum_over_polytope_with_equalities(Polyhedron *P, evalue *E,
				 unsigned nvar,
				 struct Bernoulli_data *data,
				 struct barvinok_options *options)
{
    unsigned dim = P->Dimension;
    unsigned new_dim, new_nparam;
    Matrix *T = NULL, *CP = NULL;
    evalue **subs;
    evalue *sum;
    int j;

    if (emptyQ(P))
	return evalue_zero();

    assert(P->NbEq > 0);

    remove_all_equalities(&P, NULL, &CP, &T, dim-nvar, options->MaxRays);

    if (emptyQ(P)) {
	Polyhedron_Free(P);
	return evalue_zero();
    }

    new_nparam = CP ? CP->NbColumns-1 : dim - nvar;
    new_dim = T ? T->NbColumns-1 : nvar + new_nparam;

    /* We can avoid these substitutions if E is a constant */
    subs = ALLOCN(evalue *, dim);
    for (j = 0; j < nvar; ++j) {
	if (T)
	    subs[j] = affine2evalue(T->p[j], T->p[nvar+new_nparam][new_dim],
				    new_dim);
	else
	    subs[j] = evalue_var(j);
    }
    for (j = 0; j < dim-nvar; ++j) {
	if (CP)
	    subs[nvar+j] = affine2evalue(CP->p[j], CP->p[dim-nvar][new_nparam],
					 new_nparam);
	else
	    subs[nvar+j] = evalue_var(j);
	unshift(subs[nvar+j], new_dim-new_nparam);
    }

    E = evalue_dup(E);
    evalue_substitute(E, subs);
    reduce_evalue(E);

    for (j = 0; j < dim; ++j)
	evalue_free(subs[j]);
    free(subs);

    if (new_dim-new_nparam > 0) {
	sum = sum_over_polytope(P, E, new_dim-new_nparam, data, options);
	evalue_free(E);
	Polyhedron_Free(P);
    } else {
	sum = ALLOC(evalue);
	value_init(sum->d);
	sum->x.p = new_enode(partition, 2, new_dim);
	EVALUE_SET_DOMAIN(sum->x.p->arr[0], P);
	value_clear(sum->x.p->arr[1].d);
	sum->x.p->arr[1] = *E;
	free(E);
    }

    if (CP) {
	evalue_backsubstitute(sum, CP, options->MaxRays);
	Matrix_Free(CP);
    }

    if (T)
	Matrix_Free(T);

    return sum;
}

/* Splinter P into period slices along the first variable x = period y + c,
 * 0 <= c < perdiod, * ensuring no fractional part contains the first variable,
 * and sum over all slices.
 */
static evalue *sum_over_polytope_slices(Polyhedron *P, evalue *E,
					unsigned nvar,
					Value period,
					struct Bernoulli_data *data,
					struct barvinok_options *options)
{
    evalue *sum = evalue_zero();
    evalue **subs;
    unsigned dim = P->Dimension;
    Matrix *T;
    Value i;
    Value one;
    int j;

    value_init(i);
    value_init(one);
    value_set_si(one, 1);

    subs = ALLOCN(evalue *, dim);

    T = Matrix_Alloc(dim+1, dim+1);
    value_assign(T->p[0][0], period);
    for (j = 1; j < dim+1; ++j)
	value_set_si(T->p[j][j], 1);

    for (j = 0; j < dim; ++j)
	subs[j] = evalue_var(j);
    evalue_mul(subs[0], period);

    for (value_set_si(i, 0); value_lt(i, period); value_increment(i, i)) {
	evalue *tmp;
	Polyhedron *S = DomainPreimage(P, T, options->MaxRays);
	evalue *e = evalue_dup(E);
	evalue_substitute(e, subs);
	reduce_evalue(e);

	if (S->NbEq)
	    tmp = sum_over_polytope_with_equalities(S, e, nvar, data, options);
	else
	    tmp = sum_over_polytope_base(S, e, nvar, data, options);

	assert(tmp);
	eadd(tmp, sum);
	evalue_free(tmp);

	Domain_Free(S);
	evalue_free(e);

	value_increment(T->p[0][dim], T->p[0][dim]);
	evalue_add_constant(subs[0], one);
    }

    value_clear(i);
    value_clear(one);
    Matrix_Free(T);
    for (j = 0; j < dim; ++j)
	evalue_free(subs[j]);
    free(subs);

    reduce_evalue(sum);
    return sum;
}

static evalue *sum_over_polytope(Polyhedron *P, evalue *E, unsigned nvar,
				 struct Bernoulli_data *data,
				 struct barvinok_options *options)
{
    Polyhedron *P_orig = P;
    evalue *E_orig = E;
    Value period;
    evalue *sum;
    int exact = options->approximation_method == BV_APPROX_NONE;

    if (P->NbEq)
	return sum_over_polytope_with_equalities(P, E, nvar, data, options);

    value_init(period);

    move_best_to_front(&P, &E, nvar, exact ? &period : NULL);
    if (exact && value_notone_p(period))
	sum = sum_over_polytope_slices(P, E, nvar, period, data, options);
    else
	sum = sum_over_polytope_base(P, E, nvar, data, options);

    if (P != P_orig)
	Polyhedron_Free(P);
    if (E != E_orig)
	evalue_free(E);

    value_clear(period);

    return sum;
}

evalue *Bernoulli_sum_evalue(evalue *e, unsigned nvar,
			 struct barvinok_options *options)
{
    struct Bernoulli_data data;
    int i, j;
    evalue *sum = evalue_zero();

    if (EVALUE_IS_ZERO(*e))
	return sum;

    if (nvar == 0) {
	eadd(e, sum);
	return sum;
    }

    assert(value_zero_p(e->d));
    assert(e->x.p->type == partition);

    data.size = 16;
    data.s = ALLOCN(struct evalue_section, data.size);
    data.options = options;

    for (i = 0; i < e->x.p->size/2; ++i) {
	Polyhedron *D;
	for (D = EVALUE_DOMAIN(e->x.p->arr[2*i]); D; D = D->next) {
	    Polyhedron *next = D->next;
	    evalue *tmp;
	    D->next = NULL;

	    tmp = sum_over_polytope(D, &e->x.p->arr[2*i+1], nvar, &data, options);
	    assert(tmp);
	    eadd(tmp, sum);
	    evalue_free(tmp);

	    D->next = next;
	}
    }

    free(data.s);

    reduce_evalue(sum);
    return sum;
}

evalue *Bernoulli_sum(Polyhedron *P, Polyhedron *C,
			struct barvinok_options *options)
{
    Polyhedron *CA, *D;
    evalue e;
    evalue *sum;

    if (emptyQ(P) || emptyQ(C))
	return evalue_zero();

    CA = align_context(C, P->Dimension, options->MaxRays);
    D = DomainIntersection(P, CA, options->MaxRays);
    Domain_Free(CA);

    if (emptyQ(D)) {
	Domain_Free(D);
	return evalue_zero();
    }

    value_init(e.d);
    e.x.p = new_enode(partition, 2, P->Dimension);
    EVALUE_SET_DOMAIN(e.x.p->arr[0], D);
    evalue_set_si(&e.x.p->arr[1], 1, 1);
    sum = Bernoulli_sum_evalue(&e, P->Dimension - C->Dimension, options);
    free_evalue_refs(&e);
    return sum;
}
