#define NOMINMAX

#include "textfileloader.h"

#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>

extern void DebugCheck(void);

// =================================================================================================

TableDimensions TextFileLoader::ReadLines(const char * fileName, List<String>& textLines, tLineFilter filter) {
    DebugCheck();
    std::ifstream stream(fileName);
    if (not stream.is_open())
        return TableDimensions(0,0);
    DebugCheck();
    return ReadStream(stream, textLines, filter);
}


TableDimensions TextFileLoader::CopyLines(const String& lineBuffer, List<String>& textLines, tLineFilter filter) {
    std::istringstream stream(lineBuffer);
    return ReadStream(stream, textLines, filter);
}


TableDimensions TextFileLoader::ReadStream(std::istream& stream, List<String>& textLines, tLineFilter filter) {
    std::string line;
    int rows = 0;
    int cols = 0;
    DebugCheck();
    while (std::getline(stream, line)) {
        DebugCheck();
        String s(line);
        DebugCheck();
        if (filter(s)) {
            rows++;
            cols = std::max(cols, int(line.length()));
            textLines.Append(std::move(s));
            DebugCheck();
        }
    }
    return TableDimensions(cols, rows);
}

// =================================================================================================
