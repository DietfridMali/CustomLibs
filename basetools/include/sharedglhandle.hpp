#pragma once
#if USE_STD
#	include "std_sharedglhandle.hpp"
#else
#	error define USE_STD is required!
#	include "custom_sharedglhandle.hpp"
#endif
