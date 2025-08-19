#define COMPILE_STRING
#if (USE_STD || USE_STD_STRING)
#	include "std_string.cpp"
#else
#	include "custom_string.cpp"
#endif
