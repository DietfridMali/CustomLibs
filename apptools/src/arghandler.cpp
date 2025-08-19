#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "textfileloader.h"

#include "arghandler.h"
#include "dictionary.hpp"

// =================================================================================================

ArgValue::ArgValue(const String& value, const char* delims) 
    : m_value(value)
{
    m_subValues = Parse (delims);
}


ArgValue::ArgValue(String&& value, const char* delims) 
    : m_value(std::move(value))
{
    m_subValues = Parse(delims);
}


ArgValue::ArgValue(ArgValue const& other) {
    Copy (other);
}


ArgValue& ArgValue::operator= (ArgValue const& other) {
    return Copy (other);
}


ArgValue& ArgValue::Copy (ArgValue const& other) {
    m_value = other.m_value;
    if (other.m_subValues) {
        m_subValues = new ArgumentList(); // create new list
        *m_subValues = *other.m_subValues; // then copy other list over
    }
    else
        m_subValues = nullptr;
    return *this;
}


ArgValue::ArgValue (ArgValue&& other) noexcept {
    Move (other);
}


ArgValue& ArgValue::operator= (ArgValue&& other) noexcept {
    return Move (other);
}


ArgValue& ArgValue::Move (ArgValue& other) {
    if (this != &other) {
        m_value = std::move(other.m_value);
        m_subValues = other.m_subValues;
        other.m_subValues = nullptr;
    }
    return *this;
}


// The argument parser accepts arguments delimited with ';', ',' or ':'
// Their hierarchical order is ";.:". 
// Example: test=a:b:c,d:e:f,g:h:i;1:2:3,4:5:6 would be 
// "a:b:c,d:e:f,g:h:i" and "1:2:3,4:5:6" on the highest level
// "a:b:c", "d:e:f", "g:h:i" and "1:2:3", "4:5:6" on the next level
// arguments are stored recursively
// delimiters can be skipped (you can e.g. use ';' and ':', but not exchanged)

ArgValue::ArgumentList* ArgValue::Parse(const char* delims) {
    size_t l = strlen(delims);
    if (l == 0)
        return nullptr;
    ArgumentList* subValues = new ArgumentList();
    ArgValue* argV;
    for (int i = 0; i < l; i++) {
        ManagedArray<String> values = m_value.Split(delims[i]);
        if (values.Length() > 1) {
            for (auto& v : values) {
                argV = new ArgValue(v, delims + i + 1);
                if (argV->m_value.Length())
                    subValues->Push(argV);
                else
                    delete argV;
            }
            return subValues;
        }
    }
    delete subValues;
    return nullptr;
}


String& ArgValue::GetVal (int i) {
    if (not m_subValues or m_subValues->IsEmpty ())
        return m_value;
    return (*m_subValues)[i]->m_value;
}

// =================================================================================================

String Argument::Create(String arg) {
    arg = arg.Split('#')[0];
    ManagedArray<String> values = arg.Replace("= ", "=", 1).Replace(" =", "=", 1).Replace("\n", "", 1).Split('=');
    m_key = values[0].ToLowercase();
    if (values.Length() > 1)
        m_values = ArgValue(values[1]);
    else
        m_values = ArgValue (String ("0"));
    return m_key;
}
        

String& Argument::GetVal(int i) {
    return m_values.GetVal(i);
}

    // split all arguments into one uniform value array
    // void Parse (value, delims) {
    //     values = value.split (delims [0])
    //     if (len (delims) == 1) {
    //         return values
    //     subValues = []
    //     for v in values:
    //         subValues += m_Parse (v, delims [1:])
    //     return subValues

// =================================================================================================

void ArgHandler::Add(const String& arg) {
    Argument a;
    String key = a.Create(arg);
    m_argList.Insert(std::move(key), std::move(a));
}


bool ArgHandler::LineFilter (String& line) {
    return (line [0] != '#') and (line [0] != ';');
}


int ArgHandler::LoadArgs(int argC, char** argV) {
#if !(USE_STD || USE_STD_MAP)
    m_argList.SetComparator(String::Compare);
#endif
    for (int i = argC; i > 1; --i)
        Add(String(*(++argV)));
    return argC;
}


int ArgHandler::LoadArgs(const char* fileName) {
    TextFileLoader  f;
    List<String>    fileLines;

    auto lineFilterWrapper = [this](String& line) { return this->LineFilter(line); };
    TableDimensions argC = f.ReadLines (fileName, fileLines, lineFilterWrapper);
    if (argC.GetRows() > 0)
        for (auto& line : fileLines)
            Add(line);
    return argC.GetRows();
}


Argument* ArgHandler::GetArg(const char* key) {
    return m_argList.Find(String(key));
}


const String ArgHandler::StrVal(const char* key, int i, String defVal) {
    Argument* a = GetArg(key);
    return a ? a->GetVal(i) : defVal;
}


int ArgHandler::IntVal(const char* key, int i, int defVal) {
    Argument* a = GetArg(key);
    return a ? int (a->GetVal(i)) : defVal;
}


float ArgHandler::FloatVal(const char* key, int i, float defVal) {
    Argument* a = GetArg(key);
    return a ? float (a->GetVal(i)) : defVal;
}


bool ArgHandler::BoolVal(const char* key, int i, bool defVal) {
    return bool(IntVal(key, i, int(defVal)));
}

// =================================================================================================

