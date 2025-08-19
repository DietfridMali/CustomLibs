#include "vector.hpp"

#define COMPILE_VECTOR

#if USE_GLM
#	include "glm_vector.cpp"
#else
#	include "custom_vector.cpp"
#endif
