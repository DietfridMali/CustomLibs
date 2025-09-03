#define NOMINMAX

#include "textfileloader.h"

#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>

// =================================================================================================

TableSize TextFileLoader::ReadLines(const char * fileName, List<String>& textLines, tLineFilter filter) {
    std::ifstream stream(fileName);
    if (not stream.is_open())
        return TableSize(0,0);
    return ReadStream(stream, textLines, filter);
}


TableSize TextFileLoader::CopyLines(const String& lineBuffer, List<String>& textLines, tLineFilter filter) {
    std::istringstream stream(lineBuffer);
    return ReadStream(stream, textLines, filter);
}


TableSize TextFileLoader::ReadStream(std::istream& stream, List<String>& textLines, tLineFilter filter) {
    std::string line;
    int rows = 0;
    int cols = 0;
    while (std::getline(stream, line)) {
        String s(line);
        if (filter(s)) {
            rows++;
            cols = std::max(cols, int(line.length()));
            textLines.Append(std::move(s));
        }
    }
    return TableSize(cols, rows);
}

// =================================================================================================
