#pragma once

#include <string>
#include "std_defines.h"

#include "basesingleton.hpp"
#include "string.hpp"
#include "sharedpointer.hpp"
#include "list.hpp"
#include "dictionary.hpp"

// =================================================================================================

class ArgValue 
{
    public:
        using ArgumentList = List<ArgValue*>;

        String          m_value;
        ArgumentList*   m_subValues;

        ArgValue() 
            : m_value(""), m_subValues(nullptr)
        { }
       
        ArgValue(const String& value, const char* delims = ";,:");

        ArgValue(String&& value, const char* delims = ";,:");

        ArgValue(ArgValue const& other);

        ArgValue(ArgValue&& other) noexcept;

        ~ArgValue () {
            if (m_subValues) {
                delete m_subValues;
                m_subValues = nullptr;
            }
        }

        ArgValue& Move (ArgValue& other);
            
        ArgValue& operator= (ArgValue&& other) noexcept;
                
        ArgValue& operator= (ArgValue const& other);

        ArgValue& Copy (ArgValue const& other);

        ArgumentList* Parse (const char* delims);

        String& GetVal (int i);
};

// =================================================================================================

class Argument 
{
    public:
        String      m_key;
        ArgValue    m_values;

        Argument() = default;

        Argument(const Argument&) = default; // Copy-Konstruktor

        Argument& operator=(const Argument&) = default; // Copy-Assignment

        Argument(Argument&& other) noexcept {
            Move(other);
        }

        String Create (String arg);

        String& GetVal (int i = 0);

        Argument& operator=(Argument&& other) noexcept {
            return Move(other);
        }

        Argument& Move (Argument& other) {
            if (this != &other) {
                m_key = std::move(other.m_key);
                m_values = std::move(other.m_values);
            }
            return *this;
        }
};

// =================================================================================================

class ArgHandler 
    : public BaseSingleton<ArgHandler>
{
    public:
        Dictionary<String, Argument>    m_argList;

        ArgHandler() {
#if !(USE_STD || USE_STD_MAP)
            m_argList.SetComparator(String::Compare);
#endif
        }

        bool LineFilter (String& line);
            
        void Add(const String& arg);

        void Add(String&& arg) {
            Add(static_cast<const String&>(arg));
        }

        int LoadArgs(int argC, char** argV);

        int LoadArgs(const char* fileName = "smileybattle.ini");

        Argument* GetArg(const char* key);

        const String StrVal(const char* key, int i = 0, String defVal = String (""), bool onlyDebug = true);

        int IntVal(const char* key, int i = 0, int defVal = 0, bool onlyDebug = true);

        float FloatVal(const char* key, int i = 0, float defVal = 0.0f, bool onlyDebug = true);

        bool BoolVal(const char* key, int i = 0, bool defVal = false, bool onlyDebug = true);
 };

#define argHandler ArgHandler::Instance()

// =================================================================================================
