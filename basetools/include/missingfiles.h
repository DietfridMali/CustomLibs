#pragma once

#include <atomic>

#include "basesingleton.hpp"

// =================================================================================================

class MissingFiles
	: public BaseSingleton<MissingFiles> {
public:
	using Handler = void (*)(void);

	void SetHandler(Handler handler)
	noexcept;

	void Report(const char* fileName)
	noexcept;

	inline bool IsEmpty(void) const
	noexcept
	{
		return m_count.load() == 0;
	}

private:
	std::atomic<int>		m_count{ 0 };
	std::atomic<Handler>	m_handler{ nullptr };
};

#define missingFiles MissingFiles::Instance()

// =================================================================================================
