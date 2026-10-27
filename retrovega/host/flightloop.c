/* flightloop.c: src/rvflight.inc on the host: the flight loop's frame, the same calls in the same order, without
 * the sites at fixed offsets that only the multiplayer layer (in DOSBox) hooks. */
#include "rvtypes.h"
#include "rvhooks.h"

void flight_startup(void);
unsigned short flight_keys(void);
void flight_frame(void);
void flight_music(void);
void flight_autopilot(void);
void autoAnimation_c(unsigned short camShipType, unsigned short camMode, unsigned short duration);

unsigned short rv_flight_run(void)
{
    flight_startup();
    for (;;) {
        unsigned short status = flight_keys();
        if (status)
            missionStatus = status;     /* statusSetByExitKey */
        if (missionStatus)
            break;
        flight_frame();
        flight_music();                 /* skipOrchestra */
        if (missionStatus)
            break;
    }
    return missionStatus;
}

unsigned short rv_hangar(void) { return runHangarMission(); }

void rv_autopilot_key(void) { flight_autopilot(); }

void rv_cockpit_pointer(void (*draw)(void)) { draw(); }

void autoAnimation_impl(uint16_t camShipType, uint16_t camMode, uint16_t duration)
{
    autoAnimation_c(camShipType, camMode, duration);
}
