#include <assert.h>
#include <stdlib.h>
#include <iostream>
#include <vector>
#include <gmp.h>
#include <NTL/mat_ZZ.h>
#include <NTL/LLL.h>
#include <barvinok.h>
#include <util.h>
extern "C" {
#include <polylib/polylibgmp.h>
}

using namespace NTL;
using std::cout;
using std::endl;
using std::vector;

#define ALLOC(p) (((long *) (p))[0])
#define SIZE(p) (((long *) (p))[1])
#define DATA(p) ((mp_limb_t *) (((long *) (p)) + 2))

static void value2zz(Value v, ZZ& z)
{
    int sa = v[0]._mp_size;
    int abs_sa = sa < 0 ? -sa : sa;

    _ntl_gsetlength(&z.rep, abs_sa);
    mp_limb_t * adata = DATA(z.rep);
    for (int i = 0; i < abs_sa; ++i)
	adata[i] = v[0]._mp_d[i];
    SIZE(z.rep) = sa;
}

static void zz2value(ZZ& z, Value& v)
{
    if (!z.rep) {
	value_set_si(v, 0);
	return;
    }

    int sa = SIZE(z.rep);
    int abs_sa = sa < 0 ? -sa : sa;

    mp_limb_t * adata = DATA(z.rep);
    mpz_realloc2(v, __GMP_BITS_PER_MP_LIMB * abs_sa);
    for (int i = 0; i < abs_sa; ++i)
	v[0]._mp_d[i] = adata[i];
    v[0]._mp_size = sa;
}

/*
 * We just ignore the last column and row
 * If the final element is not equal to one
 * then the result will actually be a multiple of the input
 */
static void matrix2zz(Matrix *M, mat_ZZ& m)
{
    m.SetDims(M->NbRows - 1, M->NbColumns - 1);

    for (int i = 0; i < M->NbRows-1; ++i) {
//	assert(value_one_p(M->p[i][M->NbColumns - 1]));
	for (int j = 0; j < M->NbColumns - 1; ++j) {
	    value2zz(M->p[i][j], m[i][j]);
	}
    }
}

static void values2zz(Value *p, vec_ZZ& v, int len)
{
    v.SetLength(len);

    for (int i = 0; i < len; ++i) {
	value2zz(p[i], v[i]);
    }
}

/*
 * We add a 0 at the end, because we need it afterwards
 */
static Vector * zz2vector(vec_ZZ& v)
{
    Vector *vec = Vector_Alloc(v.length()+1);
    assert(vec);
    for (int i = 0; i < v.length(); ++i)
	zz2value(v[i], vec->p[i]);

    value_set_si(vec->p[v.length()], 0);

    return vec;
}

static void rays(mat_ZZ& r, Polyhedron *C)
{
    unsigned dim = C->NbRays - 1; /* don't count zero vertex */
    assert(C->NbRays - 1 == C->Dimension);
    r.SetDims(dim, dim);
    ZZ tmp;

    int i, c;
    for (i = 0, c = 0; i < dim; ++i)
	if (value_zero_p(C->Ray[i][dim+1])) {
	    for (int j = 0; j < dim; ++j) {
		value2zz(C->Ray[i][j+1], tmp);
		r[j][c] = tmp;
	    }
	    ++c;
	}
}

static Matrix * rays(Polyhedron *C)
{
    unsigned dim = C->NbRays - 1; /* don't count zero vertex */
    assert(C->NbRays - 1 == C->Dimension);

    Matrix *M = Matrix_Alloc(dim+1, dim+1);
    assert(M);

    int i, c;
    for (i = 0, c = 0; i < dim; ++i)
	if (value_zero_p(C->Ray[i][dim+1])) {
	    Vector_Copy(C->Ray[i] + 1, M->p[c], dim);
	    value_set_si(M->p[c++][dim], 0);
	}
    value_set_si(M->p[dim][dim], 1);

    return M;
}

/*
 * Returns the largest absolute value in the vector
 */
static ZZ max(vec_ZZ& v)
{
    ZZ max = abs(v[0]);
    for (int i = 1; i < v.length(); ++i)
	if (abs(v[i]) > max)
	    max = abs(v[i]);
    return max;
}

class cone {
public:
    cone(Matrix *M) {
	Cone = 0;
	Rays = Matrix_Copy(M);
	set_det();
    }
    cone(Polyhedron *C) {
	Cone = Polyhedron_Copy(C);
	Rays = rays(C);
	set_det();
    }
    void set_det() {
	mat_ZZ A;
	matrix2zz(Rays, A);
	det = determinant(A);
	Value v;
	value_init(v);
	zz2value(det, v);
	value_clear(v);
    }

    Vector* short_vector() {
	Matrix *M = Matrix_Copy(Rays);
	Matrix *inv = Matrix_Alloc(M->NbRows, M->NbColumns);
	int ok = Matrix_Inverse(M, inv);
	assert(ok);
	Matrix_Free(M);

	ZZ det2;
	mat_ZZ B;
	mat_ZZ U;
	matrix2zz(inv, B);
	long r = LLL(det2, B, U);

	ZZ min = max(U[0]);
	int index = 0;
	for (int i = 1; i < U.NumRows(); ++i) {
	    ZZ tmp = max(U[1]);
	    if (tmp < min) {
		min = tmp;
		index = i;
	    }
	}

	Matrix_Free(inv);
	return zz2vector(U[index]);
    }

    ~cone() {
	Polyhedron_Free(Cone);
	Matrix_Free(Rays);
    }

    Polyhedron *poly() {
	if (!Cone) {
	    Matrix *M = Matrix_Alloc(Rays->NbRows+1, Rays->NbColumns+1);
	    for (int i = 0; i < Rays->NbRows; ++i) {
		Vector_Copy(Rays->p[i], M->p[i]+1, Rays->NbColumns);
		value_set_si(M->p[i][0], 1);
	    }
	    Vector_Set(M->p[Rays->NbRows]+1, 0, Rays->NbColumns-1);
	    value_set_si(M->p[Rays->NbRows][0], 1);
	    value_set_si(M->p[Rays->NbRows][Rays->NbColumns], 1);
	    Cone = Rays2Polyhedron(M, 600);
	    Matrix_Free(M);
	}
	return Cone;
    }

    ZZ det;
    Polyhedron *Cone;
    Matrix *Rays;
};

class dpoly {
public:
    vec_ZZ coeff;
    dpoly(int d, ZZ& degree, int offset = 0) {
	coeff.SetLength(d+1);

	int min = d;
	if (degree < ZZ(INIT_VAL, d))
	    min = to_int(degree);

	ZZ c = ZZ(INIT_VAL, 1);
	if (!offset)
	    coeff[0] = c;
	for (int i = 1; i <= min; ++i) {
	    c *= (degree -i + 1);
	    c /= i;
	    coeff[i-offset] = c;
	}
    }
    void operator *= (dpoly& f) {
	assert(coeff.length() == f.coeff.length());
	vec_ZZ old = coeff;
	coeff = f.coeff[0] * coeff;
	for (int i = 1; i < coeff.length(); ++i)
	    for (int j = 0; i+j < coeff.length(); ++j)
		coeff[i+j] += f.coeff[i] * old[j];
    }
    void div(dpoly& d, mpq_t count, ZZ& sign) {
	int len = coeff.length();
	Value tmp;
	value_init(tmp);
	mpq_t* c = new mpq_t[coeff.length()];
	mpq_t qtmp;
	mpq_init(qtmp);
	for (int i = 0; i < len; ++i) {
	    mpq_init(c[i]);
	    zz2value(coeff[i], tmp);
	    mpq_set_z(c[i], tmp);

	    for (int j = 1; j <= i; ++j) {
		zz2value(d.coeff[j], tmp);
		mpq_set_z(qtmp, tmp);
		mpq_mul(qtmp, qtmp, c[i-j]);
		mpq_sub(c[i], c[i], qtmp);
	    }

	    zz2value(d.coeff[0], tmp);
	    mpq_set_z(qtmp, tmp);
	    mpq_div(c[i], c[i], qtmp);
	}
	if (sign == -1)
	    mpq_sub(count, count, c[len-1]);
	else
	    mpq_add(count, count, c[len-1]);

	value_clear(tmp);
	mpq_clear(qtmp);
	for (int i = 0; i < len; ++i)
	    mpq_clear(c[i]);
	delete [] c;
    }
};

/*
 * Barvinok's Decomposition of a simplicial cone
 *
 * Returns two lists of polyhedra
 */
void decompose(Polyhedron *C, Polyhedron **ppos, Polyhedron **pneg)
{
    Polyhedron *pos = 0, *neg = 0;
    vector<cone *> nonuni;
    cone * c = new cone(C);
    ZZ det = c->det;
    if (abs(det) > 1) {
	nonuni.push_back(c);
    } else {
	if (det > 0) 
	    pos = Polyhedron_Copy(c->Cone);
	else
	    neg = Polyhedron_Copy(c->Cone);
	delete c;
    }
    while (!nonuni.empty()) {
	c = nonuni.back();
	nonuni.pop_back();
	Vector* v = c->short_vector();
	for (int i = 0; i < c->Rays->NbRows - 1; ++i) {
	    Matrix* M = Matrix_Copy(c->Rays);
	    Vector_Copy(v->p, M->p[i], v->Size);
	    cone * pc = new cone(M);
	    if (abs(pc->det) > 1)
		nonuni.push_back(pc);
	    else {
		Polyhedron *p = Polyhedron_Copy(pc->poly());
		if (pc->det > 0) {
		    p->next = pos;
		    pos = p;
		} else {
		    p->next = neg;
		    neg = p;
		}
		delete pc;
	    }
	    Matrix_Free(M);
	}
	Vector_Free(v);
	delete c;
    }
    if (det > 0) {
	*ppos = pos;
	*pneg = neg;
    } else {
	*ppos = neg;
	*pneg = pos;
    }
}

static int rand(int max) {
    return (int) (((double)(max))*rand()/(RAND_MAX+1.0));
}

const int MAX_TRY=10;
/*
 * Searches for a vector that is not othogonal to any
 * of the rays in rays.
 */
static void nonorthog(mat_ZZ& rays, vec_ZZ& lambda)
{
    int dim = rays.NumCols();
    bool found = false;
    lambda.SetLength(dim);
    for (int i = 2; !found && i <= 2*dim; i+=2) {
	for (int j = 0; j < MAX_TRY; ++j) {
	    for (int k = 0; k < dim; ++k) {
		int r = rand(i)+2;
		int v = (2*(r%2)-1) * (r >> 1);
		lambda[k] = v;
	    }
	    int k = 0;
	    for (; k < rays.NumRows(); ++k)
		if (lambda * rays[k] == 0)
		    break;
	    if (k == rays.NumRows()) {
		found = true;
		break;
	    }
	}
    }
    assert(found);
}

static void add_rays(mat_ZZ& rays, Polyhedron *i, int *r)
{
    unsigned dim = i->Dimension;
    for (int k = 0; k < i->NbRays; ++k) {
	if (!value_zero_p(i->Ray[k][dim+1]))
	    continue;
	values2zz(i->Ray[k]+1, rays[(*r)++], dim);
    }
}

void normalize(vec_ZZ& vertex, Polyhedron *i, vec_ZZ& lambda, 
	       ZZ& sign, ZZ& num, vec_ZZ& den)
{
    unsigned dim = i->Dimension;
    int r = 0;
    mat_ZZ rays;
    rays.SetDims(dim, dim);
    add_rays(rays, i, &r);
    den = rays * lambda;
    num = vertex * lambda;
    int change = 0;

    for (int j = 0; j < den.length(); ++j) {
	if (den[j] > 0)
	    change ^= 1;
	else {
	    den[j] = abs(den[j]);
	    num += den[j];
	}
    }
    if (change)
	sign = -sign;
}

void count(Polyhedron *P, Value* result)
{
    Polyhedron ** vpos = new (Polyhedron *)[P->NbRays];
    Polyhedron ** vneg = new (Polyhedron *)[P->NbRays];

    int nrays = 0;
    unsigned dim = P->Dimension;

    for (int j = 0; j < P->NbRays; ++j) {
	Polyhedron *C = supporting_cone(P, j, 600);
	printf("%d:\n", j);
	Polyhedron_Print(stdout, P_VALUE_FMT, C);
	Polyhedron *Polar = Polyhedron_Polar(C, 600);
	Polyhedron_Free(C);
	Polyhedron_Print(stdout, P_VALUE_FMT, Polar);

	Polyhedron *polpos, *polneg;
	decompose(Polar, &polpos, &polneg);
	Polyhedron_Free(Polar);

	puts("Pos:");
	Polyhedron_Print(stdout, P_VALUE_FMT, polpos);

	puts("Neg:");
	Polyhedron_Print(stdout, P_VALUE_FMT, polneg);

	Polyhedron *pos = NULL, *neg = NULL;
	for (Polyhedron *i = polpos; i; i = i->next) {
	    Polyhedron *A = Polyhedron_Polar(i, 600);
	    A->next = pos;
	    pos = A;
	    assert(A->NbRays-1 == dim);
	    nrays += A->NbRays - 1;
	}
	for (Polyhedron *i = polneg; i; i = i->next) {
	    Polyhedron *A = Polyhedron_Polar(i, 600);
	    A->next = neg;
	    neg = A;
	    assert(A->NbRays-1 == dim);
	    nrays += A->NbRays - 1;
	}
	Domain_Free(polpos);
	Domain_Free(polneg);

	puts("Pos:");
	Polyhedron_Print(stdout, P_VALUE_FMT, pos);

	puts("Neg:");
	Polyhedron_Print(stdout, P_VALUE_FMT, neg);

	vpos[j] = pos;
	vneg[j] = neg;
    }

    cout << nrays << endl;

    mat_ZZ rays;
    rays.SetDims(nrays, dim);
    int r = 0;
    for (int j = 0; j < P->NbRays; ++j) {
	for (Polyhedron *i = vpos[j]; i; i = i->next)
	    add_rays(rays, i, &r);
	for (Polyhedron *i = vneg[j]; i; i = i->next)
	    add_rays(rays, i, &r);
    }
    cout << rays;
    vec_ZZ lambda;
    nonorthog(rays, lambda);
    cout << lambda;
    cout << rays * lambda;

    vec_ZZ sign;
    vec_ZZ num;
    mat_ZZ den;
    sign.SetLength(nrays/dim);
    num.SetLength(nrays/dim);
    den.SetDims(nrays/dim,dim);

    int f = 0;
    for (int j = 0; j < P->NbRays; ++j) {
	vec_ZZ vertex;
	values2zz(P->Ray[j]+1, vertex, dim);
	for (Polyhedron *i = vpos[j]; i; i = i->next) {
	    sign[f] = 1;
	    normalize(vertex, i, lambda, sign[f], num[f], den[f]);
	    ++f;
	}
	for (Polyhedron *i = vneg[j]; i; i = i->next) {
	    sign[f] = -1;
	    normalize(vertex, i, lambda, sign[f], num[f], den[f]);
	    ++f;
	}
    }
    ZZ min = num[0];
    for (int j = 1; j < num.length(); ++j)
	if (num[j] < min)
	    min = num[j];
    for (int j = 0; j < num.length(); ++j)
	num[j] -= min;
    cout << sign;
    cout << num;
    cout << den;

    f = 0;
    mpq_t count;
    mpq_init(count);
    for (int j = 0; j < P->NbRays; ++j) {
	for (Polyhedron *i = vpos[j]; i; i = i->next) {
	    dpoly d(dim, num[f]);
	    dpoly n(dim, den[f][0], 1);
	    for (int k = 1; k < dim; ++k) {
		dpoly f(dim, den[f][k], 1);
		n *= f;
	    }
	    cout << d.coeff << "/" << n.coeff;
	    d.div(n, count, sign[f]);
	    mpq_out_str(stdout, 10, count);
	    ++f;
	}
	for (Polyhedron *i = vneg[j]; i; i = i->next) {
	    dpoly d(dim, num[f]);
	    dpoly n(dim, den[f][0], 1);
	    for (int k = 1; k < dim; ++k) {
		dpoly f(dim, den[f][k], 1);
		n *= f;
	    }
	    cout << d.coeff << "/" << n.coeff;
	    d.div(n, count, sign[f]);
	    mpq_out_str(stdout, 10, count);
	    ++f;
	}
    }
    assert(value_one_p(&count[0]._mp_den));
    value_assign(*result, &count[0]._mp_num);
    mpq_clear(count);

    for (int j = 0; j < P->NbRays; ++j) {
	Domain_Free(vpos[j]);
	Domain_Free(vneg[j]);
    }

    delete [] vpos;
    delete [] vneg;
}
