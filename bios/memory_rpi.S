/*
 * memory.S - memory initialization
 *
 * Copyright (C) 2001-2017 by Authors:
 *
 * Authors:
 *  MAD  Martin Doering
 *  PES  Petr Stehlik
 *  jfdn Jean-François DEL NERO
 *  VRI  Vincent Rivière
 *  RFB  Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "memory.h"

// ==== Definitions ==========================================================

        .globl  meminit                 // memory initialization

// ==== References ===========================================================

        .extern _warm_magic
        .extern _stktop

        // functions for detecting hardware
        .extern _check_read_byte
        .extern _bzero

        // ST-RAM settings
        .extern memctrl
        .extern _phystop
        .extern memvalid
        .extern memval2
        .extern memval3

// ===========================================================================
// ==== meminit - Reuse or detect RAM settings, then initialize RAM areas ====
// ===========================================================================
meminit:

        // We will end up moving sp, so we save away lr into r11
        mov r11, lr

        /* We will accumulate meminit_flags in this register */
        mov r7,  #0                  // _meminit_flags = 0


// ===========================================================================
// ==== memchk - Check if old memory config can be used ======================
// ===========================================================================
// Inputs:
//   r7.b: _meminit_flags (input/output)
// Outputs:
//   r6.b: old memctrl
//   r5.l: old _phystop
//   r3.l: old _ramvalid (#if CONF_WITH_TTRAM)
//   r4.l: old _ramtop (#if CONF_WITH_TTRAM)
memchk:

        get_unaligned     r0,memvalid,r1
        ldr	r1,	=0x752019f3
        cmp     r1, r0
        bne     memconf

        get_unaligned     r0,memval2,r1
        ldr	r1,	=0x237698aa
        cmp     r1, r0
        bne     memconf

        get_unaligned     r0,memval3,r1
        ldr	r1,	=0x5555aaaa
        cmp     r1, r0
        bne     memconf

        // The old RAM settings are valid, load them into registers.
        // They will stay in safe place in case we clear the sysvars.
        ldr     r6, =memctrl
        ldrb    r6, [r6]

        get_unaligned  r5,_phystop,r1

// ===========================================================================
// ==== The ST-RAM configuration is valid ====================================
// ===========================================================================
// Inputs:
//   r7.b: _meminit_flags (input/output)
//   r6.b: old memctrl
//   r5.l: old _phystop
//   r3.l: old _ramvalid (#if CONF_WITH_TTRAM)
//   r4.l: old _ramtop (#if CONF_WITH_TTRAM)

memory_reused:
#if CONF_DETECT_FIRST_BOOT_WITHOUT_MEMCONF
        // If _warm_magic == WARM_MAGIC, this is explicitly not a first boot
        ldr	r1,	=0x5741524D         // WARM_MAGIC
        ldr     r0, =_warm_magic
        ldr     r0, [r0]
        cmp     r1, r0
        beq     .warm_magic_done        // Indeed, this is not a first boot

        // _warm_magic != WARM_MAGIC: this is a first boot
        orr     r7,r7,#MEMINIT_FIRST_BOOT
.warm_magic_done:
#endif
        b     stram_detected

// ===========================================================================
// ==== memconf - Detect RAM and configure the memory controller =============
// ===========================================================================
// Inputs:
//   r7.b: _meminit_flags (input/output)
// Outputs:
//   r6.b: new value of 0xffff8001 (maybe fake), to put into memctrl.
//   r5.l: detected end of the ST-RAM, to put into _phystop.
memconf:

memconf_done:
        // Since the memory was not valid, this is a first boot
        orr     r7, r7, #MEMINIT_FIRST_BOOT
 
// ===========================================================================
// ==== End of ST-RAM detection ==============================================
// ===========================================================================
/*
 * At this point:
 * - The size of the ST-RAM has been detected
 * - The MMU has been properly initalized
 * - The ST-RAM is ready to be used
 *
 * r7.b = _meminit_flags
 * r5.l = new phystop
 */
stram_detected:
        ldr     sp, =_stktop

        tst     r7, #MEMINIT_FIRST_BOOT // first boot ?
        beq     any_boot                // no: skip first boot initialization

// ===========================================================================
// ==== This part is executed only on first boot =============================
// ===========================================================================

        bl _raspi_vcmem_init

// ===========================================================================
// ==== This part is executed on any boot (first boot or reset) ==============
// ===========================================================================
any_boot:
        // Clear the BSS segment.
        // Our stack is explicitly set outside the BSS, so this is safe:
        // bzero() will be able to return.

        ldr     r0, =__bss
        ldr     r1, =__ebss
        sub     r1, r1, r0
        bl      _bzero

        bx      r11 // return from meminit
