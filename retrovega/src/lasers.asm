; lasers.asm: the laser bolts' fast paths (lasers.h).  Each does what its
; reference in lasers.c does, to the bit (bolts.c compares them when a flight
; starts): rv_bolts_place = lz_place, rv_lines_draw = lz_lines_draw, and so
; on.  Far cdecl, as the medium model calls them;
; SI DI BP DS kept.  32-bit registers throughout (486): DOSBox charges an
; instruction a cycle whatever its width, so a bolt's eighteen products cost
; what eighteen moves would.
        cpu 486
%define LZ_BALL_SMALL 8                  ; lasers.h's: the small balls' sprites, in a block
%define LZ_BALL_SMALL_SET 204

segment LASERS_TEXT public align=16 class=CODE use16

; int rv_bolts_place(const LzBolt far *b, int n, const LzCam far *cam,
;                    LzLine far *lines, int maxlines, LzFat far *fats, int maxfats, int far *nfats)
; The bolts through the camera: each line-sized one out to lines[] (set up
; for rv_lines_draw), each bigger one to fats[]; returns the lines' count and
; stores the fat ones'.
;
; The frame: the camera copied to [bp-96] (CAM below: LzCam's offsets), the
; bolt's numbers under it.
%define CAM(k) [bp-96+(k)]
%define RZ   [bp-100]                   ; the middle less the camera, z (sixteenths of a km)
%define BX_  [bp-104]                   ; X Y Z: the middle in camera units
%define BY_  [bp-108]
%define BZ_  [bp-112]
%define XH   [bp-116]                   ; the head and the tail
%define YH   [bp-120]
%define ZH   [bp-124]
%define XT   [bp-128]
%define YT   [bp-132]
%define ZT   [bp-136]
%define R16  [bp-140]
%define SXH  [bp-144]                   ; on screen, x16
%define SYH  [bp-148]
%define SXT  [bp-152]
%define SYT  [bp-156]
%define LEFT [bp-158]                   ; bolts still to do
%define NL   [bp-160]                   ; lines out
%define NF   [bp-162]                   ; fat ones out
%define FOFF [bp-164]                   ; where the next fat one goes (its segment in FS)
%define AX_  [bp-166]                   ; the ends ordered, a the upper, x16
%define AY_  [bp-168]
%define BXX  [bp-170]
%define BYY  [bp-172]
%define NUM  [bp-176]                   ; the near plane's cut: zmin less the other end's z
%define FRAME 180

; an end brought to the near plane along the bolt: %1 = %2 + (%1 - %2) NUM / ECX
%macro CUT 2
        mov eax,%1
        sub eax,%2
        imul dword NUM
        idiv ecx
        add eax,%2
        mov %1,eax
%endmacro

; a line through (%1, %2) - (%3, %4) (x16 words, the first the upper) into
; es:di as lz_line_of makes it.  Uses eax ebx ecx edx.
%macro LINE_OF 4
        movsx eax,word %3
        movsx edx,word %1
        sub eax,edx
        mov ebx,eax                 ; dx
        mov ax,%2
        sar ax,4
        mov [es:di],ax              ; y0
        mov dx,%4
        sar dx,4
        inc dx
        mov [es:di+2],dx            ; y1
        movsx eax,word %1
        shl eax,12
        add eax,0x7FFF
        mov [es:di+4],eax           ; a's x'
        movsx ecx,word %3
        shl ecx,12
        add ecx,0x7FFF
        mov [es:di+8],ecx           ; b's x'
        sub dx,[es:di]
        cmp dx,1
        jne %%rows
        mov [es:di+12],ecx          ; one row: x1 = b, the way in m's sign
        mov eax,ebx
        sar eax,31
        mov [es:di+16],eax
        jmp %%done
%%rows:
        movsx ecx,word %4
        movsx eax,word %2
        sub ecx,eax                 ; dy (at least a row's worth)
        mov eax,ebx
        shl eax,16
        cdq
        idiv ecx
        mov [es:di+16],eax          ; m = dx / dy, 16.16
        mov ax,[es:di]
        inc ax
        shl ax,4
        sub ax,%2                   ; e: a to the bottom of its row
        movsx eax,ax
        imul eax,ebx
        mov edx,4096
        imul edx
        idiv ecx
        add eax,[es:di+4]
        mov [es:di+12],eax          ; x1 = a + dx e / dy
%%done:
%endmacro

        global _rv_bolts_place
_rv_bolts_place:
        push bp
        mov bp,sp
        sub sp,FRAME
        push si
        push di
        push ds
        cld
        lds si,[bp+12]              ; the camera into the frame: 21 dwords
        push ss
        pop es
        lea di,CAM(0)
        mov cx,21
        rep movsd
        mov ax,[bp+10]
        mov LEFT,ax
        xor ax,ax
        mov NL,ax
        mov NF,ax
        mov ax,[bp+22]
        mov FOFF,ax
        mov fs,[bp+24]
        les di,[bp+16]              ; the lines out
        lds si,[bp+6]               ; the bolts in
        cmp word LEFT,0
        jle .done
.bolt:
        ; the middle less the camera, in sixteenths of a km: an axis past 2^20 (65,536 km) is gone
        mov eax,[si]
        sub eax,CAM(0)
        cmp eax,0x2000000
        jae .next
        shr eax,4
        sub eax,0x100000
        mov ebx,eax                 ; rx
        mov eax,[si+4]
        sub eax,CAM(4)
        cmp eax,0x2000000
        jae .next
        shr eax,4
        sub eax,0x100000
        mov ecx,eax                 ; ry
        mov eax,[si+8]
        sub eax,CAM(8)
        cmp eax,0x2000000
        jae .next
        shr eax,4
        sub eax,0x100000
        mov RZ,eax                  ; rz
        ; Z = r . front: past the far plane, gone
        imul eax,CAM(20)
        mov edx,ebx
        imul edx,CAM(12)
        add eax,edx
        mov edx,ecx
        imul edx,CAM(16)
        add eax,edx
        cmp eax,CAM(64)
        jg .next
        mov BZ_,eax
        ; X = r . right, Y = r . up
        mov eax,ebx
        imul eax,CAM(24)
        mov edx,ecx
        imul edx,CAM(28)
        add eax,edx
        mov edx,RZ
        imul edx,CAM(32)
        add eax,edx
        mov BX_,eax
        mov eax,ebx
        imul eax,CAM(36)
        mov edx,ecx
        imul edx,CAM(40)
        add eax,edx
        mov edx,RZ
        imul edx,CAM(44)
        add eax,edx
        mov BY_,eax
        ; the half axis through the camera; the head = middle + it, the tail = middle - it
        movsx ebx,word [si+12]
        movsx ecx,word [si+14]
        movsx eax,word [si+16]
        imul eax,CAM(20)
        mov edx,ebx
        imul edx,CAM(12)
        add eax,edx
        mov edx,ecx
        imul edx,CAM(16)
        add eax,edx                 ; HZ
        mov edx,BZ_
        add edx,eax
        mov ZH,edx
        mov edx,BZ_
        sub edx,eax
        mov ZT,edx
        movsx eax,word [si+16]
        imul eax,CAM(32)
        mov edx,ebx
        imul edx,CAM(24)
        add eax,edx
        mov edx,ecx
        imul edx,CAM(28)
        add eax,edx                 ; HX
        mov edx,BX_
        add edx,eax
        mov XH,edx
        mov edx,BX_
        sub edx,eax
        mov XT,edx
        movsx eax,word [si+16]
        imul eax,CAM(44)
        mov edx,ebx
        imul edx,CAM(36)
        add eax,edx
        mov edx,ecx
        imul edx,CAM(40)
        add eax,edx                 ; HY
        mov edx,BY_
        add edx,eax
        mov YH,edx
        mov edx,BY_
        sub edx,eax
        mov YT,edx
        ; the near plane: an end behind it is brought to it along the bolt; both behind, gone
        mov ebx,CAM(60)
        mov eax,ZH
        cmp eax,ebx
        jge .head_in
        cmp ZT,ebx
        jl .next
        sub ebx,ZT
        mov NUM,ebx                 ; zmin - zt
        mov ecx,eax
        sub ecx,ZT                  ; zh - zt
        CUT XH,XT
        CUT YH,YT
        mov eax,CAM(60)
        mov ZH,eax
        jmp .cut
.head_in:
        cmp ZT,ebx
        jge .cut
        sub ebx,eax
        mov NUM,ebx                 ; zmin - zh
        mov ecx,ZT
        sub ecx,eax                 ; zt - zh
        CUT XT,XH
        CUT YT,YH
        mov eax,CAM(60)
        mov ZT,eax
.cut:
        ; the radius at the middle's depth, no more than rmax16
        mov ecx,ZH
        add ecx,ZT
        sar ecx,1
        mov eax,CAM(68)
        cdq
        idiv ecx
        movsx edx,word CAM(72)
        cmp eax,edx
        jle .r_ok
        mov eax,edx
.r_ok:  mov R16,eax
        ; the ends on screen, x16
        mov eax,XH
        imul dword CAM(48)
        idiv dword ZH
        add eax,CAM(52)
        mov SXH,eax
        mov eax,YH
        imul dword CAM(48)
        idiv dword ZH
        mov edx,CAM(56)
        sub edx,eax
        mov SYH,edx
        mov eax,XT
        imul dword CAM(48)
        idiv dword ZT
        add eax,CAM(52)
        mov SXT,eax
        mov eax,YT
        imul dword CAM(48)
        idiv dword ZT
        mov edx,CAM(56)
        sub edx,eax
        mov SYT,edx
        ; kept if its box with the radius touches the screen's
        mov eax,SXH
        mov edx,SXT
        cmp eax,edx
        jle .x_lo
        xchg eax,edx
.x_lo:  add edx,R16
        movsx ecx,word CAM(76)
        cmp edx,ecx
        jl .next
        sub eax,R16
        movsx ecx,word CAM(78)
        cmp eax,ecx
        jge .next
        mov eax,SYH
        mov edx,SYT
        cmp eax,edx
        jle .y_lo
        xchg eax,edx
.y_lo:  add edx,R16
        movsx ecx,word CAM(80)
        cmp edx,ecx
        jl .next
        sub eax,R16
        movsx ecx,word CAM(82)
        cmp eax,ecx
        jge .next
        ; a: the upper end (the left one when level)
        mov eax,SYH
        cmp eax,SYT
        jl .head_a
        jg .tail_a
        mov eax,SXH
        cmp eax,SXT
        jle .head_a
.tail_a:
        mov ax,SXT
        mov AX_,ax
        mov ax,SYT
        mov AY_,ax
        mov ax,SXH
        mov BXX,ax
        mov ax,SYH
        mov BYY,ax
        jmp .ordered
.head_a:
        mov ax,SXH
        mov AX_,ax
        mov ax,SYH
        mov AY_,ax
        mov ax,SXT
        mov BXX,ax
        mov ax,SYT
        mov BYY,ax
.ordered:
        mov ax,R16
        cmp ax,CAM(74)
        jge .fat
        ; a line (lz_line_of): room for it, and not absurdly long
        mov ax,NL
        cmp ax,[bp+20]
        jge .next
        movsx eax,word BXX
        movsx edx,word AX_
        sub eax,edx                 ; dx
        cmp eax,16384
        jge .next
        cmp eax,-16384
        jle .next
        LINE_OF AX_,AY_,BXX,BYY
.stored:
        add di,20
        inc word NL
        jmp .next
.fat:
        mov ax,NF
        cmp ax,[bp+26]
        jge .next
        push es
        push di
        push fs
        pop es
        mov di,FOFF
        mov ax,AX_
        stosw
        mov ax,AY_
        stosw
        mov ax,BXX
        stosw
        mov ax,BYY
        stosw
        mov ax,R16
        stosw
        xor ax,ax
        stosw
        mov FOFF,di
        inc word NF
        pop di
        pop es
.next:
        add si,18
        dec word LEFT
        jnz .bolt
.done:
        lds bx,[bp+28]
        mov ax,NF
        mov [bx],ax
        mov ax,NL
        pop ds
        pop di
        pop si
        mov sp,bp
        pop bp
        retf

; void rv_lines_draw(const LzLine far *l, int n, int colour, unsigned char far *band, int band_y0,
;                    int y0, int y1, const short far *ext, int ext_y0)
; The lines into the band, rows y0 .. y1-1 only (the caller has cut those to
; the opening's rows); every row clipped to its extent ext[2k], ext[2k+1].
%define ROWOFF [bp-2]                   ; the row being drawn, in the band
%define NLEFT  [bp-4]
%define NMID   [bp-6]
%define EMIT   [bp-8]                   ; emit_pos or emit_neg: the line's way
%define YE     [bp-10]
%define YS     [bp-12]
        global _rv_lines_draw
_rv_lines_draw:
        push bp
        mov bp,sp
        sub sp,12
        push si
        push di
        push ds
        cld
        mov ax,[bp+10]
        mov NLEFT,ax
        or ax,ax
        jle .done
        mov es,[bp+16]
        mov fs,[bp+26]
        lds si,[bp+6]
.line:
        mov ax,[si]                 ; ys = max(the line's first row, y0)
        cmp ax,[bp+20]
        jge .s
        mov ax,[bp+20]
.s:     mov cx,[si+2]               ; ye = min(its end, y1)
        cmp cx,[bp+22]
        jle .e
        mov cx,[bp+22]
.e:     cmp ax,cx
        jge .nextline
        mov YS,ax
        mov YE,cx
        mov dx,ax
        sub dx,[bp+18]
        imul dx,dx,640
        add dx,[bp+14]
        mov ROWOFF,dx
        mov bx,ax
        sub bx,[bp+28]
        shl bx,2
        add bx,[bp+24]              ; fs:bx: the first row's extent
        mov word EMIT,emit_pos
        cmp dword [si+16],0
        jge .way
        mov word EMIT,emit_neg
.way:
        cmp ax,[si]
        jne .inside
        ; its first row: from a to where it leaves the row (b, when it is all in one)
        mov edx,[si+4]
        mov ecx,[si+12]
        mov ax,[si+2]
        dec ax
        cmp ax,[si]
        jne .first
        mov ecx,[si+8]
.first: call word EMIT
        inc word YS
        jmp .middle
.inside:
        ; the band starts within it: x' at the row's top = x1 + m (ys - y0 - 1)
        movsx eax,ax
        movsx edx,word [si]
        sub eax,edx
        dec eax
        imul eax,[si+16]
        add eax,[si+12]
        mov edx,eax
.middle:
        ; the rows before its last: a row's worth of m each
        mov cx,[si+2]
        dec cx
        cmp cx,YE
        jle .m
        mov cx,YE
.m:     sub cx,YS
        jle .last
        mov NMID,cx
.mid:   mov ecx,edx
        add ecx,[si+16]
        call word EMIT
        dec word NMID
        jnz .mid
.last:
        ; its last row, ending at b, when the band reaches it and it is not the first
        mov cx,YE
        cmp cx,[si+2]
        jne .nextline
        mov cx,[si+2]
        dec cx
        cmp cx,[si]
        je .nextline
        mov ecx,[si+8]
        call word EMIT
.nextline:
        add si,20
        dec word NLEFT
        jnz .line
.done:
        pop ds
        pop di
        pop si
        mov sp,bp
        pop bp
        retf

; one row of a line: edx = x' at its top, ecx = at its bottom (16.16); the
; pixels whose centres lie between, else the one under the middle; clipped to
; fs:bx's extent; then the next row (ROWOFF, bx) and edx = ecx.  Near calls
; from rv_lines_draw, its frame in bp.
emit_pos:
        mov eax,edx
        sar eax,16                  ; xs
        mov edi,ecx
        sar edi,16                  ; xe
        cmp di,ax
        jg .ok
        lea eax,[edx+ecx-0x10000]
        sar eax,17
        mov di,ax
        inc di
.ok:    mov edx,ecx
        jmp emit_clip
emit_neg:
        mov eax,ecx
        sar eax,16                  ; xs: the bottom is the left
        mov edi,edx
        sar edi,16
        cmp di,ax
        jg .ok
        lea eax,[edx+ecx-0x10000]
        sar eax,17
        mov di,ax
        inc di
.ok:    mov edx,ecx
emit_clip:
        cmp ax,[fs:bx]
        jge .lo
        mov ax,[fs:bx]
.lo:    cmp di,[fs:bx+2]
        jle .hi
        mov di,[fs:bx+2]
.hi:    mov cx,di
        sub cx,ax
        jle .none
        mov di,ROWOFF
        add di,ax
        mov al,[bp+12]
        rep stosb
.none:  add word ROWOFF,640
        add bx,4
        ret

; void rv_pass_draw(const LzPass far *p, unsigned char far *band, int band_y0, int y0, int y1,
;                   const short far *ext, int ext_y0, const unsigned char far *roots)
; One capsule of a fat bolt into the band, rows y0 .. y1-1 (cut by the
; caller to the opening's), clipped to each row's extent.  roots: lz_sqrt16.
; void rv_flame_draw(the same, const LzFlame far *f, const LzFlameLook far *look)
; lz_flame_draw: an engine's flame through its footprint's rows and spans,
; each pixel shaded (shade_span) where the capsule's are filled.
%define PROW   [bp-2]                   ; ROWOFF
%define PYC    [bp-4]                   ; the row's centre, x16
%define PXS    [bp-6]
%define PROWS  [bp-8]
%define PC     [bp-12]                  ; the middle line's x at the row, 16.16 px
%define PSH    [bp-14]                  ; 1: a flame, shaded
%define QS00   [bp-18]                  ; the flame's numbers (LzFlame), dwords
%define QSX    [bp-22]
%define QSY    [bp-26]
%define QE00   [bp-30]
%define QEX    [bp-34]
%define QEY    [bp-38]
%define QKK    [bp-42]
%define QMODE  [bp-44]
%define QSEED  [bp-46]
%define QLKS   [bp-48]                  ; the look's segment (its offset made 0)
%define QN     [bp-50]                  ; the noise's index
%define QPOFF  [bp-52]                  ; the row's first pixel's ramp: 256 (ramp_a) where x + y is odd, else 512
%define QCNT   [bp-54]                  ; pixels left in the span
%define QE     [bp-58]                  ; e at the pixel (dword)
%define QSX2   [bp-62]                  ; the steps for two pixels (dwords)
%define QEX2   [bp-66]
%define PFRAME 66
        global _rv_flame_draw
_rv_flame_draw:
        push bp
        mov bp,sp
        sub sp,PFRAME
        mov word PSH,1
        jmp pass_body
        global _rv_pass_draw
_rv_pass_draw:
        push bp
        mov bp,sp
        sub sp,PFRAME
        mov word PSH,0
pass_body:
        push si
        push di
        push ds
        cld
        cmp word PSH,0
        je .pass
        lds si,[bp+30]              ; the flame's numbers into the frame
        mov eax,[si]
        mov QS00,eax
        mov eax,[si+4]
        mov QSX,eax
        mov eax,[si+8]
        mov QSY,eax
        mov eax,[si+12]
        mov QE00,eax
        mov eax,[si+16]
        mov QEX,eax
        mov eax,[si+20]
        mov QEY,eax
        mov eax,[si+24]
        mov QKK,eax
        mov ax,[si+28]
        mov QMODE,ax
        mov ax,[si+30]
        mov QSEED,ax
        mov eax,QSX
        shl eax,1
        mov QSX2,eax
        mov eax,QEX
        shl eax,1
        mov QEX2,eax
        mov ax,[bp+34]              ; the look: seg + off / 16 (it is paragraph aligned)
        shr ax,4
        add ax,[bp+36]
        mov QLKS,ax
.pass:  lds si,[bp+6]
        mov ax,[si]                 ; ys = max(top, y0)
        cmp ax,[bp+16]
        jge .s
        mov ax,[bp+16]
.s:     mov cx,[si+2]               ; ye = min(bot, y1)
        cmp cx,[bp+18]
        jle .e
        mov cx,[bp+18]
.e:     sub cx,ax
        jle .done
        mov PROWS,cx
        mov dx,ax
        shl dx,4
        add dx,8
        mov PYC,dx
        movsx edx,ax
        movsx ecx,word [si]
        sub edx,ecx
        imul edx,[si+28]
        add edx,[si+24]
        mov PC,edx                  ; c = c0 + m (ys - top)
        mov dx,ax
        sub dx,[bp+14]
        imul dx,dx,640
        add dx,[bp+10]
        mov PROW,dx
        mov bx,ax
        sub bx,[bp+24]
        shl bx,2
        add bx,[bp+20]
        mov fs,[bp+22]
        mov es,[bp+12]
        mov gs,[bp+28]
.row:
        mov dx,PYC
        ; the left: a's arc, the body, b's arc
        cmp dx,[si+14]
        jl .la
        cmp dx,[si+16]
        jl .lbody
        mov ax,dx
        sub ax,[si+10]
        call root
        mov cx,[si+8]
        sub cx,ax
        add cx,7
        sar cx,4
        jmp .ldone
.la:    mov ax,dx
        sub ax,[si+6]
        call root
        mov cx,[si+4]
        sub cx,ax
        add cx,7
        sar cx,4
        jmp .ldone
.lbody: mov eax,PC
        sub eax,[si+32]
        add eax,0x7FFF
        sar eax,16
        mov cx,ax
.ldone: mov PXS,cx
        ; the right
        cmp dx,[si+18]
        jl .ra
        cmp dx,[si+20]
        jl .rbody
        mov ax,dx
        sub ax,[si+10]
        call root
        add ax,[si+8]
        add ax,8
        sar ax,4
        jmp .rdone
.ra:    mov ax,dx
        sub ax,[si+6]
        call root
        add ax,[si+4]
        add ax,8
        sar ax,4
        jmp .rdone
.rbody: mov eax,PC
        add eax,[si+32]
        add eax,0x8000
        sar eax,16
.rdone: mov cx,PXS                  ; [cx, ax) clipped to the row's extent
        cmp cx,[fs:bx]
        jge .lo
        mov cx,[fs:bx]
.lo:    cmp ax,[fs:bx+2]
        jle .hi
        mov ax,[fs:bx+2]
.hi:    sub ax,cx
        jle .skip
        mov di,PROW
        cmp word PSH,0
        jne .shade
        add di,cx
        mov cx,ax
        mov al,[si+22]
        rep stosb
        jmp .skip
.shade: call shade_span
.skip:  add word PROW,640
        add bx,4
        add word PYC,16
        mov eax,[si+28]
        add PC,eax
        dec word PROWS
        jnz .row
.done:
        pop ds
        pop di
        pop si
        mov sp,bp
        pop bp
        retf

; void rv_flame_setup(int ax, int ay, int tx, int ty, int r16, int seed, LzFlame far *f, LzFat far *fp)
; lz_flame_setup: an engine's flame's numbers and the capsule round it, to
; the bit (C's 64-bit muldivs are an imul and an idiv here).
%define GT2  [bp-4]                     ; dwords: tx^2 + ty^2, its root, len r16, r16
%define GLEN [bp-8]
%define GLR  [bp-12]
%define GR   [bp-16]
%macro MULDIV 2                         ; eax = eax x %1 / %2 (C's muldiv: the product whole, then divided)
        mov ecx,%1
        imul ecx
        idiv dword %2
%endmacro
        global _rv_flame_setup
_rv_flame_setup:
        push bp
        mov bp,sp
        sub sp,16
        push si
        push di
        push ds
        les di,[bp+18]
        movsx eax,word [bp+10]
        imul eax,eax
        movsx ecx,word [bp+12]
        imul ecx,ecx
        add eax,ecx
        mov GT2,eax
        call isqrt32
        mov GLEN,eax
        movsx eax,word [bp+14]
        mov GR,eax
        mov ax,[bp+16]
        mov [es:di+30],ax           ; seed
        mov eax,GLEN
        shl eax,2
        cmp eax,GR
        jge .slant
        ; end on, or nearly: a disc; s down and e across it, in radii
        mov word [es:di+28],1
        xor eax,eax
        mov [es:di+4],eax           ; sx
        mov [es:di+20],eax          ; ey
        mov eax,16
        MULDIV 65536,GR
        mov [es:di+8],eax           ; sy
        mov [es:di+16],eax          ; ex
        movsx ecx,word [bp+8]
        mov eax,8
        sub eax,ecx
        MULDIV 65536,GR
        mov [es:di],eax             ; s00
        movsx ecx,word [bp+6]
        mov eax,8
        sub eax,ecx
        MULDIV 65536,GR
        mov [es:di+12],eax          ; e00
        mov dword [es:di+24],65536  ; kk
        mov word [bp+10],0          ; (no length: the footprint a disc)
        mov word [bp+12],0
        jmp .foot
.slant: mov word [es:di+28],0
        movsx eax,word [bp+10]
        shl eax,4
        MULDIV 65536,GT2
        mov [es:di+4],eax           ; sx = 16 tx / t2
        movsx eax,word [bp+12]
        shl eax,4
        MULDIV 65536,GT2
        mov [es:di+8],eax           ; sy
        movsx ecx,word [bp+6]
        mov eax,8
        sub eax,ecx
        movsx ecx,word [bp+10]
        imul eax,ecx
        mov ebx,eax                 ; (8 - ax) tx
        movsx ecx,word [bp+8]
        mov eax,8
        sub eax,ecx
        movsx ecx,word [bp+12]
        imul eax,ecx                ; (8 - ay) ty
        add eax,ebx
        MULDIV 65536,GT2
        mov [es:di],eax             ; s00
        mov eax,GLEN
        imul eax,GR
        mov GLR,eax                 ; len r16
        movsx eax,word [bp+12]
        shl eax,4
        MULDIV 65536,GLR
        mov [es:di+16],eax          ; ex = 16 ty / (len r16)
        movsx eax,word [bp+10]
        shl eax,4
        neg eax
        MULDIV 65536,GLR
        mov [es:di+20],eax          ; ey = -16 tx / (len r16)
        movsx ecx,word [bp+6]
        mov eax,8
        sub eax,ecx
        movsx ecx,word [bp+12]
        imul eax,ecx
        mov ebx,eax                 ; (8 - ax) ty
        movsx ecx,word [bp+8]
        mov eax,8
        sub eax,ecx
        movsx ecx,word [bp+10]
        imul eax,ecx                ; (8 - ay) tx
        sub ebx,eax
        mov eax,ebx
        MULDIV 65536,GLR
        mov [es:di+12],eax          ; e00
        mov eax,GLEN
        MULDIV 65536,GR
        mov [es:di+24],eax          ; kk = len / r16
.foot:  les di,[bp+22]              ; the footprint: r16 9/8 + 2 (at most LZ_RMAX16), a the upper end
        mov ax,[bp+14]
        imul ax,ax,9
        cwd
        mov cx,8
        idiv cx
        add ax,2
        cmp ax,128
        jle .rf
        mov ax,128
.rf:    mov [es:di+8],ax
        mov word [es:di+10],0
        mov ax,[bp+6]
        mov bx,[bp+8]
        mov cx,ax
        add cx,[bp+10]              ; ax + tx
        mov dx,bx
        add dx,[bp+12]              ; ay + ty
        cmp word [bp+12],0
        jl .swap
        jg .keep
        cmp word [bp+10],0
        jge .keep
.swap:  xchg ax,cx
        xchg bx,dx
.keep:  mov [es:di],ax
        mov [es:di+2],bx
        mov [es:di+4],cx
        mov [es:di+6],dx
        pop ds
        pop di
        pop si
        mov sp,bp
        pop bp
        retf

; a flame's span: cx its first column, ax how many, di the row's offset in
; the band (es), the row's centre at PYC.  Two pixels at a time: their s and
; e (linear, from the frame's QS00 .. QEY), r = |e| or the round root's
; distance (max + 3/8 min), the profile's intensity at (s, r + the noise's
; jitter), plus the noise, and the level's two colours of the stipple
; (lasers.c's flame_span).  The noise's index in dl.
shade_span:
        push si
        push bx
        push fs
        mov QCNT,ax
        add di,cx                   ; es:di the first pixel
        mov fs,QLKS
        mov ax,PYC
        sar ax,4                    ; y
        mov bx,ax
        imul bx,bx,7
        mov dx,cx
        imul dx,dx,13
        add bx,dx
        add bx,QSEED
        and bx,255
        mov QN,bx                   ; (y 7 + x 13 + seed) & 255
        mov bx,ax
        add bx,cx
        and bx,1                    ; (y + x) & 1: odd, the first is ramp_a's (256), else ramp_b's (512)
        shl bx,8
        neg bx
        add bx,512
        mov QPOFF,bx
        movsx eax,ax
        movsx ecx,cx
        mov esi,QSY
        imul esi,eax
        add esi,QS00
        mov edx,QSX
        imul edx,ecx
        add esi,edx                 ; s = s00 + sy y + sx x
        mov edx,QEY
        imul edx,eax
        add edx,QE00
        mov eax,QEX
        imul eax,ecx
        add edx,eax
        mov QE,edx                  ; e = e00 + ey y + ex x
        movzx edx,word QN
.px:    mov ecx,QE
        mov eax,ecx
        sar eax,31
        xor ecx,eax
        sub ecx,eax                 ; |e|
        xor ebx,ebx                 ; the profile's row (s >> 12) x 16: 0 at the root
        cmp word QMODE,0
        jne .disc
        or esi,esi
        js .root
        cmp esi,65536
        jge .next
        mov ebx,esi
        shr ebx,12
        shl bx,4
        jmp .rr
.disc:  mov eax,esi                 ; end on: c = |s|
        push edx
        mov edx,eax
        sar edx,31
        xor eax,edx
        sub eax,edx
        pop edx
        jmp .oct
.root:  mov eax,esi                 ; the round root: c = (-s kk) >> 16
        neg eax
        push edx
        imul dword QKK
        shrd eax,edx,16
        pop edx
.oct:   cmp ecx,eax                 ; r = max + 3/8 min
        jge .emax
        xchg ecx,eax
.emax:  lea eax,[eax+eax*2]
        sar eax,3
        add ecx,eax
.rr:    cmp ecx,19 << 12
        jge .next
        shr ecx,12
        movsx ax,byte [fs:edx+768]  ; r's jitter
        add cx,ax
        jns .rpos
        xor cx,cx
.rpos:  cmp cx,16
        jge .next
        add bx,cx
        movzx ax,byte [fs:ebx]      ; the profile
        or ax,ax
        jz .next
        movsx cx,byte [fs:edx+1024] ; the intensity's noise
        add ax,cx
        jns .tpos
        xor ax,ax
.tpos:  cmp ax,255
        jle .tok
        mov ax,255
.tok:   movzx ebx,ax
        add bx,QPOFF
        mov al,[fs:ebx]             ; the first pixel's colour
        xor bx,768
        mov ah,[fs:ebx]             ; the second's: the other ramp
        cmp word QCNT,1
        je .one
        mov [es:di],ax
        jmp .next
.one:   mov [es:di],al
.next:  add di,2
        add esi,QSX2
        mov eax,QEX2
        add QE,eax
        add dl,26
        sub word QCNT,2
        jg .px
        pop fs
        pop bx
        pop si
        ret

; ax = d (x16, the row's centre from an end's) -> ax = half the chord there:
; lz_sqrt16[max(r^2 - d^2, 0) / 4] (gs: the table, its offset at [bp+26])
root:
        imul ax,ax
        mov di,[si+12]
        sub di,ax
        jg .pos
        xor di,di
.pos:   shr di,2
        add di,[bp+26]
        movzx ax,byte [gs:di]
        ret

; void rv_bolts_move(LzBolt far *b, int n, long k): every bolt flies k
; positions for each sixteenth of a km of its half axis (lz_move).
        global _rv_bolts_move
_rv_bolts_move:
        push bp
        mov bp,sp
        push si
        push ds
        mov cx,[bp+10]
        or cx,cx
        jle .done
        lds si,[bp+6]
        mov ebx,[bp+12]
.bolt:  movsx eax,word [si+12]
        imul eax,ebx
        add [si],eax
        movsx eax,word [si+14]
        imul eax,ebx
        add [si+4],eax
        movsx eax,word [si+16]
        imul eax,ebx
        add [si+8],eax
        add si,18
        dec cx
        jnz .bolt
.done:  pop ds
        pop si
        pop bp
        retf

; int rv_fat_setup(const LzFat far *f, const LzStyle far *st, LzPass far *pass, LzLine far *core,
;                  int far *core_line)
; lz_fat_setup: a fat bolt's capsules (the body, and the core if it is thick
; enough) into pass[], the count back; a core too thin for a capsule as a
; line into *core (*core_line 1).
%define FAX  [bp-2]                     ; the bolt's ends and radius
%define FAY  [bp-4]
%define FBX  [bp-6]
%define FBY  [bp-8]
%define FR   [bp-10]
%define FDX  [bp-14]                    ; bx - ax, by - ay (dwords)
%define FDY  [bp-18]
%define FLEN [bp-22]                    ; its length x16 << FSH
%define FSH  [bp-24]
%define FNP  [bp-26]                    ; passes made
%define FPR  [bp-28]                    ; the pass's radius and colour
%define FCOL [bp-30]
%define FNUM [bp-34]
%define FFO  [bp-38]
%define FFRAME 40
        global _rv_fat_setup
_rv_fat_setup:
        push bp
        mov bp,sp
        sub sp,FFRAME
        push si
        push di
        push ds
        call fat_load
        les di,[bp+14]              ; the passes out
        mov word FNP,0
        lds si,[bp+10]              ; the style
        mov ax,FR
        mov FPR,ax
        mov al,[si+1]               ; body
        mov FCOL,al
        call pass_of
        mov ax,FR
        imul ax,ax,3
        sar ax,3                    ; the core: 3 r / 8
        cmp ax,[si+4]               ; body16: a capsule core, or a line
        jl .line
        mov FPR,ax
        mov al,[si+2]               ; core
        mov FCOL,al
        call pass_of
        lds si,[bp+22]
        mov word [si],0
        jmp .done
.line:  les di,[bp+18]
        LINE_OF FAX,FAY,FBX,FBY
        lds si,[bp+22]
        mov word [si],1
.done:  mov ax,FNP
        pop ds
        pop di
        pop si
        mov sp,bp
        pop bp
        retf

; int rv_capsule_setup(const LzFat far *f, int colour, LzPass far *pass)
; lz_capsule_setup: one capsule of f's ends and radius, in one colour (the
; engines' flames are three of these).
        global _rv_capsule_setup
_rv_capsule_setup:
        push bp
        mov bp,sp
        sub sp,FFRAME
        push si
        push di
        push ds
        call fat_load
        les di,[bp+12]
        mov word FNP,0
        mov ax,FR
        mov FPR,ax
        mov al,[bp+10]
        mov FCOL,al
        call pass_of
        pop ds
        pop di
        pop si
        mov sp,bp
        pop bp
        retf

; the fat bolt at [bp+6] into the frame's locals: its ends and radius, b - a,
; and its length with as many more bits as keep its square in 31 (seg_len).
; Leaves ds:si on it.
fat_load:
        lds si,[bp+6]
        mov ax,[si]
        mov FAX,ax
        mov ax,[si+2]
        mov FAY,ax
        mov ax,[si+4]
        mov FBX,ax
        mov ax,[si+6]
        mov FBY,ax
        mov ax,[si+8]
        mov FR,ax
        movsx eax,word FBX
        movsx edx,word FAX
        sub eax,edx
        mov FDX,eax
        movsx eax,word FBY
        movsx edx,word FAY
        sub eax,edx
        mov FDY,eax
        mov eax,FDX
        imul eax,eax
        mov ecx,FDY
        imul ecx,ecx
        add eax,ecx
        xor cx,cx
.sh:    cmp cx,8
        jge .root
        cmp eax,0x20000000
        jae .root
        shl eax,2
        inc cx
        jmp .sh
.root:  mov FSH,cx
        call isqrt32
        mov FLEN,eax
        ret

; one pass (pass_of) into es:di, radius FPR, colour FCOL; then di += 40, FNP++.
; Keeps si (the style).
pass_of:
        mov ax,FAX
        mov [es:di+4],ax
        mov ax,FAY
        mov [es:di+6],ax
        mov ax,FBX
        mov [es:di+8],ax
        mov ax,FBY
        mov [es:di+10],ax
        mov word [es:di+38],0       ; pad
        mov ax,FPR
        mov [es:di+36],ax           ; r
        imul ax,ax
        mov [es:di+12],ax           ; r2
        movzx ax,byte FCOL
        mov [es:di+22],ax           ; colour
        mov ax,FAY
        sub ax,FPR
        add ax,7
        sar ax,4
        mov [es:di],ax              ; top = (ay - r + 7) >> 4
        mov ax,FBY
        add ax,FPR
        sub ax,8
        sar ax,4
        inc ax
        mov [es:di+2],ax            ; bot
        cmp dword FDY,0
        jne .slant
        mov word [es:di+14],32767   ; level: a's arc on the left, b's on the right
        mov word [es:di+16],32767
        mov word [es:di+18],-32768
        mov word [es:di+20],-32768
        xor eax,eax
        mov [es:di+32],eax          ; hw
        mov [es:di+28],eax          ; m
        mov [es:di+24],eax          ; c0
        jmp .out
.slant:
        ; the tangent rows: off = r dx 2^sh / len, floor and ceiling
        movsx eax,word FPR
        imul eax,FDX
        mov cl,FSH
        shl eax,cl
        mov FNUM,eax
        cdq
        idiv dword FLEN             ; truncated
        mov FFO,eax
        mov ecx,eax                 ; co
        mov ebx,eax
        imul ebx,FLEN
        cmp ebx,FNUM
        jle .f1
        dec dword FFO               ; fo * len > num: floor is one less
.f1:    cmp ebx,FNUM
        jge .c1
        inc ecx                     ; co * len < num: ceiling one more
.c1:    mov ax,FAY
        add ax,cx
        mov [es:di+14],ax           ; la = ay + co
        mov ax,FBY
        sub ax,cx
        inc ax
        mov [es:di+20],ax           ; rb = by - co + 1
        mov ecx,FFO
        mov ax,FBY
        add ax,cx
        inc ax
        mov [es:di+16],ax           ; lb = by + fo + 1
        mov ax,FAY
        sub ax,cx
        mov [es:di+18],ax           ; ra = ay - fo
        ; hw = r len (4096 >> sh) / dy
        movsx eax,word FPR
        imul eax,FLEN
        mov ebx,4096
        mov cl,FSH
        shr ebx,cl
        mov ecx,FDY
        call mdiv
        mov [es:di+32],eax
        ; m = (dx << 16) / dy
        mov eax,FDX
        shl eax,16
        cdq
        idiv dword FDY
        mov [es:di+28],eax
        ; c0 = (ax << 12) + (yc - ay) dx 4096 / dy, yc the top row's centre
        mov ax,[es:di]
        shl ax,4
        add ax,8
        movsx eax,ax
        movsx edx,word FAY
        sub eax,edx
        imul eax,FDX
        mov ebx,4096
        mov ecx,FDY
        call mdiv
        movsx edx,word FAX
        shl edx,12
        add eax,edx
        mov [es:di+24],eax
.out:   add di,40
        inc word FNP
        ret

; eax = the low 32 bits of a ebx / ecx, the product whole and the quotient
; truncated (lasers.c's muldiv: an idiv would trap where the quotient passes
; 32 bits).  Uses edx.
mdiv:
        imul ebx                    ; edx:eax
        mov ebx,edx
        xor ebx,ecx                 ; bit 31: the quotient's sign
        push ebx
        or edx,edx
        jns .np
        not eax
        not edx
        add eax,1
        adc edx,0
.np:    mov ebx,ecx
        or ebx,ebx
        jns .cp
        neg ebx
.cp:    push eax
        mov eax,edx
        xor edx,edx
        div ebx                     ; the high half's quotient, its remainder in edx
        pop eax
        div ebx                     ; the low half's
        pop ebx
        or ebx,ebx
        jns .pos
        neg eax
.pos:   ret

; eax = the integer square root of eax (unsigned; lasers.c's isqrt).  Uses ebx ecx edx.
isqrt32:
        mov ecx,eax                 ; v
        xor eax,eax                 ; r
        mov ebx,0x40000000          ; bit
.big:   cmp ebx,ecx
        jbe .loop
        shr ebx,2
        jmp .big
.loop:  or ebx,ebx
        jz .done
        mov edx,eax
        add edx,ebx
        cmp ecx,edx
        jb .less
        sub ecx,edx
        shr eax,1
        add eax,ebx
        jmp .next
.less:  shr eax,1
.next:  shr ebx,2
        jmp .loop
.done:  ret


; long rv_balls_place(const LzBall far *b, int n, const LzCam far *cam, const LzBallKind far *k, int dmax,
;                     unsigned anim, LzBallOut far *out, int maxout, const unsigned char far *small)
; lz_balls_place: the balls through the camera, each one on screen out (its
; sprite's top left, diameter and set): a small one to out's front, its
; sprite in `small`; a big one to out's back, its sprite 0.  ax the small
; ones, dx the big.  A ball past its kind's z1 is one pixel (no division for
; its size): nearly all of them.
%define BCAM(k) [bp-96+(k)]
%define BZZ   [bp-100]                  ; Z, camera units
%define BXX_  [bp-104]                  ; its middle's x on screen, x16
%define BXMIN [bp-108]                  ; the box, pixels
%define BXMAX [bp-112]
%define BYMIN [bp-116]
%define BYMAX [bp-120]
%define BLEFT [bp-122]                  ; balls still to do
%define BROOM [bp-124]                  ; room left in out
%define BNS   [bp-126]                  ; small balls out, big
%define BNB   [bp-128]
%define BFRONT [bp-130]                 ; where the next small one goes (es), the next big one
%define BBACK [bp-132]
%define BKOFF [bp-134]                  ; the kinds' offset (fs)
%define BD    [bp-136]                  ; the diameter
%define BFRAME 136
        global _rv_balls_place
_rv_balls_place:
        push bp
        mov bp,sp
        sub sp,BFRAME
        push si
        push di
        push ds
        cld
        lds si,[bp+12]              ; the camera into the frame: 21 dwords
        push ss
        pop es
        lea di,BCAM(0)
        mov cx,21
        rep movsd
        movsx eax,word BCAM(76)     ; its box in pixels
        sar eax,4
        mov BXMIN,eax
        movsx eax,word BCAM(78)
        sar eax,4
        mov BXMAX,eax
        movsx eax,word BCAM(80)
        sar eax,4
        mov BYMIN,eax
        movsx eax,word BCAM(82)
        sar eax,4
        mov BYMAX,eax
        xor ax,ax
        mov BNS,ax
        mov BNB,ax
        mov ax,[bp+10]
        mov BLEFT,ax
        lfs ax,[bp+16]
        mov BKOFF,ax
        les di,[bp+24]              ; out: the small ones from its front, the big from its back
        mov BFRONT,di
        mov ax,[bp+28]
        mov BROOM,ax
        dec ax
        imul ax,ax,12
        add ax,di
        mov BBACK,ax
        lds si,[bp+6]               ; the balls
        cmp word BLEFT,0
        jle .done
        cmp word BROOM,0
        jle .done
.ball:  mov eax,[si]                ; the middle less the camera: an axis past 2^20 sixteenths, gone
        sub eax,BCAM(0)
        cmp eax,0x2000000
        jae .next
        shr eax,4
        sub eax,0x100000
        mov ebx,eax                 ; rx
        mov eax,[si+4]
        sub eax,BCAM(4)
        cmp eax,0x2000000
        jae .next
        shr eax,4
        sub eax,0x100000
        mov ecx,eax                 ; ry
        mov eax,[si+8]
        sub eax,BCAM(8)
        cmp eax,0x2000000
        jae .next
        shr eax,4
        sub eax,0x100000
        mov edi,eax                 ; rz
        imul eax,BCAM(20)           ; Z = r . front: before the near plane or past the far, gone
        mov edx,ebx
        imul edx,BCAM(12)
        add eax,edx
        mov edx,ecx
        imul edx,BCAM(16)
        add eax,edx
        cmp eax,BCAM(64)
        jg .next
        cmp eax,BCAM(60)
        jl .next
        mov BZZ,eax
        mov eax,ebx                 ; X = r . right: cx16 + X f16 / Z
        imul eax,BCAM(24)
        mov edx,ecx
        imul edx,BCAM(28)
        add eax,edx
        mov edx,edi
        imul edx,BCAM(32)
        add eax,edx
        imul dword BCAM(48)
        idiv dword BZZ
        add eax,BCAM(52)
        mov BXX_,eax
        mov eax,ebx                 ; Y = r . up: cy16 - Y f16 / Z
        imul eax,BCAM(36)
        imul ecx,BCAM(40)
        add eax,ecx
        imul edi,BCAM(44)
        add eax,edi
        imul dword BCAM(48)
        idiv dword BZZ
        mov ecx,BCAM(56)
        sub ecx,eax
        movzx bx,byte [si+18]       ; its kind
        shl bx,4
        add bx,BKOFF
        mov eax,BZZ
        cmp eax,[fs:bx+4]           ; past z1: one pixel
        jg .one
        mov eax,[fs:bx]             ; d = (rf / Z + 4) >> 3 (2 at least here), dmax at most
        cdq
        idiv dword BZZ
        add eax,4
        sar eax,3
        cmp ax,[bp+20]
        jle .dk
        mov ax,[bp+20]
.dk:    mov BD,ax
        dec ax                      ; the top left: the middle less (d - 1) 8
        shl ax,3
        movsx edx,ax
        sub ecx,edx
        mov eax,BXX_
        sub eax,edx
        mov di,BFRONT               ; where it goes
        cmp word BD,LZ_BALL_SMALL
        jle .box
        mov di,BBACK
        jmp .box
.one:   mov word BD,1
        mov eax,BXX_
        mov di,BFRONT
.box:   sar eax,4                   ; x0, y0: off the box, gone
        sar ecx,4
        cmp eax,BXMAX
        jge .next
        cmp ecx,BYMAX
        jge .next
        movzx edx,word BD
        add edx,eax
        cmp edx,BXMIN
        jle .next
        movzx edx,word BD
        add edx,ecx
        cmp edx,BYMIN
        jle .next
        mov [es:di],ax
        mov [es:di+2],cx
        mov dx,BD
        mov [es:di+4],dl
        mov al,[si+19]              ; its set: the kind's first + (anim + phase) & mask
        add al,[bp+22]
        and al,[fs:bx+10]
        add al,[fs:bx+8]
        mov [es:di+5],al
        mov word [es:di+6],0
        cmp dl,LZ_BALL_SMALL
        ja .big
        movzx ax,al                 ; a small one's sprite: small + set 204 + the set's smaller ones
        imul ax,ax,LZ_BALL_SMALL_SET
        movzx bx,dl
        add al,[cs:ball_before+bx-1]
        adc ah,0
        add ax,[bp+30]
        mov [es:di+8],ax
        mov ax,[bp+32]
        mov [es:di+10],ax
        add word BFRONT,12
        inc word BNS
        dec word BROOM
        jz .done
        jmp .next
.big:   mov dword [es:di+8],0
        sub word BBACK,12
        inc word BNB
        dec word BROOM
        jz .done
.next:  add si,20
        dec word BLEFT
        jnz .ball
.done:  mov ax,BNS
        mov dx,BNB
        pop ds
        pop di
        pop si
        mov sp,bp
        pop bp
        retf

ball_before:                            ; the pixels of the sprites smaller than 1 .. 8
        db 0, 1, 5, 14, 30, 55, 91, 140

; void rv_balls_draw(const LzBallOut far *o, int n, unsigned char far *band, int band_y0, int y0, int y1,
;                    const short far *ext, int ext_y0)
; lz_balls_draw: each ball's sprite rows that fall in rows y0 .. y1-1 (cut
; by the caller to the extents'), each clipped to its row's extent, the 0
; pixels clear.  A ball not in the rows costs five to nine instructions (the
; balls are visited once a band); one of one pixel goes straight in; the
; rest two pixels a step.
%define DEND  [bp-2]                    ; the list's end (its offset)
%define DSEG  [bp-4]                    ; the list's segment (ds between balls)
%define DSI   [bp-6]                    ; the ball being drawn
%define DYS   [bp-8]                    ; its rows: the next, the end
%define DYE   [bp-10]
%define DROW  [bp-12]                   ; the next row's start in the band
%define DEXT  [bp-14]                   ; its extent (fs)
%define DX0   [bp-16]                   ; the sprite's left, its diameter
%define DD    [bp-18]
%define DSPR  [bp-20]                   ; the next row's first pixel in the sprite (ds while it is drawn)
%define DFRAME 20
        global _rv_balls_draw
_rv_balls_draw:
        push bp
        mov bp,sp
        sub sp,DFRAME
        push si
        push di
        push ds
        cld
        mov cx,[bp+10]
        or cx,cx
        jle .done
        lds si,[bp+6]
        mov DSEG,ds
        imul cx,cx,12
        add cx,si
        mov DEND,cx
        mov es,[bp+14]
        mov fs,[bp+24]
.ball:  mov ax,[si+2]               ; its top at or past the rows' end, or its bottom at or before their start: not here
        cmp ax,[bp+20]
        jge .next
        movzx cx,byte [si+4]
        add cx,ax
        cmp cx,[bp+18]
        jle .next
        mov dx,[si]                 ; its left
        cmp byte [si+4],1
        jne .rows
        mov bx,ax                   ; one pixel: inside its row's extent, straight in
        sub bx,[bp+26]
        shl bx,2
        add bx,[bp+22]
        cmp dx,[fs:bx]
        jl .next
        cmp dx,[fs:bx+2]
        jge .next
        lgs bx,[si+8]
        mov cl,[gs:bx]
        or cl,cl
        jz .next
        mov di,ax
        sub di,[bp+16]
        imul di,di,640
        add di,[bp+12]
        add di,dx
        mov [es:di],cl
.next:  add si,12
        cmp si,DEND
        jb .ball
.done:  pop ds
        pop di
        pop si
        mov sp,bp
        pop bp
        retf

.rows:  mov bx,ax                   ; its top; ys = max(top, y0), ye = min(bottom, y1)
        cmp ax,[bp+18]
        jge .s
        mov ax,[bp+18]
.s:     cmp cx,[bp+20]
        jle .e
        mov cx,[bp+20]
.e:     mov DYS,ax
        mov DYE,cx
        mov DX0,dx
        movzx cx,byte [si+4]
        mov DD,cx
        mov DSI,si
        mov dx,ax                   ; the sprite's row ys: spr + (ys - top) d
        sub dx,bx
        imul dx,cx
        add dx,[si+8]
        mov DSPR,dx
        mov dx,ax                   ; the band's
        sub dx,[bp+16]
        imul dx,dx,640
        add dx,[bp+12]
        mov DROW,dx
        sub ax,[bp+26]              ; its extent
        shl ax,2
        add ax,[bp+22]
        mov DEXT,ax
        mov ds,[si+10]              ; the sprite's segment (the list's again after the ball)
.row:   mov bx,DEXT
        mov cx,DX0                  ; xs = max(x0, lo), xe = min(x0 + d, hi)
        cmp cx,[fs:bx]
        jge .xl
        mov cx,[fs:bx]
.xl:    mov dx,DX0
        add dx,DD
        cmp dx,[fs:bx+2]
        jle .xh
        mov dx,[fs:bx+2]
.xh:    sub dx,cx                   ; the pixels
        jle .rnext
        mov di,DROW
        add di,cx
        mov si,DSPR
        add si,cx
        sub si,DX0
        shr dx,1                    ; two at a time, an odd one first
        jnc .two
        lodsb
        or al,al
        jz .o
        mov [es:di],al
.o:     inc di
        or dx,dx
        jz .rnext
.two:   lodsw
        or al,al
        jz .t1
        mov [es:di],al
.t1:    or ah,ah
        jz .t2
        mov [es:di+1],ah
.t2:    add di,2
        dec dx
        jnz .two
.rnext: mov ax,DD
        add DSPR,ax
        add word DROW,640
        add word DEXT,4
        inc word DYS
        mov ax,DYS
        cmp ax,DYE
        jl .row
        mov ds,DSEG
        mov si,DSI
        jmp .next
