
#pragma once

#include "glew.h"
#include "array.hpp"
#include "sharedglhandle.hpp"

class BaseSSBO {
public:
	static inline bool IsAvailable;
	
	BaseSSBO() {
		IsAvailable = (strstr((const char*)glGetString(GL_EXTENSIONS), "GL_ARB_shader_storage_buffer_object"));
	}
};

template <typename DATA_T>
class SSBO
	: public BaseSSBO
{
public:
	SharedGLHandle			m_handle;
	ManagedArray<DATA_T>	m_data;

	SSBO()
	{
		m_handle = SharedGLHandle(0, glGenBuffers, glDeleteBuffers);
	}


	inline DATA_T* Data(void) {
		return m_data.Data();
	}


	inline int DataSize(void) {
		return m_data.DataSize();
	}


	bool Create(int size = 0) {
		if (not IsAvailable)
			return false;
		if (m_handle.Claim() == 0)
			return false;
		m_data.Resize(size);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_handle);
		glBufferData(GL_SHADER_STORAGE_BUFFER, DataSize(), Data(), GL_DYNAMIC_DRAW);
		return true;
	}


	void Destroy(void) {
		if (m_handle) {
			Release();
			m_handle.Release();
			m_data.Destroy();
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		}
	}

	bool Bind(GLuint bindingPoint) {
		if (not m_handle)
			return false;
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, m_handle);
		return true;
	}


	void Release(GLuint bindingPoint) {
		if (m_handle)
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, 0);
	}


	bool Upload(void) {
		if (not m_handle)
			return false;
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_handle.Handle());
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, this->DataSize(), this->Data());
		return true;
	}


	bool Download(void) const {
		if (not m_handle)
			return false;
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_handle.Handle());
		void* ptr = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
		if (not ptr)
			return false;
		memcpy(this->Data(), ptr, this->DataSize());
		glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
		return true;
	}


	inline GLuint GetHandle(void) {
		return GLuint(m_handle);
	}

	void Clear(DATA_T value) {
		if (m_handle)
			glClearNamedBufferData(m_handle, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &value);
	}
};