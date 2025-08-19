// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#pragma once

#include <utility>

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "quicksort.hpp"
#include "array.hpp"
#include "list.hpp"

#ifndef _WIN32
#	define sscanf_s		sscanf
#endif

#define DEBUG_STRINGS 0
#if DEBUG_STRINGS
#	define LOG(fmt, s)	Log(fmt,s)
#else
#	define LOG(fmt, s)	
#endif

// =================================================================================================

class String : public SharedArray<char> {
	private:
		int32_t	m_length;
		char	m_empty;
		char	m_char[2];

		void Log(const char* fmt, const char* s);

	public:
		static int32_t	m_count;
		static int32_t	m_dbgCount;
	
		//----------------------------------------

		String() 
			: SharedArray<char>(), m_length(0), m_empty('\0'), m_char("")
		{
			Init();
			SetBuffer(&m_empty, 1);
		}

		String& Copy(const String& other, bool allowStatic = true);

		String& Move(String& other);

		int ComputeCapacity(const char* source, int capacity);

		String(const char* source, int capacity = -1);

		String(const std::string source, int capacity = -1)
			: SharedArray<char>(), m_length(0), m_empty('\0'), m_char("")
		{
			const char* s = source.c_str();
			Copy(const_cast<char*>(s), ComputeCapacity(s, capacity));
		}

			
		String(char* source, int capacity = -1)
			: SharedArray<char>(), m_length(0), m_empty('\0'), m_char("")
		{
			Copy(source, ComputeCapacity(source, capacity));
		}

		
		String& Copy(char* source, int capacity);

		String(const String& other)
			: SharedArray<char>(), m_length(0), m_empty('\0'), m_char("")
		{
			LOG("Copy-Creating string '%s'\n", other.Data());
			Copy(other);
		}

		String(String&& other) noexcept
			: SharedArray<char>(), m_length(0), m_empty('\0'), m_char("")
		{
			LOG("Move-Creating string '%s'\n", other.Data());
			Move(other);
		}

		explicit String(const char c);

		explicit String(const int n);

		explicit String(const size_t n);

		~String();

		explicit String(const float n) {
			Init();
			*this = n;
		}

		void Create(int32_t size) {
			Reserve(size);
			*Data() = '\0';
		}

		String& operator=(const String& other) {
			return Copy(other);
		}

		String& operator=(String&& other) noexcept {
			return Move(other);
		}

		inline int32_t Length(void) const {
			return m_length;
		}

		inline void SetLength(const int32_t length) {
			m_length = length;
		}

		inline bool IsEmpty(void) const {
			return m_length == 0;
		}

		String& operator= (const char s[]);

		//----------------------------------------

		String& operator= (const size_t n) {
			char s [100];
			snprintf (s, sizeof (s), "%zd", n);
			return *this = s;
		}

		//----------------------------------------

		String& operator= (const float n) {
			char s[100];
			snprintf(s, sizeof(s), "%f", n);
			return *this = s;
		}

		//----------------------------------------

		operator char*() {
			return Data() ? Data() : &m_empty;
		}

		//----------------------------------------

		operator const char* () const {
			return (const char *) (Data() ? Data() : &m_empty);
		}

		//----------------------------------------

		operator unsigned char* () {
			return (unsigned char*)(Data() ? Data() : &m_empty);
		}

		//----------------------------------------

		explicit operator uint16_t() {
			uint16_t i;
			sscanf_s(Data(), "%hu", &i);
			return i;
		}

		//----------------------------------------

		explicit operator int() {
			int i;
			sscanf_s(Data(), "%d", &i);
			return i;
		}

		//----------------------------------------

		explicit operator size_t() {
			size_t i;
#ifdef _WIN32
			sscanf_s(Data(), "%lld", &i);
#else
			sscanf(Data(), "%ld", &i);
#endif
			return i;
		}

		//----------------------------------------

		explicit operator float() {
			float i;
			sscanf_s(Data(), "%f", &i);
			return i;
		}

		//----------------------------------------

		explicit operator bool() {
			int i;
			sscanf_s(Data(), "%d", &i);
			return bool(i);
		}

		//----------------------------------------

		inline const bool operator== (const String& other) const {
			return (Data () and other.Data ()) ? strcmp (Data (), other.Data ()) == 0 : false;
		}

		//----------------------------------------

		bool operator!= (const String& other) const { 
			return (Data () and other.Data ()) ? strcmp (Data (), other.Data ()) != 0 : true;
		}

		//----------------------------------------

		bool operator< (const String& other) const {
			return (Data() and other.Data()) ? strcmp(Data(), other.Data()) < 0 : false;
		}

		//----------------------------------------

		bool operator> (const String& other) const {
			return (Data() and other.Data()) ? strcmp(Data(), other.Data()) > 0 : false;
		}

		//----------------------------------------

		bool operator<= (const String& other) const {
			return (Data() and other.Data()) ? strcmp(Data(), other.Data()) <= 0 : false;
		}

		//----------------------------------------

		bool operator>= (const String& other) const {
			return (Data() and other.Data()) ? strcmp(Data(), other.Data()) >= 0 : false;
		}

		//----------------------------------------

		inline const bool operator== (const char* other) const {
			return (Data () and other) ? strcmp(Data(), other) == 0 : false;
		}

		//----------------------------------------

		bool operator!= (const char* other) const {
			return (Data () and other) ? strcmp(Data(), other) != 0 : true;
		}

		//----------------------------------------

		String& operator+= (int32_t n);

		String& Append(const char* s, int32_t l);

		String& operator+= (const char* s) {
			return Append(s, static_cast<int32_t>(strlen(s)));
		}

		inline String& operator+= (String const & other) { 
			*this += other.Data (); 
			return *this;
		}

		String operator+ (const String& other);

		String operator+ (const char* other);

		static inline const char* ToCharPtr(const char* s) {
			return s;
		}

		static inline const char* ToCharPtr(const String& s) {
			return static_cast<const char*>(s.Data());
		}

		static String Concat(std::initializer_list<String> values);

		String SubStr(int32_t nOffset, int32_t nLength) const;

		String& Delete(int32_t nOffset, int32_t nLength);

		inline int Find(const char* pattern) const {
			const char* p = strstr(Data(), pattern);
			return p ? int (p - Data()) : -1;
		}

		String Replace(const char* oldPattern, const char* newPattern = "", int repetitions = 0);

		List<String> Split(char delim) const;

		bool IsLowercase(void);

		bool IsUppercase(void);

		String ToLowercase(void);

		String ToUppercase(void);

		static int Compare(void* context, const String& s1, const String& s2) {
			return (s1.Data() and s2.Data()) ? strcmp(s1.Data(), s2.Data()) : 0;
		}

		//----------------------------------------

		template<typename... Args>
		static String Concat(Args&&... args) {
			struct Fragment {
				const char* ptr;
				int32_t len;
			};

			Fragment fragments[] = {
				{ ToCharPtr(args), static_cast<int32_t>(std::strlen(ToCharPtr(args))) }...
			};

			int32_t totalLength = 0;
			for (const auto& f : fragments)
				totalLength += f.len;

			String result;
			result.Resize(totalLength + 1); // +1 für Nullterminator

			char* dest = result.Data();
			for (const auto& f : fragments) {
				std::memcpy(dest, f.ptr, f.len);
				dest += f.len;
			}

			*dest = '\0'; // Nullterminator setzen
			result.SetLength(totalLength); // logische Länge (ohne Terminator)

			return result;
		}

	};

	// =================================================================================================
