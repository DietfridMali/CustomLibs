#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <cctype>

#include "string.hpp"
#include "array.hpp"

using FileList = ManagedArray<String>;

// =================================================================================================

class FileLister {
public:
    static FileList Get(const String& pattern);
};

// =================================================================================================

