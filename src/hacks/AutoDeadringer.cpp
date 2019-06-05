/*
 * AutoDeadringer.cpp
 *
 *  Created on: Apr 12, 2018
 *      Author: bencat07
 */
#include <settings/Bool.hpp>
#include "common.hpp"

namespace hacks::shared::deadringer
{
static settings::Boolean enable{ "auto-deadringer.enable", "0" };
static settings::Int trigger_health{ "auto-deadringer.health", "30" };

bool IsProjectile(CachedEntity *ent)
{
    return (ent->m_iClassID() == CL_CLASS(CTFProjectile_Rocket) || ent->m_iClassID() == CL_CLASS(CTFProjectile_Flare) || ent->m_iClassID() == CL_CLASS(CTFProjectile_EnergyBall) || ent->m_iClassID() == CL_CLASS(CTFProjectile_HealingBolt) || ent->m_iClassID() == CL_CLASS(CTFProjectile_Arrow) || ent->m_iClassID() == CL_CLASS(CTFProjectile_SentryRocket) || ent->m_iClassID() == CL_CLASS(CTFProjectile_Cleaver) || ent->m_iClassID() == CL_CLASS(CTFGrenadePipebombProjectile) || ent->m_iClassID() == CL_CLASS(CTFProjectile_EnergyRing));
}
int NearbyEntities()
{
    int ret = 0;
    if (CE_BAD(LOCAL_E) || CE_BAD(LOCAL_W))
        return ret;
    for (int i = 0; i < HIGHEST_ENTITY; i++)
    {
        CachedEntity *ent = ENTITY(i);
        if (CE_BAD(ent))
            continue;
        if (ent == LOCAL_E)
            continue;
        if (!ent->m_bAlivePlayer())
            continue;
        if (ent->m_flDistance() <= 300.0f)
            ret++;
    }
    return ret;
}
static void CreateMove()
{
    if (!enable)
        return;
    if (CE_BAD(LOCAL_E) || !LOCAL_E->m_bAlivePlayer() || CE_BAD(LOCAL_W))
        return;
    if (g_pLocalPlayer->clazz != tf_spy)
        return;
    if (CE_BYTE(LOCAL_E, netvar.m_bFeignDeathReady))
        return;
    if (HasCondition<TFCond_Cloaked>(LOCAL_E) || HasCondition<TFCond_CloakFlicker>(LOCAL_E))
        return;
    if (CE_INT(LOCAL_E, netvar.iHealth) < (int) trigger_health && NearbyEntities() > 1)
        current_user_cmd->buttons |= IN_ATTACK2;
    for (int i = 0; i < HIGHEST_ENTITY; i++)
    {
        CachedEntity *ent = ENTITY(i);
        if (CE_BAD(ent))
            continue;
        if (!IsProjectile(ent) && !ent->m_bGrenadeProjectile())
            continue;
        if (!ent->m_bEnemy())
            continue;
        if (ent->m_Type() != ENTITY_PROJECTILE)
            continue;
        if (ent->m_bCritProjectile() && ent->m_flDistance() <= 500.0f)
            current_user_cmd->buttons |= IN_ATTACK2;
        else if (ent->m_flDistance() < 100.0f)
            current_user_cmd->buttons |= IN_ATTACK2;
    }
}
static InitRoutine EC([]() { EC::Register(EC::CreateMove, CreateMove, "AutoDeadringer", EC::average); });
} // namespace hacks::shared::deadringer
