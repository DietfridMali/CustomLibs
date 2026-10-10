#include "missingfiles.h"
#include "loghandler.h"

// =================================================================================================

void MissingFiles::SetHandler(Handler handler)
noexcept
{
	m_handler.store(handler);
}


void MissingFiles::Report(const char* fileName)
noexcept
{
	logHandler.Print("missing game data: '%s'\n", fileName);
	++m_count;
	Handler handler = m_handler.load();
	if (handler)
		handler();
}

// =================================================================================================
