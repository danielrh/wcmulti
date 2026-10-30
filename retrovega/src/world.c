/* world.c: the flight's world as the game logic sees it (world.h): vectors, a slot's ship, the gameplay random
 * stream.  No data of its own: everything is in the fixed block's slot arrays, where the layer reads and writes it
 * (spec 6.2: the engine keeps no copy of what the arrays say). */
#include <string.h>
#include <stdlib.h>
#include "rv.h"
#include "campaign.h"
#include "lightfield.h"
#include "collide.h"
#include "world.h"
#include "logic.h"

int32_t vec_dot(const RvVec3 *a, const RvVec3 *b) { return a->x * b->x + a->y * b->y + a->z * b->z; }

void vec_norm(RvVec3 *v)
{
    uint32_t len = lf_sqrt((uint32_t)(v->x * v->x + v->y * v->y + v->z * v->z));
    if (!len)
        return;
    v->x = v->x * 256 / (int32_t)len;
    v->y = v->y * 256 / (int32_t)len;
    v->z = v->z * 256 / (int32_t)len;
}

void vec_cross(RvVec3 *out, const RvVec3 *a, const RvVec3 *b)
{
    out->x = (a->y * b->z - a->z * b->y) / 256;
    out->y = (a->z * b->x - a->x * b->z) / 256;
    out->z = (a->x * b->y - a->y * b->x) / 256;
}

/* rounded to the nearest (the + 8192: half of lf_sin's 16384), not down: a turn of a unit or two moves a 256-long
 * vector by only a component or two, so rounding down lost a third of a slow ship's turn, and turned every step a
 * little the same way (a turn and the same turn back ended 4 degrees off) */
void vec_turn(RvVec3 *a, RvVec3 *b, int16_t t)
{
    int32_t c = lf_cos(t), s = lf_sin(t);
    RvVec3 na, nb;
    na.x = (a->x * c + b->x * s + 8192) >> 14; na.y = (a->y * c + b->y * s + 8192) >> 14;
    na.z = (a->z * c + b->z * s + 8192) >> 14;
    nb.x = (b->x * c - a->x * s + 8192) >> 14; nb.y = (b->y * c - a->y * s + 8192) >> 14;
    nb.z = (b->z * c - a->z * s + 8192) >> 14;
    *a = na;
    *b = nb;
}

int16_t slot_is_ship(int16_t s)
{
    return s >= 0 && s <= RV_MAX_SHIP_SLOT && (maybeEntityType[s] == ET_SHIP || maybeEntityType[s] == ET_CAPSHIP);
}

const RvShipType __far *slot_type(int16_t s)
{
    return ship_type(gInstanceShipTypes[s]);
}

/* the frame kept right-handed and unit length (the layer may write values that drift) */
void ship_orthonormalise(int16_t s)
{
    RvVec3 *f = &gOrientationFrontVector[s], *r = &gOrientationRightVector[s], *u = &gOrientationUpVector[s];
    int32_t d;
    vec_norm(f);
    d = vec_dot(f, r) / 256;
    r->x -= f->x * d / 256; r->y -= f->y * d / 256; r->z -= f->z * d / 256;
    vec_norm(r);
    vec_cross(u, f, r);
    vec_norm(u);
}

void ship_face(int16_t s, int16_t yaw, int16_t pitch)
{
    gOrientationFrontVector[s].x = 0; gOrientationFrontVector[s].y = 0; gOrientationFrontVector[s].z = 256;
    gOrientationRightVector[s].x = 256; gOrientationRightVector[s].y = 0; gOrientationRightVector[s].z = 0;
    gOrientationUpVector[s].x = 0; gOrientationUpVector[s].y = 256; gOrientationUpVector[s].z = 0;
    vec_turn(&gOrientationFrontVector[s], &gOrientationRightVector[s], yaw);
    vec_turn(&gOrientationFrontVector[s], &gOrientationUpVector[s], pitch);
    ship_orthonormalise(s);
}

int32_t player_km(int16_t s)
{
    int32_t dx = labs(gPositionVector[s].x - gPositionVector[0].x) / 256, dy = labs(gPositionVector[s].y - gPositionVector[0].y) / 256,
         dz = labs(gPositionVector[s].z - gPositionVector[0].z) / 256, m = dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
    return m + (dx + dy + dz - m) / 2;
}

void ship_rel(int16_t from, int16_t s, RvVec3 *out)
{
    RvVec3 d;
    d.x = (gPositionVector[s].x - gPositionVector[from].x) / 256;   /* (km: a km's 256 squared would overflow) */
    d.y = (gPositionVector[s].y - gPositionVector[from].y) / 256;
    d.z = (gPositionVector[s].z - gPositionVector[from].z) / 256;
    out->x = vec_dot(&d, &gOrientationRightVector[from]) / 256;
    out->y = vec_dot(&d, &gOrientationUpVector[from]) / 256;
    out->z = vec_dot(&d, &gOrientationFrontVector[from]) / 256;
}

void ship_guns(int16_t s)
{
    const RvShipType __far *t = slot_type(s);
    if (t)
        _fmemcpy(gunTable + s * gunTable_SIZE, t->loadout, gunTable_SIZE);
    else
        gunTable[s * gunTable_SIZE] = 0;
}

int16_t ship_spawn(int16_t s, int16_t type, int16_t side, int16_t controller, int32_t x, int32_t y, int32_t z,
                   int16_t yaw)
{
    static const char __far s_refused[] = "world: ship_spawn into slot %d refused: the ship slots are 0..%d";
    const RvShipType __far *t = ship_type(type);
    if (s < 0 || s > RV_MAX_SHIP_SLOT) {   /* (the rest of the slots are temporaries: no ship goes there) */
        rv_logf(s_refused, s, RV_MAX_SHIP_SLOT);
        return -1;
    }
    maybeEntityType[s] = t && t->type >= 0x20 ? ET_CAPSHIP : ET_SHIP;
    gInstanceShipTypes[s] = (uint16_t)type;
    commAnimInfo[s] = (uint16_t)side;
    rvController[s] = (uint8_t)controller;
    gPositionVector[s].x = x; gPositionVector[s].y = y; gPositionVector[s].z = z;
    gVelocityVector[s].x = gVelocityVector[s].y = gVelocityVector[s].z = 0;
    ship_face(s, yaw, 0);
    setSpeed[s] = t ? t->cruise_kps : 200;
    speedLo[s] = setSpeed[s];
    SHIELD(s, 0) = SHIELD_MAX(s, 0) = t ? t->shield_f : 10;
    SHIELD(s, 1) = SHIELD_MAX(s, 1) = t ? t->shield_r : 10;
    ARMOR(s, 0) = t ? t->armor_f : 10;
    ARMOR(s, 1) = t ? t->armor_r : 10;
    ARMOR(s, 2) = t ? t->armor_l : 10;
    ARMOR(s, 3) = t ? t->armor_rt : 10;
    damagePoints[s] = 0;
    coreHp[s] = 4;
    gunEnergy[s] = 100;
    gunDamage[s] = 0;
    engineFlag[s] = 0;
    ship_guns(s);
    parentShipForShip[s] = 0xFF;
    shipStateByte[s] = 0xFF;
    slotMissionShip[s] = 0xFFFF;
    missileTarget[s] = 0xFF;
    rvTarget[s] = 0xFF;
    shipAiState[s] = 0;
    damageProperty[s] = 0;
    rvAfterburner[s] = 0;
    gMaybeShipRadius[s] = (uint16_t)(collide_half(s) / 256);   /* (km: the half box) */
    rvFlags[s] = 0;
    logic_prepare_spawn(s);
    return s;
}

void ship_clear(int16_t s)
{
    RV_CHECK(s, RV_SLOTS);
    maybeEntityType[s] = ET_NONE;
    rvController[s] = CTL_FROZEN;
    parentShipForShip[s] = 0xFF;
    slotMissionShip[s] = 0xFFFF;
    shipAiState[s] = 0;
}

int16_t ship_free_slot(void)
{
    int16_t s;
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++)
        if (maybeEntityType[s] == ET_NONE)
            return s;
    return -1;
}

uint16_t game_rand(void)
{
    randomSeed = randomSeed * UINT32_C(1103515245) + UINT32_C(12345);
    return (uint16_t)(randomSeed >> 16) & 0x7FFF;
}
