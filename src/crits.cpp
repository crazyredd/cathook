/*
 * crits.cpp
 *
 *  Created on: Feb 25, 2017
 *      Author: nullifiedcat
 */

#include <settings/Bool.hpp>
#include "common.hpp"

std::unordered_map<int, int> command_number_mod{};

namespace criticals
{

static settings::Boolean crit_info{ "crit.info", "false" };
static settings::Button crit_key{ "crit.key", "<null>" };
static settings::Boolean crit_melee{ "crit.melee", "false" };
static settings::Boolean crit_legiter{ "crit.force-gameplay", "false" };
static settings::Boolean crit_experimental{ "crit.experimental", "false" };

int find_next_random_crit_for_weapon(IClientEntity *weapon)
{
    int tries = 0, number = current_user_cmd->command_number, found = 0, seed_backup;

    crithack_saved_state state{};
    state.Save(weapon);

    seed_backup = *g_PredictionRandomSeed;
    while (!found && tries < 4096)
    {
        *g_PredictionRandomSeed = MD5_PseudoRandom(number) & 0x7FFFFFFF;
        found                   = re::C_TFWeaponBase::CalcIsAttackCritical(weapon);
        if (found)
            break;
        ++tries;
        ++number;
    }

    *g_PredictionRandomSeed = seed_backup;
    if (!crit_experimental || g_pLocalPlayer->weapon_mode == weaponmode::weapon_melee)
        state.Load(weapon);
    if (found)
        return number;
    return 0;
}

void unfuck_bucket(IClientEntity *weapon)
{
    static bool changed;
    static float last_bucket;
    static int last_weapon;

    if (current_user_cmd->command_number)
        changed = false;

    float &bucket = re::C_TFWeaponBase::crit_bucket_(weapon);

    if (bucket != last_bucket)
    {
        if (changed && weapon->entindex() == last_weapon)
        {
            bucket = last_bucket;
        }
        changed = true;
    }
    last_weapon = weapon->entindex();
    last_bucket = bucket;
}

struct cached_calculation_s
{
    int command_number;
    int init_command;
    int weapon_entity;
};

static cached_calculation_s cached_calculation{};

static int number                = 0;
static int lastnumber            = 0;
static int lastusercmd           = 0;
static const model_t *lastweapon = nullptr;

bool force_crit(IClientEntity *weapon)
{
    auto command_number = current_user_cmd->command_number;

    if (lastnumber < command_number || lastweapon != weapon->GetModel() || lastnumber - command_number > 1000)
    {
        if (!*crit_experimental || g_pLocalPlayer->weapon_mode == weapon_melee)
        {
            if (cached_calculation.init_command > command_number || command_number - cached_calculation.init_command > 4096 || (command_number && (cached_calculation.command_number < command_number)))
                cached_calculation.weapon_entity = 0;
            if (cached_calculation.weapon_entity == weapon->entindex())
                return bool(cached_calculation.command_number);
        }
        number = find_next_random_crit_for_weapon(weapon);
    }
    else
        number = lastnumber;
    // logging::Info("Found critical: %d -> %d", command_number,
    //              number);
    if (crit_experimental && GetWeaponMode() != weapon_melee)
    {
        cached_calculation.command_number = number;
        cached_calculation.weapon_entity  = LOCAL_W->m_IDX;
        if (!crit_legiter)
        {
            if (number && number != command_number)
            {
                command_number_mod[command_number]                     = number;
                auto ch                                                = (INetChannel *) g_IEngine->GetNetChannelInfo();
                *(int *) ((uint64_t) ch + offsets::m_nOutSequenceNr()) = number - 1;
            }
        }
        else if (number && number != command_number && number - 30 < command_number)
        {

            command_number_mod[command_number]                     = number;
            auto ch                                                = (INetChannel *) g_IEngine->GetNetChannelInfo();
            *(int *) ((uint64_t) ch + offsets::m_nOutSequenceNr()) = number - 1;
        }
    }
    else
    {
        if (!crit_legiter)
        {
            if (command_number != number && number && number != command_number)
                current_user_cmd->buttons &= ~IN_ATTACK;
            else
                current_user_cmd->buttons |= IN_ATTACK;
        }
        else
        {
            if (command_number + 30 > number && number && number != command_number)
                current_user_cmd->buttons &= ~IN_ATTACK;
            else
                current_user_cmd->buttons |= IN_ATTACK;
        }
    }
    lastweapon = weapon->GetModel();
    lastnumber = number;
    return number != 0;
}

void create_move()
{
    if (!crit_key && !crit_melee)
        return;
    if (!random_crits_enabled())
        return;
    if (CE_BAD(LOCAL_W))
        return;
    if (current_user_cmd->command_number)
        lastusercmd = current_user_cmd->command_number;
    IClientEntity *weapon = RAW_ENT(LOCAL_W);
    if (!re::C_TFWeaponBase::IsBaseCombatWeapon(weapon))
        return;
    if (!re::C_TFWeaponBase::AreRandomCritsEnabled(weapon))
        return;
    if (!CanShoot())
        return;
    unfuck_bucket(weapon);
    if ((current_user_cmd->buttons & IN_ATTACK) && crit_key && crit_key.isKeyDown() && current_user_cmd->command_number)
    {
        force_crit(weapon);
    }
    else if ((current_user_cmd->buttons & IN_ATTACK) && current_user_cmd->command_number && GetWeaponMode() == weapon_melee && crit_melee && g_pLocalPlayer->weapon()->m_iClassID() != CL_CLASS(CTFKnife))
    {
        force_crit(weapon);
    }
}

bool random_crits_enabled()
{
    static ConVar *tf_weapon_criticals = g_ICvar->FindVar("tf_weapon_criticals");
    return tf_weapon_criticals->GetBool();
}

#if ENABLE_VISUALS
void draw()
{
    if (CE_BAD(LOCAL_W))
        return;
    IClientEntity *weapon = RAW_ENT(LOCAL_W);
    if (!weapon)
        return;
    if (!re::C_TFWeaponBase::IsBaseCombatWeapon(weapon))
        return;
    if (!re::C_TFWeaponBase::AreRandomCritsEnabled(weapon))
        return;
    if (crit_info && CE_GOOD(LOCAL_W))
    {
        if (crit_key.isKeyDown())
        {
            AddCenterString("FORCED CRITS!", colors::red);
        }
        IF_GAME(IsTF2())
        {
            if (!random_crits_enabled())
                AddCenterString("Random crits are disabled", colors::yellow);
            else
            {
                if (!re::C_TFWeaponBase::CanFireCriticalShot(RAW_ENT(LOCAL_W), false, nullptr))
                    AddCenterString("Weapon can't randomly crit", colors::yellow);
                else if (lastusercmd)
                {
                    if (number > lastusercmd)
                    {
                        float nextcrit = ((float) number - (float) lastusercmd) / (float) 90;
                        if (nextcrit > 0.0f)
                        {
                            AddCenterString(format("Time to next crit: ", nextcrit, "s"), colors::orange);
                        }
                    }
                    AddCenterString("Weapon can randomly crit");
                }
            }
            AddCenterString(format("Bucket: ", re::C_TFWeaponBase::crit_bucket_(RAW_ENT(LOCAL_W))));
        }
        // AddCenterString(format("Time: ",
        // *(float*)((uintptr_t)RAW_ENT(LOCAL_W) + 2872u)));
    }
}
#endif
static InitRoutine init([]() {
    EC::Register(EC::CreateMove, criticals::create_move, "cm_crits", EC::very_late);
#if ENABLE_VISUALS
    EC::Register(EC::Draw, criticals::draw, "draw_crits");
#endif
});
} // namespace criticals

void crithack_saved_state::Load(IClientEntity *entity)
{
    *(float *) (uintptr_t(entity) + 2868) = unknown2868;
    *(float *) (uintptr_t(entity) + 2864) = unknown2864;
    *(float *) (uintptr_t(entity) + 2880) = unknown2880;
    *(float *) (uintptr_t(entity) + 2616) = bucket2616;
    *(int *) (uintptr_t(entity) + 2620)   = unknown2620;
    *(int *) (uintptr_t(entity) + 2876)   = seed2876;
    *(char *) (uintptr_t(entity) + 2839)  = unknown2839;
}

void crithack_saved_state::Save(IClientEntity *entity)
{
    unknown2868 = *(float *) (uintptr_t(entity) + 2868);
    unknown2864 = *(float *) (uintptr_t(entity) + 2864);
    unknown2880 = *(float *) (uintptr_t(entity) + 2880);
    bucket2616  = *(float *) (uintptr_t(entity) + 2616);
    unknown2620 = *(int *) (uintptr_t(entity) + 2620);
    seed2876    = *(int *) (uintptr_t(entity) + 2876);
    unknown2839 = *(char *) (uintptr_t(entity) + 2839);
}
