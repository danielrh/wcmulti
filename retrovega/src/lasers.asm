; lasers.asm: the laser bolts' fast paths (lasers.h).  Each does what its
; reference in lasers.c does, to the bit (bolts.c compares them when a flight
; starts): rv_bolts_place = lz_place, rv_lines_draw = lz_lines_draw, and so
; on.  Far cdecl, as the medium model calls them;
; SI DI BP DS kept.  32-bit registers throughout (486): DOSBox charges an
; instruction a cycle whatever its width, so a bolt's eighteen products cost
; what eighteen moves would.
        cpu 486
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
%define PROW   [bp-2]                   ; ROWOFF
%define PYC    [bp-4]                   ; the row's centre, x16
%define PXS    [bp-6]
%define PROWS  [bp-8]
%define PC     [bp-12]                  ; the middle line's x at the row, 16.16 px
        global _rv_pass_draw
_rv_pass_draw:
        push bp
        mov bp,sp
        sub sp,12
        push si
        push di
        push ds
        cld
        lds si,[bp+6]
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
        add di,cx
        mov cx,ax
        mov al,[si+22]
        rep stosb
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

