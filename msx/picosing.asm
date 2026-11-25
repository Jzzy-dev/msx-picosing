; PicoSing - karaoke for the MSX PicoVerse 2350 - MSX side
; picosing.asm - 16 KB cartridge ROM (4000h-7FFFh), sjasmplus
;
; The Pico does everything: songs, music, mics, scoring and the picture.
; This ROM only
;   - sets SCREEN 5 and maps the cartridge into page 2 as well,
;   - sends keys (and joystick 1) to 7F80h,
;   - asks for a display list (writes the sequence number of the last list
;     it ran to 7F81h) and, after the next frame interrupt, runs it when
;     its sequence byte at 8000h has changed: FILL (HMMV), COPY (OTIR to
;     VRAM) and PALETTE operations, see pico/game/dlist.h.
; Needs an MSX2.

; ---- BIOS ----
WRTVDP  equ 0047h
CHGMOD  equ 005Fh
CHGCLR  equ 0062h
CHSNS   equ 009Ch
CHGET   equ 009Fh
CHPUT   equ 00A2h
INITXT  equ 006Ch
GTSTCK  equ 00D5h
GTTRIG  equ 00D8h
KILBUF  equ 0156h
RSLREG  equ 0138h
ENASLT  equ 0024h
EXPTBL  equ 0FCC1h
SLTTBL  equ 0FCC5h
EXBRSA  equ 0FAF8h
RG8SAV  equ 0FFE7h
FORCLR  equ 0F3E9h
BAKCLR  equ 0F3EAh
BDRCLR  equ 0F3EBh

; ---- VDP ----
VDP_D   equ 98h
VDP_C   equ 99h
VDP_P   equ 9Ah
VDP_I   equ 9Bh

; ---- Pico window ----
K_KEY   equ 7F80h               ; one key code per write
K_LIST  equ 7F81h               ; write: send the next display list
K_HELLO equ 7F83h               ; write A5h: the screen was just cleared
DL      equ 8000h               ; display list: +0 seq, +1 len, +3 flags, +4 ops
OP_END  equ 0
OP_FILL equ 1
OP_COPY equ 2
OP_PAL  equ 3

; ---- keys sent ----
KEY_RIGHT equ 1Ch
KEY_LEFT  equ 1Dh
KEY_UP    equ 1Eh
KEY_DOWN  equ 1Fh
KEY_RET   equ 0Dh
KEY_ESC   equ 1Bh

; ---- RAM (page 3) ----
SEQ     equ 0C000h
CW      equ 0C001h
CH      equ 0C002h
JDIR    equ 0C003h
JREP    equ 0C004h
JTRG    equ 0C005h
WAITS   equ 0C006h              ; frames without a new list

        output "PICOSING.ROM"
        org 4000h

        db "AB"
        dw init
        dw 0,0,0
        ds 6,0

; ---------------------------------------------------------------------
init:
        ld a,(EXBRSA)           ; no sub-ROM: an MSX1
        or a
        jp z,msx1

        xor a
        ld (BAKCLR),a
        ld (BDRCLR),a
        ld a,15
        ld (FORCLR),a
        ld a,5
        call CHGMOD
        call CHGCLR
        ld a,(RG8SAV)           ; sprites off
        or 2
        ld b,a
        ld c,8
        call WRTVDP
        call KILBUF

        call page1_slot         ; the cartridge in page 2 too (the list window)
        ld h,80h
        call ENASLT

        xor a
        ld (JDIR),a
        ld (JREP),a
        ld (JTRG),a
        ld a,0A5h
        ld (K_HELLO),a
        ld a,(DL)
        ld (SEQ),a
        ld (K_LIST),a           ; ask for the first list
        xor a
        ld (WAITS),a

; The list is run right after the frame interrupt, while the VDP is not
; drawing the picture (the bottom border, blanking and the top border):
; it is ready by then, because the next one is asked for straight after
; running one, so the Pico has a whole frame to make it. A list never
; changes the screen while the TV draws that part of it (no torn moving
; line), unless it is too big for the pause.
main:
        ei
        halt
        ld a,(DL)
        ld hl,SEQ
        cp (hl)
        jr z,.none
        ld (hl),a
        di
        call run_list
        ei
        xor a
        ld (WAITS),a
        ld a,(SEQ)
        ld (K_LIST),a           ; the next list, made while this frame shows
        jr .input
.none:
        ld hl,WAITS             ; no list for 20 frames: ask again
        inc (hl)
        ld a,(hl)
        cp 20
        jr c,.input
        ld (hl),0
        ld a,(SEQ)
        ld (K_LIST),a
.input:
        call keys
        call joystick
        jr main

; ---------------------------------------------------------------------
; A = slot ID of page 1 (this ROM)
page1_slot:
        call RSLREG
        rrca
        rrca
        and 3
        ld e,a
        ld d,0
        ld hl,EXPTBL
        add hl,de
        ld a,(hl)
        and 80h
        jr z,.primary
        ld hl,SLTTBL
        add hl,de
        ld a,(hl)
        and 0Ch                 ; sub-slot of page 1
        or e
        or 80h
        ret
.primary:
        ld a,e
        ret

; ---------------------------------------------------------------------
keys:
        call CHSNS
        ret z
        call CHGET
        ld (K_KEY),a
        jr keys

; joystick 1: directions repeat after a while, buttons A = RETURN, B = ESC
joystick:
        ld a,1
        call GTSTCK
        ld b,0
        cp 1
        jr nz,.n1
        ld b,KEY_UP
.n1:    cp 3
        jr nz,.n2
        ld b,KEY_RIGHT
.n2:    cp 5
        jr nz,.n3
        ld b,KEY_DOWN
.n3:    cp 7
        jr nz,.n4
        ld b,KEY_LEFT
.n4:    ld a,(JDIR)
        cp b
        ld a,b
        ld (JDIR),a
        jr z,.same
        or a
        jr z,.trig
        ld (K_KEY),a
        ld a,15
        ld (JREP),a
        jr .trig
.same:
        or a
        jr z,.trig
        ld hl,JREP
        dec (hl)
        jr nz,.trig
        ld (hl),4
        ld (K_KEY),a
.trig:
        ld a,1
        call GTTRIG
        and 1
        ld c,a                  ; bit 0: A pressed
        ld a,3
        call GTTRIG
        and 2
        or c                    ; bit 1: B pressed
        ld c,a
        ld a,(JTRG)
        cpl
        and c                   ; newly pressed
        ld b,a
        ld a,c
        ld (JTRG),a
        bit 0,b
        jr z,.tb
        ld a,KEY_RET
        ld (K_KEY),a
.tb:    bit 1,b
        ret z
        ld a,KEY_ESC
        ld (K_KEY),a
        ret

; ---------------------------------------------------------------------
; Run the display list at DL+4 (interrupts off)
run_list:
        ld hl,DL+4
.next:
        ld a,(hl)
        inc hl
        cp OP_COPY
        jr z,op_copy
        cp OP_FILL
        jr z,op_fill
        cp OP_PAL
        jp z,op_pal
        jp wait_ce              ; END (or unknown): let the last FILL finish

; 02 x y w h data: rows of w bytes to VRAM (y*128 + x)
; per row: 2 address writes and OTIR, about 115 + 21*w T-states
op_copy:
        ld e,(hl)               ; E = x
        inc hl
        ld d,(hl)               ; D = y
        inc hl
        ld a,(hl)
        inc hl
        ld ixl,a                ; IXL = w
        ld a,(hl)
        inc hl
        ld ixh,a                ; IXH = h
        call wait_ce
        ld c,VDP_D
        call .r14
.row:
        ld a,d
        rrca
        ld b,a
        and 80h
        or e                    ; A7 = y bit 0, A6-A0 = x
        out (VDP_C),a
        ld a,b
        and 3Fh                 ; A13-A8 = y bits 6-1
        or 40h                  ; write
        out (VDP_C),a
        ld b,ixl
        otir
        inc d
        ld a,d
        and 7Fh
        call z,.r14             ; y crossed 128: A16-A14 change
        dec ixh
        jp nz,.row
        jp run_list.next
.r14:
        ld a,d
        rlca
        and 1                   ; A16-A14 = y bit 7
        out (VDP_C),a
        ld a,80h+14
        out (VDP_C),a
        ret

; 01 x y w h c: HMMV
op_fill:
        call wait_ce
        ld a,36
        out (VDP_C),a
        ld a,80h+17             ; R#17 = 36, auto-increment
        out (VDP_C),a
        ld c,VDP_I
        ld a,(hl)               ; DX = x * 2
        inc hl
        add a,a
        out (c),a
        xor a
        out (c),a
        ld a,(hl)               ; DY = y (page 0)
        inc hl
        out (c),a
        xor a
        out (c),a
        ld a,(hl)               ; NX = w * 2
        inc hl
        add a,a
        out (c),a
        ld a,0
        adc a,0
        out (c),a
        ld a,(hl)               ; NY = h
        inc hl
        out (c),a
        xor a
        out (c),a
        ld a,(hl)               ; CLR
        inc hl
        out (c),a
        xor a                   ; ARG
        out (c),a
        ld a,0C0h               ; HMMV
        out (c),a
        jp run_list.next

; 03 palette[32]
op_pal:
        xor a
        out (VDP_C),a
        ld a,80h+16
        out (VDP_C),a
        ld bc,32*256+VDP_P
        otir
        jp run_list.next

; wait until the VDP command engine is free (S#2 bit 0)
wait_ce:
        ld a,2
        out (VDP_C),a
        ld a,80h+15
        out (VDP_C),a
.w:     in a,(VDP_C)
        rrca
        jr c,.w
        xor a
        out (VDP_C),a
        ld a,80h+15
        out (VDP_C),a
        ret

; ---------------------------------------------------------------------
msx1:
        call INITXT
        ld hl,txt_msx1
.p:     ld a,(hl)
        or a
        jr z,.stop
        call CHPUT
        inc hl
        jr .p
.stop:  jr .stop

txt_msx1:
        db "PicoSing needs an MSX2.",0

code_end:
        assert code_end < 7F00h
        ds 8000h-$, 0FFh
