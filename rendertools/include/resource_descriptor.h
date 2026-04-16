#pragma once

#include "string.hpp"

// =================================================================================================

class ResourceDescriptor
{
private:
	uint64_t	m_ownerId{ 0 };  // ID of the CommandList that owns this descriptor
	uint64_t	m_executionId{ 0 };  // ID of the last CommandList that executed this descriptor
	String		m_ownerName{ "" };  // optional debug name of the owning CommandList

public:
	inline void SetOwner(uint64_t id, String name = "") noexcept {
		m_ownerId = id;
		m_ownerName = name;
	}

	inline uint64_t GetExecutionId(void) const noexcept {
		return m_executionId;
	}

	inline void	SetOwnerName(const String& ownerName) noexcept {
		m_ownerName = ownerName;
	}

	inline void SetExecutionId(uint64_t executionId) noexcept {
		m_executionId = executionId;
	}

	inline uint64_t GetOwnerId(void) const noexcept {
		return m_ownerId;
	}

	inline String GetOwnerName(void) const noexcept {
		return m_ownerName;
	}
};

// =================================================================================================

