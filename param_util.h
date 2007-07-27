#include <barvinok/polylib.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct barvinok_options;

Param_Polyhedron *Polyhedron2Param_Polyhedron(Polyhedron *Din, Polyhedron *Cin,
					      struct barvinok_options *options);
void Param_Vertex_Common_Denominator(Param_Vertices *V);
void Param_Inner_Product(Value *constraint, Matrix *Vertex, Value *row);

#if defined(__cplusplus)
}
#endif
