; lowlevel.asm: the few calls C cannot make with int86 (32-bit registers).
; Far cdecl, as the medium model calls them; SI DI BP DS kept.
        cpu 386
segment LOWLEVEL_TEXT public align=16 class=CODE use16

; void rv_xms(RvRegs near *r): call the XMS driver's entry (rv_xms_entry,
; a far pointer) with EAX EBX ECX EDX SI from *r, and store them back.
        global _rv_xms
        extern _rv_xms_entry
_rv_xms:
        push bp
        mov bp,sp
        push si
        push di
        mov di,[bp+6]               ; r
        mov eax,[di]
        mov ebx,[di+4]
        mov ecx,[di+8]
        mov edx,[di+12]
        mov si,[di+16]
        push di
        call far [_rv_xms_entry]
        pop di
        mov [di],eax
        mov [di+4],ebx
        mov [di+8],ecx
        mov [di+12],edx
        pop di
        pop si
        pop bp
        retf
