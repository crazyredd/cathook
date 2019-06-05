/*
 * CreateMove.cpp
 *
 *  Created on: Jan 8, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"
#include "hack.hpp"
#include "MiscTemporary.hpp"
#include "SeedPrediction.hpp"
#include <link.h>
#include <hacks/hacklist.hpp>
#include <settings/Bool.hpp>
#include <hacks/AntiAntiAim.hpp>
#include "NavBot.hpp"
#include "HookTools.hpp"
#include "irc.hpp"

#include "HookedMethods.hpp"
#include "PreDataUpdate.hpp"

static settings::Boolean minigun_jump{ "misc.minigun-jump-tf2c", "false" };
static settings::Boolean roll_speedhack{ "misc.roll-speedhack", "false" };
static settings::Boolean engine_pred{ "misc.engine-prediction", "true" };
static settings::Boolean debug_projectiles{ "debug.projectiles", "false" };
static settings::Int semiauto{ "misc.semi-auto", "0" };
static settings::Int fakelag_amount{ "misc.fakelag", "0" };
static settings::Boolean fuckmode{ "misc.fuckmode", "false" };

#if !ENABLE_VISUALS
static settings::Boolean no_shake{ "visual.no-shake", "true" };
#endif

class CMoveData;
namespace engine_prediction
{

void RunEnginePrediction(IClientEntity *ent, CUserCmd *ucmd)
{
    if (!ent)
        return;
    typedef void (*SetupMoveFn)(IPrediction *, IClientEntity *, CUserCmd *, class IMoveHelper *, CMoveData *);
    typedef void (*FinishMoveFn)(IPrediction *, IClientEntity *, CUserCmd *, CMoveData *);

    void **predictionVtable  = *((void ***) g_IPrediction);
    SetupMoveFn oSetupMove   = (SetupMoveFn)(*(unsigned *) (predictionVtable + 19));
    FinishMoveFn oFinishMove = (FinishMoveFn)(*(unsigned *) (predictionVtable + 20));
    // CMoveData *pMoveData = (CMoveData*)(sharedobj::client->lmap->l_addr +
    // 0x1F69C0C);  CMoveData movedata {};
    auto object          = std::make_unique<char[]>(165);
    CMoveData *pMoveData = (CMoveData *) object.get();

    // Backup
    float frameTime = g_GlobalVars->frametime;
    float curTime   = g_GlobalVars->curtime;

    CUserCmd defaultCmd{};
    if (ucmd == nullptr)
    {
        ucmd = &defaultCmd;
    }

    // Set Usercmd for prediction
    NET_VAR(ent, 4188, CUserCmd *) = ucmd;

    // Set correct CURTIME
    g_GlobalVars->curtime   = g_GlobalVars->interval_per_tick * NET_INT(ent, netvar.nTickBase);
    g_GlobalVars->frametime = g_GlobalVars->interval_per_tick;

    *g_PredictionRandomSeed = MD5_PseudoRandom(current_user_cmd->command_number) & 0x7FFFFFFF;

    // Run The Prediction
    g_IGameMovement->StartTrackPredictionErrors(reinterpret_cast<CBasePlayer *>(ent));
    oSetupMove(g_IPrediction, ent, ucmd, NULL, pMoveData);
    g_IGameMovement->ProcessMovement(reinterpret_cast<CBasePlayer *>(ent), pMoveData);
    oFinishMove(g_IPrediction, ent, ucmd, pMoveData);
    g_IGameMovement->FinishTrackPredictionErrors(reinterpret_cast<CBasePlayer *>(ent));

    // Reset User CMD
    NET_VAR(ent, 4188, CUserCmd *) = nullptr;

    g_GlobalVars->frametime = frameTime;
    g_GlobalVars->curtime   = curTime;

    // Adjust tickbase
    NET_INT(ent, netvar.nTickBase)++;
    return;
}
} // namespace engine_prediction

void PrecalculateCanShoot()
{
    auto weapon = g_pLocalPlayer->weapon();
    // Check if player and weapon are good
    if (CE_BAD(g_pLocalPlayer->entity) || CE_BAD(weapon))
    {
        calculated_can_shoot = false;
        return;
    }

    // flNextPrimaryAttack without reload
    static float next_attack = 0.0f;
    // Last shot fired using weapon
    static float last_attack = 0.0f;
    // Last weapon used
    static CachedEntity *last_weapon = nullptr;
    float server_time                = (float) (CE_INT(g_pLocalPlayer->entity, netvar.nTickBase)) * g_GlobalVars->interval_per_tick;
    float new_next_attack            = CE_FLOAT(weapon, netvar.flNextPrimaryAttack);
    float new_last_attack            = CE_FLOAT(weapon, netvar.flLastFireTime);

    // Reset everything if using a new weapon/shot fired
    if (new_last_attack != last_attack || last_weapon != weapon)
    {
        next_attack = new_next_attack;
        last_attack = new_last_attack;
        last_weapon = weapon;
    }
    // Check if can shoot
    calculated_can_shoot = next_attack <= server_time;
}

static int attackticks = 0;
namespace hooked_methods
{
DEFINE_HOOKED_METHOD(CreateMove, bool, void *this_, float input_sample_time, CUserCmd *cmd)
{
#define TICK_INTERVAL (g_GlobalVars->interval_per_tick)
#define TIME_TO_TICKS(dt) ((int) (0.5f + (float) (dt) / TICK_INTERVAL))
#define TICKS_TO_TIME(t) (TICK_INTERVAL * (t))
#define ROUND_TO_TICKS(t) (TICK_INTERVAL * TIME_TO_TICKS(t))
    volatile uintptr_t **fp;
    __asm__ volatile("mov %%ebp, %0" : "=r"(fp));
    bSendPackets = reinterpret_cast<bool *>(**fp - 8);

    g_Settings.is_create_move = true;
    bool time_replaced, ret, speedapplied;
    float curtime_old, servertime, speed, yaw;
    Vector vsilent, ang;

    tickcount++;
    current_user_cmd = cmd;
    IF_GAME(IsTF2C())
    {
        if (CE_GOOD(LOCAL_W) && minigun_jump && LOCAL_W->m_iClassID() == CL_CLASS(CTFMinigun))
            CE_INT(LOCAL_W, netvar.iWeaponState) = 0;
    }
    ret = original::CreateMove(this_, input_sample_time, cmd);

    if (!cmd)
    {
        g_Settings.is_create_move = false;
        return ret;
    }

    // Disabled because this causes EXTREME aimbot inaccuracy
    // Actually dont disable it. It causes even more inaccuracy
    if (!cmd->command_number)
    {
        g_Settings.is_create_move = false;
        return ret;
    }

    if (!isHackActive())
    {
        g_Settings.is_create_move = false;
        return ret;
    }

    if (!g_IEngine->IsInGame())
    {
        g_Settings.bInvalid       = true;
        g_Settings.is_create_move = false;
        return true;
    }

    PROF_SECTION(CreateMove);
#if ENABLE_VISUALS
    stored_buttons = current_user_cmd->buttons;
    if (freecam_is_toggled)
    {
        current_user_cmd->sidemove    = 0.0f;
        current_user_cmd->forwardmove = 0.0f;
    }
#endif
    if (current_user_cmd && current_user_cmd->command_number)
        last_cmd_number = current_user_cmd->command_number;

    /**bSendPackets = true;
    if (hacks::shared::lagexploit::ExploitActive()) {
        *bSendPackets = ((current_user_cmd->command_number % 4) == 0);
        //logging::Info("%d", *bSendPackets);
    }*/

    // logging::Info("canpacket: %i", ch->CanPacket());
    // if (!cmd) return ret;

    time_replaced = false;
    curtime_old   = g_GlobalVars->curtime;

    INetChannel *ch;
    ch = (INetChannel *) g_IEngine->GetNetChannelInfo();
    if (ch && !hooks::IsHooked((void *) ch))
    {
        hooks::netchannel.Set(ch);
        hooks::netchannel.HookMethod(HOOK_ARGS(SendDatagram));
        hooks::netchannel.HookMethod(HOOK_ARGS(CanPacket));
        hooks::netchannel.HookMethod(HOOK_ARGS(SendNetMsg));
        hooks::netchannel.HookMethod(HOOK_ARGS(Shutdown));
        hooks::netchannel.Apply();
#if ENABLE_IPC
        ipc::UpdateServerAddress();
#endif
    }
    if (*fuckmode)
    {
        static int prevbuttons = 0;
        current_user_cmd->buttons |= prevbuttons;
        prevbuttons |= current_user_cmd->buttons;
    }
    hooked_methods::CreateMove();

    static bool firstcall = false;
    static float interp_f = 0.0f;
    static int min_interp = 0;
    static float ratio    = 0;
    if (nolerp)
    {
        // current_user_cmd->tick_count += 1;
        if (!firstcall)
            min_interp = sv_client_min_interp_ratio->GetInt();
        if (sv_client_min_interp_ratio->GetInt() != -1)
        {
            // sv_client_min_interp_ratio->m_nFlags = 0;
            sv_client_min_interp_ratio->SetValue(-1);
        }
        if (!firstcall)
            interp_f = cl_interp->m_fValue;
        if (cl_interp->m_fValue != 0)
        {
            cl_interp->SetValue(0);
            cl_interp->m_fValue = 0.0f;
            cl_interp->m_nValue = 0;
        }
        if (!firstcall)
            ratio = cl_interp_ratio->GetInt();
        if (cl_interp_ratio->GetInt() != 0)
            cl_interp_ratio->SetValue(0);
        // if (cl_interpolate->GetInt() != 0) cl_interpolate->SetValue(0);
        firstcall = true;
    }
    else if (firstcall)
    {
        sv_client_min_interp_ratio->SetValue(min_interp);
        cl_interp->SetValue(interp_f);
        cl_interp_ratio->SetValue(ratio);
        firstcall = false;
    }

    if (!g_Settings.bInvalid && CE_GOOD(g_pLocalPlayer->entity))
    {
        servertime            = (float) CE_INT(g_pLocalPlayer->entity, netvar.nTickBase) * g_GlobalVars->interval_per_tick;
        g_GlobalVars->curtime = servertime;
        time_replaced         = true;
    }
    if (g_Settings.bInvalid)
    {
        entity_cache::Invalidate();
    }

    //	PROF_BEGIN();
    {
        PROF_SECTION(EntityCache);
        entity_cache::Update();
    }
#if !ENABLE_VISUALS
    if (no_shake && CE_GOOD(LOCAL_E) && LOCAL_E->m_bAlivePlayer())
    {
        NET_VECTOR(RAW_ENT(LOCAL_E), netvar.vecPunchAngle)    = { 0.0f, 0.0f, 0.0f };
        NET_VECTOR(RAW_ENT(LOCAL_E), netvar.vecPunchAngleVel) = { 0.0f, 0.0f, 0.0f };
    }
#endif
    //	PROF_END("Entity Cache updating");
    {
        PROF_SECTION(CM_PlayerResource);
        g_pPlayerResource->Update();
    }
    {
        PROF_SECTION(CM_LocalPlayer);
        g_pLocalPlayer->Update();
    }
    PrecalculateCanShoot();
    if (firstcm)
    {
        DelayTimer.update();
        //        hacks::tf2::NavBot::Init();
        //        hacks::tf2::NavBot::initonce();
        nav::status = nav::off;
#if ENABLE_IRC
        IRC::auth();
#endif
        hacks::tf2::NavBot::init(true);
        firstcm = false;
    }
    g_Settings.bInvalid = false;
    {
        PROF_SECTION(CM_AAA);
        hacks::shared::anti_anti_aim::createMove();
    }

    if (CE_GOOD(g_pLocalPlayer->entity))
    {
        if (!g_pLocalPlayer->life_state && CE_GOOD(g_pLocalPlayer->weapon()))
        {
            // Walkbot can leave game.
            if (!g_IEngine->IsInGame())
            {
                g_Settings.is_create_move = false;
                return ret;
            }
            if (current_user_cmd->buttons & IN_ATTACK)
                ++attackticks;
            else
                attackticks = 0;
            if (semiauto)
                if (current_user_cmd->buttons & IN_ATTACK)
                    if (attackticks % *semiauto < *semiauto - 1)
                        current_user_cmd->buttons &= ~IN_ATTACK;
            static int fakelag_queue = 0;
            if (CE_GOOD(LOCAL_E))
                if (fakelag_amount)
                {
                    *bSendPackets = *fakelag_amount == fakelag_queue;
                    fakelag_queue++;
                    if (fakelag_queue > *fakelag_amount)
                        fakelag_queue = 0;
                }
            {
                PROF_SECTION(CM_antiaim);
                hacks::shared::antiaim::ProcessUserCmd(cmd);
            }
            if (debug_projectiles)
                projectile_logging::Update();
            Prediction_CreateMove();
        }
    }
    {
        PROF_SECTION(CM_WRAPPER);
        EC::run(EC::CreateMove_NoEnginePred);
        if (engine_pred)
            engine_prediction::RunEnginePrediction(RAW_ENT(LOCAL_E), current_user_cmd);

        EC::run(EC::CreateMove);
    }
    if (time_replaced)
        g_GlobalVars->curtime = curtime_old;
    g_Settings.bInvalid = false;
    {
        PROF_SECTION(CM_chat_stack);
        chat_stack::OnCreateMove();
    }

    // TODO Auto Steam Friend

#if ENABLE_IPC
    {
        PROF_SECTION(CM_playerlist);
        static Timer ipc_update_timer{};
        //	playerlist::DoNotKillMe();
        if (ipc_update_timer.test_and_set(1000 * 10))
        {
            ipc::UpdatePlayerlist();
        }
    }
#endif
    if (CE_GOOD(g_pLocalPlayer->entity))
    {
        speedapplied = false;
        if (roll_speedhack && cmd->buttons & IN_DUCK && !(cmd->buttons & IN_ATTACK) && !HasCondition<TFCond_Charging>(LOCAL_E))
        {
            speed                     = Vector{ cmd->forwardmove, cmd->sidemove, 0.0f }.Length();
            static float prevspeedang = 0.0f;
            if (fabs(speed) > 0.0f)
            {

                Vector vecMove(cmd->forwardmove, cmd->sidemove, 0.0f);
                vecMove *= -1;
                float flLength = vecMove.Length();
                Vector angMoveReverse{};
                VectorAngles(vecMove, angMoveReverse);
                cmd->forwardmove = -flLength;
                cmd->sidemove    = 0.0f; // Move only backwards, no sidemove
                float res        = g_pLocalPlayer->v_OrigViewangles.y - angMoveReverse.y;
                while (res > 180)
                    res -= 360;
                while (res < -180)
                    res += 360;
                if (res - prevspeedang > 90.0f)
                    res = (res + prevspeedang) / 2;
                prevspeedang                     = res;
                cmd->viewangles.y                = res;
                cmd->viewangles.z                = 90.0f;
                g_pLocalPlayer->bUseSilentAngles = true;
                speedapplied                     = true;
            }
        }
        if (g_pLocalPlayer->bUseSilentAngles)
        {
            if (!speedapplied)
            {
                vsilent.x = cmd->forwardmove;
                vsilent.y = cmd->sidemove;
                vsilent.z = cmd->upmove;
                speed     = sqrt(vsilent.x * vsilent.x + vsilent.y * vsilent.y);
                VectorAngles(vsilent, ang);
                yaw              = DEG2RAD(ang.y - g_pLocalPlayer->v_OrigViewangles.y + cmd->viewangles.y);
                cmd->forwardmove = cos(yaw) * speed;
                cmd->sidemove    = sin(yaw) * speed;
                if (cmd->viewangles.x >= 90 && cmd->viewangles.x <= 270)
                    cmd->forwardmove = -cmd->forwardmove;
            }

            ret = false;
        }
        if (cmd && (cmd->buttons & IN_ATTACK || !(hacks::shared::antiaim::isEnabled() && *fakelag_amount && *bSendPackets)))
            g_Settings.brute.last_angles[LOCAL_E->m_IDX] = cmd->viewangles;
        for (int i = 0; i <= g_IEngine->GetMaxClients(); i++)
        {

            CachedEntity *ent = ENTITY(i);
            if (CE_GOOD(LOCAL_E))
                if (ent == LOCAL_E)
                    continue;
            if (CE_BAD(ent) || !ent->m_bAlivePlayer())
                continue;
            INetChannel *ch = (INetChannel *) g_IEngine->GetNetChannelInfo();
            if (NET_FLOAT(RAW_ENT(ent), netvar.m_flSimulationTime) <= 1.5f)
                continue;
            float latency = hacks::shared::backtrack::getRealLatency();
            g_Settings.brute.choke[i].push_back(NET_FLOAT(RAW_ENT(ent), netvar.m_flSimulationTime) == g_Settings.brute.lastsimtime);
            g_Settings.brute.last_angles[ent->m_IDX] = NET_VECTOR(RAW_ENT(ent), netvar.m_angEyeAngles);
            if (!g_Settings.brute.choke[i].empty() && g_Settings.brute.choke[i].size() > 20)
                g_Settings.brute.choke[i].pop_front();
        }
    }

    //	PROF_END("CreateMove");
    if (!(cmd->buttons & IN_ATTACK))
    {
        // LoadSavedState();
    }
    g_pLocalPlayer->bAttackLastTick = (cmd->buttons & IN_ATTACK);
    g_Settings.is_create_move       = false;
    return ret;
}
} // namespace hooked_methods
