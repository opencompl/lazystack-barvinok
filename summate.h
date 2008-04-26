#include <barvinok/evalue.h>

struct barvinok_options;

#if defined(__cplusplus)
extern "C" {
#endif

evalue *barvinok_summate_unweighted(Polyhedron *P, Polyhedron *C,
				    struct barvinok_options *options);

#if defined(__cplusplus)
}
#endif