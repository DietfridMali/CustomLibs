#pragma once

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "cquicksort.h"
#include "carray.h"
#include "clist.h"

//-----------------------------------------------------------------------------

class String : public Array < char > {
	private:
		size_t	m_length;

	public:
		explicit String() : m_length (0) {
			Init();
		}

		String (const char* s, int l = -1) {
			m_length = (size_t) (!s ? 0 : (l < 0) ? strlen (s) : l);
			if (l < 0)
				l = -l;
			if (l < int (m_length))
				l = int(m_length);
			Init (); 
			Create (size_t (l) + 1);
			memcpy (Buffer (), s, Length ());
			Buffer () [Length ()] = '\0';
			}

		//----------------------------------------

		String (String const& other) : Array<char> (other) {
			m_length = other.Length ();
		}

		//----------------------------------------

		String (String&& other) noexcept 
			: Array<char> (other) 
		{
			m_length = other.Length ();
			Move (other);
		}

		//----------------------------------------

		explicit String(const int n) {
			Init();
			char s [100];
			snprintf (s, sizeof (s), "%d", n);
			m_length = strlen (s);
			Create (m_length + 1);
			memcpy (Buffer (), s, m_length + 1);
		}

		//----------------------------------------

		explicit String (const size_t n) {
			Init ();
			char s [100];
			snprintf (s, sizeof (s), "%lld", n);
			m_length = strlen (s);
			Create (m_length + 1);
			memcpy (Buffer (), s, m_length + 1);
		}

		//----------------------------------------

		explicit String(const float n) {
			Init();
			*this = n;
		}

		//----------------------------------------

		void Reserve(size_t size) {
			Create(size);
			*Buffer() = '\0';
		}

		//----------------------------------------

		String& operator=(String const& other) {
			Array<char>::operator=(other);
			m_length = other.Length();
			return *this;
		}

		//----------------------------------------

		String& operator=(String&& other) noexcept {
			m_length = other.Length ();
			Move (other);
			return *this;
		}

		//----------------------------------------

		inline size_t Length (void) const { 
			return m_length; 
		}

		//----------------------------------------

		inline void SetLength(const size_t length) {
			m_length = length;
		}

		//----------------------------------------

		inline bool Empty(void) const {
			return m_length == 0;
		}

		//----------------------------------------

		String operator= (const char s[]) {
			size_t l = (size_t) strlen (s);
			if (l + 1 > Capacity ()) {
				Resize (l + 1);
				if (l + 1 > Capacity ())
					return *this;
				}
			memcpy (Buffer (), s, l + 1);
			m_length = l;
			return *this;
		}


		//----------------------------------------

		String operator= (const size_t n) {
			char s [100];
			snprintf (s, sizeof (s), "%zd", n);
			size_t l = strlen (s);
			if (l + 1 > Capacity()) {
				Resize(l + 1);
				if (l + 1 > Capacity())
					return *this;
			}
			memcpy (Buffer (), s, l + 1);
			return *this;
		}

		//----------------------------------------

		String operator= (const float n) {
			char s[100];
			snprintf(s, sizeof(s), "%f", n);
			return *this = s;
		}

		//----------------------------------------

		explicit operator char*() {
			return Buffer();
		}

		//----------------------------------------

		explicit operator uint16_t() {
			uint16_t i;
			sscanf_s(Buffer(), "%hu", &i);
			return i;
		}

		//----------------------------------------

		explicit operator int() {
			int i;
			sscanf_s(Buffer(), "%d", &i);
			return i;
		}

		//----------------------------------------

		explicit operator size_t() {
			size_t i;
			sscanf_s(Buffer(), "%lld", &i);
			return i;
		}

		//----------------------------------------

		explicit operator float() {
			float i;
			sscanf_s(Buffer(), "%f", &i);
			return i;
		}

		//----------------------------------------

		explicit operator bool() {
			int i;
			sscanf_s(Buffer(), "%d", &i);
			return bool(i);
		}

		//----------------------------------------

		inline const bool operator== (String const other) {
			return (Buffer () && other.Buffer ()) ? strcmp (Buffer (), other.Buffer ()) == 0 : false;
		}

		//----------------------------------------

		inline bool operator!= (String const other) { 
			return (Buffer () && other.Buffer ()) ? strcmp (Buffer (), other.Buffer ()) != 0 : true;
		}

		//----------------------------------------

		inline const bool operator== (const char* other) {
			return (Buffer () && other) ? strcmp(Buffer(), other) == 0 : false;
		}

		//----------------------------------------

		inline bool operator!= (const char* other) {
			return (Buffer () && other) ? strcmp(Buffer(), other) != 0 : true;
		}

		//----------------------------------------

		String& operator+= (size_t n) {
			size_t s = Length() + n + 1;
			if (s > Capacity())
				Resize(s);
			m_length += n;
			return *this;
		}

		//----------------------------------------

		String& Append(const char* s, size_t l) {
			if (!l)
				return *this;
			size_t ol = Length();
			size_t nl = ol + l;
			*this += l;
			if (nl + 1 > Capacity())
				return *this;	// resizing failed
			memcpy(Buffer(ol), s, l);
			*Buffer(nl) = '\0';
			return *this;
		}

		//----------------------------------------

		String& operator+= (const char* s) {
			return Append(s, strlen(s));
		}

		//----------------------------------------

		inline String& operator+= (String const & other) { 
			*this += other.Buffer (); 
			return *this;
		}

		//----------------------------------------

		String operator+ (const String& other) {
			size_t l = Length () + other.Length ();
			String s (Buffer (), int (l));
			memcpy(s.Buffer() + Length(), other.Buffer(), other.Length() + 1);
			return s;
			}

		//----------------------------------------

		String operator+ (const char* other) {
			size_t ol = strlen(other);
			size_t l = Length() + ol;
			String s(Buffer(), int(l));
			memcpy(s.Buffer() + Length(), other, ol + 1);
			return s;
		}

		//----------------------------------------

		String SubStr (size_t nOffset, size_t nLength) {
			if (nOffset > Length ())
				nLength = 0;
			else if (nLength > Length () + 1 - nOffset)
				nLength = Length () + 1 - nOffset;
			return String (Buffer () + nOffset, int (nLength));
			}

		//----------------------------------------

		String& Delete (size_t nOffset, size_t nLength) {
			if (nOffset > Length ())
				return *this;
			if (nLength > Length () + 1 - nOffset)
				nLength = Length () + 1 - nOffset;
			memcpy (Buffer () + nOffset, Buffer () + nOffset + nLength, Length () + 2 - nOffset + nLength);
			m_length -= nLength;
			return *this;
			}

		//----------------------------------------

		inline int Find(const char* pattern) {
			const char* p = strstr(Buffer(), pattern);
			return p ? int (p - Buffer()) : -1;
		}

		//----------------------------------------

		String Replace (const char* oldPattern, const char* newPattern = "", int repetitions = 0) {
			if (!oldPattern)
				return *this;
			int		ol = int (strlen (oldPattern)), 
					nl = int (strlen (newPattern)),
					dl = 0, 
					dpl = nl - ol;
			int		r = repetitions;
			char*	s, * p;
			for (s = Buffer(); p = strstr(s, oldPattern); s = p + ol) {
				dl += dpl;
				if (r && !--r)
					break;
			}
			if (!dl)
				return *this;
			String result = String("", -int (Length()) - dl);
			r = repetitions;
			for (s = Buffer(); p = strstr(s, oldPattern); ) {
				int l = int (p - s);
				if (l)
					result.Append(s, l);
				result.Append (newPattern, nl);
				s = p + ol;
				if (r && !--r)
					break;
			}
			if (*s)
				result.Append (s, Length() - (s - Buffer()));
			*result.Buffer(result.Length ()) = '\0';
			return result;
		}

		//----------------------------------------

		List<String> Split(char delim) {
			List<String> subStrings;
			for (const char* ps = Buffer(); *ps; ps++) {
				const char* pe{ ps };
				for (; *pe && (*pe != delim); pe++)
					;
				subStrings.Append (String(ps, int (pe - ps)));
				if (!*pe)
					break;
				ps = pe;
			}
			return subStrings;
		}

		//----------------------------------------

		String Lower(void) {
			String s = *this;
			for (char* ps = s.Buffer(); *ps; ps++)
				*ps = tolower(*ps);
			return s;
		}

		//----------------------------------------

		String Upper(void) {
			String s = *this;
			for (char* ps = s.Buffer(); *ps; ps++)
				*ps = toupper(*ps);
			return s;
		}

		//----------------------------------------

		static int Compare(const String& s1, const String& s2) {
			return strcmp(s1.Buffer(), s2.Buffer());
		}

		//----------------------------------------

	};

//-----------------------------------------------------------------------------
