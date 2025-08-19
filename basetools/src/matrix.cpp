#define COMPILE_MATRIX
#if USE_GLM
#   include "glm_matrix.cpp"
#else
#	include "custom_matrix.cpp"
#endif
