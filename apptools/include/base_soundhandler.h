#pragma once

#include <math.h>
#include "std_defines.h"
#include "conversions.hpp"
#include "SDL.h"
#include "SDL_mixer.h"
#include "string.hpp"
#include "vector.hpp"
#include "array.hpp"
#include "list.hpp"
#include "dictionary.hpp"
#include "basesingleton.hpp"

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
        size_t      m_startTime;
        size_t      m_endTime;

        SoundObject(int id = -1, String name = String(""), int channel = -1, Mix_Chunk * sound = nullptr, Vector4f position = {0, 0, 0}, float volume = 1.0f)
            : m_id(id)
            , m_name(name)
            , m_channel(channel)
            , m_sound(sound)
            , m_position(position)
            , m_owner (nullptr)
            , m_volume(volume)
            , m_startTime (0)
            , m_endTime(0)
        {}

        ~SoundObject () {
            if (Busy ())
                Stop ();
        }

        bool Play (int loops = 0);

        void FadeOut(int fadeTime);

        bool Stop (void);

        void SetPanning (float left, float right);

        void SetVolume (float volume);

        bool Busy (void) const;

        bool IsSilent(void) const;
    };

    // =================================================================================================
// The sound handler class handles sound creation and sound channel management
// It tries to provide 128 sound channels. They are preinitialized and are kept in m_idleChannels
// (list of available channels) and busyChannels (list of channels currently used for playing back sound)
// When a new sound is to played, a channel is picked from the idleChannels list. If there are no idle
// channels available, the oldest playing channel from busyChannels will be reused. Since channels are 
// append to busyChannels in the temporal sequence they are deployed, the first channel in busyChannels
// will always be the oldest one.

class BaseSoundHandler 
    : public PolymorphSingleton<BaseSoundHandler>
{
    public:
        Dictionary<String, Mix_Chunk*>  m_sounds;
        List<SoundObject>               m_idleChannels;
        List<SoundObject>               m_busyChannels;
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
        bool                            m_supportsOGG { false };
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

#pragma warning(push)
#pragma warning(disable:4100)
        virtual int32_t GetSoundNames(List<String>& soundNames) { return 0; }
#pragma warning(pop)

        static BaseSoundHandler& Instance(void) { return dynamic_cast<BaseSoundHandler&>(PolymorphSingleton::Instance()); }

        // preload sound data. Sound data is kept in a dictionary. The sound name is the key to it.
        bool LoadSounds(String soundFolder);

        SoundObject* FindSoundByOwner(const void* owner, const String& soundName);

        inline SoundObject* FindSoundByOwner(const void* owner, String&& soundName) {
            return FindSoundByOwner(owner, static_cast<const String&>(soundName));
        }

        // update all sound volumes depending on application specific cirumstances (e.g. listener or sound source have been moving)
#pragma warning(push)
#pragma warning(disable:4100)
        virtual void UpdateSound(SoundObject& soundObject) { }
#pragma warning(pop)


        // play back the sound with the name 'name'. Position, viewer and DistFunc serve for computing the sound volume
        // depending on the distance of the viewer to the sound position
        SoundObject* Start(const String& soundName, const SoundParams& params, size_t startTime, const Vector3f position = Vector3f::NONE, const void* owner = nullptr);

        template <typename T>
        inline int Play(T&& soundName, const SoundParams& params, size_t startTime, const Vector3f position = Vector3f::NONE, const void* owner = nullptr) {
            SoundObject* activeSound = Start(std::forward<T>(soundName), params, startTime, position, owner);
            return (activeSound == nullptr) ? -1 : activeSound->m_id;
        }

        void FadeOut(int id, int fadeTime);
            
        void Stop(int id, void* owner = nullptr);

        inline bool IsPlaying(int id) {
            return Mix_Playing(id);
        }

        void StopSoundsByOwner(void* owner);

        // move all channels that are not playing back sound anymore from the busyChannels to the idleChannels list
        void Cleanup(void);

        // cleanup expired channels and update sound volumes
        void Update(void);

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

        inline float GetMasterVolume(void) noexcept {
            return m_masterVolume;
        }

        inline float GetMusicVolume(void) noexcept {
            return m_musicVolume;
        }

        void SetMasterVolume(float volume) {
            m_masterVolume = volume;
            for (auto& so : m_busyChannels)
                UpdateSound(so);
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

        inline bool SupportsOGG(void) noexcept {
            return m_supportsOGG;
        }

private:
        // compute stereo panning from the angle between the viewer direction and the vector from the viewer to the sound source
#pragma warning(push)
#pragma warning(disable:4100)
    virtual float Pan(Vector3f& position) { return 0.0f; }
#pragma warning(pop)

        void UpdateVolume(SoundObject& soundObject, float distance);

        // get a channel for playing back a new sound
        // if all channels are busy, pick the oldest busy one
        SoundObject& GetChannel(void);

        template <typename Predicate>
        void ConditionalStop(Predicate condition, void* owner = nullptr)
        {
            for (auto it = m_busyChannels.begin(); it != m_busyChannels.end(); )
            {
                SoundObject& c = *it;
                if (not (condition(c) and c.Stop()))
                    ++it;
                else {
#ifdef _DEBUG
                    if (owner and (c.m_owner != owner))
                        fprintf(stderr, "stopped other owner's sound\n");
#endif
                    m_idleChannels.Append(c);
                    // Achtung: erase mit reverse_iterator!
                    it = m_busyChannels.Discard(it);
                }
            }
        }
};

// =================================================================================================
