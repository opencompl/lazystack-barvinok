#include <barvinok/barvinok.h>
#include <omega.h>
#include <vector>

typedef std::vector<Variable_ID> varvector;

Polyhedron *relation2Domain(Relation& r, varvector& vv, varvector& params,
				unsigned MaxRays);
Relation Polyhedron2relation(Polyhedron *P,
			  unsigned exist, unsigned nparam, char **params);
void dump(Relation& r);
