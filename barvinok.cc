#include <assert.h>
#include <iostream>
#include <vector>
#include <deque>
#include <string>
#include <sstream>
#include <gmp.h>
#include <NTL/mat_ZZ.h>
#include <NTL/LLL.h>
#include <util.h>
extern "C" {
#include <polylib/polylibgmp.h>
#include "ev_operations.h"
#include "piputil.h"
}
#include "config.h"
#include <barvinok.h>
#include <genfun.h>

#ifdef NTL_STD_CXX
using namespace NTL;
#endif
using std::cout;
using std::endl;
using std::vector;
using std::deque;
using std::string;
using std::ostringstream;

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

void zz2value(ZZ& z, Value& v)
{
    if (!z.rep) {
	value_set_si(v, 0);
	return;
    }

    int sa = SIZE(z.rep);
    int abs_sa = sa < 0 ? -sa : sa;

    mp_limb_t * adata = DATA(z.rep);
    _mpz_realloc(v, abs_sa);
    for (int i = 0; i < abs_sa; ++i)
	v[0]._mp_d[i] = adata[i];
    v[0]._mp_size = sa;
}

#undef ALLOC
#define ALLOC(t,p) p = (t*)malloc(sizeof(*p))

/*
 * We just ignore the last column and row
 * If the final element is not equal to one
 * then the result will actually be a multiple of the input
 */
static void matrix2zz(Matrix *M, mat_ZZ& m, unsigned nr, unsigned nc)
{
    m.SetDims(nr, nc);

    for (int i = 0; i < nr; ++i) {
//	assert(value_one_p(M->p[i][M->NbColumns - 1]));
	for (int j = 0; j < nc; ++j) {
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
 */
static void zz2values(vec_ZZ& v, Value *p)
{
    for (int i = 0; i < v.length(); ++i)
	zz2value(v[i], p[i]);
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
    for (i = 0, c = 0; i <= dim && c < dim; ++i)
	if (value_zero_p(C->Ray[i][dim+1])) {
	    Vector_Copy(C->Ray[i] + 1, M->p[c], dim);
	    value_set_si(M->p[c++][dim], 0);
	}
    assert(c == dim);
    value_set_si(M->p[dim][dim], 1);

    return M;
}

static Matrix * rays2(Polyhedron *C)
{
    unsigned dim = C->NbRays - 1; /* don't count zero vertex */
    assert(C->NbRays - 1 == C->Dimension);

    Matrix *M = Matrix_Alloc(dim, dim);
    assert(M);

    int i, c;
    for (i = 0, c = 0; i <= dim && c < dim; ++i)
	if (value_zero_p(C->Ray[i][dim+1]))
	    Vector_Copy(C->Ray[i] + 1, M->p[c++], dim);
    assert(c == dim);

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
	matrix2zz(Rays, A, Rays->NbRows - 1, Rays->NbColumns - 1);
	det = determinant(A);
    }

    Vector* short_vector(vec_ZZ& lambda) {
	Matrix *M = Matrix_Copy(Rays);
	Matrix *inv = Matrix_Alloc(M->NbRows, M->NbColumns);
	int ok = Matrix_Inverse(M, inv);
	assert(ok);
	Matrix_Free(M);

	ZZ det2;
	mat_ZZ B;
	mat_ZZ U;
	matrix2zz(inv, B, inv->NbRows - 1, inv->NbColumns - 1);
	long r = LLL(det2, B, U);

	ZZ min = max(B[0]);
	int index = 0;
	for (int i = 1; i < B.NumRows(); ++i) {
	    ZZ tmp = max(B[i]);
	    if (tmp < min) {
		min = tmp;
		index = i;
	    }
	}

	Matrix_Free(inv);

	lambda = B[index];

	Vector *z = Vector_Alloc(U[index].length()+1);
	assert(z);
	zz2values(U[index], z->p);
	value_set_si(z->p[U[index].length()], 0);

	Value tmp;
	value_init(tmp);
	Polyhedron *C = poly();
	int i;
	for (i = 0; i < C->NbConstraints; ++i) {
	    Inner_Product(z->p, C->Constraint[i]+1, z->Size-1, &tmp);
	    if (value_pos_p(tmp))
		break;
	}
	if (i == C->NbConstraints) {
	    value_set_si(tmp, -1);
	    Vector_Scale(z->p, z->p, tmp, z->Size-1);
	}
	value_clear(tmp);
	return z;
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
	    Cone = Rays2Polyhedron(M, M->NbRows+1);
	    assert(Cone->NbConstraints == Cone->NbRays);
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

	int min = d + offset;
	if (degree >= 0 && degree < ZZ(INIT_VAL, min))
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

class dpoly_n {
public:
    Matrix *coeff;
    ~dpoly_n() {
	Matrix_Free(coeff);
    }
    dpoly_n(int d, ZZ& degree_0, ZZ& degree_1, int offset = 0) {
	Value d0, d1;
	value_init(d0);
	value_init(d1);
	zz2value(degree_0, d0);
	zz2value(degree_1, d1);
	coeff = Matrix_Alloc(d+1, d+1+1);
	value_set_si(coeff->p[0][0], 1);
	value_set_si(coeff->p[0][d+1], 1);
	for (int i = 1; i <= d; ++i) {
	    value_multiply(coeff->p[i][0], coeff->p[i-1][0], d0);
	    Vector_Combine(coeff->p[i-1], coeff->p[i-1]+1, coeff->p[i]+1,
			   d1, d0, i);
	    value_set_si(coeff->p[i][d+1], i);
	    value_multiply(coeff->p[i][d+1], coeff->p[i][d+1], coeff->p[i-1][d+1]);
	    value_decrement(d0, d0);
	}
	value_clear(d0);
	value_clear(d1);
    }
    void div(dpoly& d, Vector *count, ZZ& sign) {
	int len = coeff->NbRows;
	Matrix * c = Matrix_Alloc(coeff->NbRows, coeff->NbColumns);
	Value tmp;
	value_init(tmp);
	for (int i = 0; i < len; ++i) {
	    Vector_Copy(coeff->p[i], c->p[i], len+1);
	    for (int j = 1; j <= i; ++j) {
		zz2value(d.coeff[j], tmp);
		value_multiply(tmp, tmp, c->p[i][len]);
		value_oppose(tmp, tmp);
		Vector_Combine(c->p[i], c->p[i-j], c->p[i],
			       c->p[i-j][len], tmp, len);
		value_multiply(c->p[i][len], c->p[i][len], c->p[i-j][len]);
	    }
	    zz2value(d.coeff[0], tmp);
	    value_multiply(c->p[i][len], c->p[i][len], tmp);
	}
	if (sign == -1) {
	    value_set_si(tmp, -1);
	    Vector_Scale(c->p[len-1], count->p, tmp, len);
	    value_assign(count->p[len], c->p[len-1][len]);
	} else
	    Vector_Copy(c->p[len-1], count->p, len+1);
	Vector_Normalize(count->p, len+1);
	value_clear(tmp);
	Matrix_Free(c);
    }
};

struct dpoly_r_term {
    int	    *powers;
    ZZ	    coeff;
};

/* len: number of elements in c
 * each element in c is the coefficient of a power of t
 * in the MacLaurin expansion
 */
struct dpoly_r {
    vector< dpoly_r_term * >	*c;
    int len;
    int dim;
    ZZ denom;

    void add_term(int i, int * powers, ZZ& coeff) {
	for (int k = 0; k < c[i].size(); ++k) {
	    if (memcmp(c[i][k]->powers, powers, dim * sizeof(int)) == 0) {
		c[i][k]->coeff += coeff;
		return;
	    }
	}
	dpoly_r_term *t = new dpoly_r_term;
	t->powers = new int[dim];
	memcpy(t->powers, powers, dim * sizeof(int));
	t->coeff = coeff;
	c[i].push_back(t);
    }
    dpoly_r(int len, int dim) {
	denom = 1;
	this->len = len;
	this->dim = dim;
	c = new vector< dpoly_r_term * > [len];
    }
    dpoly_r(dpoly& num, dpoly& den, int pos, int sign, int dim) {
	denom = 1;
	len = num.coeff.length();
	c = new vector< dpoly_r_term * > [len];
	this->dim = dim;
	int powers[dim];

	for (int i = 0; i < len; ++i) {
	    ZZ coeff = num.coeff[i];
	    memset(powers, 0, dim * sizeof(int));
	    powers[pos] = sign;

	    add_term(i, powers, coeff);

	    for (int j = 1; j <= i; ++j) {
		for (int k = 0; k < c[i-j].size(); ++k) {
		    memcpy(powers, c[i-j][k]->powers, dim*sizeof(int));
		    powers[pos] += sign;
		    coeff = -den.coeff[j-1] * c[i-j][k]->coeff;
		    add_term(i, powers, coeff);
		}
	    }
	}
	//dump();
    }
    dpoly_r *div(dpoly& d) {
	dpoly_r *rc = new dpoly_r(len, dim);
	rc->denom = power(d.coeff[0], len);
	ZZ inv_d = rc->denom / d.coeff[0];
	ZZ coeff;

	for (int i = 0; i < len; ++i) {
	    for (int k = 0; k < c[i].size(); ++k) {
		coeff = c[i][k]->coeff * inv_d;
		rc->add_term(i, c[i][k]->powers, coeff);
	    }

	    for (int j = 1; j <= i; ++j) {
		for (int k = 0; k < rc->c[i-j].size(); ++k) {
		    coeff = - d.coeff[j] * rc->c[i-j][k]->coeff / d.coeff[0];
		    rc->add_term(i, rc->c[i-j][k]->powers, coeff);
		}
	    }
	}
	return rc;
    }
    void div(dpoly& d, ZZ& sign, gen_fun *gf, mat_ZZ& pden, mat_ZZ& den,
		vec_ZZ& num_p) {
	dpoly_r * rc = div(d);
	//rc.dump();
	int common = pden.NumRows();

	vector< dpoly_r_term * >& final = rc->c[len-1];
	int rows;
	for (int j = 0; j < final.size(); ++j) {
	    rows = common;
	    pden.SetDims(rows, pden.NumCols());
	    for (int k = 0; k < dim; ++k) {
		int n = final[j]->powers[k];
		if (n == 0)
		    continue;
		int abs_n = n < 0 ? -n : n;
		pden.SetDims(rows+abs_n, pden.NumCols());
		for (int l = 0; l < abs_n; ++l) {
		    if (n > 0)
			pden[rows+l] = den[k];
		    else
			pden[rows+l] = -den[k];
		}
		rows += abs_n;
	    }
	    final[j]->coeff *= sign;
	    gf->add(final[j]->coeff, rc->denom, num_p, pden);
	}
	delete rc;
    }
    void dump(void) {
	for (int i = 0; i < len; ++i) {
	    cout << endl;
	    cout << i << endl;
	    cout << c[i].size() << endl;
	    for (int j = 0; j < c[i].size(); ++j) {
		for (int k = 0; k < dim; ++k) {
		    cout << c[i][j]->powers[k] << " ";
		}
		cout << ": " << c[i][j]->coeff << endl;
	    }
	    cout << endl;
	}
    }
};

struct decomposer {
    void decompose(Polyhedron *C);
    virtual void handle(Polyhedron *P, int sign) = 0;
};

struct polar_decomposer : public decomposer {
    void decompose(Polyhedron *C, unsigned MaxRays);
    virtual void handle(Polyhedron *P, int sign);
    virtual void handle_polar(Polyhedron *P, int sign) = 0;
};

void decomposer::decompose(Polyhedron *C)
{
    vector<cone *> nonuni;
    cone * c = new cone(C);
    ZZ det = c->det;
    int s = sign(det);
    assert(det != 0);
    if (abs(det) > 1) {
	nonuni.push_back(c);
    } else {
	handle(C, 1);
	delete c;
    }
    vec_ZZ lambda;
    while (!nonuni.empty()) {
	c = nonuni.back();
	nonuni.pop_back();
	Vector* v = c->short_vector(lambda);
	for (int i = 0; i < c->Rays->NbRows - 1; ++i) {
	    if (lambda[i] == 0)
		continue;
	    Matrix* M = Matrix_Copy(c->Rays);
	    Vector_Copy(v->p, M->p[i], v->Size);
	    cone * pc = new cone(M);
	    assert (pc->det != 0);
	    if (abs(pc->det) > 1) {
		assert(abs(pc->det) < abs(c->det));
		nonuni.push_back(pc);
	    } else {
		handle(pc->poly(), sign(pc->det) * s);
		delete pc;
	    }
	    Matrix_Free(M);
	}
	Vector_Free(v);
	delete c;
    }
}

void polar_decomposer::decompose(Polyhedron *cone, unsigned MaxRays)
{
    Polyhedron_Polarize(cone);
    if (cone->NbRays - 1 != cone->Dimension) {
	Polyhedron *tmp = cone;
	cone = triangularize_cone(cone, MaxRays);
	Polyhedron_Free(tmp);
    }
    for (Polyhedron *Polar = cone; Polar; Polar = Polar->next)
	decomposer::decompose(Polar);
    Domain_Free(cone);
}

void polar_decomposer::handle(Polyhedron *P, int sign)
{
    Polyhedron_Polarize(P);
    handle_polar(P, sign);
}

/*
 * Barvinok's Decomposition of a simplicial cone
 *
 * Returns two lists of polyhedra
 */
void barvinok_decompose(Polyhedron *C, Polyhedron **ppos, Polyhedron **pneg)
{
    Polyhedron *pos = *ppos, *neg = *pneg;
    vector<cone *> nonuni;
    cone * c = new cone(C);
    ZZ det = c->det;
    int s = sign(det);
    assert(det != 0);
    if (abs(det) > 1) {
	nonuni.push_back(c);
    } else {
	Polyhedron *p = Polyhedron_Copy(c->Cone);
	p->next = pos;
	pos = p;
	delete c;
    }
    vec_ZZ lambda;
    while (!nonuni.empty()) {
	c = nonuni.back();
	nonuni.pop_back();
	Vector* v = c->short_vector(lambda);
	for (int i = 0; i < c->Rays->NbRows - 1; ++i) {
	    if (lambda[i] == 0)
		continue;
	    Matrix* M = Matrix_Copy(c->Rays);
	    Vector_Copy(v->p, M->p[i], v->Size);
	    cone * pc = new cone(M);
	    assert (pc->det != 0);
	    if (abs(pc->det) > 1) {
		assert(abs(pc->det) < abs(c->det));
		nonuni.push_back(pc);
	    } else {
		Polyhedron *p = pc->poly();
		pc->Cone = 0;
		if (sign(pc->det) == s) {
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
    *ppos = pos;
    *pneg = neg;
}

/*
 *  Returns a single list of npos "positive" cones followed by nneg
 *  "negative" cones.
 *  The input cone is freed
 */
void decompose(Polyhedron *cone, Polyhedron **parts, int *npos, int *nneg, unsigned MaxRays)
{
    Polyhedron_Polarize(cone);
    if (cone->NbRays - 1 != cone->Dimension) {
	Polyhedron *tmp = cone;
	cone = triangularize_cone(cone, MaxRays);
	Polyhedron_Free(tmp);
    }
    Polyhedron *polpos = NULL, *polneg = NULL;
    *npos = 0; *nneg = 0;
    for (Polyhedron *Polar = cone; Polar; Polar = Polar->next)
	barvinok_decompose(Polar, &polpos, &polneg);

    Polyhedron *last;
    for (Polyhedron *i = polpos; i; i = i->next) {
	Polyhedron_Polarize(i);
	++*npos;
	last = i;
    }
    for (Polyhedron *i = polneg; i; i = i->next) {
	Polyhedron_Polarize(i);
	++*nneg;
    }
    if (last) {
	last->next = polneg;
	*parts = polpos;
    } else
	*parts = polneg;
    Domain_Free(cone);
}

const int MAX_TRY=10;
/*
 * Searches for a vector that is not orthogonal to any
 * of the rays in rays.
 */
static void nonorthog(mat_ZZ& rays, vec_ZZ& lambda)
{
    int dim = rays.NumCols();
    bool found = false;
    lambda.SetLength(dim);
    if (dim == 0)
	return;

    for (int i = 2; !found && i <= 50*dim; i+=4) {
	for (int j = 0; j < MAX_TRY; ++j) {
	    for (int k = 0; k < dim; ++k) {
		int r = random_int(i)+2;
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

static void randomvector(Polyhedron *P, vec_ZZ& lambda, int nvar)
{
    Value tmp;
    int max = 10 * 16;
    unsigned int dim = P->Dimension;
    value_init(tmp);

    for (int i = 0; i < P->NbRays; ++i) {
	for (int j = 1; j <= dim; ++j) {
	    value_absolute(tmp, P->Ray[i][j]);
	    int t = VALUE_TO_LONG(tmp) * 16;
	    if (t > max)
		max = t;
	}
    }
    for (int i = 0; i < P->NbConstraints; ++i) {
	for (int j = 1; j <= dim; ++j) {
	    value_absolute(tmp, P->Constraint[i][j]);
	    int t = VALUE_TO_LONG(tmp) * 16;
	    if (t > max)
		max = t;
	}
    }
    value_clear(tmp);

    lambda.SetLength(nvar);
    for (int k = 0; k < nvar; ++k) {
	int r = random_int(max*dim)+2;
	int v = (2*(r%2)-1) * (max/2*dim + (r >> 1));
	lambda[k] = v;
    }
}

static void add_rays(mat_ZZ& rays, Polyhedron *i, int *r, int nvar = -1, 
		     bool all = false)
{
    unsigned dim = i->Dimension;
    if (nvar == -1)
	nvar = dim;
    for (int k = 0; k < i->NbRays; ++k) {
	if (!value_zero_p(i->Ray[k][dim+1]))
	    continue;
	if (!all && nvar != dim && First_Non_Zero(i->Ray[k]+1, nvar) == -1)
	    continue;
	values2zz(i->Ray[k]+1, rays[(*r)++], nvar);
    }
}

void lattice_point(Value* values, Polyhedron *i, vec_ZZ& vertex)
{
    unsigned dim = i->Dimension;
    if(!value_one_p(values[dim])) {
	Matrix* Rays = rays(i);
	Matrix *inv = Matrix_Alloc(Rays->NbRows, Rays->NbColumns);
	int ok = Matrix_Inverse(Rays, inv);
	assert(ok);
	Matrix_Free(Rays);
	Rays = rays(i);
	Vector *lambda = Vector_Alloc(dim+1);
	Vector_Matrix_Product(values, inv, lambda->p);
	Matrix_Free(inv);
	for (int j = 0; j < dim; ++j)
	    mpz_cdiv_q(lambda->p[j], lambda->p[j], lambda->p[dim]);
	value_set_si(lambda->p[dim], 1);
	Vector *A = Vector_Alloc(dim+1);
	Vector_Matrix_Product(lambda->p, Rays, A->p);
	Vector_Free(lambda);
	Matrix_Free(Rays);
	values2zz(A->p, vertex, dim);
	Vector_Free(A);
    } else
	values2zz(values, vertex, dim);
}

static evalue *term(int param, ZZ& c, Value *den = NULL)
{
    evalue *EP = new evalue();
    value_init(EP->d);
    value_set_si(EP->d,0);
    EP->x.p = new_enode(polynomial, 2, param + 1);
    evalue_set_si(&EP->x.p->arr[0], 0, 1);
    value_init(EP->x.p->arr[1].x.n);
    if (den == NULL)
	value_set_si(EP->x.p->arr[1].d, 1);
    else
	value_assign(EP->x.p->arr[1].d, *den);
    zz2value(c, EP->x.p->arr[1].x.n);
    return EP;
}

static void vertex_period(
		    Polyhedron *i, vec_ZZ& lambda, Matrix *T, 
		    Value lcm, int p, Vector *val, 
		    evalue *E, evalue* ev,
		    ZZ& offset)
{
    unsigned nparam = T->NbRows - 1;
    unsigned dim = i->Dimension;
    Value tmp;
    ZZ nump;

    if (p == nparam) {
	vec_ZZ vertex;
	ZZ num, l;
	Vector * values = Vector_Alloc(dim + 1);
	Vector_Matrix_Product(val->p, T, values->p);
	value_assign(values->p[dim], lcm);
	lattice_point(values->p, i, vertex);
	num = vertex * lambda;
	value2zz(lcm, l);
	num *= l;
	num += offset;
	value_init(ev->x.n);
	zz2value(num, ev->x.n);
	value_assign(ev->d, lcm);
	Vector_Free(values);
	return;
    }

    value_init(tmp);
    vec_ZZ vertex;
    values2zz(T->p[p], vertex, dim);
    nump = vertex * lambda;
    if (First_Non_Zero(val->p, p) == -1) {
	value_assign(tmp, lcm);
	evalue *ET = term(p, nump, &tmp);
	eadd(ET, E);   
	free_evalue_refs(ET); 
	delete ET;
    }

    value_assign(tmp, lcm);
    if (First_Non_Zero(T->p[p], dim) != -1)
	Vector_Gcd(T->p[p], dim, &tmp);
    Gcd(tmp, lcm, &tmp);
    if (value_lt(tmp, lcm)) {
	ZZ count;

	value_division(tmp, lcm, tmp);
	value_set_si(ev->d, 0);
	ev->x.p = new_enode(periodic, VALUE_TO_INT(tmp), p+1);
	value2zz(tmp, count);
	do {
	    value_decrement(tmp, tmp);
	    --count;
	    ZZ new_offset = offset - count * nump;
	    value_assign(val->p[p], tmp);
	    vertex_period(i, lambda, T, lcm, p+1, val, E, 
			  &ev->x.p->arr[VALUE_TO_INT(tmp)], new_offset);
	} while (value_pos_p(tmp));
    } else
	vertex_period(i, lambda, T, lcm, p+1, val, E, ev, offset);
    value_clear(tmp);
}

static void mask_r(Matrix *f, int nr, Vector *lcm, int p, Vector *val, evalue *ev)
{
    unsigned nparam = lcm->Size;

    if (p == nparam) {
	Vector * prod = Vector_Alloc(f->NbRows);
	Matrix_Vector_Product(f, val->p, prod->p);
	int isint = 1;
	for (int i = 0; i < nr; ++i) {
	    value_modulus(prod->p[i], prod->p[i], f->p[i][nparam+1]);
	    isint &= value_zero_p(prod->p[i]);
	}
	value_set_si(ev->d, 1);
	value_init(ev->x.n);
	value_set_si(ev->x.n, isint);
	Vector_Free(prod);
	return;
    }

    Value tmp;
    value_init(tmp);
    if (value_one_p(lcm->p[p]))
	mask_r(f, nr, lcm, p+1, val, ev);
    else { 
	value_assign(tmp, lcm->p[p]);
	value_set_si(ev->d, 0);
	ev->x.p = new_enode(periodic, VALUE_TO_INT(tmp), p+1);
	do {
	    value_decrement(tmp, tmp);
	    value_assign(val->p[p], tmp);
	    mask_r(f, nr, lcm, p+1, val, &ev->x.p->arr[VALUE_TO_INT(tmp)]);
	} while (value_pos_p(tmp));
    }
    value_clear(tmp);
}

static evalue *multi_monom(vec_ZZ& p)
{
    evalue *X = new evalue();
    value_init(X->d);
    value_init(X->x.n);
    unsigned nparam = p.length()-1;
    zz2value(p[nparam], X->x.n);
    value_set_si(X->d, 1);
    for (int i = 0; i < nparam; ++i) {
	if (p[i] == 0)
	    continue;
	evalue *T = term(i, p[i]);
	eadd(T, X); 
	free_evalue_refs(T); 
	delete T;
    }
    return X;
}

/*
 * Check whether mapping polyhedron P on the affine combination
 * num yields a range that has a fixed quotient on integer
 * division by d
 * If zero is true, then we are only interested in the quotient
 * for the cases where the remainder is zero.
 * Returns NULL if false and a newly allocated value if true.
 */
static Value *fixed_quotient(Polyhedron *P, vec_ZZ& num, Value d, bool zero)
{
    Value* ret = NULL;
    int len = num.length();
    Matrix *T = Matrix_Alloc(2, len);
    zz2values(num, T->p[0]);
    value_set_si(T->p[1][len-1], 1);
    Polyhedron *I = Polyhedron_Image(P, T, P->NbConstraints);
    Matrix_Free(T);

    int i;
    for (i = 0; i < I->NbRays; ++i)
	if (value_zero_p(I->Ray[i][2])) {
	    Polyhedron_Free(I);
	    return NULL;
	}

    Value min, max;
    value_init(min);
    value_init(max);
    int bounded = line_minmax(I, &min, &max);
    assert(bounded);

    if (zero)
	mpz_cdiv_q(min, min, d);
    else
	mpz_fdiv_q(min, min, d);
    mpz_fdiv_q(max, max, d);

    if (value_eq(min, max)) {
	ALLOC(Value, ret);
	value_init(*ret);
	value_assign(*ret, min);
    } 
    value_clear(min);
    value_clear(max);
    return ret;
}

/*
 * Normalize linear expression coef modulo m
 * Removes common factor and reduces coefficients
 * Returns index of first non-zero coefficient or len
 */
static int normal_mod(Value *coef, int len, Value *m)
{
    Value gcd;
    value_init(gcd);

    Vector_Gcd(coef, len, &gcd);
    Gcd(gcd, *m, &gcd);
    Vector_AntiScale(coef, coef, gcd, len);

    value_division(*m, *m, gcd);
    value_clear(gcd);

    if (value_one_p(*m))
	return len;

    int j;
    for (j = 0; j < len; ++j)
	mpz_fdiv_r(coef[j], coef[j], *m);
    for (j = 0; j < len; ++j)
	if (value_notzero_p(coef[j]))
	    break;

    return j;
}

#ifdef USE_MODULO
static void mask(Matrix *f, evalue *factor)
{
    int nr = f->NbRows, nc = f->NbColumns;
    int n;
    bool found = false;
    for (n = 0; n < nr && value_notzero_p(f->p[n][nc-1]); ++n)
	if (value_notone_p(f->p[n][nc-1]) &&
	    value_notmone_p(f->p[n][nc-1]))
		found = true;
    if (!found)
	return;

    evalue EP;
    nr = n;

    Value m;
    value_init(m);

    evalue EV;
    value_init(EV.d);
    value_init(EV.x.n);
    value_set_si(EV.x.n, 1);

    for (n = 0; n < nr; ++n) {
	value_assign(m, f->p[n][nc-1]);
	if (value_one_p(m) || value_mone_p(m))
	    continue;

	int j = normal_mod(f->p[n], nc-1, &m);
	if (j == nc-1) {
	    free_evalue_refs(factor);
	    value_init(factor->d);
	    evalue_set_si(factor, 0, 1);
	    break;
	}
	vec_ZZ row;
	values2zz(f->p[n], row, nc-1);
	ZZ g;
	value2zz(m, g);
	if (j < (nc-1)-1 && row[j] > g/2) {
	    for (int k = j; k < (nc-1); ++k)
		if (row[k] != 0)
		    row[k] = g - row[k];
	}

	value_init(EP.d);
	value_set_si(EP.d, 0);
	EP.x.p = new_enode(relation, 2, 0);
	value_clear(EP.x.p->arr[1].d);
	EP.x.p->arr[1] = *factor;
	evalue *ev = &EP.x.p->arr[0];
	value_set_si(ev->d, 0);
	ev->x.p = new_enode(fractional, 3, -1);
	evalue_set_si(&ev->x.p->arr[1], 0, 1);
	evalue_set_si(&ev->x.p->arr[2], 1, 1);
	evalue *E = multi_monom(row);
	value_assign(EV.d, m);
	emul(&EV, E);
	value_clear(ev->x.p->arr[0].d);
	ev->x.p->arr[0] = *E;
	delete E;
	*factor = EP;
    }

    value_clear(m);
    free_evalue_refs(&EV); 
}
#else
/*
 * 
 */
static void mask(Matrix *f, evalue *factor)
{
    int nr = f->NbRows, nc = f->NbColumns;
    int n;
    bool found = false;
    for (n = 0; n < nr && value_notzero_p(f->p[n][nc-1]); ++n)
	if (value_notone_p(f->p[n][nc-1]) &&
	    value_notmone_p(f->p[n][nc-1]))
		found = true;
    if (!found)
	return;

    Value tmp;
    value_init(tmp);
    nr = n;
    unsigned np = nc - 2;
    Vector *lcm = Vector_Alloc(np);
    Vector *val = Vector_Alloc(nc);
    Vector_Set(val->p, 0, nc);
    value_set_si(val->p[np], 1);
    Vector_Set(lcm->p, 1, np);
    for (n = 0; n < nr; ++n) {
	if (value_one_p(f->p[n][nc-1]) ||
	    value_mone_p(f->p[n][nc-1]))
	    continue;
	for (int j = 0; j < np; ++j)
	    if (value_notzero_p(f->p[n][j])) {
		Gcd(f->p[n][j], f->p[n][nc-1], &tmp);
		value_division(tmp, f->p[n][nc-1], tmp);
		value_lcm(tmp, lcm->p[j], &lcm->p[j]);
	    }
    }
    evalue EP;
    value_init(EP.d);
    mask_r(f, nr, lcm, 0, val, &EP);
    value_clear(tmp);
    Vector_Free(val);
    Vector_Free(lcm);
    emul(&EP,factor); 
    free_evalue_refs(&EP);
}
#endif

struct term_info {
    evalue	   *E;
    ZZ		    constant;
    ZZ		    coeff;
    int		    pos;
};

static bool mod_needed(Polyhedron *PD, vec_ZZ& num, Value d, evalue *E)
{
    Value *q = fixed_quotient(PD, num, d, false);

    if (!q)
	return true;

    value_oppose(*q, *q);
    evalue EV;
    value_init(EV.d);
    value_set_si(EV.d, 1);
    value_init(EV.x.n);
    value_multiply(EV.x.n, *q, d);
    eadd(&EV, E);
    free_evalue_refs(&EV); 
    value_clear(*q);
    free(q);
    return false;
}

static void ceil_mod(Value *coef, int len, Value d, ZZ& f, evalue *EP, Polyhedron *PD)
{
    Value m;
    value_init(m);
    value_set_si(m, -1);

    Vector_Scale(coef, coef, m, len);

    value_assign(m, d);
    int j = normal_mod(coef, len, &m);

    if (j == len) {
	value_clear(m);
	return;
    }

    vec_ZZ num;
    values2zz(coef, num, len);

    ZZ g;
    value2zz(m, g);

    evalue tmp;
    value_init(tmp.d);
    evalue_set_si(&tmp, 0, 1);

    int p = j;
    if (g % 2 == 0)
	while (j < len-1 && (num[j] == g/2 || num[j] == 0))
	    ++j;
    if ((j < len-1 && num[j] > g/2) || (j == len-1 && num[j] >= (g+1)/2)) {
	for (int k = j; k < len-1; ++k)
	    if (num[k] != 0)
		num[k] = g - num[k];
	num[len-1] = g - 1 - num[len-1];
	value_assign(tmp.d, m);
	ZZ t = f*(g-1);
	zz2value(t, tmp.x.n);
	eadd(&tmp, EP);
	f = -f;
    }

    if (p >= len-1) {
	ZZ t = num[len-1] * f;
	zz2value(t, tmp.x.n);
	value_assign(tmp.d, m);
	eadd(&tmp, EP);
    } else {
	evalue *E = multi_monom(num);
	evalue EV;
	value_init(EV.d);

	if (PD && !mod_needed(PD, num, m, E)) {
	    value_init(EV.x.n);
	    zz2value(f, EV.x.n);
	    value_assign(EV.d, m);
	    emul(&EV, E);
	    eadd(E, EP);
	} else {
	    value_init(EV.x.n);
	    value_set_si(EV.x.n, 1);
	    value_assign(EV.d, m);
	    emul(&EV, E);
	    value_clear(EV.x.n);
	    value_set_si(EV.d, 0);
	    EV.x.p = new_enode(fractional, 3, -1);
	    evalue_copy(&EV.x.p->arr[0], E);
	    evalue_set_si(&EV.x.p->arr[1], 0, 1);
	    value_init(EV.x.p->arr[2].x.n);
	    zz2value(f, EV.x.p->arr[2].x.n);
	    value_set_si(EV.x.p->arr[2].d, 1);

	    eadd(&EV, EP);
	}

	free_evalue_refs(&EV); 
	free_evalue_refs(E); 
	delete E;
    }

    free_evalue_refs(&tmp); 

out:
    value_clear(m);
}

evalue* bv_ceil3(Value *coef, int len, Value d, Polyhedron *P)
{
    Vector *val = Vector_Alloc(len);

    Value t;
    value_init(t);
    value_set_si(t, -1);
    Vector_Scale(coef, val->p, t, len);
    value_absolute(t, d);

    vec_ZZ num;
    values2zz(val->p, num, len);
    evalue *EP = multi_monom(num);

    evalue tmp;
    value_init(tmp.d);
    value_init(tmp.x.n);
    value_set_si(tmp.x.n, 1);
    value_assign(tmp.d, t);

    emul(&tmp, EP);

    ZZ one;
    one = 1;
    ceil_mod(val->p, len, t, one, EP, P);
    value_clear(t);

    /* copy EP to malloc'ed evalue */
    evalue *E;
    ALLOC(evalue, E);
    *E = *EP;
    delete EP;

    free_evalue_refs(&tmp); 
    Vector_Free(val);

    return E;
}

#ifdef USE_MODULO
evalue* lattice_point(
    Polyhedron *i, vec_ZZ& lambda, Matrix *W, Value lcm, Polyhedron *PD)
{
    unsigned nparam = W->NbColumns - 1;

    Matrix* Rays = rays2(i);
    Matrix *T = Transpose(Rays);
    Matrix *T2 = Matrix_Copy(T);
    Matrix *inv = Matrix_Alloc(T2->NbRows, T2->NbColumns);
    int ok = Matrix_Inverse(T2, inv);
    assert(ok);
    Matrix_Free(Rays);
    Matrix_Free(T2);
    mat_ZZ vertex;
    matrix2zz(W, vertex, W->NbRows, W->NbColumns);

    vec_ZZ num;
    num = lambda * vertex;

    evalue *EP = multi_monom(num);

    evalue tmp;
    value_init(tmp.d);
    value_init(tmp.x.n);
    value_set_si(tmp.x.n, 1);
    value_assign(tmp.d, lcm);

    emul(&tmp, EP);

    Matrix *L = Matrix_Alloc(inv->NbRows, W->NbColumns);
    Matrix_Product(inv, W, L);

    mat_ZZ RT;
    matrix2zz(T, RT, T->NbRows, T->NbColumns);
    Matrix_Free(T);

    vec_ZZ p = lambda * RT;

    for (int i = 0; i < L->NbRows; ++i) {
	ceil_mod(L->p[i], nparam+1, lcm, p[i], EP, PD);
    }

    Matrix_Free(L);

    Matrix_Free(inv);
    free_evalue_refs(&tmp); 
    return EP;
}
#else
evalue* lattice_point(
    Polyhedron *i, vec_ZZ& lambda, Matrix *W, Value lcm, Polyhedron *PD)
{
    Matrix *T = Transpose(W);
    unsigned nparam = T->NbRows - 1;

    evalue *EP = new evalue();
    value_init(EP->d);
    evalue_set_si(EP, 0, 1);

    evalue ev;
    Vector *val = Vector_Alloc(nparam+1);
    value_set_si(val->p[nparam], 1);
    ZZ offset(INIT_VAL, 0);
    value_init(ev.d);
    vertex_period(i, lambda, T, lcm, 0, val, EP, &ev, offset);
    Vector_Free(val);
    eadd(&ev, EP);
    free_evalue_refs(&ev);   

    Matrix_Free(T);

    reduce_evalue(EP);

    return EP;
}
#endif

void lattice_point(
    Param_Vertices* V, Polyhedron *i, vec_ZZ& lambda, term_info* term,
    Polyhedron *PD)
{
    unsigned nparam = V->Vertex->NbColumns - 2;
    unsigned dim = i->Dimension;
    mat_ZZ vertex;
    vertex.SetDims(V->Vertex->NbRows, nparam+1);
    Value lcm, tmp;
    value_init(lcm);
    value_init(tmp);
    value_set_si(lcm, 1);
    for (int j = 0; j < V->Vertex->NbRows; ++j) {
	value_lcm(lcm, V->Vertex->p[j][nparam+1], &lcm);
    }
    if (value_notone_p(lcm)) {
	Matrix * mv = Matrix_Alloc(dim, nparam+1);
	for (int j = 0 ; j < dim; ++j) {
	    value_division(tmp, lcm, V->Vertex->p[j][nparam+1]);
	    Vector_Scale(V->Vertex->p[j], mv->p[j], tmp, nparam+1);
	}

	term->E = lattice_point(i, lambda, mv, lcm, PD);
	term->constant = 0;

	Matrix_Free(mv);
	value_clear(lcm);
	value_clear(tmp);
	return;
    }
    for (int i = 0; i < V->Vertex->NbRows; ++i) {
	assert(value_one_p(V->Vertex->p[i][nparam+1]));  // for now
	values2zz(V->Vertex->p[i], vertex[i], nparam+1);
    }

    vec_ZZ num;
    num = lambda * vertex;

    int p = -1;
    int nn = 0;
    for (int j = 0; j < nparam; ++j)
	if (num[j] != 0) {
	    ++nn;
	    p = j;
	}
    if (nn >= 2) {
	term->E = multi_monom(num);
	term->constant = 0;
    } else {
	term->E = NULL;
	term->constant = num[nparam];
	term->pos = p;
	if (p != -1)
	    term->coeff = num[p];
    }

    value_clear(lcm);
    value_clear(tmp);
}

static void normalize(ZZ& sign, ZZ& num, vec_ZZ& den)
{
    unsigned dim = den.length();

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

/* input:
 *	f: the powers in the denominator for the remaining vars
 *	      each row refers to a factor
 *      den_s: for each factor, the power of  (s+1)
 *	sign
 *	num_s: powers in the numerator corresponding to the summed vars
 *	num_p: powers in the numerator corresponidng to the remaining vars
 * number of rays in cone: "dim" = "k"
 * length of each ray: "dim" = "d"
 * for now, it is assume: k == d
 * output:
 *	den_p: for each factor
 *		0: independent of remaining vars
 *		1: power corresponds to corresponding row in f
 *	       -1: power is inverse of corresponding row in f
 */
static void normalize(ZZ& sign,
		      ZZ& num_s, vec_ZZ& num_p, vec_ZZ& den_s, vec_ZZ& den_p,
		      mat_ZZ& f)
{
    unsigned dim = f.NumRows();
    unsigned nparam = num_p.length();
    unsigned nvar = dim - nparam;

    int change = 0;

    for (int j = 0; j < den_s.length(); ++j) {
	if (den_s[j] == 0) {
	    den_p[j] = 1;
	    continue;
	}
	int k;
	for (k = 0; k < nparam; ++k)
	    if (f[j][k] != 0)
		break;
	if (k < nparam) {
	    if (den_s[j] > 0) {
		den_p[j] = -1;
		num_p -= f[j];
	    } else
		den_p[j] = 1;
	} else
	    den_p[j] = 0;
	if (den_s[j] > 0)
	    change ^= 1;
	else {
	    den_s[j] = abs(den_s[j]);
	    num_s += den_s[j];
	}
    }

    if (change)
	sign = -sign;
}

struct counter : public polar_decomposer {
    vec_ZZ lambda;
    mat_ZZ rays;
    vec_ZZ vertex;
    vec_ZZ den;
    ZZ sign;
    ZZ num;
    int j;
    Polyhedron *P;
    unsigned dim;
    mpq_t count;

    counter(Polyhedron *P) {
	this->P = P;
	dim = P->Dimension;
	randomvector(P, lambda, dim);
	rays.SetDims(dim, dim);
	den.SetLength(dim);
	mpq_init(count);
    }

    void start(unsigned MaxRays);

    ~counter() {
	mpq_clear(count);
    }

    virtual void handle_polar(Polyhedron *P, int sign);
};

void counter::handle_polar(Polyhedron *C, int s)
{
    int r = 0;
    assert(C->NbRays-1 == dim);
    add_rays(rays, C, &r);
    for (int k = 0; k < dim; ++k) {
	assert(lambda * rays[k] != 0);
    }

    sign = s;

    lattice_point(P->Ray[j]+1, C, vertex);
    num = vertex * lambda;
    den = rays * lambda;
    normalize(sign, num, den);

    dpoly d(dim, num);
    dpoly n(dim, den[0], 1);
    for (int k = 1; k < dim; ++k) {
	dpoly fact(dim, den[k], 1);
	n *= fact;
    }
    d.div(n, count, sign);
}

void counter::start(unsigned MaxRays)
{
    for (j = 0; j < P->NbRays; ++j) {
	Polyhedron *C = supporting_cone(P, j);
	decompose(C, MaxRays);
    }
}

typedef Polyhedron * Polyhedron_p;

void barvinok_count(Polyhedron *P, Value* result, unsigned NbMaxCons)
{
    Polyhedron ** vcone;
    ZZ sign;
    unsigned dim;
    int allocated = 0;
    Value factor;
    Polyhedron *Q;
    int r = 0;

    if (emptyQ(P)) {
	value_set_si(*result, 0);
	return;
    }
    if (P->NbBid == 0)
	for (; r < P->NbRays; ++r)
	    if (value_zero_p(P->Ray[r][P->Dimension+1]))
		break;
    if (P->NbBid !=0 || r < P->NbRays) {
	value_set_si(*result, -1);
	return;
    }
    if (P->NbEq != 0) {
	P = remove_equalities(P);
	if (emptyQ(P)) {
	    Polyhedron_Free(P);
	    value_set_si(*result, 0);
	    return;
	}
	allocated = 1;
    }
    value_init(factor);
    value_set_si(factor, 1);
    Q = Polyhedron_Reduce(P, &factor);
    if (Q) {
	if (allocated)
	    Polyhedron_Free(P);
	P = Q;
	allocated = 1;
    }
    if (P->Dimension == 0) {
	value_assign(*result, factor);
	if (allocated)
	    Polyhedron_Free(P);
	value_clear(factor);
	return;
    }

    counter cnt(P);
    cnt.start(NbMaxCons);

    assert(value_one_p(&cnt.count[0]._mp_den));
    value_multiply(*result, &cnt.count[0]._mp_num, factor);

    if (allocated)
	Polyhedron_Free(P);
    value_clear(factor);
}

static void uni_polynom(int param, Vector *c, evalue *EP)
{ 
    unsigned dim = c->Size-2;
    value_init(EP->d);
    value_set_si(EP->d,0);
    EP->x.p = new_enode(polynomial, dim+1, param+1);
    for (int j = 0; j <= dim; ++j)
	evalue_set(&EP->x.p->arr[j], c->p[j], c->p[dim+1]);
}

static void multi_polynom(Vector *c, evalue* X, evalue *EP)
{
    unsigned dim = c->Size-2;
    evalue EC;

    value_init(EC.d);
    evalue_set(&EC, c->p[dim], c->p[dim+1]);

    value_init(EP->d);
    evalue_set(EP, c->p[dim], c->p[dim+1]);
        
    for (int i = dim-1; i >= 0; --i) {
	emul(X, EP);       
	value_assign(EC.x.n, c->p[i]);
	eadd(&EC, EP);
    }
    free_evalue_refs(&EC);
}

Polyhedron *unfringe (Polyhedron *P, unsigned MaxRays)
{
    int len = P->Dimension+2;
    Polyhedron *T, *R = P;
    Value g;
    value_init(g);
    Vector *row = Vector_Alloc(len);
    value_set_si(row->p[0], 1);

    R = DomainConstraintSimplify(Polyhedron_Copy(P), MaxRays);

    Matrix *M = Matrix_Alloc(2, len-1);
    value_set_si(M->p[1][len-2], 1);
    for (int v = 0; v < P->Dimension; ++v) {
	value_set_si(M->p[0][v], 1);
	Polyhedron *I = Polyhedron_Image(P, M, 2+1);
	value_set_si(M->p[0][v], 0);
	for (int r = 0; r < I->NbConstraints; ++r) {
	    if (value_zero_p(I->Constraint[r][0]))
		continue;
	    if (value_zero_p(I->Constraint[r][1]))
		continue;
	    if (value_one_p(I->Constraint[r][1]))
		continue;
	    if (value_mone_p(I->Constraint[r][1]))
		continue;
	    value_absolute(g, I->Constraint[r][1]);
	    Vector_Set(row->p+1, 0, len-2);
	    value_division(row->p[1+v], I->Constraint[r][1], g);
	    mpz_fdiv_q(row->p[len-1], I->Constraint[r][2], g);
	    T = R;
	    R = AddConstraints(row->p, 1, R, MaxRays);
	    if (T != P)
		Polyhedron_Free(T);
	}
	Polyhedron_Free(I);
    }
    Matrix_Free(M);
    Vector_Free(row);
    value_clear(g);
    return R;
}

static Polyhedron *reduce_domain(Polyhedron *D, Matrix *CT, Polyhedron *CEq,
				 Polyhedron **fVD, int nd, unsigned MaxRays)
{
    assert(CEq);

    Polyhedron *Dt;
    Dt = CT ? DomainPreimage(D, CT, MaxRays) : D;
    Polyhedron *rVD = DomainIntersection(Dt, CEq, MaxRays);

    /* if rVD is empty or too small in geometric dimension */
    if(!rVD || emptyQ(rVD) ||
	    (rVD->Dimension-rVD->NbEq < Dt->Dimension-Dt->NbEq-CEq->NbEq)) {
	if(rVD)
	    Domain_Free(rVD);
	if (CT)
	    Domain_Free(Dt);
	return 0;		/* empty validity domain */
    }

    if (CT)
	Domain_Free(Dt);

    fVD[nd] = Domain_Copy(rVD);
    for (int i = 0 ; i < nd; ++i) {
	Polyhedron *I = DomainIntersection(fVD[nd], fVD[i], MaxRays);
	if (emptyQ(I)) {
	    Domain_Free(I);
	    continue;
	}
	Polyhedron *F = DomainSimplify(I, fVD[nd], MaxRays);
	if (F->NbEq == 1) {
	    Polyhedron *T = rVD;
	    rVD = DomainDifference(rVD, F, MaxRays);
	    Domain_Free(T);
	}
	Domain_Free(F);
	Domain_Free(I);
    }

    rVD = DomainConstraintSimplify(rVD, MaxRays);
    if (emptyQ(rVD)) {
	Domain_Free(fVD[nd]);
	Domain_Free(rVD);
	return 0;
    }

    Value c;
    value_init(c);
    barvinok_count(rVD, &c, MaxRays);
    if (value_zero_p(c)) {
	Domain_Free(rVD);
	rVD = 0;
    }
    value_clear(c);

    return rVD;
}

static bool Polyhedron_is_infinite(Polyhedron *P, unsigned nparam)
{
    int r;
    for (r = 0; r < P->NbRays; ++r)
	if (value_zero_p(P->Ray[r][0]) ||
		value_zero_p(P->Ray[r][P->Dimension+1])) {
	    int i;
	    for (i = P->Dimension - nparam; i < P->Dimension; ++i)
		if (value_notzero_p(P->Ray[r][i+1]))
		    break;
	    if (i >= P->Dimension)
		break;
	}
    return r <  P->NbRays;
}

/* Check whether all rays point in the positive directions
 * for the parameters
 */
static bool Polyhedron_has_positive_rays(Polyhedron *P, unsigned nparam)
{
    int r;
    for (r = 0; r < P->NbRays; ++r)
	if (value_zero_p(P->Ray[r][P->Dimension+1])) {
	    int i;
	    for (i = P->Dimension - nparam; i < P->Dimension; ++i)
		if (value_neg_p(P->Ray[r][i+1]))
		    return false;
	}
    return true;
}

typedef evalue * evalue_p;

struct enumerator : public polar_decomposer {
    vec_ZZ lambda;
    unsigned dim, nbV;
    evalue ** vE;
    int _i;
    mat_ZZ rays;
    vec_ZZ den;
    ZZ sign;
    Polyhedron *P;
    Param_Vertices *V;
    term_info num;
    Vector *c;
    mpq_t count;

    enumerator(Polyhedron *P, unsigned dim, unsigned nbV) {
	this->P = P;
	this->dim = dim;
	this->nbV = nbV;
	randomvector(P, lambda, dim);
	rays.SetDims(dim, dim);
	den.SetLength(dim);
	c = Vector_Alloc(dim+2);

	vE = new evalue_p[nbV];
	for (int j = 0; j < nbV; ++j)
	    vE[j] = 0;

	mpq_init(count);
    }

    void decompose_at(Param_Vertices *V, int _i, unsigned MaxRays) {
	Polyhedron *C = supporting_cone_p(P, V);
	this->_i = _i;
	this->V = V;

	vE[_i] = new evalue;
	value_init(vE[_i]->d);
	evalue_set_si(vE[_i], 0, 1);

	decompose(C, MaxRays);
    }

    ~enumerator() {
	mpq_clear(count);
	Vector_Free(c);

	for (int j = 0; j < nbV; ++j)
	    if (vE[j]) {
		free_evalue_refs(vE[j]);
		delete vE[j];
	    }
	delete [] vE;
    }

    virtual void handle_polar(Polyhedron *P, int sign);
};

void enumerator::handle_polar(Polyhedron *C, int s)
{
    int r = 0;
    assert(C->NbRays-1 == dim);
    add_rays(rays, C, &r);
    for (int k = 0; k < dim; ++k) {
	assert(lambda * rays[k] != 0);
    }

    sign = s;

    lattice_point(V, C, lambda, &num, 0);
    den = rays * lambda;
    normalize(sign, num.constant, den);

    dpoly n(dim, den[0], 1);
    for (int k = 1; k < dim; ++k) {
	dpoly fact(dim, den[k], 1);
	n *= fact;
    }
    if (num.E != NULL) {
	ZZ one(INIT_VAL, 1);
	dpoly_n d(dim, num.constant, one);
	d.div(n, c, sign);
	evalue EV; 
	multi_polynom(c, num.E, &EV);
	eadd(&EV , vE[_i]);
	free_evalue_refs(&EV);
	free_evalue_refs(num.E);
	delete num.E; 
    } else if (num.pos != -1) {
	dpoly_n d(dim, num.constant, num.coeff);
	d.div(n, c, sign);
	evalue EV;
	uni_polynom(num.pos, c, &EV);
	eadd(&EV , vE[_i]);
	free_evalue_refs(&EV);
    } else {
	mpq_set_si(count, 0, 1);
	dpoly d(dim, num.constant);
	d.div(n, count, sign);
	evalue EV;
	value_init(EV.d);
	evalue_set(&EV, &count[0]._mp_num, &count[0]._mp_den);
	eadd(&EV , vE[_i]);
	free_evalue_refs(&EV);
    } 
}

evalue* barvinok_enumerate_ev(Polyhedron *P, Polyhedron* C, unsigned MaxRays)
{
    //P = unfringe(P, MaxRays);
    Polyhedron *CEq = NULL, *rVD, *pVD, *CA;
    Matrix *CT = NULL;
    Param_Polyhedron *PP = NULL;
    Param_Domain *D, *next;
    Param_Vertices *V;
    int r = 0;
    unsigned nparam = C->Dimension;
    evalue *eres;
    ALLOC(evalue, eres);
    value_init(eres->d);
    value_set_si(eres->d, 0);

    evalue factor;
    value_init(factor.d);
    evalue_set_si(&factor, 1, 1);
    
    CA = align_context(C, P->Dimension, MaxRays);
    P = DomainIntersection(P, CA, MaxRays);
    Polyhedron_Free(CA);

    if (C->Dimension == 0 || emptyQ(P)) {
constant:
	eres->x.p = new_enode(partition, 2, C->Dimension);
	EVALUE_SET_DOMAIN(eres->x.p->arr[0], 
	    DomainConstraintSimplify(CEq ? CEq : Polyhedron_Copy(C), MaxRays));
	value_set_si(eres->x.p->arr[1].d, 1);
	value_init(eres->x.p->arr[1].x.n);
	if (emptyQ(P))
	    value_set_si(eres->x.p->arr[1].x.n, 0);
	else
	    barvinok_count(P, &eres->x.p->arr[1].x.n, MaxRays);
out:
	emul(&factor, eres);
	reduce_evalue(eres);
	free_evalue_refs(&factor);
	Polyhedron_Free(P);
	if (CT)
	    Matrix_Free(CT);
	if (PP)
	    Param_Polyhedron_Free(PP);
	   
	return eres;
    }
    if (Polyhedron_is_infinite(P, nparam))
	goto constant;

    if (P->NbEq != 0) {
	Matrix *f;
	P = remove_equalities_p(P, P->Dimension-nparam, &f);
	mask(f, &factor);
	Matrix_Free(f);
    }
    if (P->Dimension == nparam) {
	CEq = P;
	P = Universe_Polyhedron(0);
	goto constant;
    }

    Polyhedron *Q = ParamPolyhedron_Reduce(P, P->Dimension-nparam, &factor);
    if (Q) {
	Polyhedron_Free(P);
	if (Q->Dimension == nparam) {
	    CEq = Q;
	    P = Universe_Polyhedron(0);
	    goto constant;
	}
	P = Q;
    }
    Polyhedron *oldP = P;
    PP = Polyhedron2Param_SimplifiedDomain(&P,C,MaxRays,&CEq,&CT);
    if (P != oldP)
	Polyhedron_Free(oldP);

    if (isIdentity(CT)) {
	Matrix_Free(CT);
	CT = NULL;
    } else {
	assert(CT->NbRows != CT->NbColumns);
	if (CT->NbRows == 1) 		// no more parameters
	    goto constant;
	nparam = CT->NbRows - 1;
    }

    unsigned dim = P->Dimension - nparam;

    enumerator et(P, dim, PP->nbV);

    int nd;
    for (nd = 0, D=PP->D; D; ++nd, D=D->next);
    struct section { Polyhedron *D; evalue E; };
    section *s = new section[nd];
    Polyhedron **fVD = new Polyhedron_p[nd];

    for(nd = 0, D=PP->D; D; D=next) {
	next = D->next;

	Polyhedron *rVD = reduce_domain(D->Domain, CT, CEq,
					fVD, nd, MaxRays);
	if (!rVD)
	    continue;

	pVD = CT ? DomainImage(rVD,CT,MaxRays) : rVD;

	value_init(s[nd].E.d);
	evalue_set_si(&s[nd].E, 0, 1);

	FORALL_PVertex_in_ParamPolyhedron(V,D,PP) // _i is internal counter
	    if (!et.vE[_i])
		et.decompose_at(V, _i, MaxRays);
	    eadd(et.vE[_i] , &s[nd].E);
	END_FORALL_PVertex_in_ParamPolyhedron;
	reduce_in_domain(&s[nd].E, pVD);

	if (CT)
	    addeliminatedparams_evalue(&s[nd].E, CT);
	s[nd].D = rVD;
	++nd;
	if (rVD != pVD)
	    Domain_Free(pVD);
    }

    if (nd == 0)
	evalue_set_si(eres, 0, 1);
    else {
	eres->x.p = new_enode(partition, 2*nd, C->Dimension);
	for (int j = 0; j < nd; ++j) {
	    EVALUE_SET_DOMAIN(eres->x.p->arr[2*j], s[j].D);
	    value_clear(eres->x.p->arr[2*j+1].d);
	    eres->x.p->arr[2*j+1] = s[j].E;
	    Domain_Free(fVD[j]);
	}
    }
    delete [] s;
    delete [] fVD;


    if (CEq)
	Polyhedron_Free(CEq);

    goto out;
}

Enumeration* barvinok_enumerate(Polyhedron *P, Polyhedron* C, unsigned MaxRays)
{
    evalue *EP = barvinok_enumerate_ev(P, C, MaxRays);

    return partition2enumeration(EP);
}

static void SwapColumns(Value **V, int n, int i, int j)
{
    for (int r = 0; r < n; ++r)
	value_swap(V[r][i], V[r][j]);
}

static void SwapColumns(Polyhedron *P, int i, int j)
{
    SwapColumns(P->Constraint, P->NbConstraints, i, j);
    SwapColumns(P->Ray, P->NbRays, i, j);
}

static void negative_test_constraint(Value *l, Value *u, Value *c, int pos,
				     int len, Value *v)
{
    value_oppose(*v, u[pos+1]);
    Vector_Combine(l+1, u+1, c+1, *v, l[pos+1], len-1);
    value_multiply(*v, *v, l[pos+1]);
    value_substract(c[len-1], c[len-1], *v);
    value_set_si(*v, -1);
    Vector_Scale(c+1, c+1, *v, len-1);
    value_decrement(c[len-1], c[len-1]);
    ConstraintSimplify(c, c, len, v);
}

static bool parallel_constraints(Value *l, Value *u, Value *c, int pos,
				 int len)
{
    bool parallel;
    Value g1;
    Value g2;
    value_init(g1);
    value_init(g2);

    Vector_Gcd(&l[1+pos], len, &g1);
    Vector_Gcd(&u[1+pos], len, &g2);
    Vector_Combine(l+1+pos, u+1+pos, c+1, g2, g1, len);
    parallel = First_Non_Zero(c+1, len) == -1;

    value_clear(g1);
    value_clear(g2);

    return parallel;
}

static void negative_test_constraint7(Value *l, Value *u, Value *c, int pos,
				      int exist, int len, Value *v)
{
    Value g;
    value_init(g);

    Vector_Gcd(&u[1+pos], exist, v);
    Vector_Gcd(&l[1+pos], exist, &g);
    Vector_Combine(l+1, u+1, c+1, *v, g, len-1);
    value_multiply(*v, *v, g);
    value_substract(c[len-1], c[len-1], *v);
    value_set_si(*v, -1);
    Vector_Scale(c+1, c+1, *v, len-1);
    value_decrement(c[len-1], c[len-1]);
    ConstraintSimplify(c, c, len, v);

    value_clear(g);
}

static void oppose_constraint(Value *c, int len, Value *v)
{
    value_set_si(*v, -1);
    Vector_Scale(c+1, c+1, *v, len-1);
    value_decrement(c[len-1], c[len-1]);
}

static bool SplitOnConstraint(Polyhedron *P, int i, int l, int u,
			      int nvar, int len, int exist, int MaxRays,
			      Vector *row, Value& f, bool independent,
			      Polyhedron **pos, Polyhedron **neg)
{
    negative_test_constraint(P->Constraint[l], P->Constraint[u],
			     row->p, nvar+i, len, &f);
    *neg = AddConstraints(row->p, 1, P, MaxRays);

    /* We found an independent, but useless constraint
     * Maybe we should detect this earlier and not
     * mark the variable as INDEPENDENT
     */
    if (emptyQ((*neg))) {
	Polyhedron_Free(*neg);
	return false;
    }

    oppose_constraint(row->p, len, &f);
    *pos = AddConstraints(row->p, 1, P, MaxRays);

    if (emptyQ((*pos))) {
	Polyhedron_Free(*neg);
	Polyhedron_Free(*pos);
	return false;
    }

    return true;
}

/*
 * unimodularly transform P such that constraint r is transformed
 * into a constraint that involves only a single (the first)
 * existential variable
 *
 */
static Polyhedron *rotate_along(Polyhedron *P, int r, int nvar, int exist,
				unsigned MaxRays)
{
    Value g;
    value_init(g);

    Vector *row = Vector_Alloc(exist);
    Vector_Copy(P->Constraint[r]+1+nvar, row->p, exist);
    Vector_Gcd(row->p, exist, &g);
    if (value_notone_p(g))
	Vector_AntiScale(row->p, row->p, g, exist);
    value_clear(g);

    Matrix *M = unimodular_complete(row);
    Matrix *M2 = Matrix_Alloc(P->Dimension+1, P->Dimension+1);
    for (r = 0; r < nvar; ++r)
	value_set_si(M2->p[r][r], 1);
    for ( ; r < nvar+exist; ++r)
	Vector_Copy(M->p[r-nvar], M2->p[r]+nvar, exist);
    for ( ; r < P->Dimension+1; ++r)
	value_set_si(M2->p[r][r], 1);
    Polyhedron *T = Polyhedron_Image(P, M2, MaxRays);

    Matrix_Free(M2);
    Matrix_Free(M);
    Vector_Free(row);

    return T;
}

static bool SplitOnVar(Polyhedron *P, int i, 
			      int nvar, int len, int exist, int MaxRays,
			      Vector *row, Value& f, bool independent,
			      Polyhedron **pos, Polyhedron **neg)
{
    int j;

    for (int l = P->NbEq; l < P->NbConstraints; ++l) {
	if (value_negz_p(P->Constraint[l][nvar+i+1]))
	    continue;

	if (independent) {
	    for (j = 0; j < exist; ++j)
		if (j != i && value_notzero_p(P->Constraint[l][nvar+j+1]))
		    break;
	    if (j < exist)
		continue;
	}

	for (int u = P->NbEq; u < P->NbConstraints; ++u) {
	    if (value_posz_p(P->Constraint[u][nvar+i+1]))
		continue;

	    if (independent) {
		for (j = 0; j < exist; ++j)
		    if (j != i && value_notzero_p(P->Constraint[u][nvar+j+1]))
			break;
		if (j < exist)
		    continue;
	    }

	    if (SplitOnConstraint(P, i, l, u, 
				   nvar, len, exist, MaxRays,
				   row, f, independent,
				   pos, neg)) {
		if (independent) {
		    if (i != 0)
			SwapColumns(*neg, nvar+1, nvar+1+i);
		}
		return true;
	    }
	}
    }

    return false;
}

static bool double_bound_pair(Polyhedron *P, int nvar, int exist,
			 int i, int l1, int l2,
			 Polyhedron **pos, Polyhedron **neg)
{
    Value f;
    value_init(f);
    Vector *row = Vector_Alloc(P->Dimension+2);
    value_set_si(row->p[0], 1);
    value_oppose(f, P->Constraint[l1][nvar+i+1]);
    Vector_Combine(P->Constraint[l1]+1, P->Constraint[l2]+1,
		   row->p+1,
		   P->Constraint[l2][nvar+i+1], f,
		   P->Dimension+1);
    ConstraintSimplify(row->p, row->p, P->Dimension+2, &f);
    *pos = AddConstraints(row->p, 1, P, 0);
    value_set_si(f, -1);
    Vector_Scale(row->p+1, row->p+1, f, P->Dimension+1);
    value_decrement(row->p[P->Dimension+1], row->p[P->Dimension+1]);
    *neg = AddConstraints(row->p, 1, P, 0);
    Vector_Free(row);
    value_clear(f);

    return !emptyQ((*pos)) && !emptyQ((*neg));
}

static bool double_bound(Polyhedron *P, int nvar, int exist,
			 Polyhedron **pos, Polyhedron **neg)
{
    for (int i = 0; i < exist; ++i) {
	int l1, l2;
	for (l1 = P->NbEq; l1 < P->NbConstraints; ++l1) {
	    if (value_negz_p(P->Constraint[l1][nvar+i+1]))
		continue;
	    for (l2 = l1 + 1; l2 < P->NbConstraints; ++l2) {
		if (value_negz_p(P->Constraint[l2][nvar+i+1]))
		    continue;
		if (double_bound_pair(P, nvar, exist, i, l1, l2, pos, neg))
		    return true;
	    }
	}
	for (l1 = P->NbEq; l1 < P->NbConstraints; ++l1) {
	    if (value_posz_p(P->Constraint[l1][nvar+i+1]))
		continue;
	    if (l1 < P->NbConstraints)
		for (l2 = l1 + 1; l2 < P->NbConstraints; ++l2) {
		    if (value_posz_p(P->Constraint[l2][nvar+i+1]))
			continue;
		if (double_bound_pair(P, nvar, exist, i, l1, l2, pos, neg))
		    return true;
	    }
	}
	return false;
    }
    return false;
}

enum constraint { 
ALL_POS = 1 << 0,
ONE_NEG = 1 << 1,
INDEPENDENT = 1 << 2,
ROT_NEG = 1 << 3
};

static evalue* enumerate_or(Polyhedron *D,
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
#ifdef DEBUG_ER
    fprintf(stderr, "\nER: Or\n");
#endif /* DEBUG_ER */

    Polyhedron *N = D->next;
    D->next = 0;
    evalue *EP = 
	barvinok_enumerate_e(D, exist, nparam, MaxRays);
    Polyhedron_Free(D);

    for (D = N; D; D = N) {
	N = D->next;
	D->next = 0;

	evalue *EN = 
	    barvinok_enumerate_e(D, exist, nparam, MaxRays);

	eor(EN, EP);
	free_evalue_refs(EN); 
	free(EN);
	Polyhedron_Free(D);
    }

    reduce_evalue(EP);

    return EP;
}

static evalue* enumerate_sum(Polyhedron *P,
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
    int nvar = P->Dimension - exist - nparam;
    int toswap = nvar < exist ? nvar : exist;
    for (int i = 0; i < toswap; ++i)
	SwapColumns(P, 1 + i, nvar+exist - i);
    nparam += nvar;

#ifdef DEBUG_ER
    fprintf(stderr, "\nER: Sum\n");
#endif /* DEBUG_ER */

    evalue *EP = barvinok_enumerate_e(P, exist, nparam, MaxRays);

    for (int i = 0; i < /* nvar */ nparam; ++i) {
	Matrix *C = Matrix_Alloc(1, 1 + nparam + 1);
	value_set_si(C->p[0][0], 1);
	evalue split;
	value_init(split.d);
	value_set_si(split.d, 0);
	split.x.p = new_enode(partition, 4, nparam);
	value_set_si(C->p[0][1+i], 1);
	Matrix *C2 = Matrix_Copy(C);
	EVALUE_SET_DOMAIN(split.x.p->arr[0],
	    Constraints2Polyhedron(C2, MaxRays));
	Matrix_Free(C2);
	evalue_set_si(&split.x.p->arr[1], 1, 1);
	value_set_si(C->p[0][1+i], -1);
	value_set_si(C->p[0][1+nparam], -1);
	EVALUE_SET_DOMAIN(split.x.p->arr[2],
	    Constraints2Polyhedron(C, MaxRays));
	evalue_set_si(&split.x.p->arr[3], 1, 1);
	emul(&split, EP);
	free_evalue_refs(&split);
	Matrix_Free(C);
    }
    reduce_evalue(EP);
    evalue_range_reduction(EP);

    evalue_frac2floor(EP);

    evalue *sum = esum(EP, nvar);

    free_evalue_refs(EP); 
    free(EP);
    EP = sum;

    evalue_range_reduction(EP);

    return EP;
}

static evalue* split_sure(Polyhedron *P, Polyhedron *S,
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
    int nvar = P->Dimension - exist - nparam;

    Matrix *M = Matrix_Alloc(exist, S->Dimension+2);
    for (int i = 0; i < exist; ++i)
	value_set_si(M->p[i][nvar+i+1], 1);
    Polyhedron *O = S;
    S = DomainAddRays(S, M, MaxRays);
    Polyhedron_Free(O);
    Polyhedron *F = DomainAddRays(P, M, MaxRays);
    Polyhedron *D = DomainDifference(F, S, MaxRays);
    O = D;
    D = Disjoint_Domain(D, 0, MaxRays);
    Polyhedron_Free(F);
    Domain_Free(O);
    Matrix_Free(M);

    M = Matrix_Alloc(P->Dimension+1-exist, P->Dimension+1);
    for (int j = 0; j < nvar; ++j)
	value_set_si(M->p[j][j], 1);
    for (int j = 0; j < nparam+1; ++j)
	value_set_si(M->p[nvar+j][nvar+exist+j], 1);
    Polyhedron *T = Polyhedron_Image(S, M, MaxRays);
    evalue *EP = barvinok_enumerate_e(T, 0, nparam, MaxRays);
    Polyhedron_Free(S);
    Polyhedron_Free(T);
    Matrix_Free(M);

    for (Polyhedron *Q = D; Q; Q = Q->next) {
	Polyhedron *N = Q->next;
	Q->next = 0;
	T = DomainIntersection(P, Q, MaxRays);
	evalue *E = barvinok_enumerate_e(T, exist, nparam, MaxRays);
	eadd(E, EP);
	free_evalue_refs(E); 
	free(E);
	Polyhedron_Free(T);
	Q->next = N;
    }
    Domain_Free(D);
    return EP;
}

static evalue* enumerate_sure(Polyhedron *P,
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
    int i;
    Polyhedron *S = P;
    int nvar = P->Dimension - exist - nparam;
    Value lcm;
    Value f;
    value_init(lcm);
    value_init(f);

    for (i = 0; i < exist; ++i) {
	Matrix *M = Matrix_Alloc(S->NbConstraints, S->Dimension+2);
	int c = 0;
	value_set_si(lcm, 1);
	for (int j = 0; j < S->NbConstraints; ++j) {
	    if (value_negz_p(S->Constraint[j][1+nvar+i]))
		continue;
	    if (value_one_p(S->Constraint[j][1+nvar+i]))
		continue;
	    value_lcm(lcm, S->Constraint[j][1+nvar+i], &lcm);
	}

	for (int j = 0; j < S->NbConstraints; ++j) {
	    if (value_negz_p(S->Constraint[j][1+nvar+i]))
		continue;
	    if (value_one_p(S->Constraint[j][1+nvar+i]))
		continue;
	    value_division(f, lcm, S->Constraint[j][1+nvar+i]);
	    Vector_Scale(S->Constraint[j], M->p[c], f, S->Dimension+2);
	    value_substract(M->p[c][S->Dimension+1], 
			    M->p[c][S->Dimension+1],
			    lcm);
	    value_increment(M->p[c][S->Dimension+1], 
			    M->p[c][S->Dimension+1]);
	    ++c;
	}
	Polyhedron *O = S;
	S = AddConstraints(M->p[0], c, S, MaxRays);
	if (O != P)
	    Polyhedron_Free(O);
	Matrix_Free(M);
	if (emptyQ(S)) {
	    Polyhedron_Free(S);
	    value_clear(lcm);
	    value_clear(f);
	    return 0;
	}
    }
    value_clear(lcm);
    value_clear(f);

#ifdef DEBUG_ER
    fprintf(stderr, "\nER: Sure\n");
#endif /* DEBUG_ER */

    return split_sure(P, S, exist, nparam, MaxRays);
}

static evalue* enumerate_sure2(Polyhedron *P,
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
    int nvar = P->Dimension - exist - nparam;
    int r;
    for (r = 0; r < P->NbRays; ++r)
	if (value_one_p(P->Ray[r][0]) &&
		value_one_p(P->Ray[r][P->Dimension+1]))
	    break;

    if (r >= P->NbRays)
	return 0;

    Matrix *M = Matrix_Alloc(nvar + 1 + nparam, P->Dimension+2);
    for (int i = 0; i < nvar; ++i)
	value_set_si(M->p[i][1+i], 1);
    for (int i = 0; i < nparam; ++i)
	value_set_si(M->p[i+nvar][1+nvar+exist+i], 1);
    Vector_Copy(P->Ray[r]+1+nvar, M->p[nvar+nparam]+1+nvar, exist);
    value_set_si(M->p[nvar+nparam][0], 1);
    value_set_si(M->p[nvar+nparam][P->Dimension+1], 1);
    Polyhedron * F = Rays2Polyhedron(M, MaxRays);
    Matrix_Free(M);

    Polyhedron *I = DomainIntersection(F, P, MaxRays);
    Polyhedron_Free(F);

#ifdef DEBUG_ER
    fprintf(stderr, "\nER: Sure2\n");
#endif /* DEBUG_ER */

    return split_sure(P, I, exist, nparam, MaxRays);
}

static evalue* enumerate_cyclic(Polyhedron *P,
			  unsigned exist, unsigned nparam, 
			  evalue * EP, int r, int p, unsigned MaxRays)
{
    int nvar = P->Dimension - exist - nparam;

    /* If EP in its fractional maps only contains references
     * to the remainder parameter with appropriate coefficients
     * then we could in principle avoid adding existentially
     * quantified variables to the validity domains.
     * We'd have to replace the remainder by m { p/m }
     * and multiply with an appropriate factor that is one
     * only in the appropriate range.
     * This last multiplication can be avoided if EP
     * has a single validity domain with no (further)
     * constraints on the remainder parameter
     */

    Matrix *CT = Matrix_Alloc(nparam+1, nparam+3);
    Matrix *M = Matrix_Alloc(1, 1+nparam+3);
    for (int j = 0; j < nparam; ++j)
	if (j != p)
	    value_set_si(CT->p[j][j], 1);
    value_set_si(CT->p[p][nparam+1], 1);
    value_set_si(CT->p[nparam][nparam+2], 1);
    value_set_si(M->p[0][1+p], -1);
    value_absolute(M->p[0][1+nparam], P->Ray[0][1+nvar+exist+p]);
    value_set_si(M->p[0][1+nparam+1], 1);
    Polyhedron *CEq = Constraints2Polyhedron(M, 1);
    Matrix_Free(M);
    addeliminatedparams_enum(EP, CT, CEq, MaxRays, nparam);
    Polyhedron_Free(CEq);
    Matrix_Free(CT);

    return EP;
}

static void enumerate_vd_add_ray(evalue *EP, Matrix *Rays, unsigned MaxRays)
{
    if (value_notzero_p(EP->d))
	return;

    assert(EP->x.p->type == partition);
    assert(EP->x.p->pos == EVALUE_DOMAIN(EP->x.p->arr[0])->Dimension);
    for (int i = 0; i < EP->x.p->size/2; ++i) {
	Polyhedron *D = EVALUE_DOMAIN(EP->x.p->arr[2*i]);
	Polyhedron *N = DomainAddRays(D, Rays, MaxRays);
	EVALUE_SET_DOMAIN(EP->x.p->arr[2*i], N);
	Domain_Free(D);
    }
}

static evalue* enumerate_line(Polyhedron *P,
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
    if (P->NbBid == 0)
	return 0;

#ifdef DEBUG_ER
    fprintf(stderr, "\nER: Line\n");
#endif /* DEBUG_ER */

    int nvar = P->Dimension - exist - nparam;
    int i, j;
    for (i = 0; i < nparam; ++i)
	if (value_notzero_p(P->Ray[0][1+nvar+exist+i]))
	    break;
    assert(i < nparam);
    for (j = i+1; j < nparam; ++j)
	if (value_notzero_p(P->Ray[0][1+nvar+exist+i]))
	    break;
    assert(j >= nparam); // for now

    Matrix *M = Matrix_Alloc(2, P->Dimension+2);
    value_set_si(M->p[0][0], 1);
    value_set_si(M->p[0][1+nvar+exist+i], 1);
    value_set_si(M->p[1][0], 1);
    value_set_si(M->p[1][1+nvar+exist+i], -1);
    value_absolute(M->p[1][1+P->Dimension], P->Ray[0][1+nvar+exist+i]);
    value_decrement(M->p[1][1+P->Dimension], M->p[1][1+P->Dimension]);
    Polyhedron *S = AddConstraints(M->p[0], 2, P, MaxRays);
    evalue *EP = barvinok_enumerate_e(S, exist, nparam, MaxRays);
    Polyhedron_Free(S);
    Matrix_Free(M);

    return enumerate_cyclic(P, exist, nparam, EP, 0, i, MaxRays);
}

static int single_param_pos(Polyhedron*P, unsigned exist, unsigned nparam, 
			    int r)
{
    int nvar = P->Dimension - exist - nparam;
    if (First_Non_Zero(P->Ray[r]+1, nvar) != -1)
	return -1;
    int i = First_Non_Zero(P->Ray[r]+1+nvar+exist, nparam);
    if (i == -1)
	return -1;
    if (First_Non_Zero(P->Ray[r]+1+nvar+exist+1, nparam-i-1) != -1)
	return -1;
    return i;
}

static evalue* enumerate_remove_ray(Polyhedron *P, int r,
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
#ifdef DEBUG_ER
    fprintf(stderr, "\nER: RedundantRay\n");
#endif /* DEBUG_ER */

    Value one;
    value_init(one);
    value_set_si(one, 1);
    int len = P->NbRays-1;
    Matrix *M = Matrix_Alloc(2 * len, P->Dimension+2);
    Vector_Copy(P->Ray[0], M->p[0], r * (P->Dimension+2));
    Vector_Copy(P->Ray[r+1], M->p[r], (len-r) * (P->Dimension+2));
    for (int j = 0; j < P->NbRays; ++j) {
	if (j == r)
	    continue;
	Vector_Combine(P->Ray[j], P->Ray[r], M->p[len+j-(j>r)], 
		       one, P->Ray[j][P->Dimension+1], P->Dimension+2);
    }

    P = Rays2Polyhedron(M, MaxRays);
    Matrix_Free(M);
    evalue *EP = barvinok_enumerate_e(P, exist, nparam, MaxRays);
    Polyhedron_Free(P);
    value_clear(one);

    return EP;
}

static evalue* enumerate_redundant_ray(Polyhedron *P,
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
    assert(P->NbBid == 0);
    int nvar = P->Dimension - exist - nparam;
    Value m;
    value_init(m);

    for (int r = 0; r < P->NbRays; ++r) {
	if (value_notzero_p(P->Ray[r][P->Dimension+1]))
	    continue;
	int i1 = single_param_pos(P, exist, nparam, r);
	if (i1 == -1)
	    continue;
	for (int r2 = r+1; r2 < P->NbRays; ++r2) {
	    if (value_notzero_p(P->Ray[r2][P->Dimension+1]))
		continue;
	    int i2 = single_param_pos(P, exist, nparam, r2);
	    if (i2 == -1)
		continue;
	    if (i1 != i2)
		continue;

	    value_division(m, P->Ray[r][1+nvar+exist+i1], 
			      P->Ray[r2][1+nvar+exist+i1]);
	    value_multiply(m, m, P->Ray[r2][1+nvar+exist+i1]);
	    /* r2 divides r => r redundant */
	    if (value_eq(m, P->Ray[r][1+nvar+exist+i1])) {
		value_clear(m);
		return enumerate_remove_ray(P, r, exist, nparam, MaxRays);
	    }

	    value_division(m, P->Ray[r2][1+nvar+exist+i1], 
			      P->Ray[r][1+nvar+exist+i1]);
	    value_multiply(m, m, P->Ray[r][1+nvar+exist+i1]);
	    /* r divides r2 => r2 redundant */
	    if (value_eq(m, P->Ray[r2][1+nvar+exist+i1])) {
		value_clear(m);
		return enumerate_remove_ray(P, r2, exist, nparam, MaxRays);
	    }
	}
    }
    value_clear(m);
    return 0;
}

static Polyhedron *upper_bound(Polyhedron *P, 
                               int pos, Value *max, Polyhedron **R)
{
    Value v;
    int r;
    value_init(v);

    *R = 0;
    Polyhedron *N;
    Polyhedron *B = 0;
    for (Polyhedron *Q = P; Q; Q = N) {
	N = Q->next;
	for (r = 0; r < P->NbRays; ++r) {
	    if (value_zero_p(P->Ray[r][P->Dimension+1]) &&
		    value_pos_p(P->Ray[r][1+pos]))
		break;
	}
	if (r < P->NbRays) {
	    Q->next = *R;
	    *R = Q;
	    continue;
	} else {
	    Q->next = B;
	    B = Q;
	}
	for (r = 0; r < P->NbRays; ++r) {
	    if (value_zero_p(P->Ray[r][P->Dimension+1]))
		continue;
	    mpz_fdiv_q(v, P->Ray[r][1+pos], P->Ray[r][1+P->Dimension]);
	    if ((!Q->next && r == 0) || value_gt(v, *max))
		value_assign(*max, v);
	}
    }
    value_clear(v);
    return B;
}

static evalue* enumerate_ray(Polyhedron *P,
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
    assert(P->NbBid == 0);
    int nvar = P->Dimension - exist - nparam;

    int r;
    for (r = 0; r < P->NbRays; ++r)
	if (value_zero_p(P->Ray[r][P->Dimension+1]))
	    break;
    if (r >= P->NbRays)
	return 0;

    int r2;
    for (r2 = r+1; r2 < P->NbRays; ++r2)
	if (value_zero_p(P->Ray[r2][P->Dimension+1]))
	    break;
    if (r2 < P->NbRays) {
	if (nvar > 0)
	    return enumerate_sum(P, exist, nparam, MaxRays);
    }

#ifdef DEBUG_ER
    fprintf(stderr, "\nER: Ray\n");
#endif /* DEBUG_ER */

    Value m;
    Value one;
    value_init(m);
    value_init(one);
    value_set_si(one, 1);
    int i = single_param_pos(P, exist, nparam, r);
    assert(i != -1); // for now;

    Matrix *M = Matrix_Alloc(P->NbRays, P->Dimension+2);
    for (int j = 0; j < P->NbRays; ++j) {
	Vector_Combine(P->Ray[j], P->Ray[r], M->p[j], 
		       one, P->Ray[j][P->Dimension+1], P->Dimension+2);
    }
    Polyhedron *S = Rays2Polyhedron(M, MaxRays);
    Matrix_Free(M);
    Polyhedron *D = DomainDifference(P, S, MaxRays);
    Polyhedron_Free(S);
    // Polyhedron_Print(stderr, P_VALUE_FMT, D);
    assert(value_pos_p(P->Ray[r][1+nvar+exist+i])); // for now
    Polyhedron *R;
    D = upper_bound(D, nvar+exist+i, &m, &R);
    assert(D);
    Domain_Free(D);

    M = Matrix_Alloc(2, P->Dimension+2);
    value_set_si(M->p[0][0], 1);
    value_set_si(M->p[1][0], 1);
    value_set_si(M->p[0][1+nvar+exist+i], -1);
    value_set_si(M->p[1][1+nvar+exist+i], 1);
    value_assign(M->p[0][1+P->Dimension], m);
    value_oppose(M->p[1][1+P->Dimension], m);
    value_addto(M->p[1][1+P->Dimension], M->p[1][1+P->Dimension], 
		P->Ray[r][1+nvar+exist+i]);
    value_decrement(M->p[1][1+P->Dimension], M->p[1][1+P->Dimension]);
    // Matrix_Print(stderr, P_VALUE_FMT, M);
    D = AddConstraints(M->p[0], 2, P, MaxRays);
    // Polyhedron_Print(stderr, P_VALUE_FMT, D);
    value_substract(M->p[0][1+P->Dimension], M->p[0][1+P->Dimension], 
		    P->Ray[r][1+nvar+exist+i]);
    // Matrix_Print(stderr, P_VALUE_FMT, M);
    S = AddConstraints(M->p[0], 1, P, MaxRays);
    // Polyhedron_Print(stderr, P_VALUE_FMT, S);
    Matrix_Free(M);

    evalue *EP = barvinok_enumerate_e(D, exist, nparam, MaxRays);
    Polyhedron_Free(D);
    value_clear(one);
    value_clear(m);

    if (value_notone_p(P->Ray[r][1+nvar+exist+i]))
	EP = enumerate_cyclic(P, exist, nparam, EP, r, i, MaxRays);
    else {
	M = Matrix_Alloc(1, nparam+2);
	value_set_si(M->p[0][0], 1);
	value_set_si(M->p[0][1+i], 1);
	enumerate_vd_add_ray(EP, M, MaxRays);
	Matrix_Free(M);
    }

    if (!emptyQ(S)) {
	evalue *E = barvinok_enumerate_e(S, exist, nparam, MaxRays);
	eadd(E, EP);
	free_evalue_refs(E);
	free(E);
    }
    Polyhedron_Free(S);

    if (R) {
	assert(nvar == 0);
	evalue *ER = enumerate_or(R, exist, nparam, MaxRays);
	eor(ER, EP);
	free_evalue_refs(ER);
	free(ER);
    }

    return EP;
}

static evalue* new_zero_ep()
{
    evalue *EP;
    ALLOC(evalue, EP);
    value_init(EP->d);
    evalue_set_si(EP, 0, 1);
    return EP;
}

static evalue* enumerate_vd(Polyhedron **PA,
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
    Polyhedron *P = *PA;
    int nvar = P->Dimension - exist - nparam;
    Param_Polyhedron *PP = NULL;
    Polyhedron *C = Universe_Polyhedron(nparam);
    Polyhedron *CEq;
    Matrix *CT;
    Polyhedron *PR = P;
    PP = Polyhedron2Param_SimplifiedDomain(&PR,C,MaxRays,&CEq,&CT);
    Polyhedron_Free(C);

    int nd;
    Param_Domain *D, *last;
    Value c;
    value_init(c);
    for (nd = 0, D=PP->D; D; D=D->next, ++nd)
	;

    Polyhedron **VD = new Polyhedron_p[nd];
    Polyhedron **fVD = new Polyhedron_p[nd];
    for(nd = 0, D=PP->D; D; D=D->next) {
	Polyhedron *rVD = reduce_domain(D->Domain, CT, CEq,
					fVD, nd, MaxRays);
	if (!rVD)
	    continue;

	VD[nd++] = rVD;
	last = D;
    }

    evalue *EP = 0;

    if (nd == 0)
	EP = new_zero_ep();

    /* This doesn't seem to have any effect */
    if (nd == 1) {
	Polyhedron *CA = align_context(VD[0], P->Dimension, MaxRays);
	Polyhedron *O = P;
	P = DomainIntersection(P, CA, MaxRays);
	if (O != *PA)
	    Polyhedron_Free(O);
	Polyhedron_Free(CA);
	if (emptyQ(P))
	    EP = new_zero_ep();
    }

    if (!EP && CT->NbColumns != CT->NbRows) {
	Polyhedron *CEqr = DomainImage(CEq, CT, MaxRays);
	Polyhedron *CA = align_context(CEqr, PR->Dimension, MaxRays);
	Polyhedron *I = DomainIntersection(PR, CA, MaxRays);
	Polyhedron_Free(CEqr);
	Polyhedron_Free(CA);
#ifdef DEBUG_ER
	fprintf(stderr, "\nER: Eliminate\n");
#endif /* DEBUG_ER */
	nparam -= CT->NbColumns - CT->NbRows;
	EP = barvinok_enumerate_e(I, exist, nparam, MaxRays);
	nparam += CT->NbColumns - CT->NbRows;
	addeliminatedparams_enum(EP, CT, CEq, MaxRays, nparam);
	Polyhedron_Free(I);
    }
    if (PR != *PA)
	Polyhedron_Free(PR);
    PR = 0;

    if (!EP && nd > 1) {
#ifdef DEBUG_ER
	fprintf(stderr, "\nER: VD\n");
#endif /* DEBUG_ER */
	for (int i = 0; i < nd; ++i) {
	    Polyhedron *CA = align_context(VD[i], P->Dimension, MaxRays);
	    Polyhedron *I = DomainIntersection(P, CA, MaxRays);

	    if (i == 0)
		EP = barvinok_enumerate_e(I, exist, nparam, MaxRays);
	    else {
		evalue *E = barvinok_enumerate_e(I, exist, nparam, MaxRays);
		eadd(E, EP);
		free_evalue_refs(E); 
		free(E);
	    }
	    Polyhedron_Free(I);
	    Polyhedron_Free(CA);
	}
    }

    for (int i = 0; i < nd; ++i) {
	Polyhedron_Free(VD[i]);
	Polyhedron_Free(fVD[i]);
    }
    delete [] VD;
    delete [] fVD;
    value_clear(c);

    if (!EP && nvar == 0) {
	Value f;
	value_init(f);
	Param_Vertices *V, *V2;
	Matrix* M = Matrix_Alloc(1, P->Dimension+2);

	FORALL_PVertex_in_ParamPolyhedron(V, last, PP) {
	    bool found = false;
	    FORALL_PVertex_in_ParamPolyhedron(V2, last, PP) {
		if (V == V2) {
		    found = true;
		    continue;
		}
		if (!found)
		    continue;
		for (int i = 0; i < exist; ++i) {
		    value_oppose(f, V->Vertex->p[i][nparam+1]);
		    Vector_Combine(V->Vertex->p[i],
				   V2->Vertex->p[i],
				   M->p[0] + 1 + nvar + exist,
				   V2->Vertex->p[i][nparam+1],
				   f,
				   nparam+1);
		    int j;
		    for (j = 0; j < nparam; ++j)
			if (value_notzero_p(M->p[0][1+nvar+exist+j]))
			    break;
		    if (j >= nparam)
			continue;
		    ConstraintSimplify(M->p[0], M->p[0], 
				       P->Dimension+2, &f);
		    value_set_si(M->p[0][0], 0);
		    Polyhedron *para = AddConstraints(M->p[0], 1, P,
						      MaxRays);
		    if (emptyQ(para)) {
			Polyhedron_Free(para);
			continue;
		    }
		    Polyhedron *pos, *neg;
		    value_set_si(M->p[0][0], 1);
		    value_decrement(M->p[0][P->Dimension+1],
				    M->p[0][P->Dimension+1]);
		    neg = AddConstraints(M->p[0], 1, P, MaxRays);
		    value_set_si(f, -1);
		    Vector_Scale(M->p[0]+1, M->p[0]+1, f, 
				 P->Dimension+1);
		    value_decrement(M->p[0][P->Dimension+1],
				    M->p[0][P->Dimension+1]);
		    value_decrement(M->p[0][P->Dimension+1],
				    M->p[0][P->Dimension+1]);
		    pos = AddConstraints(M->p[0], 1, P, MaxRays);
		    if (emptyQ(neg) && emptyQ(pos)) {
			Polyhedron_Free(para);
			Polyhedron_Free(pos);
			Polyhedron_Free(neg);
			continue;
		    }
#ifdef DEBUG_ER
		    fprintf(stderr, "\nER: Order\n");
#endif /* DEBUG_ER */
		    EP = barvinok_enumerate_e(para, exist, nparam, MaxRays);
		    evalue *E;
		    if (!emptyQ(pos)) {
			E = barvinok_enumerate_e(pos, exist, nparam, MaxRays);
			eadd(E, EP);
			free_evalue_refs(E); 
			free(E);
		    }
		    if (!emptyQ(neg)) {
			E = barvinok_enumerate_e(neg, exist, nparam, MaxRays);
			eadd(E, EP);
			free_evalue_refs(E); 
			free(E);
		    }
		    Polyhedron_Free(para);
		    Polyhedron_Free(pos);
		    Polyhedron_Free(neg);
		    break;
		}
		if (EP)
		    break;
	    } END_FORALL_PVertex_in_ParamPolyhedron;
	    if (EP)
		break;
	} END_FORALL_PVertex_in_ParamPolyhedron;

	if (!EP) {
	    /* Search for vertex coordinate to split on */
	    /* First look for one independent of the parameters */
	    FORALL_PVertex_in_ParamPolyhedron(V, last, PP) {
		for (int i = 0; i < exist; ++i) {
		    int j;
		    for (j = 0; j < nparam; ++j)
			if (value_notzero_p(V->Vertex->p[i][j]))
			    break;
		    if (j < nparam)
			continue;
		    value_set_si(M->p[0][0], 1);
		    Vector_Set(M->p[0]+1, 0, nvar+exist);
		    Vector_Copy(V->Vertex->p[i], 
				M->p[0] + 1 + nvar + exist, nparam+1);
		    value_oppose(M->p[0][1+nvar+i], 
				 V->Vertex->p[i][nparam+1]);

		    Polyhedron *pos, *neg;
		    value_set_si(M->p[0][0], 1);
		    value_decrement(M->p[0][P->Dimension+1],
				    M->p[0][P->Dimension+1]);
		    neg = AddConstraints(M->p[0], 1, P, MaxRays);
		    value_set_si(f, -1);
		    Vector_Scale(M->p[0]+1, M->p[0]+1, f, 
				 P->Dimension+1);
		    value_decrement(M->p[0][P->Dimension+1],
				    M->p[0][P->Dimension+1]);
		    value_decrement(M->p[0][P->Dimension+1],
				    M->p[0][P->Dimension+1]);
		    pos = AddConstraints(M->p[0], 1, P, MaxRays);
		    if (emptyQ(neg) || emptyQ(pos)) {
			Polyhedron_Free(pos);
			Polyhedron_Free(neg);
			continue;
		    }
		    Polyhedron_Free(pos);
		    value_increment(M->p[0][P->Dimension+1],
				    M->p[0][P->Dimension+1]);
		    pos = AddConstraints(M->p[0], 1, P, MaxRays);
#ifdef DEBUG_ER
		    fprintf(stderr, "\nER: Vertex\n");
#endif /* DEBUG_ER */
		    pos->next = neg;
		    EP = enumerate_or(pos, exist, nparam, MaxRays);
		    break;
		}
		if (EP)
		    break;
	    } END_FORALL_PVertex_in_ParamPolyhedron;
	}

	if (!EP) {
	    /* Search for vertex coordinate to split on */
	    /* Now look for one that depends on the parameters */
	    FORALL_PVertex_in_ParamPolyhedron(V, last, PP) {
		for (int i = 0; i < exist; ++i) {
		    value_set_si(M->p[0][0], 1);
		    Vector_Set(M->p[0]+1, 0, nvar+exist);
		    Vector_Copy(V->Vertex->p[i], 
				M->p[0] + 1 + nvar + exist, nparam+1);
		    value_oppose(M->p[0][1+nvar+i], 
				 V->Vertex->p[i][nparam+1]);

		    Polyhedron *pos, *neg;
		    value_set_si(M->p[0][0], 1);
		    value_decrement(M->p[0][P->Dimension+1],
				    M->p[0][P->Dimension+1]);
		    neg = AddConstraints(M->p[0], 1, P, MaxRays);
		    value_set_si(f, -1);
		    Vector_Scale(M->p[0]+1, M->p[0]+1, f, 
				 P->Dimension+1);
		    value_decrement(M->p[0][P->Dimension+1],
				    M->p[0][P->Dimension+1]);
		    value_decrement(M->p[0][P->Dimension+1],
				    M->p[0][P->Dimension+1]);
		    pos = AddConstraints(M->p[0], 1, P, MaxRays);
		    if (emptyQ(neg) || emptyQ(pos)) {
			Polyhedron_Free(pos);
			Polyhedron_Free(neg);
			continue;
		    }
		    Polyhedron_Free(pos);
		    value_increment(M->p[0][P->Dimension+1],
				    M->p[0][P->Dimension+1]);
		    pos = AddConstraints(M->p[0], 1, P, MaxRays);
#ifdef DEBUG_ER
		    fprintf(stderr, "\nER: ParamVertex\n");
#endif /* DEBUG_ER */
		    pos->next = neg;
		    EP = enumerate_or(pos, exist, nparam, MaxRays);
		    break;
		}
		if (EP)
		    break;
	    } END_FORALL_PVertex_in_ParamPolyhedron;
	}

	Matrix_Free(M);
	value_clear(f);
    }

    if (CEq)
	Polyhedron_Free(CEq);
    if (CT)
	Matrix_Free(CT);
    if (PP)
	Param_Polyhedron_Free(PP);
    *PA = P;

    return EP;
}

#ifndef HAVE_PIPLIB
evalue *barvinok_enumerate_pip(Polyhedron *P,
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
    return 0;
}
#else
evalue *barvinok_enumerate_pip(Polyhedron *P,
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
    int nvar = P->Dimension - exist - nparam;
    evalue *EP = new_zero_ep();
    Polyhedron *Q, *N, *T = 0;
    Value min, tmp;
    value_init(min);
    value_init(tmp);

#ifdef DEBUG_ER
    fprintf(stderr, "\nER: PIP\n");
#endif /* DEBUG_ER */

    for (int i = 0; i < P->Dimension; ++i) {
	bool pos = false;
	bool neg = false;
	bool posray = false;
	bool negray = false;
	value_set_si(min, 0);
	for (int j = 0; j < P->NbRays; ++j) {
	    if (value_pos_p(P->Ray[j][1+i])) {
		pos = true;
		if (value_zero_p(P->Ray[j][1+P->Dimension]))
		    posray = true;
	    } else if (value_neg_p(P->Ray[j][1+i])) {
		neg = true;
		if (value_zero_p(P->Ray[j][1+P->Dimension]))
		    negray = true;
		else {
		    mpz_fdiv_q(tmp, 
			       P->Ray[j][1+i], P->Ray[j][1+P->Dimension]);
		    if (value_lt(tmp, min))
			value_assign(min, tmp);
		}
	    }
	}
	if (pos && neg) {
	    assert(!(posray && negray));
	    assert(!negray);		// for now
	    Polyhedron *O = T ? T : P;
	    /* shift by a safe amount */
	    Matrix *M = Matrix_Alloc(O->NbRays, O->Dimension+2);
	    Vector_Copy(O->Ray[0], M->p[0], O->NbRays * (O->Dimension+2));
	    for (int j = 0; j < P->NbRays; ++j) {
		if (value_notzero_p(M->p[j][1+P->Dimension])) {
		    value_multiply(tmp, min, M->p[j][1+P->Dimension]);
		    value_substract(M->p[j][1+i], M->p[j][1+i], tmp);
		}
	    }
	    if (T)
		Polyhedron_Free(T);
	    T = Rays2Polyhedron(M, MaxRays);
	    Matrix_Free(M);
	} else if (neg) {
	    /* negating a parameter requires that we substitute in the
	     * sign again afterwards.
	     * Disallow for now.
	     */
	    assert(i < nvar+exist);
	    if (!T)
		T = Polyhedron_Copy(P);
	    for (int j = 0; j < T->NbRays; ++j)
		value_oppose(T->Ray[j][1+i], T->Ray[j][1+i]);
	    for (int j = 0; j < T->NbConstraints; ++j)
		value_oppose(T->Constraint[j][1+i], T->Constraint[j][1+i]);
	}
    }
    value_clear(min);
    value_clear(tmp);

    Polyhedron *D = pip_lexmin(T ? T : P, exist, nparam);
    for (Q = D; Q; Q = N) {
	N = Q->next;
	Q->next = 0;
	evalue *E;
	exist = Q->Dimension - nvar - nparam;
	E = barvinok_enumerate_e(Q, exist, nparam, MaxRays);
	Polyhedron_Free(Q);
	eadd(E, EP);
	free_evalue_refs(E); 
	free(E);
    }

    if (T)
	Polyhedron_Free(T);

    return EP;
}
#endif


static bool is_single(Value *row, int pos, int len)
{
    return First_Non_Zero(row, pos) == -1 && 
	   First_Non_Zero(row+pos+1, len-pos-1) == -1;
}

static evalue* barvinok_enumerate_e_r(Polyhedron *P, 
			  unsigned exist, unsigned nparam, unsigned MaxRays);

#ifdef DEBUG_ER
static int er_level = 0;

evalue* barvinok_enumerate_e(Polyhedron *P, 
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
    fprintf(stderr, "\nER: level %i\n", er_level);
    int nvar = P->Dimension - exist - nparam;
    fprintf(stderr, "%d %d %d\n", nvar, exist, nparam);

    Polyhedron_Print(stderr, P_VALUE_FMT, P);
    ++er_level;
    P = DomainConstraintSimplify(Polyhedron_Copy(P), MaxRays);
    evalue *EP = barvinok_enumerate_e_r(P, exist, nparam, MaxRays);
    Polyhedron_Free(P);
    --er_level;
    return EP;
}
#else
evalue* barvinok_enumerate_e(Polyhedron *P, 
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
    P = DomainConstraintSimplify(Polyhedron_Copy(P), MaxRays);
    evalue *EP = barvinok_enumerate_e_r(P, exist, nparam, MaxRays);
    Polyhedron_Free(P);
    return EP;
}
#endif

static evalue* barvinok_enumerate_e_r(Polyhedron *P, 
			  unsigned exist, unsigned nparam, unsigned MaxRays)
{
    if (exist == 0) {
	Polyhedron *U = Universe_Polyhedron(nparam);
	evalue *EP = barvinok_enumerate_ev(P, U, MaxRays);
	//char *param_name[] = {"P", "Q", "R", "S", "T" };
	//print_evalue(stdout, EP, param_name);
	Polyhedron_Free(U);
	return EP;
    }

    int nvar = P->Dimension - exist - nparam;
    int len = P->Dimension + 2;

    if (emptyQ(P))
	return new_zero_ep();

    if (nvar == 0 && nparam == 0) {
	evalue *EP = new_zero_ep();
	barvinok_count(P, &EP->x.n, MaxRays);
	if (value_pos_p(EP->x.n))
	    value_set_si(EP->x.n, 1);
	return EP;
    }

    int r;
    for (r = 0; r < P->NbRays; ++r)
	if (value_zero_p(P->Ray[r][0]) ||
		value_zero_p(P->Ray[r][P->Dimension+1])) {
	    int i;
	    for (i = 0; i < nvar; ++i)
		if (value_notzero_p(P->Ray[r][i+1]))
		    break;
	    if (i >= nvar)
		continue;
	    for (i = nvar + exist; i < nvar + exist + nparam; ++i)
		if (value_notzero_p(P->Ray[r][i+1]))
		    break;
	    if (i >= nvar + exist + nparam)
		break;
	}
    if (r <  P->NbRays) {
	evalue *EP = new_zero_ep();
	value_set_si(EP->x.n, -1);
	return EP;
    }

    int first;
    for (r = 0; r < P->NbEq; ++r)
	if ((first = First_Non_Zero(P->Constraint[r]+1+nvar, exist)) != -1)
		break;
    if (r < P->NbEq) {
	if (First_Non_Zero(P->Constraint[r]+1+nvar+first+1, 
			   exist-first-1) != -1) {
	    Polyhedron *T = rotate_along(P, r, nvar, exist, MaxRays);
#ifdef DEBUG_ER
	    fprintf(stderr, "\nER: Equality\n");
#endif /* DEBUG_ER */
	    evalue *EP = barvinok_enumerate_e(T, exist-1, nparam, MaxRays);
	    Polyhedron_Free(T);
	    return EP;
	} else {
#ifdef DEBUG_ER
	    fprintf(stderr, "\nER: Fixed\n");
#endif /* DEBUG_ER */
	    if (first == 0)
		return barvinok_enumerate_e(P, exist-1, nparam, MaxRays);
	    else {
		Polyhedron *T = Polyhedron_Copy(P);
		SwapColumns(T, nvar+1, nvar+1+first);
		evalue *EP = barvinok_enumerate_e(T, exist-1, nparam, MaxRays);
		Polyhedron_Free(T);
		return EP;
	    }
	}
    }

    Vector *row = Vector_Alloc(len);
    value_set_si(row->p[0], 1);

    Value f;
    value_init(f);

    enum constraint* info = new constraint[exist];
    for (int i = 0; i < exist; ++i) {
	info[i] = ALL_POS;
	for (int l = P->NbEq; l < P->NbConstraints; ++l) {
	    if (value_negz_p(P->Constraint[l][nvar+i+1]))
		continue;
	    bool l_parallel = is_single(P->Constraint[l]+nvar+1, i, exist);
	    for (int u = P->NbEq; u < P->NbConstraints; ++u) {
		if (value_posz_p(P->Constraint[u][nvar+i+1]))
		    continue;
		bool lu_parallel = l_parallel ||
			    is_single(P->Constraint[u]+nvar+1, i, exist);
		value_oppose(f, P->Constraint[u][nvar+i+1]);
		Vector_Combine(P->Constraint[l]+1, P->Constraint[u]+1, row->p+1,
			       f, P->Constraint[l][nvar+i+1], len-1);
		if (!(info[i] & INDEPENDENT)) {
		    int j;
		    for (j = 0; j < exist; ++j)
			if (j != i && value_notzero_p(row->p[nvar+j+1]))
			    break;
		    if (j == exist) {
			//printf("independent: i: %d, l: %d, u: %d\n", i, l, u);
			info[i] = (constraint)(info[i] | INDEPENDENT);
		    }
		}
		if (info[i] & ALL_POS) {
		    value_addto(row->p[len-1], row->p[len-1], 
			      P->Constraint[l][nvar+i+1]);
		    value_addto(row->p[len-1], row->p[len-1], f);
		    value_multiply(f, f, P->Constraint[l][nvar+i+1]);
		    value_substract(row->p[len-1], row->p[len-1], f);
		    value_decrement(row->p[len-1], row->p[len-1]);
		    ConstraintSimplify(row->p, row->p, len, &f);
		    value_set_si(f, -1);
		    Vector_Scale(row->p+1, row->p+1, f, len-1);
		    value_decrement(row->p[len-1], row->p[len-1]);
		    Polyhedron *T = AddConstraints(row->p, 1, P, MaxRays);
		    if (!emptyQ(T)) {
			//printf("not all_pos: i: %d, l: %d, u: %d\n", i, l, u);
			info[i] = (constraint)(info[i] ^ ALL_POS);
		    }
		    //puts("pos remainder");
		    //Polyhedron_Print(stdout, P_VALUE_FMT, T);
		    Polyhedron_Free(T);
		}
		if (!(info[i] & ONE_NEG)) {
		    if (lu_parallel) {
			negative_test_constraint(P->Constraint[l],
						 P->Constraint[u],
						 row->p, nvar+i, len, &f);
			oppose_constraint(row->p, len, &f);
			Polyhedron *T = AddConstraints(row->p, 1, P, MaxRays);
			if (emptyQ(T)) {
			    //printf("one_neg i: %d, l: %d, u: %d\n", i, l, u);
			    info[i] = (constraint)(info[i] | ONE_NEG);
			}
			//puts("neg remainder");
			//Polyhedron_Print(stdout, P_VALUE_FMT, T);
			Polyhedron_Free(T);
		    } else if (!(info[i] & ROT_NEG)) {
			if (parallel_constraints(P->Constraint[l],
						 P->Constraint[u],
						 row->p, nvar, exist)) {
			    negative_test_constraint7(P->Constraint[l],
						     P->Constraint[u],
						     row->p, nvar, exist,
						     len, &f);
			    oppose_constraint(row->p, len, &f);
			    Polyhedron *T = AddConstraints(row->p, 1, P, MaxRays);
			    if (emptyQ(T)) {
				// printf("rot_neg i: %d, l: %d, u: %d\n", i, l, u);
				info[i] = (constraint)(info[i] | ROT_NEG);
				r = l;
			    }
			    //puts("neg remainder");
			    //Polyhedron_Print(stdout, P_VALUE_FMT, T);
			    Polyhedron_Free(T);
			}
		    }
		}
		if (!(info[i] & ALL_POS) && (info[i] & (ONE_NEG | ROT_NEG)))
		    goto next;
	    }
	}
	if (info[i] & ALL_POS)
	    break;
next:
	;
    }

    /*
    for (int i = 0; i < exist; ++i)
	printf("%i: %i\n", i, info[i]);
    */
    for (int i = 0; i < exist; ++i)
	if (info[i] & ALL_POS) {
#ifdef DEBUG_ER
	    fprintf(stderr, "\nER: Positive\n");
#endif /* DEBUG_ER */
	    // Eliminate
	    // Maybe we should chew off some of the fat here
	    Matrix *M = Matrix_Alloc(P->Dimension, P->Dimension+1);
	    for (int j = 0; j < P->Dimension; ++j)
		value_set_si(M->p[j][j + (j >= i+nvar)], 1);
	    Polyhedron *T = Polyhedron_Image(P, M, MaxRays);
	    Matrix_Free(M);
	    evalue *EP = barvinok_enumerate_e(T, exist-1, nparam, MaxRays);
	    Polyhedron_Free(T);
	    value_clear(f);
	    Vector_Free(row);
	    delete [] info;
	    return EP;
	}
    for (int i = 0; i < exist; ++i)
	if (info[i] & ONE_NEG) {
#ifdef DEBUG_ER
	    fprintf(stderr, "\nER: Negative\n");
#endif /* DEBUG_ER */
	    Vector_Free(row);
	    value_clear(f);
	    delete [] info;
	    if (i == 0)
		return barvinok_enumerate_e(P, exist-1, nparam, MaxRays);
	    else {
		Polyhedron *T = Polyhedron_Copy(P);
		SwapColumns(T, nvar+1, nvar+1+i);
		evalue *EP = barvinok_enumerate_e(T, exist-1, nparam, MaxRays);
		Polyhedron_Free(T);
		return EP;
	    }
	}
    for (int i = 0; i < exist; ++i)
	if (info[i] & ROT_NEG) {
#ifdef DEBUG_ER
	    fprintf(stderr, "\nER: Rotate\n");
#endif /* DEBUG_ER */
	    Vector_Free(row);
	    value_clear(f);
	    delete [] info;
	    Polyhedron *T = rotate_along(P, r, nvar, exist, MaxRays);
	    evalue *EP = barvinok_enumerate_e(T, exist-1, nparam, MaxRays);
	    Polyhedron_Free(T);
	    return EP;
	}
    for (int i = 0; i < exist; ++i)
	if (info[i] & INDEPENDENT) {
	    Polyhedron *pos, *neg;

	    /* Find constraint again and split off negative part */

	    if (SplitOnVar(P, i, nvar, len, exist, MaxRays,
			   row, f, true, &pos, &neg)) {
#ifdef DEBUG_ER
		fprintf(stderr, "\nER: Split\n");
#endif /* DEBUG_ER */

		evalue *EP = 
		    barvinok_enumerate_e(neg, exist-1, nparam, MaxRays);
		evalue *E = 
		    barvinok_enumerate_e(pos, exist, nparam, MaxRays);
		eadd(E, EP);
		free_evalue_refs(E); 
		free(E);
		Polyhedron_Free(neg);
		Polyhedron_Free(pos);
		value_clear(f);
		Vector_Free(row);
		delete [] info;
		return EP;
	    }
	}
    delete [] info;

    Polyhedron *O = P;
    Polyhedron *F;

    evalue *EP;

    EP = enumerate_line(P, exist, nparam, MaxRays);
    if (EP)
	goto out;

    EP = barvinok_enumerate_pip(P, exist, nparam, MaxRays);
    if (EP)
	goto out;

    EP = enumerate_redundant_ray(P, exist, nparam, MaxRays);
    if (EP)
	goto out;

    EP = enumerate_sure(P, exist, nparam, MaxRays);
    if (EP)
	goto out;

    EP = enumerate_ray(P, exist, nparam, MaxRays);
    if (EP)
	goto out;

    EP = enumerate_sure2(P, exist, nparam, MaxRays);
    if (EP)
	goto out;

    F = unfringe(P, MaxRays);
    if (!PolyhedronIncludes(F, P)) {
#ifdef DEBUG_ER
	fprintf(stderr, "\nER: Fringed\n");
#endif /* DEBUG_ER */
	EP = barvinok_enumerate_e(F, exist, nparam, MaxRays);
	Polyhedron_Free(F);
	goto out;
    }
    Polyhedron_Free(F);

    if (nparam)
	EP = enumerate_vd(&P, exist, nparam, MaxRays);
    if (EP)
	goto out2;

    if (nvar != 0) {
	EP = enumerate_sum(P, exist, nparam, MaxRays);
	goto out2;
    }

    assert(nvar == 0);

    int i;
    Polyhedron *pos, *neg;
    for (i = 0; i < exist; ++i)
	if (SplitOnVar(P, i, nvar, len, exist, MaxRays,
		       row, f, false, &pos, &neg))
	    break;

    assert (i < exist);

    pos->next = neg;
    EP = enumerate_or(pos, exist, nparam, MaxRays);

out2:
    if (O != P)
	Polyhedron_Free(P);

out:
    value_clear(f);
    Vector_Free(row);
    return EP;
}

gen_fun * barvinok_series(Polyhedron *P, Polyhedron* C, unsigned MaxRays)
{
    Polyhedron ** vcone;
    Polyhedron *CA;
    unsigned nparam = C->Dimension;
    unsigned dim, nvar;
    vec_ZZ sign;
    int ncone = 0;
    sign.SetLength(ncone);

    CA = align_context(C, P->Dimension, MaxRays);
    P = DomainIntersection(P, CA, MaxRays);
    Polyhedron_Free(CA);

    assert(!Polyhedron_is_infinite(P, nparam));
    assert(P->NbBid == 0);
    assert(Polyhedron_has_positive_rays(P, nparam));
    assert(P->NbEq == 0);

    dim = P->Dimension;
    nvar = dim - nparam;
    vcone = new Polyhedron_p[P->NbRays];

    for (int j = 0; j < P->NbRays; ++j) {
	if (!value_pos_p(P->Ray[j][dim+1]))
	    continue;

	int npos, nneg;
	Polyhedron *C = supporting_cone(P, j);
	decompose(C, &vcone[j], &npos, &nneg, MaxRays);
	ncone += npos + nneg;
	sign.SetLength(ncone);
	for (int k = 0; k < npos; ++k)
	    sign[ncone-nneg-k-1] = 1;
	for (int k = 0; k < nneg; ++k)
	    sign[ncone-k-1] = -1;
    }

    mat_ZZ rays;
    rays.SetDims(ncone * dim, nvar);
    int r = 0;
    for (int j = 0; j < P->NbRays; ++j) {
	if (!value_pos_p(P->Ray[j][dim+1]))
	    continue;

	for (Polyhedron *i = vcone[j]; i; i = i->next) {
	    add_rays(rays, i, &r, nvar);
	}
    }
    rays.SetDims(r, nvar);
    vec_ZZ lambda;
    nonorthog(rays, lambda);
    //randomvector(P, lambda, nvar);

    /*
    cout << "rays: " << rays;
    cout << "lambda: " << lambda;
    */

    int f = 0;
    ZZ num_s;
    vec_ZZ num_p;
    num_p.SetLength(nparam);
    vec_ZZ vertex;
    vec_ZZ den_s;
    den_s.SetLength(dim);
    vec_ZZ den_p;
    den_p.SetLength(dim);
    mat_ZZ den;
    den.SetDims(dim, nparam);
    ZZ one;
    one = 1;
    mpq_t count;
    mpq_init(count);

    gen_fun * gf = new gen_fun;

    rays.SetDims(dim, nvar);

    for (int j = 0; j < P->NbRays; ++j) {
	if (!value_pos_p(P->Ray[j][dim+1]))
	    continue;

	for (Polyhedron *i = vcone[j]; i; i = i->next, ++f) {
	    lattice_point(P->Ray[j]+1, i, vertex);
	    int k = 0;
	    num_s = 0;
	    for ( ; k < nvar; ++k)
		num_s += vertex[k] * lambda[k];
	    for ( ; k < dim; ++k)
		num_p[k-nvar] = vertex[k];

	    int r = 0;
	    add_rays(rays, i, &r, nvar, true);
	    for (r = 0; r < dim; ++r)
		values2zz(i->Ray[r]+1+nvar, den[r], nparam);
	    den_s = rays * lambda;

	    normalize(sign[f], num_s, num_p, den_s, den_p, den);

	    int only_param = 0;
	    int no_param = 0;
	    for (int k = 0; k < dim; ++k) {
		if (den_p[k] == 0)
		    ++no_param;
		else if (den_s[k] == 0)
		    ++only_param;
	    }
	    if (no_param == 0) {
		for (int k = 0; k < dim; ++k)
		    if (den_p[k] == -1)
			den[k] = -den[k];
		gf->add(sign[f], one, num_p, den);
	    } else if (no_param + only_param == dim) {
		int k, l;
		mat_ZZ pden;
		pden.SetDims(only_param, nparam);

		for (k = 0, l = 0; k < dim; ++k)
		    if (den_p[k] != 0)
			pden[l++] = den[k];

		for (k = 0; k < dim; ++k)
		    if (den_s[k] != 0)
			break;

		dpoly n(no_param, num_s);
		dpoly d(no_param, den_s[k], 1);
		for ( ; ++k < dim; k)
		    if (den_s[k] != 0) {
			dpoly fact(no_param, den_s[k], 1);
			d *= fact;
		    }

		mpq_set_si(count, 0, 1);
		n.div(d, count, sign[f]);

		ZZ qn, qd;
		value2zz(mpq_numref(count), qn);
		value2zz(mpq_denref(count), qd);

		gf->add(qn, qd, num_p, pden);
	    } else {
		int k, l;
		dpoly_r * r = 0;
		mat_ZZ pden;
		pden.SetDims(only_param, nparam);

		for (k = 0, l = 0; k < dim; ++k)
		    if (den_s[k] == 0)
			pden[l++] = den[k];

		for (k = 0; k < dim; ++k)
		    if (den_p[k] == 0)
			break;

		dpoly n(no_param, num_s);
		dpoly d(no_param, den_s[k], 1);
		for ( ; ++k < dim; )
		    if (den_p[k] == 0) {
			dpoly fact(no_param, den_s[k], 1);
			d *= fact;
		    }

		for (k = 0; k < dim; ++k) {
		    if (den_s[k] == 0 || den_p[k] == 0)
			continue;

		    dpoly pd(no_param-1, den_s[k], 1);
		    int s = den_p[k] < 0 ? -1 : 1;

		    if (r == 0)
			r = new dpoly_r(n, pd, k, s, dim);
		    else
			assert(0);  // for now
		}

		r->div(d, sign[f], gf, pden, den, num_p);
	    }

    /*
    cout << "sign: " << sign[f];
    cout << "num_s: " << num_s;
    cout << "num_p: " << num_p;
    cout << "den_s: " << den_s;
    cout << "den_p: " << den_p;
    cout << "den: " << den;
    cout << "only_param: " << only_param;
    cout << "no_param: " << no_param;
    cout << endl;
    */

	}
    }

    mpq_clear(count);

    return gf;
}
