; rocks.asm: the asteroid fields' per-rock work (rocks.h).  rv_rocks_place
; does what rocks.c's lz_rocks_place does, to the bit (RVT.EXE's self-test
; compares them); rv_rocks_xms and rv_rocks_copy draw a big rock's rows.
; Far cdecl, as the medium model calls them; SI DI BP DS kept.  32-bit
; registers throughout (486): DOSBox charges an instruction a cycle whatever
; its width.
        cpu 486
        extern _rv_xms_entry            ; xms.c's: the driver's entry (far pointer, DGROUP)

segment ROCKS_TEXT public align=16 class=CODE use16

; int rv_rocks_place(const LzRock far *r, int n, const LzCam far *cam, LzRockCam far *rc,
;                    LzRockOut far *out, int maxout, short far *reap, short far *hitl)
; Every rock through the camera: out of the bubble (or 2^20 sixteenths of a
; km away on an axis, or 32,767 km) to reap[]; within reach on every axis to
; hitl[]; before the near plane, gone; else its diameter (the fade past
; fade2), its middle on screen, off the box gone, out[].  The lists' counts
; go back in rc; ax the rocks out.
%define CAM(k) [bp-96+(k)]              ; LzCam (84 bytes)
%define RC(k)  [bp-136+(k)]             ; LzRockCam (40)
%define RX    [bp-140]                  ; the middle less the camera (sixteenths of a km)
%define RY    [bp-144]
%define RZ    [bp-148]
%define XX    [bp-152]                  ; X Y Z: camera units
%define YY    [bp-156]
%define ZZ    [bp-160]
%define D2    [bp-164]                  ; its distance squared, km^2
%define XMIN  [bp-168]                  ; the box, pixels
%define XMAX  [bp-172]
%define YMIN  [bp-176]
%define YMAX  [bp-180]
%define LEFT  [bp-182]                  ; rocks still to do
%define INDEX [bp-184]                  ; this one's
%define NOUT  [bp-186]
%define NREAP [bp-188]
%define NHIT  [bp-190]
%define REAPO [bp-192]                  ; the lists' offsets (fs, gs)
%define HITO  [bp-194]
%define DD    [bp-196]                  ; the diameter
%define SX    [bp-200]
%define PFRAME 200
        global _rv_rocks_place
_rv_rocks_place:
        push bp
        mov bp,sp
        sub sp,PFRAME
        push si
        push di
        push ds
        cld
        push ss
        pop es
        lds si,[bp+12]              ; the camera into the frame: 21 dwords
        lea di,CAM(0)
        mov cx,21
        rep movsd
        lds si,[bp+16]              ; the rock terms: 10 dwords
        lea di,RC(0)
        mov cx,10
        rep movsd
        movsx eax,word CAM(76)      ; the box in pixels
        sar eax,4
        mov XMIN,eax
        movsx eax,word CAM(78)
        sar eax,4
        mov XMAX,eax
        movsx eax,word CAM(80)
        sar eax,4
        mov YMIN,eax
        movsx eax,word CAM(82)
        sar eax,4
        mov YMAX,eax
        xor ax,ax
        mov NOUT,ax
        mov NREAP,ax
        mov NHIT,ax
        mov INDEX,ax
        lfs ax,[bp+26]
        mov REAPO,ax
        lgs ax,[bp+30]
        mov HITO,ax
        les di,[bp+20]              ; out
        mov ax,[bp+10]
        mov LEFT,ax
        lds si,[bp+6]               ; the rocks
        cmp word LEFT,0
        jle .done
        cmp word [bp+24],0
        jle .done
.rock:  mov eax,[si]                ; the middle less the camera: an axis past 2^20 sixteenths, reaped
        sub eax,CAM(0)
        cmp eax,0x2000000
        jae .reap
        shr eax,4
        sub eax,0x100000
        mov RX,eax
        mov eax,[si+4]
        sub eax,CAM(4)
        cmp eax,0x2000000
        jae .reap
        shr eax,4
        sub eax,0x100000
        mov RY,eax
        mov eax,[si+8]
        sub eax,CAM(8)
        cmp eax,0x2000000
        jae .reap
        shr eax,4
        sub eax,0x100000
        mov RZ,eax
        mov eax,RX                  ; Z = r . front
        imul eax,CAM(12)
        mov edx,RY
        imul edx,CAM(16)
        add eax,edx
        mov edx,RZ
        imul edx,CAM(20)
        add eax,edx
        mov ZZ,eax
        mov eax,RX                  ; X = r . right
        imul eax,CAM(24)
        mov edx,RY
        imul edx,CAM(28)
        add eax,edx
        mov edx,RZ
        imul edx,CAM(32)
        add eax,edx
        mov XX,eax
        mov eax,RX                  ; Y = r . up
        imul eax,CAM(36)
        mov edx,RY
        imul edx,CAM(40)
        add eax,edx
        mov edx,RZ
        imul edx,CAM(44)
        add eax,edx
        mov YY,eax
        ; its distance squared in km: an axis past 32,767 km, or the whole past the bubble, reaped
        sar eax,12
        cmp eax,32767
        jg .reap
        cmp eax,-32767
        jl .reap
        imul eax,eax
        mov ecx,eax
        mov eax,XX
        sar eax,12
        cmp eax,32767
        jg .reap
        cmp eax,-32767
        jl .reap
        imul eax,eax
        add ecx,eax
        mov eax,ZZ
        sar eax,12
        cmp eax,32767
        jg .reap
        cmp eax,-32767
        jl .reap
        imul eax,eax
        add ecx,eax
        cmp ecx,RC(4)               ; (unsigned: three squares can pass 2^31)
        ja .reap
        mov D2,ecx
        ; within reach (the player's and the rock's radius) on every axis: to the hit list
        movzx eax,byte [si+13]
        shl eax,15
        add eax,RC(12)
        mov edx,eax
        neg edx
        cmp XX,eax
        jge .nohit
        cmp XX,edx
        jle .nohit
        cmp YY,eax
        jge .nohit
        cmp YY,edx
        jle .nohit
        cmp ZZ,eax
        jge .nohit
        cmp ZZ,edx
        jle .nohit
        mov bx,NHIT
        cmp bx,RC(24)
        jge .nohit
        shl bx,1
        add bx,HITO
        mov ax,INDEX
        mov [gs:bx],ax
        inc word NHIT
.nohit: mov eax,ZZ                  ; before the near plane: not drawn
        cmp eax,CAM(60)
        jl .next
        movzx eax,byte [si+13]      ; d x16 = r8 dk / Z
        mul dword RC(0)
        div dword ZZ
        mov ecx,D2                  ; past fade2: x (far2 - d2) / (far2 - fade2)
        cmp ecx,RC(8)
        jbe .nofade
        mov ecx,RC(4)
        sub ecx,D2
        mul ecx
        mov ecx,RC(4)
        sub ecx,RC(8)
        div ecx
.nofade:
        add eax,8                   ; d = (d16 + 8) >> 4: none, gone; dmax at most
        shr eax,4
        jz .next
        movsx edx,word RC(20)
        cmp eax,edx
        jle .dok
        mov eax,edx
.dok:   mov DD,ax
        mov eax,XX                  ; its middle: sx = (cx16 + X f16 / Z) >> 4
        imul dword CAM(48)
        idiv dword ZZ
        add eax,CAM(52)
        sar eax,4
        mov SX,eax
        mov eax,YY                  ; sy = (cy16 - Y f16 / Z) >> 4
        imul dword CAM(48)
        idiv dword ZZ
        mov ecx,CAM(56)
        sub ecx,eax
        sar ecx,4
        ; the box: (its top left = the middle less (d - 1) / 2) and d past it
        movzx edx,word DD
        dec edx
        shr edx,1
        mov eax,SX
        sub eax,edx
        cmp eax,XMAX
        jge .next
        movzx ebx,word DD
        add eax,ebx
        cmp eax,XMIN
        jle .next
        mov eax,ecx
        sub eax,edx
        cmp eax,YMAX
        jge .next
        add eax,ebx
        cmp eax,YMIN
        jle .next
        mov ax,SX
        mov [es:di],ax
        mov [es:di+2],cx
        mov ax,DD
        mov [es:di+4],ax
        mov al,[si+12]
        mov [es:di+6],al
        movzx eax,word RC(16)       ; its frame: (phase + (anim spin >> 2)) & mask
        movsx ecx,byte [si+15]
        imul eax,ecx
        sar eax,2
        add al,[si+14]
        and al,RC(18)
        mov [es:di+7],al
        mov eax,ZZ
        mov [es:di+8],eax
        add di,12
        inc word NOUT
        mov ax,NOUT
        cmp ax,[bp+24]
        jge .done
        jmp .next
.reap:  mov bx,NREAP
        cmp bx,RC(22)
        jge .next
        shl bx,1
        add bx,REAPO
        mov ax,INDEX
        mov [fs:bx],ax
        inc word NREAP
.next:  add si,16
        inc word INDEX
        dec word LEFT
        jnz .rock
.done:  les bx,[bp+16]              ; the lists' counts back in rc
        mov ax,NREAP
        mov [es:bx+26],ax
        mov ax,NHIT
        mov [es:bx+28],ax
        mov ax,NOUT
        pop ds
        pop di
        pop si
        mov sp,bp
        pop bp
        retf

; void rv_rocks_xms(const unsigned char far *hdr, int rows, int x, int y, unsigned handle,
;                   unsigned long off, unsigned char far *band, int band_y0, int y0, int y1,
;                   const short far *ext, int ext_y0)
; A big rock's rows in rows y0 .. y1-1 (cut by the caller to the extents'):
; each row's run (hdr: left + 1, length; its pixels at off in the handle's
; memory, the rows' runs one after another) clipped to its row's extent and
; moved by the XMS driver straight into the band.  The driver moves even
; lengths: a run cut to an odd one loses its pixel at the cut.
%define XM_LEN   [bp-16]                ; the move's record (16 bytes): length,
%define XM_SH    [bp-12]                ;   source handle, source offset,
%define XM_SOFF  [bp-10]
%define XM_DH    [bp-6]                 ;   destination handle (0), destination seg:off
%define XM_DOFF  [bp-4]
%define XM_DSEG  [bp-2]
%define XOFF  [bp-20]                   ; the next row's pixels (in the handle's memory)
%define XROW  [bp-22]                   ; the next row (of the sprite)
%define XSY   [bp-24]                   ; its row on screen
%define XEND  [bp-26]                   ; the last screen row + 1
%define XFRAME 26
        global _rv_rocks_xms
_rv_rocks_xms:
        push bp
        mov bp,sp
        sub sp,XFRAME
        push si
        push di
        mov ax,ss                   ; (the driver reads the record at ds:si, the record is on the stack: ss must be
        mov dx,ds                   ;  DGROUP, as Watcom's medium model has it)
        cmp ax,dx
        jne .out
        mov ax,[bp+16]              ; the record's constant parts
        mov XM_SH,ax
        mov word XM_DH,0
        mov eax,[bp+18]
        mov XOFF,eax
        les si,[bp+6]               ; es:si the rows' pairs
        lfs bx,[bp+32]              ; fs the extents
        mov ax,[bp+14]              ; rows past y0's above it: their runs skipped
        mov cx,[bp+28]
        sub cx,ax                   ; y0 - y: rows before the band's first
        jle .start
        cmp cx,[bp+10]
        jge .out
        mov XROW,cx
        xor edx,edx
.skip:  movzx eax,byte [es:si+1]
        add edx,eax
        add si,2
        loop .skip
        add XOFF,edx
        mov ax,[bp+28]
        jmp .first
.start: mov word XROW,0
        mov ax,[bp+14]
.first: mov XSY,ax                  ; the first screen row; the end: min(y + rows, y1)
        mov ax,[bp+14]
        add ax,[bp+10]
        cmp ax,[bp+30]
        jle .e
        mov ax,[bp+30]
.e:     mov XEND,ax
        mov ax,XSY
        cmp ax,XEND
        jge .out
.row:   movzx cx,byte [es:si+1]     ; the run: its length (0: none)
        jcxz .nextrow
        movzx ax,byte [es:si]
        dec ax
        add ax,[bp+12]              ; xa = x + left
        mov dx,ax
        add dx,cx                   ; xb
        mov bx,XSY                  ; the row's extent
        sub bx,[bp+36]
        shl bx,2
        add bx,[bp+32]
        mov di,ax                   ; ca = max(xa, l)
        cmp di,[fs:bx]
        jge .l
        mov di,[fs:bx]
.l:     cmp dx,[fs:bx+2]            ; cb = min(xb, r)
        jle .r
        mov dx,[fs:bx+2]
.r:     sub dx,di                   ; n
        jle .nextrow
        test dl,1                   ; odd: cut at the side that was cut
        jz .even
        dec dx
        jz .nextrow
        cmp di,ax
        je .even                    ; (the left as it was: the right was cut, its last pixel goes)
        inc di                      ; (the left was cut: its first pixel goes)
.even:  movzx edx,dx
        mov XM_LEN,edx
        mov bx,di                   ; the source: the run's start plus the cut
        sub bx,ax
        movzx ebx,bx
        add ebx,XOFF
        mov XM_SOFF,ebx
        mov ax,XSY                  ; the destination: band + (sy - band_y0) 640 + ca
        sub ax,[bp+26]
        imul ax,ax,640
        add ax,[bp+22]
        add ax,di
        mov XM_DOFF,ax
        mov ax,[bp+24]
        mov XM_DSEG,ax
        push si
        push es
        lea si,XM_LEN               ; (ds:si the record: ss is DGROUP, as ds)
        mov ah,0x0B
        call far [_rv_xms_entry]
        pop es
        pop si
.nextrow:
        movzx eax,byte [es:si+1]
        add XOFF,eax
        add si,2
        inc word XSY
        mov ax,XSY
        cmp ax,XEND
        jl .row
.out:   pop di
        pop si
        mov sp,bp
        pop bp
        retf

; void rv_rocks_copy(const unsigned char far *hdr, int rows, int x, int y, int k,
;                    const unsigned char far *pix, unsigned char far *band, int band_y0, int y0, int y1,
;                    const short far *ext, int ext_y0)
; The same rock's rows from pix (its runs one after another), each pixel k x k
; (1 .. 4): a run made k times wider once in the line buffer, then copied to
; each of its k screen rows in the band rows y0 .. y1-1, clipped to the row's
; extent.
%define CLINE(k) [bp-532+(k)]           ; the widened run (512 bytes: 128 x 4)
%define COFF  [bp-2]                    ; the next row's pixels in pix
%define CSY   [bp-4]                    ; the source row's first screen row
%define CROW  [bp-6]                    ; rows of the sprite left
%define CXA   [bp-8]                    ; the widened run on screen: its first column, its width
%define CW    [bp-10]
%define CSRC  [bp-12]                   ; the widened run's start: pix's offset (k = 1) or the line's
%define CFRAME 532
        global _rv_rocks_copy
_rv_rocks_copy:
        push bp
        mov bp,sp
        sub sp,CFRAME
        push si
        push di
        push ds
        cld
        mov ax,[bp+18]
        mov COFF,ax
        mov ax,[bp+14]
        mov CSY,ax
        mov ax,[bp+10]
        mov CROW,ax
        lgs bx,[bp+6]               ; gs:bx the rows' pairs
        lfs ax,[bp+32]              ; fs the extents
.src:   cmp word CROW,0
        jle .out
        mov ax,CSY                  ; at or past y1: done; its k rows all before y0: skipped
        cmp ax,[bp+30]
        jge .out
        movzx cx,byte [gs:bx+1]     ; its run's length (0: none)
        add ax,[bp+16]
        cmp ax,[bp+28]
        jle .nextsrc
        or cx,cx
        jz .nextsrc
        movzx ax,byte [gs:bx]       ; the widened run: x + (left) k, its width len k
        dec ax
        imul ax,[bp+16]
        add ax,[bp+12]
        mov CXA,ax
        mov ax,cx
        imul ax,[bp+16]
        mov CW,ax
        mov ax,COFF
        mov CSRC,ax
        cmp word [bp+16],1
        je .rows
        push bx                     ; widen the run into the line: each pixel k times
        mov si,COFF
        mov ds,[bp+20]
        push ss
        pop es
        lea di,CLINE(0)
        mov CSRC,di
        mov dx,[bp+16]
        cmp dx,2
        je .w2
        cmp dx,3
        je .w3
.w4:    lodsb
        mov ah,al
        stosw
        stosw
        loop .w4
        jmp .wdone
.w3:    lodsb
        mov ah,al
        stosw
        stosb
        loop .w3
        jmp .wdone
.w2:    lodsb
        mov ah,al
        stosw
        loop .w2
.wdone: pop bx
.rows:  mov dx,CSY                  ; its k screen rows (1 when k is 1) in y0 .. y1-1
        xor cx,cx
.krow:  cmp dx,[bp+28]
        jl .knext
        cmp dx,[bp+30]
        jge .nextsrc
        push bx
        push cx
        push dx
        mov bx,dx                   ; the row's extent
        sub bx,[bp+36]
        shl bx,2
        add bx,[bp+32]
        mov ax,CXA                  ; ca = max(xa, l), cb = min(xa + w, r)
        mov di,ax
        cmp di,[fs:bx]
        jge .cl
        mov di,[fs:bx]
.cl:    add ax,CW
        cmp ax,[fs:bx+2]
        jle .cr
        mov ax,[fs:bx+2]
.cr:    sub ax,di
        jle .kdone
        mov cx,ax                   ; n
        mov si,di                   ; the source: the run's start plus the cut
        sub si,CXA
        add si,CSRC
        mov ax,dx                   ; the destination: band + (sy - band_y0) 640 + ca
        sub ax,[bp+26]
        imul ax,ax,640
        add ax,[bp+22]
        add di,ax
        mov es,[bp+24]
        mov ds,[bp+20]              ; (k = 1: pix; else the line, in ss)
        cmp word [bp+16],1
        je .copy
        push ss
        pop ds
.copy:  mov ax,cx
        shr cx,2
        rep movsd
        mov cx,ax
        and cx,3
        rep movsb
.kdone: pop dx
        pop cx
        pop bx
.knext: inc dx
        inc cx
        cmp cx,[bp+16]
        jl .krow
.nextsrc:
        movzx ax,byte [gs:bx+1]
        add COFF,ax
        add bx,2
        mov ax,CSY
        add ax,[bp+16]
        mov CSY,ax
        dec word CROW
        jmp .src
.out:   pop ds
        pop di
        pop si
        mov sp,bp
        pop bp
        retf
