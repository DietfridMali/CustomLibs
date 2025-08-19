#if defined(COMPILE_STRING)

#include "legacy_string.h"

int32_t String::m_count = 0;
int32_t String::m_dbgCount = 0;

void String::Log(const char* fmt, const char* s) {
		if (++m_count == m_dbgCount)
			m_dbgCount = m_dbgCount;
		char sig[85];
		if (not s)
			strncpy_s(sig, "(null)", 24);
		else {
			strncpy_s(sig, s, 24);
			if (strlen(s) > sizeof(sig) - 5) {
				memcpy(sig, s, 20);
				memcpy(sig + 20, " ...", 5);
			}
		}
		fprintf(stderr, "%6d: ", m_count);
		fprintf(stderr, fmt, sig);
	}

	//----------------------------------------

	String& String::Copy(const String& other, bool allowStatic) {
		if ((this != &other) /* and (*this != other) */) {
			m_length = other.m_length;
			if (not m_length)
				SetBuffer(&m_empty, 1);
			else if (m_length == 1) {
				*m_char = *other.m_char;
				SetBuffer(m_char, 2);
			}
			else
				SharedArray<char>::CopyData(other, allowStatic, 0);
		}
		return *this;
	}

	//----------------------------------------

	String& String::Move(String& other) {
		if ((this != &other) and (*this != other)) {
			m_length = other.m_length;
			if (not m_length)
				SetBuffer(&m_empty, 1);
			else if (m_length == 1) {
				*m_char = *other.m_char;
				SetBuffer(m_char, 2);
			}
			else {
				LOG("Grabbing string '%s'\n", other.Data());
				Init();
				SharedArray<char>::Move(other);
				other.SetLength(0);
				return *this;
			}
			other.Destroy();
		}
		return *this;
	}

	//----------------------------------------
	// allocate string with buffer size capacity
	// min capacity is length of passed initialization string + 1 (for zero byte)
	// capacity can be larger than that to create a string buffer large enough
	// for subsequent concatenation or pattern replacement operations with that string
	// Will allocate a static buffer unless capacity > length, which means buffer will
	// be allocated bigger in advance for subsequent string operations (e.g. concatenation)

	int String::ComputeCapacity(const char* source, int capacity) {
		m_length = source ? static_cast<int32_t>(strlen(source)) : 0;
		if (capacity < 0)
			capacity = int(m_length);
		else if (m_length >= capacity)
			m_length = capacity;
		return capacity + 1;
	}

	//----------------------------------------

	String::String(const char* source, int capacity)
		: SharedArray<char>(), m_length(0), m_empty('\0'), m_char("")
	{
		capacity = ComputeCapacity(source, capacity);
#if 1
		// make a static buffer out of this string constant - much faster and more 
		// memory efficient for handling true string constants in a (variable size) String
		if (m_length == capacity - 1)
			SetBuffer(const_cast<char*>(source), capacity);
		else
#endif
			Copy(const_cast<char*>(source), capacity);
	}

	//----------------------------------------

	String& String::Copy(char* source, int capacity) {
		if (not m_length)
			SetBuffer(&m_empty, 1);
		else if (m_length == 1) {
			m_char[0] = *source;
			SetBuffer(m_char, 2);
		}
		else { // capacity always > 2 here
			Reserve(int32_t(capacity));
			memcpy(Data(), source, m_length);
			*Data(m_length) = '\0';
			LOG("Creating string '%source'\n", Data());
		}
		return *this;
	}

	//----------------------------------------

	String::String(const char c)
		: SharedArray<char>(), m_length(0), m_empty('\0'), m_char("")
	{
		Init();
		m_length = 1;
		m_char[0] = c;
		SetBuffer(m_char, 2);
	}

	//----------------------------------------

	String::String(const int n)
		: SharedArray<char>(), m_length(0), m_empty('\0'), m_char("")
	{
		Init();
		char s[20];
		snprintf(s, sizeof(s), "%d", n);
		m_length = static_cast<int32_t>(strlen(s));
		Reserve(m_length + 1);
		memcpy(Data(), s, m_length + 1);
	}

	//----------------------------------------

	String::String(const size_t n)
		: SharedArray<char>(), m_length(0), m_empty('\0'), m_char("")
	{
		Init();
		char s[20];
#ifdef _WIN32
		snprintf(s, sizeof(s), "%lld", n);
#else
		snprintf(s, sizeof(s), "%ld", n);
#endif
		m_length = static_cast<int32_t>(strlen(s));
		Reserve(m_length + 1);
		memcpy(Data(), s, m_length + 1);
	}

	//----------------------------------------

	String::~String() {
		if (Data()) {
			char msg[45];
			strncpy_s(msg, Data(), 40);
			if (Length() > 40)
				strcat(msg, " ...");
			if ((Length() > Capacity() - 1) or (strlen(Data()) > Capacity() - 1))
				LOG("String buffer overrun detected ('%s')!\n", Data());
			else
				LOG("Deleting string '%s'\n", Data());
		}
		m_length = 0;
	}

	//----------------------------------------

	String& String::operator= (const char s[]) {
		int32_t l = (int32_t)strlen(s);
		if (l + 1 > Capacity()) {
			Resize(l + 1);
			if (l + 1 > Capacity())
				return *this;
		}
		memcpy(Data(), s, l + 1);
		m_length = l;
		return *this;
	}

	//----------------------------------------

	String& String::operator+= (int32_t n) {
		int32_t s = Length() + n + 1;
		if (s > Capacity())
			Resize(s);
		m_length += n;
		return *this;
	}

	//----------------------------------------

	String& String::Append(const char* s, int32_t l) {
		if (not l)
			return *this;
		int32_t ol = Length();
		int32_t nl = ol + l;
		*this += l;
		if (nl + 1 > Capacity())
			return *this;	// resizing failed
		memcpy(Data(ol), s, l);
		*Data(nl) = '\0';
		return *this;
	}

	//----------------------------------------

	String String::operator+ (const String& other) {
		int32_t l = Length() + other.Length();
		String s(Data(), int(l));
		memcpy(s.Data() + Length(), other.Data(), other.Length() + 1);
		return s;
	}

	//----------------------------------------

	String String::operator+ (const char* other) {
		int32_t ol = static_cast<int32_t>(strlen(other));
		int32_t l = Length() + ol;
		String s(Data(), int(l) + 1);
		memcpy(s.Data() + Length(), other, ol + 1);
		s.SetLength(l);
		return s;
	}

	//----------------------------------------

	String String::Concat(std::initializer_list<String> values) {
		int32_t l = 0;
		for (auto const& v : values)
			l += v.Length();
		String result;
		result.Reserve(l + 1);
		char* p = result.Data();
		for (auto const& v : values) {
			l = v.Length();
			std::memcpy(p, v.Data(), l);
			p += l;
		}
		*p = '\0';
		return result;
	}

	//----------------------------------------

	String String::SubStr(int32_t nOffset, int32_t nLength) const {
		if (nOffset > Length())
			nLength = 0;
		else if (nLength > Length() + 1 - nOffset)
			nLength = Length() + 1 - nOffset;
		return String(Data() + nOffset, int(nLength));
	}

	//----------------------------------------

	String& String::Delete(int32_t nOffset, int32_t nLength) {
		if (nOffset > Length())
			return *this;
		if (nLength > Length() + 1 - nOffset)
			nLength = Length() + 1 - nOffset;
		memcpy(Data() + nOffset, Data() + nOffset + nLength, Length() + 2 - nOffset + nLength);
		m_length -= nLength;
		return *this;
	}

	//----------------------------------------

	String String::Replace(const char* oldPattern, const char* newPattern, int repetitions) {
		if (not oldPattern)
			return *this;
		int		ol = int(strlen(oldPattern)),
			nl = int(strlen(newPattern)),
			dl = 0, // delta length
			dpl = nl - ol; // delta pattern lengths
		int		r = repetitions;
		char* s, * p;
		// find every occurrence of old pattern in string and add up pattern length difference each time 
		// to compute complete size difference between current and resulting string
		for (s = Data(); (p = strstr(s, oldPattern)); s = p + ol) {
			dl += dpl;
			if (r and not --r)
				break;
		}
		if (not dl) // no old patterns?
			return *this;
		// create result string with sufficiently big buffer
		String result = String("", int(Length()) - dl);

		// successively find each old pattern, copy string part between string start or end of previous old pattern
		// to result string, then add new pattern and skip to end of old pattern from current position until the 
		// entire input string has been processed
		r = repetitions;
		for (s = Data(); (p = strstr(s, oldPattern)); ) {
			int l = int(p - s);
			if (l)
				result.Append(s, l);
			result.Append(newPattern, nl);
			s = p + ol;
			if (r and not --r)
				break;
		}
		if (*s)
			result.Append(s, Length() - static_cast<int32_t>((s - Data())));
		*result.Data(result.Length()) = '\0';
		return result;
	}

	//----------------------------------------

	List<String> String::Split(char delim) const {
		List<String> subStrings;
		for (char* ps = Data(); *ps; ps++) {
			char* pe{ ps };
			for (; *pe and (*pe != delim); pe++)
				;
			subStrings.Append(String(ps, int(pe - ps)));
			if (not *pe)
				break;
			ps = pe;
		}
		return subStrings;
	}

	//----------------------------------------

	bool String::IsLowercase(void) {
		for (char* ps = Data(); *ps; ps++)
			if (*ps != char(tolower(*ps)))
				return false;
		return true;
	}

	//----------------------------------------

	bool String::IsUppercase(void) {
		for (char* ps = Data(); *ps; ps++)
			if (*ps != char(toupper(*ps)))
				return false;
		return true;
	}

	//----------------------------------------

	String String::ToLowercase(void) {
		String s;
		s.Copy(*this, false);
		for (char* ps = s.Data(); *ps; ps++)
			*ps = char(tolower(*ps));
		return s;
	}

	//----------------------------------------

	String String::ToUppercase(void) {
		String s;
		s.Copy(*this, false);
		for (char* ps = s.Data(); *ps; ps++)
			*ps = char(toupper(*ps));
		return s;
	}

	//----------------------------------------

#endif //COMPILE_STRING