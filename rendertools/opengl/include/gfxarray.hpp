
#pragma once

#include "glew.h"
#include "array.hpp"
#include "gfxstates.h"
#include "sharedgfxhandle.hpp"
#include "gfxtypes.h"

class BaseGfxArray {
public:
	// Asked fresh, NOT snapshotted in the constructor. A GfxArray can be a member of an object with
	// static storage duration, and then its constructor runs long before there is a GL context: the
	// query would answer "no extensions", the snapshot would say "no SSBOs", and the array would stay
	// unavailable for the rest of the run - silently, because Create () just returns false.
	// (It was a shared static on top, so the LAST array constructed decided for all of them.)
	static bool IsAvailable(void) {
		// Two ways to have shader storage buffers, and a core profile context may only offer the second:
		// a 3.3 driver advertises them through the ARB extension, but from GL 4.3 on they are CORE and the
		// driver is free to stop listing that extension altogether. Asking for the string alone therefore
		// turned SSBOs off on exactly the contexts that have them built in - the same trap that once hid
		// multitexturing, VBOs and occlusion queries in d2x-xl.
		return gfxStates.HasExtension("GL_ARB_shader_storage_buffer_object") or gfxStates.HaveFeatureLevel(430);
	}

	BaseGfxArray() = default;
};

template <typename DATA_T, typename STORAGE_T = GfxTypes::UavTexture>
class GfxArray
	: public BaseGfxArray
{
public:
	SharedGfxHandle		m_handle;
	AutoArray<DATA_T>	m_data;

	GfxArray()
	{
		m_handle = SharedGfxHandle(0, glGenBuffers, glDeleteBuffers);
	}


	inline DATA_T* Data(void) {
		return m_data.Data();
	}


	inline int DataSize(void) {
		return m_data.DataSize();
	}


	bool Create(int width, int height = 1) {
		if (not IsAvailable ())
			return false;
		if (not gfxStates.HaveFeatureLevel(GfxStates::SSBOFeatureLevel))
			return false;
		// Set the handle up HERE and not in the constructor. SharedGfxHandle copies the allocator into a
		// lambda, and with GLEW glGenBuffers is not a function but a variable that glewInit () fills in.
		// A GfxArray that is a member of an object with static storage duration is therefore built while
		// that variable is still null, and the lambda keeps the null forever - Claim () then returns 0 and
		// Create () fails for the rest of the run. By the time anyone calls Create () there is a context.
		// (This is why it works in Paintjob Rampage: its handlers are BaseSingletons, constructed lazily
		// on first use, so their arrays never see the uninitialised GLEW pointers.)
		m_handle = SharedGfxHandle(0, glGenBuffers, glDeleteBuffers);
		if (m_handle.Claim() == 0)
			return false;
		int size = width * height;
		m_data.Resize(size);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_handle);
		glBufferData(GL_SHADER_STORAGE_BUFFER, DataSize(), Data(), GL_DYNAMIC_DRAW);
		return true;
	}


	void Destroy(void) {
		if (m_handle) {
			m_handle.Release();
			m_data.Reset();
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
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_handle);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, this->DataSize(), this->Data());
		return true;
	}


	// Upload only [first, first+count) elements; leaves the rest of the GPU buffer untouched.
	// Used to spawn one particle system without resetting the others.
	bool UploadRange(int first, int count) {
		if (not m_handle or (count <= 0))
			return false;
		int elemSize = int(sizeof(DATA_T));
		GLintptr offset = GLintptr(first) * elemSize;
		GLsizeiptr bytes = GLsizeiptr(count) * elemSize;
		if (offset + bytes > GLsizeiptr(this->DataSize()))
			return false;
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_handle);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, bytes, reinterpret_cast<const uint8_t*>(this->Data()) + size_t(offset));
		return true;
	}


	bool Download(void) const {
		if (not m_handle)
			return false;
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_handle);
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