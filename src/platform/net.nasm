; net.nasm: the UDP entries of NiceMonMEGA65's monitor, 16 to 19 of its
; dispatch table, for nice.c built with -D NICE_NET (nicebuild.py --asm).
; Entry n is a JMP at address 2n of the ROM (NiceMonMEGA65's
; monitor/README.md, "The dispatch table"), so a stub jumps there, and
; the entry returns to nice.c. They take NICE word addresses, and a
; datagram one byte per word: _nc_words gives the word address of a C
; array of words, the address of the wasm memory plus the pointer / 4,
; as NiceMonMEGA65's mon_c.nasm does.
;
; Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).

_nc_udp_open
                JMP     0x20
_nc_udp_send
                JMP     0x22
_nc_udp_recv
                JMP     0x24
_nc_udp_close
                JMP     0x26
_nc_words
                SHR     R1, R1, 2
                ADD     R1, R1, ___wasm_memory
                RET
