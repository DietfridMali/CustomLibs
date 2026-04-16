#pragma once
#if USE_STD
#	include "std_sharedgfxhandle.hpp"
#else
#	error define USE_STD is required!
#	include "custom_sharedgfxhandle.hpp"
#endif
