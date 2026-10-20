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

; void rv_blit_row(unsigned char far *row, const unsigned char far *spans,
;                  int left, int xa, int xb)
; One row of a run-length sprite (gfx.c's format: skip, count, count pixels
; ... ending 0, 0) into a back-buffer row, its first pixel at column `left`,
; drawn only inside [xa, xb).  The flight's sprites go through here: a span
; is one rep movsd (rvflight.inc's loop is the only caller that matters).
        global _rv_blit_row
_rv_blit_row:
        push bp
        mov bp,sp
        push si
        push di
        push ds
        mov es,[bp+8]               ; row
        lds si,[bp+10]              ; spans
        mov bx,[bp+14]              ; the span's column
.next:
        xor ax,ax
        lodsb
        mov dx,ax                   ; skip
        lodsb                       ; count
        or ax,ax
        jnz .have
        or dx,dx
        jz .done                    ; (0, 0): the row ends
.have:
        add bx,dx
        mov cx,ax                   ; n
        jcxz .next
        mov di,bx                   ; start
        mov dx,bx
        add dx,cx                   ; end
        cmp di,[bp+16]
        jge .a
        mov di,[bp+16]              ; start = max(start, xa)
.a:     cmp dx,[bp+18]
        jle .b
        mov dx,[bp+18]              ; end = min(end, xb)
.b:     sub dx,di                   ; the length to draw
        jle .skip
        push si
        push cx
        add si,di
        sub si,bx                   ; the first pixel to draw
        add di,[bp+6]               ; its place in the row
        mov cx,dx
        shr cx,1
        jnc .w
        movsb
.w:     shr cx,1
        jnc .d
        movsw
.d:     rep movsd
        pop cx
        pop si
.skip:
        add si,cx                   ; past the span's pixels
        add bx,cx
        jmp .next
.done:
        pop ds
        pop di
        pop si
        pop bp
        retf
