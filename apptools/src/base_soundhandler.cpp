
#include <limits>

#include "arghandler.h"
#include "base_soundhandler.h"

// =================================================================================================

bool SoundObject::Play (int loops) {
    if (0 > Mix_PlayChannel(m_channel, m_sound, loops)) {
#ifdef _DEBUG
        logHandler.Print("Couldn't play sound '%s' (%s)\n", m_name.Data(), Mix_GetError());
#endif
        return false;
    }
    return true;
}

void SoundObject::FadeOut(int fadeTime) {
    Mix_FadeOutChannel(m_channel, fadeTime);
}

bool SoundObject::Stop (void) {
    Mix_HaltChannel (m_channel);
    return true;
}

void SoundObject::SetPanning (float left, float right) {
    Mix_SetPanning (m_channel, Uint8 (left * 255), Uint8 (right * 255));
}

void SoundObject::SetVolume (float volume) {
    Mix_Volume (m_channel, int (MIX_MAX_VOLUME * volume));
}

bool SoundObject::Busy (void) const {
    return bool (Mix_Playing (m_channel));
}

// =================================================================================================
// The sound handler class handles sound creation and sound channel management
// It tries to provide 128 sound channels. Each channel has a sound object in m_channels, indexed
// by the channel number. A channel is busy while the mixer plays a sound on it. When a new sound
// is to be played, the first idle channel is picked. If there are no idle channels available, the
// channel that has been playing for the longest time will be reused. 

bool BaseSoundHandler::Setup(String soundFolder) {
#if !(USE_STD || USE_STD_MAP)
    m_sounds.SetComparator(String::Compare);
#endif
#ifdef _DEBUG
    m_soundLevel = argHandler.IntVal("soundlevel", 0, 0);
#endif
    SetMasterVolume(float(argHandler.IntValChecked("soundvolume", 0, 100, 0, 100, false)) * 0.01f);
    SetMusicVolume(float(argHandler.IntValChecked("musicvolume", 0, 100, 0, 100, false)) * 0.01f);
    SetSoundPlayback(argHandler.BoolVal("playsound", 0, 1, false));
    SetMusicPlayback(argHandler.BoolVal("playmusic", 0, 1, false));
    m_maxAudibleDistance = 30.0f;
    Destroy();
#if 1
    m_supportsMP3 = (Mix_Init(MIX_INIT_MP3) & MIX_INIT_MP3) != 0;
#endif
    if (0 > Mix_OpenAudioDevice(48000, AUDIO_S16SYS, 2, 512, nullptr, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE)) {
        logHandler.Print("Couldn't initialize sound system (%s)\n", Mix_GetError());
        return true;
    }
    m_haveAudio = true;
    SetMusicVolume(m_musicVolume);
#if 0
    int frequency, channels;
    Uint16 format;
    Mix_QuerySpec(&frequency, &format, &channels);
#endif
    m_channelCount = Mix_AllocateChannels(128);
    m_channels.Resize(m_channelCount);
    for (int i = 0; i < m_channelCount; i++)
        m_channels[i] = SoundObject(-1, String(""), i);
    return LoadSounds(soundFolder);
}


// preload sound data. Sound data is kept in a dictionary. The sound name is the key to it.
bool BaseSoundHandler::LoadSounds(String soundFolder) {
    List<String> soundNames;
    if (0 == GetSoundNames(soundNames))
        return false;
    bool isComplete = true;
    for (auto& name : soundNames) {
        String fileName = soundFolder + name + ".wav";
        Mix_Chunk* sound = Mix_LoadWAV (fileName.Data());
        if (sound)
            m_sounds.Insert(name, sound);
        else {
            isComplete = false;
            logHandler.Print("Couldn't load sound '%s' (%s)\n", name.Data(), Mix_GetError());
        }
    }
    return isComplete;
}


void BaseSoundHandler::UpdateVolume(SoundObject& soundObject, float distance) {
    if (distance >= m_maxAudibleDistance)
        soundObject.SetVolume(0);
    else {
        float volume = (m_maxAudibleDistance - distance) / m_maxAudibleDistance;
        // use half of the angle for stereo panning. Always let the remote ear hear something, too. Pan effect the weaker the further away the sound is.
        float pan = (distance < Conversions::NumericTolerance) ? 0.0f : Pan(soundObject.m_position) * 0.5f * 0.9f * volume;   
        volume *= volume * soundObject.m_volume * m_masterVolume;
        soundObject.SetVolume(volume);
        soundObject.SetPanning(abs(-0.5f + pan), 0.5f + pan);
    }
}


// get a channel for playing back a new sound
// if all channels are busy, pick the oldest busy one
SoundObject& BaseSoundHandler::GetChannel(void) {
    int channel = Mix_GroupAvailable(-1);
    if (channel < 0) {
        channel = Mix_GroupOldest(-1);
        m_channels[channel].Stop();
    }
    SoundObject& so = m_channels[channel];
    so.m_id = ((so.m_id < 0) or (so.m_id > (std::numeric_limits<int>::max)() - m_channelCount)) ? channel : so.m_id + m_channelCount;
    return so;
}


SoundObject* BaseSoundHandler::FindSound(int id) {
    if ((id < 0) or (m_channelCount == 0))
        return nullptr;
    SoundObject& so = m_channels[id % m_channelCount];
    return ((so.m_id == id) and so.Busy()) ? &so : nullptr;
}


SoundObject* BaseSoundHandler::FindSoundByOwner(const void* owner, const String& soundName) {
    if (owner != nullptr) {
        for (auto& so : m_channels) {
            if ((so.m_owner == owner) and (so.m_name == soundName) and so.Busy()) 
                return &so;
        }
    }
    return nullptr;
}


// play back the sound with the soundName 'soundName'. Position, viewer and DistFunc serve for computing the sound volume
// depending on the distance of the viewer to the sound position
SoundObject* BaseSoundHandler::Start(const String& soundName, const SoundParams& params, const Vector3f position, const void* owner) {
    //return -1;
    if (not m_playSound)
        return nullptr;
#ifdef _DEBUG // filter out sounds depending on their level
    if ((m_soundLevel == 0) or (params.level > m_soundLevel))
        return nullptr;
#endif
#if 0
    if (not position.IsValid())
        return nullptr;
#endif
    SoundObject* activeSound = FindSoundByOwner(owner, soundName);
    if (activeSound != nullptr)
        return activeSound;
    Mix_Chunk** sound = m_sounds.Find(soundName);
    if (not sound)
        return nullptr;
    SoundObject& newSound = GetChannel();
    newSound.m_name = soundName;
    newSound.m_sound = *sound; // using a copy of the sound because each channel's overall volume has to be set via the sound
    newSound.SetVolume(params.volume);
    newSound.m_volume = params.volume;
    newSound.m_position = position;
    newSound.m_owner = const_cast<void*>(owner);
    if (not newSound.Play(params.loops))
        return nullptr;
    UpdateSound(newSound);
    return &newSound;
}


void BaseSoundHandler::Stop(int id) {
    SoundObject* so = FindSound(id);
    if (so)
        so->Stop();
}


void BaseSoundHandler::StopSoundsByOwner(void* owner) {
    if (owner != nullptr) {
        for (auto& so : m_channels) {
            if ((so.m_owner == owner) and so.Busy())
                so.Stop();
        }
    }
}


void BaseSoundHandler::FadeOut(int id, int fadeTime) {
    SoundObject* so = FindSound(id);
    if (so)
        so->FadeOut(fadeTime);
}


// update sound volumes
void BaseSoundHandler::Update(void) {
    for (auto& so : m_channels) {
        if (so.Busy())
            UpdateSound(so);
    }
}


void BaseSoundHandler::PauseAudio(bool pause) {
    if (m_haveAudio)
        Mix_PauseAudio(pause ? 1 : 0);
}


void BaseSoundHandler::Destroy(void) {
    if (m_haveAudio) {
        m_haveAudio = false;
        for (auto& sound : m_sounds)
			Mix_FreeChunk(sound.second);
        m_sounds.Destroy();
        Mix_CloseAudio();
        m_channels.Reset();
        m_channelCount = 0;
        if (m_song) {
            Mix_FreeMusic(m_song);
            m_song = nullptr;
        }
    }
    Mix_Quit();
}


void BaseSoundHandler::StopMusic(void) {
    if (m_song) {
        Mix_HaltMusic();
        Mix_FreeMusic(m_song);
        m_song = nullptr;
    }
}

bool BaseSoundHandler::PlayMusic(String songName, int loops, int fadeTime) {
    if (not m_haveAudio or (m_musicVolume == 0.0f))
        return false;
    String s = songName.ToLowercase();
    if ((s.Find(".mp3") != -1) and not m_supportsMP3)
        return false;
    StopMusic();
    if (not m_playMusic or songName.IsEmpty())
        return false;
    m_lastSong = songName;
    if (not (m_song = Mix_LoadMUS((const char*)songName)))
        return false;
    if (0 == ((fadeTime > 0) ? Mix_FadeInMusic(m_song, loops, fadeTime) : Mix_PlayMusic(m_song, loops)))
        return true;
    Mix_FreeMusic(m_song);
    m_song = nullptr;
    return false;
}


void BaseSoundHandler::SetMusicPlayback(bool play) {
    if (m_playMusic != play) {
        if ((m_playMusic = play)) {
            if (not m_lastSong.IsEmpty())
                PlayMusic(m_lastSong);
        }
        else {
            StopMusic();
        }
    }
}


// =================================================================================================
