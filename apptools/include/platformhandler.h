#pragma once

#include <stdint.h>
#include "basesingleton.hpp"
#include "string.hpp"

// =================================================================================================

enum class PlatformType {
	Steam,
	XBox,
	Unknown
};

// =================================================================================================

class PlatformInterface {
public:
	virtual ~PlatformInterface() = default;

	virtual bool Login(void) {
		return false;
	}

	virtual void Logout(void) {};

	virtual void Update(void) {};

	virtual uint64_t GetUserID(void) const {
		return 0;
	}

	virtual String GetLanguage(void) const {
		return String("");
	}
};

// =================================================================================================

class PlatformHandler
	: public BaseSingleton<PlatformHandler> {
public:
	bool Init(void);

	void Shutdown(void);

	void Update(void);

	inline PlatformType GetPlatformType(void) const
	noexcept
	{
		return m_platformType;
	}

	inline bool HavePlatform(void) const
	noexcept
	{
		return m_platformType != PlatformType::Unknown;
	}

	uint64_t GetUserID(void) const;

	String GetLanguage(void) const;

	static String SystemLanguage(void);

	static String SteamLanguageTag(const char* steamLanguage);

	PlatformInterface* GetInterface(void)
	noexcept
	{
		return m_activeInterface;
	}

	void SetInterface(PlatformType type, PlatformInterface* itf) {
		if (type != PlatformType::Unknown)
			m_interfaces[int(type)] = itf;
	}

private:
	PlatformType		m_platformType{ PlatformType::Unknown };
	PlatformInterface*	m_interfaces[2]{ nullptr, nullptr };
	PlatformInterface*	m_activeInterface{ nullptr };

	bool Init(PlatformType type);
};

#define platformHandler PlatformHandler::Instance()

// =================================================================================================
