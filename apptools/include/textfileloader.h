#pragma once

#include <tuple>
#include "tablesize.h"
#include "list.hpp"
#include "string.hpp"
#include <functional> // Add this include for std::function

// =================================================================================================

class TextFileLoader {
    public:
        typedef std::function<bool(String&)> tLineFilter; // Use std::function instead of raw function pointer

        TableSize ReadLines(const char * fileName, List<String>& fileLines, tLineFilter filter);

        TableSize CopyLines(const String& lineBuffer, List<String>& textLines, tLineFilter filter);

        TableSize ReadStream(std::istream& stream, List<String>& textLines, tLineFilter filter);
};

// =================================================================================================
