#ifndef NTL_QQ_H
#define NTL_QQ_H

#include <barvinok/NTL.h>

#ifdef NTL_STD_CXX
using namespace NTL;
#endif

struct QQ {
    ZZ	n;
    ZZ	d;
    /* This is not thread-safe, but neither is NTL */
    static ZZ tmp;

    QQ() {}
    QQ(int n, int d) {
	this->n = n;
	this->d = d;
    }

    QQ& canonicalize() {
	GCD(tmp, n, d);
	n /= tmp;
	d /= tmp;
	return *this;
    }

    QQ& operator += (const QQ& a) {
	n *= a.d;
	mul(tmp, d, a.n);
	n += tmp;
	d *= a.d;
	return canonicalize();
    }

    QQ& operator *= (const QQ& a) {
	n *= a.n;
	d *= a.d;
	return canonicalize();
    }

    QQ& operator *= (const ZZ& a) {
	n *= a;
	return *this;
    }
};

NTL_vector_decl(QQ,vec_QQ);

vec_QQ& operator *= (vec_QQ& a, const ZZ& b);
vec_QQ& operator *= (vec_QQ& a, const QQ& b);

std::ostream& operator<< (std::ostream& os, const QQ& q);
std::istream& operator>> (std::istream& os, QQ& q);

NTL_io_vector_decl(QQ,vec_QQ);

#endif
