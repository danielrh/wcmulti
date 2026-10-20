; rvflight.inc: the flight loop's skeleton, included by rvhook.asm (tools/rvabi.py
; --asm) inside RVHOOK after the thunks.  Every site sits at the offset that
; abi/rvabi.def fixes (RV_SITE_*), with the instruction and register contract
; of the WC.EXE place of the same name (docs/spec.md 5.2), so the layer's
; handlers carry over.  The work is C (src/flight.c): this is only the frame
; the layer watches.  DS is DGROUP throughout (the medium model keeps it).
        extern _flight_startup          ; void: the mission's assets, slots and first picture
        extern _flight_keys             ; -> ax: 0, or the missionStatus the exit key sets
        extern _flight_frame            ; void: one simulation step and its picture (frameCounter++)
        extern _flight_music            ; void: the orchestra (music_poll)
        extern _flight_autopilot        ; void: the autopilot key's work
        extern _autoAnimation_c         ; (camShipType, camMode, duration): the camera's cut-scene

        times RV_SITE_mainLoopTop - ($ - rvhook_start) db 0xCC
; The loop proper, entered from _rv_flight_run below: interrupts on, no live
; registers but SI DI BP, the world consistent.  Runs until missionStatus is
; set, by the exit key (statusSetByExitKey), by the simulation, or by the layer.
rv_loop_top:                                ; mainLoopTop
        sti
        call far _flight_keys
        test ax, ax
        jz .checked
        times RV_SITE_statusSetByExitKey - ($ - rvhook_start) nop
        mov [RV_DS_missionStatus], ax       ; statusSetByExitKey
.checked:
        times RV_SITE_statusCheckAfterKeys - ($ - rvhook_start) nop
        cmp word [RV_DS_missionStatus], 0   ; statusCheckAfterKeys
        jne rv_loop_leave
        call far _flight_frame
        call rv_orchestra
        times RV_SITE_statusCheckAfterFrame - ($ - rvhook_start) nop
        cmp word [RV_DS_missionStatus], 0   ; statusCheckAfterFrame
        jne rv_loop_leave
        jmp rv_loop_top
rv_loop_leave:
        pop di
        pop si
        pop bp
        mov ax, [RV_DS_missionStatus]
        retf

        times RV_SITE_skipOrchestra - ($ - rvhook_start) db 0xCC
rv_orchestra:
        call far _flight_music              ; skipOrchestra: a 5-byte call the layer may step over
        ret

        times RV_SITE_afterStartup - 10 - ($ - rvhook_start) db 0xCC
; unsigned short rv_flight_run(void): the flight, from the start-up to the
; status that ends the loop (returned in AX).
        global _rv_flight_run
_rv_flight_run:
        push bp
        mov bp, sp
        push si
        push di
        call far _flight_startup            ; returns to afterStartup
        jmp rv_loop_top                     ; afterStartup

        times RV_SITE_afterHangarMission - 5 - ($ - rvhook_start) db 0xCC
; unsigned short rv_hangar(void): the runHangarMission thunk, called from the
; flight's caller's segment so its return point is afterHangarMission.
        global _rv_hangar
_rv_hangar:
        call far _runHangarMission          ; returns to afterHangarMission
        retf                                ; afterHangarMission

        times RV_SITE_autopilotKey - ($ - rvhook_start) db 0xCC
; void rv_autopilot_key(void): the autopilot key's work, through the site the
; layer steps over on clients (IP += 5 lands on the retf).
        global _rv_autopilot_key
_rv_autopilot_key:
        call far _flight_autopilot          ; autopilotKey
        retf

        times RV_SITE_cockpitPointer - 3 - ($ - rvhook_start) db 0xCC
; void rv_cockpit_pointer(void (__far *draw)(void)): the pointer in the
; cockpit, drawn through a 4-byte `call far [mem]`; the caller cleans up.
        global _rv_cockpit_pointer
_rv_cockpit_pointer:
        push bp
        mov bp, sp
        call far [word bp+6]                ; cockpitPointer
        pop bp
        retf

        times RV_SITE_autoAnimationBody - 3 - ($ - rvhook_start) db 0xCC
; The autoAnimation thunk's body: its arguments sit at [bp+6], [bp+8] and
; [bp+0Ah] once bp is set up, as the layer reads them.
        global _autoAnimation_impl
_autoAnimation_impl:
        push bp
        mov bp, sp
        push word [bp+0Ah]                  ; autoAnimationBody
        push word [bp+8]
        push word [bp+6]
        call far _autoAnimation_c
        add sp, 6
        pop bp
        retf
