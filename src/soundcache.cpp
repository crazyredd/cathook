#include "common.hpp"
#include "soundcache.hpp"
std::map<int, SoundStruct> sound_cache;

void CreateMove()
{
    if (CE_BAD(LOCAL_E))
        return;
    CUtlVector<SndInfo_t> sound_list;
    g_ISoundEngine->GetActiveSounds(sound_list);
    for (auto i : sound_list)
    {
        sound_cache[i.m_nSoundSource].sound.m_pOrigin = *i.m_pOrigin;
        sound_cache[i.m_nSoundSource].last_update.update();
    }
}
static InitRoutine init([]() {
    EC::Register(EC::CreateMove, CreateMove, "CM_SoundCache");
    EC::Register(
        EC::LevelInit, []() { sound_cache.clear(); }, "soundcache_levelinit");
});
