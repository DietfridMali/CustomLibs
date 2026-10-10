#pragma once

#include <math.h>
#include "std_defines.h"
#include "conversions.hpp"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:26819)
#endif
#include "SDL.h"
#include "SDL_mixer.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "string.hpp"
#include "vector.hpp"
#include "array.hpp"
#include "list.hpp"
#include "dictionary.hpp"
#include "basesingleton.hpp"
#include "loghandler.h"

// =================================================================================================

class SoundObject
{
    public:
        int         m_id;
        String      m_name;
        Mix_Chunk*  m_sound;
        int         m_channel;
        float       m_volume;
        Vector3f    m_position;
        void*       m_owner;

        SoundObject(int id = -1, String name = String(""), int channel = -1, Mix_Chunk * sound = nullptr, Vector4f position = {0, 0, 0}, float volume = 1.0f)
            : m_id(id)
            , m_name(name)
            , m_channel(channel)
            , m_sound(sound)
            , m_position(position)
            , m_owner (nullptr)
            , m_volume(volume)
        {}

        bool Play (int loops = 0);

        void FadeOut(int fadeTime);

        bool Stop (void);

        void SetPanning (float left, float right);

        void SetVolume (float volume);

        bool Busy (void) const;
    };

    // =================================================================================================
// The sound handler class handles sound creation and sound channel management
// It tries to provide 128 sound channels. Each channel has a sound object in m_channels, indexed
// by the channel number. A channel is busy while the mixer plays a sound on it. When a new sound
// is to be played, the first idle channel is picked. If there are no idle channels available, the
// channel that has been playing for the longest time will be reused. 

class BaseSoundHandler 
    : public PolymorphSingleton<BaseSoundHandler>
{
    public:
        Dictionary<String, Mix_Chunk*>  m_sounds;
        AutoArray<SoundObject>          m_channels;
        Mix_Music*                      m_song{ nullptr };
        int                             m_soundLevel{ 0 }; // maximum
        float                           m_masterVolume{ 1.0f };
        float                           m_musicVolume{ 1.0f };
        float                           m_maxAudibleDistance{ 0.0f };
        int                             m_channelCount{ 0 };
        bool                            m_haveAudio{ false };
        bool                            m_playSound{ true };
        bool                            m_playMusic{ true };
        bool                            m_supportsMP3 { false };
        String                          m_lastSong{ "" };

        struct SoundParams {
            float volume = 1.0f;
            int loops = 0;
            int level = 1;
        };

        BaseSoundHandler() 
        { 
            _instance = this;
        }

        virtual ~BaseSoundHandler() {
            Destroy();
        }

        void Destroy();

        virtual bool Setup(String soundFolder);

        virtual int32_t GetSoundNames(List<String>& /*soundNames*/) { return 0; }

        static BaseSoundHandler& Instance(void) { return dynamic_cast<BaseSoundHandler&>(PolymorphSingleton::Instance()); }

        // preload sound data. Sound data is kept in a dictionary. The sound name is the key to it.
        bool LoadSounds(String soundFolder);

        SoundObject* FindSoundByOwner(const void* owner, const String& soundName);

        inline SoundObject* FindSoundByOwner(const void* owner, String&& soundName) {
            return FindSoundByOwner(owner, static_cast<const String&>(soundName));
        }

        SoundObject* FindSound(int id);

        // update all sound volumes depending on application specific cirumstances (e.g. listener or sound source have been moving)
        virtual void UpdateSound(SoundObject& /*soundObject*/) { }


        // play back the sound with the name 'name'. Position, viewer and DistFunc serve for computing the sound volume
        // depending on the distance of the viewer to the sound position
        SoundObject* Start(const String& soundName, const SoundParams& params, const Vector3f position = Vector3f::NONE, const void* owner = nullptr);

        template <typename T>
        inline int Play(T&& soundName, const SoundParams& params, const Vector3f position = Vector3f::NONE, const void* owner = nullptr) {
            SoundObject* activeSound = Start(std::forward<T>(soundName), params, position, owner);
            return (activeSound == nullptr) ? -1 : activeSound->m_id;
        }

        void FadeOut(int id, int fadeTime);
            
        void Stop(int id);

        inline bool IsPlaying(int id) {
            return FindSound(id) != nullptr;
        }

        void StopSoundsByOwner(void* owner);

        // update sound volumes
        void Update(void);

        void PauseAudio(bool pause);

        bool PlayMusic(String songName, int loops = 0, int fadeTime = 0);

        void StopMusic(void);

        void SetSoundPlayback(bool play) {
            m_playSound = play;
        }

        bool GetSoundPlayback(void) {
            return m_playSound;
        }

        void SetMusicPlayback(bool play);

        inline void ToggleMusicPlayback(void) noexcept {
            SetMusicPlayback(not m_playMusic);
        }

        bool GetMusicPlayback(void) {
            return m_playMusic;
        }

        inline bool CanPlayMusic(void) noexcept {
            return m_haveAudio and m_playMusic and (m_musicVolume > 0.0f);
        }

        inline bool IsPlayingMusic(void) {
            return Mix_PlayingMusic() != 0;
        }

        inline float GetMasterVolume(void) noexcept {
            return m_masterVolume;
        }

        inline float GetMusicVolume(void) noexcept {
            return m_musicVolume;
        }

        void SetMasterVolume(float volume) {
            m_masterVolume = volume;
            for (auto& so : m_channels) {
                if (so.Busy())
                    UpdateSound(so);
            }
        }

        void SetMusicVolume(float volume) {
            m_musicVolume = volume;
            Mix_VolumeMusic((int(round(MIX_MAX_VOLUME * volume))));
        }

        void FadeOutMusic(int duration = 1000) {
             Mix_FadeOutMusic(duration);
        }

        inline bool SupportsMP3(void) noexcept {
            return m_supportsMP3;
        }

protected:
        void UpdateVolume(SoundObject& soundObject, float distance);

private:
        // compute stereo panning from the angle between the viewer direction and the vector from the viewer to the sound source
    virtual float Pan(Vector3f& /*position*/) { return 0.0f; }

        // get a channel for playing back a new sound
        // if all channels are busy, pick the oldest busy one
        SoundObject& GetChannel(void);
};

// =================================================================================================
