
#include "arghandler.h"
#include "base_soundhandler.h"

// =================================================================================================

bool SoundObject::Play (int loops) {
    if (0 > Mix_PlayChannel(m_channel, m_sound, loops)) {
        fprintf(stderr, "Couldn't play sound '%s' (%s)\n", m_name.Data(), Mix_GetError());
        return false;
    }
    m_startTime = SDL_GetTicks();
#if 0
    else if (Busy ())
        fprintf (stderr, "playing '%s' on channel %d (%d loops)\n", m_name.Data(), m_channel, loops);
    else
        fprintf (stderr, "Couldn't play sound '%s' (%s)\n", m_name.Data(), Mix_GetError ());
#endif
    return true;
}

void SoundObject::FadeOut(int fadeTime) {
    m_endTime = SDL_GetTicks() + fadeTime;
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

bool SoundObject::IsSilent(void) const {
    return (m_endTime > 0) and (m_endTime < SDL_GetTicks());
}

// =================================================================================================
// The sound handler class handles sound creation and sound channel management
// It tries to provide 128 sound channels. They are preinitialized and are kept in m_idleChannels
// (list of available channels) and busyChannels (list of channels currently used for playing back sound)
// When a new sound is to played, a channel is picked from the idleChannels list. If there are no idle
// channels available, the oldest playing channel from busyChannels will be reused. Since channels are 
// append to busyChannels in the temporal sequence they are deployed, the first channel in busyChannels
// will always be the oldest one.

bool BaseSoundHandler::Setup(String soundFolder) {
#if !(USE_STD || USE_STD_MAP)
    m_sounds.SetComparator(String::Compare);
#endif
    m_soundLevel = argHandler.IntVal("soundlevel", 0, 1);
    m_masterVolume = argHandler.FloatVal("masterVolume", 0, 1);
    m_maxAudibleDistance = 30.0f;
    Mix_Quit();
    Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG);
    if (0 > Mix_OpenAudio(48000, AUDIO_S16SYS, 2, 512))
        fprintf(stderr, "Couldn't initialize sound system (%s)\n", Mix_GetError());
#if 0
    int frequency, channels;
    Uint16 format;
    Mix_QuerySpec(&frequency, &format, &channels);
#endif
    Mix_Volume(-1, MIX_MAX_VOLUME);
    Mix_AllocateChannels(128);
    m_channelCount = Mix_AllocateChannels(-1);
    for (int i = 0; i < m_channelCount; i++)
        m_idleChannels.Append(SoundObject(i, String(""), i));
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
            fprintf(stderr, "Couldn't load sound '%s' (%s)\n", name.Data(), Mix_GetError());
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
        float pan = Pan(soundObject.m_position) * 0.5f * 0.9f * volume;   
        volume *= volume * soundObject.m_volume * m_masterVolume;
        soundObject.SetVolume(volume);
        soundObject.SetPanning(abs(-0.5f + pan), 0.5f + pan);
    }
}


// get a channel for playing back a new sound
// if all channels are busy, pick the oldest busy one
SoundObject& BaseSoundHandler::GetChannel(void) {
    if (not m_idleChannels.IsEmpty()) {
        m_busyChannels.Append(m_idleChannels.Last());
        m_idleChannels.DiscardLast();
    }
    else {
        m_busyChannels[0].Stop();
        m_busyChannels.Append(m_busyChannels.First());
        m_idleChannels.DiscardFirst();
    }
    return m_busyChannels[-1];
}


SoundObject* BaseSoundHandler::FindSoundByOwner(const void* owner, const String& soundName) {
    if (owner != nullptr) {
        for (auto& so : m_busyChannels) {
            if ((so.m_owner == owner) and (so.m_name == soundName)) 
                return &so;
        }
    }
    return nullptr;
}


// play back the sound with the soundName 'soundName'. Position, viewer and DistFunc serve for computing the sound volume
// depending on the distance of the viewer to the sound position
SoundObject* BaseSoundHandler::Start(const String& soundName, const SoundParams& params, size_t startTime, const Vector3f position, const void* owner) {
    //return -1;
    if ((m_soundLevel == 0) or (params.level > m_soundLevel))
        return nullptr;
    if (not position.IsValid())
        return nullptr;

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
    newSound.m_startTime = startTime;
    newSound.m_owner = const_cast<void*>(owner);
    if (not newSound.Play(params.loops))
        return nullptr;
    UpdateSound(newSound);
    //fprintf(stderr, "playing '%s' (%d)\n", soundName.Data(), soundObject->m_id);
    return &newSound;
}


void BaseSoundHandler::Stop(int id) {
    ConditionalStop([id](const SoundObject& so) { return so.m_id == id; });
}


void BaseSoundHandler::StopSoundsByOwner(void* owner) {
    if (owner != nullptr)
        ConditionalStop([owner](const SoundObject& so) { return so.m_owner == owner; });
}


// move all channels that are not playing back sound anymore from the busyChannels to the idleChannels list
void BaseSoundHandler::Cleanup(void) {
    ConditionalStop([](const SoundObject& so) { return not so.Busy(); });
}


void BaseSoundHandler::FadeOut(int id, int fadeTime) {
    for (auto so : m_busyChannels)
        if ((so.m_id == id) and so.Busy()) {
            so.FadeOut(fadeTime);
            break;
        }
}


// cleanup expired channels and update sound volumes
void BaseSoundHandler::Update(void) {
    Cleanup();
    for (auto& so : m_busyChannels)
        UpdateSound(so);
}

BaseSoundHandler* baseSoundHandler = nullptr;

// =================================================================================================
