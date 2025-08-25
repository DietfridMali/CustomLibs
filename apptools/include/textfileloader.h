#pragma once

#include <tuple>
#include "tabledimensions.h"
#include "list.hpp"
#include "string.hpp"
#include <functional> // Add this include for std::function

// =================================================================================================

class TextFileLoader {
    public:
        typedef std::function<bool(String&)> tLineFilter; // Use std::function instead of raw function pointer

        TableDimensions ReadLines(const char * fileName, List<String>& fileLines, tLineFilter filter);

        TableDimensions CopyLines(const String& lineBuffer, List<String>& textLines, tLineFilter filter);

        TableDimensions ReadStream(std::istream& stream, List<String>& textLines, tLineFilter filter);
};

// =================================================================================================
