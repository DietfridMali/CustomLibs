#pragma once

// =================================================================================================

class ResourceDescriptor
{
private:
	uint64_t m_owner{ 0 };  // ID of the CommandList that owns this descriptor
	uint64_t m_executionId{ 0 };  // ID of the last CommandList that executed this descriptor

public:
	void SetOwner(uint64_t ownerId) noexcept {
		m_owner = ownerId;
	}

	void SetExecutionId(uint64_t executionId) noexcept {
		m_executionId = executionId;
	}

	uint64_t GetOwner(void) const noexcept {
		return m_owner;
	}

	uint64_t GetExecutionId(void) const noexcept {
		return m_executionId;
	}
};

// =================================================================================================

