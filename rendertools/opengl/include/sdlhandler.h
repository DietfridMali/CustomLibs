#pragma once

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL.h"
#pragma warning(pop)

#include "basesingleton.hpp"

// =================================================================================================
// Wrapper for SDL_Init and SDL_Quit. Collects all SDL subsystem flags actually used in m_subSystems.
// Calls SDL_QuitSubSystem with these flags, as calling SDL_Quit() when not all subsystems have been
// initialized with SDL_Init(SDL_INIT_EVERYTHING) can cause crashes in SDL2.

class SDLHandler 
	: public BaseSingleton<SDLHandler>
{
private:
	uint32_t	m_subSystems{ 0 };

public:
	inline uint32_t Init(uint32_t subSystems) {
		uint32_t failed = 0;
		for (uint32_t i = 1; i; i <<= 1) {
			if ((subSystems & i) and not (m_subSystems & i)) {
				if (SDL_Init(i)) 
					failed |= i;
				else
					m_subSystems |= i;
			}
		}
		return failed;
	}

	inline void Quit(void) {
		for (uint32_t i = 1; i; i <<= 1)
			if (m_subSystems & i)
				SDL_QuitSubSystem(i);
	}
};

#define sdlHandler SDLHandler::Instance()

// =================================================================================================
