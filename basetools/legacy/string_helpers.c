//
// Created by DietfridMali on 10.01.2025.
//

#include "SVresult_helpers.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Helper functions for transparent handling and error handling of the
   various members and sub-members of RESULT_REC.

   Basic philosophy: Hide all implementation details of RESULT_REC, even
   internally. Access them exclusively via helper functions. Thus, code
   readability is increased due to abstraction, meaningful naming and
   simplified code adaptation in case of the actual data representation,
   i.e. the implementation of RESULT_REC, being changed.
*/

// ------------------------------------------------------------------
// Remove all occurrences of the character ch at the end of s
char* rtrim_chr (char* s, const char ch) 
{
	char* ps = s + strlen (s);
	while ((--ps >= s) && (*ps  == ch)) 
		/*nothing*/;
	*(ps + 1) = '\0';
	return s;
}

// ------------------------------------------------------------------
// Replace all occurrences of oldChar in text with newChar
char* str_repl_chr (char* text, const char oldChar, const char newChar) 
{
	for (char *p = text; *p; p++) {
		if (*p == oldChar) 
			*p = newChar;
	}
	return text;
}

// ------------------------------------------------------------------
// Return the index of the element of textList equal to text if that exists,
// otherwise -1
int get_textlist_index(const char* text, const char* textList[])
{
	for (char** pTexts = textList; *pTexts != NULL; pTexts++)
		if (text_is_eq(*pTexts, text))
			return (int) (pTexts - textList);
	return -1;
}

// ------------------------------------------------------------------

BOOLEAN text_is_one_of(const char* pattern, ...) 
{
	va_list args;
	const char* ps;

	va_start(args, pattern);

	while ((ps = va_arg(args, const char*)) != NULL) {
		if (text_is_eq(pattern, ps)) {
			va_end(args);
			return TRUE;
		}
	}
	va_end(args);
	return FALSE;
}

// ------------------------------------------------------------------

// eof
