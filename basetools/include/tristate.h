#pragma once

// =================================================================================================

template <typename T>
class Tristate {
protected:
	T m_state;
	T m_default;
	T m_undefined;

public:
	explicit Tristate(T _undefined, T _default, T _state)
		: m_undefined(_undefined), m_default(_default), m_state(_state)
	{ }

	explicit Tristate(T _undefined, T _default)
		: m_undefined(_undefined), m_default(_default), m_state(_undefined)
	{ }

	~Tristate() = default;

	inline T State(void) const noexcept { return m_state; }

	inline T Default(void) const noexcept { return m_default; }

	inline T Value(void) const noexcept { return (m_state == m_undefined) ? m_default : m_state; }

	inline Tristate& operator=(T state) {
		m_state = state; 
		return *this;
	}

	inline operator T() const noexcept { return Value(); }
};

// =================================================================================================

