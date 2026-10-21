; lowlevel.asm: the few calls C cannot make with int86 (32-bit registers).
; Far cdecl, as the medium model calls them; SI DI BP DS kept.
        cpu 486                     ; (bswap in the mirrored copy: the spec targets a 486)
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

; void rv_scale_row(const unsigned char far *line, const unsigned short far *map,
;                   int xa, int xb, unsigned char far *out)
; lightfield.c's lf_scale_row: out[x] = line[map[x]] for x in xa .. xb-1 where
; that is not clear.  The scaled draw's inner loop (ships.c).
        global _rv_scale_row
_rv_scale_row:
        push bp
        mov bp,sp
        push si
        push di
        push ds
        mov cx,[bp+16]
        sub cx,[bp+14]              ; n = xb - xa
        jle .done
        mov si,[bp+10]
        mov ax,[bp+14]
        shl ax,1
        add si,ax                   ; map + xa
        mov fs,[bp+12]
        mov es,[bp+20]
        mov di,[bp+18]
        add di,[bp+14]              ; out + xa
        mov dx,[bp+6]               ; line's offset
        mov ds,[bp+8]
        mov bp,dx                   ; (the frame is done with)
.loop:
        mov bx,[fs:si]
        add bx,bp                   ; line + map[x]
        mov al,[ds:bx]
        test al,al
        jz .skip
        mov [es:di],al
.skip:
        inc di
        add si,2
        dec cx
        jnz .loop
.done:
        pop ds
        pop di
        pop si
        pop bp
        retf

; void rv_rotate_row(const unsigned char far *dense, int w, int h, const LfRot *r,
;                    int y, int xs, int xe, unsigned char far *out)
; lightfield.c's lf_rotate_row without the bounds test: the caller passes the
; span lf_rotate_span found, so every source point is inside the w x h image.
; u and v run in 16.16 in esi and edi; a pixel's index is v * w + u.
        global _rv_rotate_row
_rv_rotate_row:
        push bp
        mov bp,sp
        push si
        push di
        sub sp,8                    ; [bp-12] du, [bp-8] dv (the frame: bp-12 .. bp-5)
        mov si,[bp+14]              ; r
        mov eax,[si+16]
        mov [bp-12],eax
        mov eax,[si+20]
        mov [bp-8],eax
        movsx eax,word [bp+16]      ; y
        imul eax,[si+24]            ; y * dru
        add eax,[si+8]              ; + u00
        movsx edx,word [bp+18]      ; xs
        imul edx,[si+16]            ; xs * du
        add eax,edx
        movsx edx,word [bp+16]
        imul edx,[si+28]            ; y * drv
        add edx,[si+12]             ; + v00
        movsx ecx,word [bp+18]
        imul ecx,[si+20]            ; xs * dv
        add edx,ecx
        mov esi,eax                 ; u
        mov edi,edx                 ; v
        mov cx,[bp+20]
        sub cx,[bp+18]              ; n = xe - xs
        jle .done
        mov gs,[bp+8]               ; dense
        mov es,[bp+24]
        mov bx,[bp+22]
        add bx,[bp+18]              ; out + xs
.loop:
        mov edx,edi
        shr edx,16                  ; v
        imul dx,[bp+10]             ; v * w (the low word is right past 32K too)
        mov eax,esi
        shr eax,16                  ; u
        add dx,ax
        add dx,[bp+6]               ; + dense's offset
        movzx edx,dx
        mov al,[gs:edx]
        test al,al
        jz .skip
        mov [es:bx],al
.skip:
        add esi,[bp-12]
        add edi,[bp-8]
        inc bx
        dec cx
        jnz .loop
.done:
        add sp,8
        pop di
        pop si
        pop bp
        retf

; void rv_blit_row_rev(unsigned char far *row, const unsigned char far *spans,
;                      int right, int xa, int xb)
; A run-length row drawn mirrored: the span at sprite columns [c, c+n) lands
; at screen columns [right-c-n, right-c) with its pixels in the other order,
; inside [xa, xb).  Four pixels at a time through bswap (486), the odd ones
; by hand.  Screen column d takes the span's pixel (right - c - 1 - d).
        global _rv_blit_row_rev
_rv_blit_row_rev:
        push bp
        mov bp,sp
        push si
        push di
        push ds
        mov es,[bp+8]               ; row
        lds si,[bp+10]              ; spans
        xor bx,bx                   ; the span's sprite column c
.next:
        xor ax,ax
        lodsb
        mov dx,ax                   ; skip
        lodsb                       ; count n
        or ax,ax
        jnz .have
        or dx,dx
        jz .done
.have:
        add bx,dx
        mov cx,ax
        jcxz .next
        mov di,[bp+14]
        sub di,bx                   ; e0 = right - c
        mov dx,di
        sub dx,cx                   ; s0 = e0 - n
        cmp dx,[bp+16]
        jge .a
        mov dx,[bp+16]              ; a = max(s0, xa)
.a:     mov ax,di
        cmp ax,[bp+18]
        jle .b
        mov ax,[bp+18]              ; b = min(e0, xb)
.b:     sub ax,dx                   ; len = b - a
        jle .skip
        push si
        push cx
        push bx
        mov bx,di
        sub bx,dx                   ; e0 - a
        add si,bx                   ; one past the source run (its pixels come out backwards)
        mov di,dx
        add di,[bp+6]               ; dst = row + a
        mov cx,ax
        mov bx,ax
        shr cx,2
        jz .tail
.four:
        sub si,4
        mov eax,[si]
        bswap eax
        mov [es:di],eax
        add di,4
        dec cx
        jnz .four
.tail:
        and bx,3
        jz .copied
.one:
        dec si
        mov al,[si]
        mov [es:di],al
        inc di
        dec bx
        jnz .one
.copied:
        pop bx
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

; void rv_rotate_row2(const unsigned char far *dense, int w, int h, const LfRot *r,
;                     int y, int xs, int xe, unsigned char far *out)
; rv_rotate_row at half the horizontal detail: every other output column is
; sampled and written twice (a 2 x 1 block), so a row costs half.  xs is
; made even by the caller; the span is still [xs, xe).
        global _rv_rotate_row2
_rv_rotate_row2:
        push bp
        mov bp,sp
        push si
        push di
        sub sp,8                    ; [bp-12] du * 2, [bp-8] dv * 2
        mov si,[bp+14]              ; r
        mov eax,[si+16]
        add eax,eax
        mov [bp-12],eax
        mov eax,[si+20]
        add eax,eax
        mov [bp-8],eax
        movsx eax,word [bp+16]      ; y
        imul eax,[si+24]            ; y * dru
        add eax,[si+8]              ; + u00
        movsx edx,word [bp+18]      ; xs
        imul edx,[si+16]            ; xs * du
        add eax,edx
        movsx edx,word [bp+16]
        imul edx,[si+28]            ; y * drv
        add edx,[si+12]             ; + v00
        movsx ecx,word [bp+18]
        imul ecx,[si+20]            ; xs * dv
        add edx,ecx
        mov esi,eax                 ; u
        mov edi,edx                 ; v
        mov cx,[bp+20]
        sub cx,[bp+18]              ; n = xe - xs
        jle .done
        inc cx
        shr cx,1                    ; pairs
        mov gs,[bp+8]               ; dense
        mov es,[bp+24]
        mov bx,[bp+22]
        add bx,[bp+18]              ; out + xs
.loop:
        mov edx,edi
        shr edx,16                  ; v
        imul dx,[bp+10]             ; v * w
        mov eax,esi
        shr eax,16                  ; u
        add dx,ax
        add dx,[bp+6]               ; + dense's offset
        movzx edx,dx
        mov al,[gs:edx]
        test al,al
        jz .skip
        mov ah,al
        mov [es:bx],ax              ; the sample, twice
.skip:
        add esi,[bp-12]
        add edi,[bp-8]
        add bx,2
        dec cx
        jnz .loop
.done:
        add sp,8
        pop di
        pop si
        pop bp
        retf

; int rv_stars_place(const short far *stars, int n, const long far *basis, int cx, int cy, int rows,
;                    short far *out)
; The sky's stars through the camera: stars are (x, y, z, c) shorts, a
; direction of length 4096 and a colour; basis the camera's front, right
; and up (256 = 1.0) as nine longs; out gets (x, y, c) for every star on
; screen (0 <= x < 640, 0 <= y < rows) and the count is returned.  Three
; products decide whether a star is near enough to straight ahead (depth
; over 0.7 of its length) before the other six and the two divisions.
        global _rv_stars_place
_rv_stars_place:
        push bp
        mov bp,sp
        sub sp,48
        push si
        push di
        push ds
        mov ds,[bp+14]              ; the basis to the frame: nine dwords
        mov si,[bp+12]
        mov di,bp
        sub di,36
        mov cx,9
.copy:
        mov eax,[ds:si]
        mov [ss:di],eax
        add si,4
        add di,4
        dec cx
        jnz .copy
        mov ax,[bp+10]
        mov [bp-38],ax              ; stars left
        mov ax,[bp+16]
        mov [bp-40],ax              ; cx
        mov ax,[bp+18]
        mov [bp-42],ax              ; cy
        mov ax,[bp+20]
        mov [bp-44],ax              ; rows
        mov word [bp-46],0          ; on screen
        mov ds,[bp+8]
        mov si,[bp+6]
        mov es,[bp+24]
        mov di,[bp+22]
        cmp word [bp-38],0
        jle .done
.star:
        movsx eax,word [si]
        imul eax,dword [bp-36]      ; x fx
        mov edx,eax
        movsx eax,word [si+2]
        imul eax,dword [bp-32]      ; y fy
        add edx,eax
        movsx eax,word [si+4]
        imul eax,dword [bp-28]      ; z fz
        add edx,eax                 ; the depth: 4096 x 256 straight ahead
        cmp edx,734003              ; 0.7 of that
        jl .next
        mov ecx,edx
        movsx eax,word [si]
        imul eax,dword [bp-24]      ; x rx
        mov ebx,eax
        movsx eax,word [si+2]
        imul eax,dword [bp-20]
        add ebx,eax
        movsx eax,word [si+4]
        imul eax,dword [bp-16]
        add ebx,eax                 ; across
        mov eax,ebx
        imul eax,eax,400            ; the focal length
        cdq
        idiv ecx
        add ax,[bp-40]              ; sx
        cmp ax,0
        jl .next
        cmp ax,640
        jge .next
        mov bx,ax
        movsx eax,word [si]
        imul eax,dword [bp-12]      ; x ux
        mov edx,eax
        movsx eax,word [si+2]
        imul eax,dword [bp-8]
        add edx,eax
        movsx eax,word [si+4]
        imul eax,dword [bp-4]
        add edx,eax                 ; up
        mov eax,edx
        imul eax,eax,400
        cdq
        idiv ecx
        mov dx,[bp-42]
        sub dx,ax                   ; sy: the camera's up is up the screen
        cmp dx,0
        jl .next
        cmp dx,[bp-44]
        jge .next
        mov [es:di],bx
        mov [es:di+2],dx
        mov ax,[si+6]
        mov [es:di+4],ax
        add di,6
        inc word [bp-46]
.next:
        add si,8
        dec word [bp-38]
        jnz .star
.done:
        mov ax,[bp-46]
        pop ds
        pop di
        pop si
        mov sp,bp
        pop bp
        retf
