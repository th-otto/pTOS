/*
 * vdi_mouse.c
 *
 * Copyright 1982 by Digital Research Inc.  All rights reserved.
 * Copyright 1999 by Caldera, Inc. and Authors:
 * Copyright 2002-2017 by The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



#include "config.h"
#include "portab.h"
#include "asm.h"
#include "biosbind.h"
#include "xbiosbind.h"
#include "aespub.h"
#include "obdefs.h"
#include "gsxdefs.h"
#include "vdi_defs.h"
#include "../bios/tosvars.h"
#include "../bios/lineavars.h"
#include "kprint.h"
#include "asm.h"
#include "mform.h"

#ifdef MACHINE_RPI
#   include "raspi_mouse.h"
#endif

/* prototypes */
static void cur_display(Mcdb *sprite, MCS *savebuf, WORD x, WORD y);
static void cur_replace(MCS *savebuf);
static void vb_draw(void);             /* user button vector */


/* FIXME: should go to linea variables */
void (*user_wheel)(WORD wheel_number, WORD wheel_amount);   /* user provided mouse wheel vector */
void (*old_statvec)(UBYTE *);             /* original IKBD status packet routine */


#if !WITH_AES
/* Default Mouse Cursor Definition */
static const MFORM arrow_mform = {
    1, 0, 1, 0, 1,
    /* background definition */
    {
        0xE000, /* %1110000000000000 */
        0xF000, /* %1111000000000000 */
        0xF800, /* %1111100000000000 */
        0xFC00, /* %1111110000000000 */
        0xFE00, /* %1111111000000000 */
        0xFF00, /* %1111111100000000 */
        0xFF80, /* %1111111110000000 */
        0xFFC0, /* %1111111111000000 */
        0xFE00, /* %1111111000000000 */
        0xFE00, /* %1111111000000000 */
        0xEF00, /* %1110111100000000 */
        0x0F00, /* %0000111100000000 */
        0x0780, /* %0000011110000000 */
        0x0780, /* %0000011110000000 */
        0x03C0, /* %0000001111000000 */
        0x0000  /* %0000000000000000 */
    },
    /* foreground definition */
    {
        0x4000, /* %0100000000000000 */
        0x6000, /* %0110000000000000 */
        0x7000, /* %0111000000000000 */
        0x7800, /* %0111100000000000 */
        0x7C00, /* %0111110000000000 */
        0x7E00, /* %0111111000000000 */
        0x7F00, /* %0111111100000000 */
        0x7F80, /* %0111111110000000 */
        0x7C00, /* %0111110000000000 */
        0x6C00, /* %0110110000000000 */
        0x4600, /* %0100011000000000 */
        0x0600, /* %0000011000000000 */
        0x0300, /* %0000001100000000 */
        0x0300, /* %0000001100000000 */
        0x0180, /* %0000000110000000 */
        0x0000  /* %0000000000000000 */
    }
};
#define default_mform() &arrow_mform
#endif


/*
 * do_nothing - doesn't do much  :-)
 */

static void do_nothing_v(void)
{
}
static void do_nothing_i(WORD a)
{
	UNUSED(a);
}
static void do_nothing_ii(WORD a, WORD b)
{
	UNUSED(a);
	UNUSED(b);
}



/*
 * dis_cur - Displays the mouse cursor if the number of hide
 *           operations has gone back to 0.
 *
 *  Decrement the counter for the number of hide operations performed.
 *  If this is not the last one then do nothing because the cursor
 *  should remain hidden.
 *
 *   Outputs:
 *      hide_cnt = hide_cnt - 1
 *      draw_flag = 0
 */

static void dis_cur(void)
{
    linea_vars.mouse_flag += 1;            /* disable mouse redrawing */
    linea_vars.HIDE_CNT -= 1;   /* decrement hide operations counter */
    if (linea_vars.HIDE_CNT == 0) {
        cur_display(&linea_vars.mouse_cdb, mcs_ptr, linea_vars.GCURX, linea_vars.GCURY);  /* display the cursor */
        linea_vars.draw_flag = 0;          /* disable vbl drawing routine */
    }
    else if (linea_vars.HIDE_CNT < 0) {
        linea_vars.HIDE_CNT = 0;           /* hide counter should not become negative */
    }
    linea_vars.mouse_flag -= 1;            /* re-enable mouse drawing */
}



/*
 * hide_cur
 *
 * This routine hides the mouse cursor if it has not already
 * been hidden.
 *
 * Inputs:         None
 *
 * Outputs:
 *    hide_cnt = hide_cnt + 1
 *    draw_flag = 0
 */

static void hide_cur(void)
{
    linea_vars.mouse_flag += 1;            /* disable mouse redrawing */

    /*
     * Increment the counter for the number of hide operations performed.
     * If this is the first one then remove the cursor from the screen.
     * If not then do nothing, because the cursor wasn't on the screen.
     */
    linea_vars.HIDE_CNT += 1;   /* increment it */
    if (linea_vars.HIDE_CNT == 1) {        /* if cursor was not hidden... */
        cur_replace(mcs_ptr);   /* remove the cursor from screen */
        linea_vars.draw_flag = 0;          /* disable vbl drawing routine */
    }

    linea_vars.mouse_flag -= 1;            /* re-enable mouse drawing */
}



/*
 * gloc_key - get locator key
 *
 * returns:  0    - nothing
 *           1    - button pressed
 *                  TERM_CH = 16 bit char info
 *           2    - coordinate info
 *                     X1 = new x
 *                     Y1 = new y
 *
 * The variable cur_ms_stat holds the bitmap of mouse status since the last
 * interrupt. The bits are
 *
 * 0 - 0x01 Left mouse button status  (0=up)
 * 1 - 0x02 Right mouse button status (0=up)
 * 2 - 0x04 Reserved
 * 3 - 0x08 Reserved
 * 4 - 0x10 Reserved
 * 5 - 0x20 Mouse move flag (1=moved)
 * 6 - 0x40 Right mouse button status flag (0=hasn't changed)
 * 7 - 0x80 Left mouse button status flag  (0=hasn't changed)
 */

static WORD gloc_key(void)
{
    WORD retval = 0;

    /*
     * check for mouse button or keyboard key
     */
    if (linea_vars.cur_ms_stat & 0xc0) {           /* some button status bits set? */
        if (linea_vars.cur_ms_stat & 0x40)         /* if bit 6 set,                     */
            linea_vars.TERM_CH = 0x20;             /* send terminator code for left key */
        else
            linea_vars.TERM_CH = 0x21;             /* send terminator code for right key */
        linea_vars.cur_ms_stat &= 0x23;            /* clear mouse button status (bit 6/7) */
        retval = 1;                     /* set button pressed flag */
    } else if (gchr_key()) {            /* see if a character present at con */
        retval = 1;                     /* set button pressed flag */
    }

    /*
     * check for mouse movement
     */
    if (linea_vars.cur_ms_stat & 0x20) {           /* if bit #5 set ... */
        Point * point = (Point*)PTSIN;

        linea_vars.cur_ms_stat &= ~0x20;   /* clear bit 5 */
        point->x = linea_vars.GCURX;       /* set X = GCURX */
        point->y = linea_vars.GCURY;       /* set Y = GCURY */
        retval += 2;
    }

    return retval;
}



/*
 * LOCATOR_INPUT: implements vrq_locator()/vsm_locator()
 *
 * These functions return the status of the logical 'locator' device.
 *
 * vrq_locator() operation in Atari TOS and EmuTOS
 * -----------------------------------------------
 * 1. The first call to vrq_locator() always returns immediately: the
 *    output mouse positions are the same as the input, and the
 *    terminating character is set to 0x20, indicating the left mouse
 *    button.
 * 2. Subsequent calls return when either a keyboard key is pressed, or
 *    a mouse button is pressed OR released (thus a normal mouse button
 *    action satisfies TWO calls to vrq_locator()).  The output mouse
 *    positions are the current positions, and the terminating character
 *    is the ASCII key pressed, or 0x20 for the left mouse button / 0x21
 *    for the right.
 *    As a consequence, pressing the space key twice is indistingishable
 *    from pressing/releasing the left mouse button, and likewise for
 *    the exclamation mark and the right mouse button.
 *
 * vsm_locator() operation in Atari TOS and EmuTOS
 * -----------------------------------------------
 * 1. The first call to vsm_locator() always sets the terminating
 *    character to 0x20 and CONTRL[4] to 1 (indicating the left mouse
 *    button).
 * 2. On every call:
 *    . if the mouse has been moved, CONTRL[2] is set to 1
 *    . if a keyboard key is pressed, the terminating character is the
 *      ASCII value of the key pressed, and CONTRL[4] is set to 1
 *    . if a mouse button is pressed or released, the terminating
 *      character is 0x20 for the left button, 0x21 for the right
 *      button, and CONTRL[4] is set to 1
 *    . the output mouse psitions are always set to the same as the
 *      input
 *
 * Differences from official Atari documentation
 * ---------------------------------------------
 * 1. No special behaviour is described for the first call to
 *    vrq_locator() or vsm_locator().
 * 2. No mention is made of button press & release being separate
 *    events.
 * 3. For vsm_locator(), the output mouse positions should be the
 *    current positions, not the input positions.
 */
void vdi_v_locator(Vwk * vwk)
{
    WORD i;
    Point * point = (Point*)PTSIN;

    /* Set the initial locator position. */
    linea_vars.GCURX = point->x;
    linea_vars.GCURY = point->y;

    if (linea_vars.loc_mode == 0) {    /* handle request mode (vrq_locator()) */
        dis_cur();
        /* loop till button or keyboard event */
        while (!(gloc_key() & 1)) {
        }
        INTOUT[0] = linea_vars.TERM_CH & 0x00ff;

        CONTRL->nintout = 1;
        CONTRL->nptsout = 1;

        PTSOUT[0] = point->x;
        PTSOUT[1] = point->y;
        hide_cur();
    } else {                /* handle sample mode (vsm_locator()) */
        i = gloc_key();
        if (i & 1) {
            CONTRL->nintout = 1;
            INTOUT[0] = linea_vars.TERM_CH & 0x00ff;
        }
        if (i & 2) {
            CONTRL->nptsout = 1;
            PTSOUT[0] = point->x;
            PTSOUT[1] = point->y;
        }
    }
}



/*
 * vdi_v_show_c - show cursor
 */
void vdi_v_show_c(Vwk * vwk)
{
    if (!INTIN[0] && linea_vars.HIDE_CNT)
        linea_vars.HIDE_CNT = 1;           /* reset cursor to on */

    dis_cur();
}



/*
 * vdi_v_hide_c - hide cursor
 */
void vdi_v_hide_c(Vwk * vwk)
{
    hide_cur();
}



/*
 * vdi_vq_mouse - Query mouse position and button status
 */
void vdi_vq_mouse(Vwk * vwk)
{
    INTOUT[0] = linea_vars.MOUSE_BT;

    CONTRL->nintout = 1;
    CONTRL->nptsout = 1;

    PTSOUT[0] = linea_vars.GCURX;
    PTSOUT[1] = linea_vars.GCURY;
}



/*
 * VALUATOR_INPUT: implements vrq_valuator()/vsm_valuator()
 *
 * These functions return the status of the logical 'valuator' device.
 * The "GEM Programmer's Guide: VDI" indicates that these functions
 * are not required, and both Atari TOS and EmuTOS (using the original
 * imported DRI source) implement them as dummy functions.
*/
void vdi_v_valuator(Vwk * vwk)
{
}



#ifdef __mc68000__
/*
 * Call the user_but vector from C
 */
static void call_user_but(WORD status)
{
	register WORD val asm("d0") = status;
	register void (*func)(WORD) asm("a0") = linea_vars.user_but; /* prototype not quite right: status passed in d0 */
	
	__asm__ __volatile__(
		" jsr (%[a0])"
	:
	: "d"(val), [a0]"a"(func)
	: "cc" AND_MEMORY);
}

/*
 * Call the user_wheel vector from C.
 */
static void call_user_wheel(WORD wheel_number, WORD wheel_amount)
{
	register WORD number asm("d0") = wheel_number;
	register WORD amount asm("d1") = wheel_amount;
	register void (*func)(WORD, WORD) asm("a0") = user_wheel; /* prototype not quite right: status passed in d0 */
	
	__asm__ __volatile__(
		" jsr (%[a0])"
	:
	: "d"(number), "d"(amount), [a0]"a"(func)
	: "cc" AND_MEMORY);
}
#endif

#ifdef __arm__
/*
 * Call the user_but vector from C
 */
static void call_user_but(WORD status)
{
	register WORD val asm("r0") = status;
	register void (*func)(WORD) asm("r1") = linea_vars.user_but; /* prototype not quite right: status passed in d0 */
	
	__asm__ __volatile__(
		" blx %[func]"
	:
	: "r"(val), [func]"r"(func)
	: "cc" AND_MEMORY);
}

/*
 * Call the user_wheel vector from C.
 */
static void call_user_wheel(WORD wheel_number, WORD wheel_amount)
{
	register WORD number asm("r0") = wheel_number;
	register WORD amount asm("r1") = wheel_amount;
	register void (*func)(WORD, WORD) asm("r0") = user_wheel; /* prototype not quite right: status passed in d0 */
	
	__asm__ __volatile__(
		" blx %[func]"
	:
	: "r"(number), "r"(amount), [func]"r"(func)
	: "cc" AND_MEMORY);
}

/*
 * mov_cur - moves the mouse cursor to its new location
 *           unless the cursor is currently hidden.
 *
 * Inputs:
 *    r0 = new x-coordinate for mouse cursor
 *    r1 = new y-coordinate for mouse cursor
 *
 * Outputs:        None
 */
void mov_cur(WORD new_x, WORD new_y)      /* user button vector */
{
	ULONG cpsr;
	
	if (linea_vars.HIDE_CNT == 0)
		return;
	cpsr = disable_interrupts();
	linea_vars.newx = new_x;
	linea_vars.newy = new_y;
	linea_vars.draw_flag = TRUE;
	set_cpsr(cpsr);
}

#endif /* __arm__ */




/*
 * vdi_vex_butv
 *
 * This routine replaces the mouse button change vector with
 * the address of a user-supplied routine.  The previous value
 * is returned so that it also may be called when there is a
 * change in the mouse button status.
 *
 * Inputs:
 *    contrl[7], contrl[8] - pointer to user routine
 *
 * Outputs:
 *    contrl[9], contrl[10] - pointer to old routine
 */
void vdi_vex_butv(Vwk * vwk)
{
    CONTRL->ptr2 = linea_vars.user_but;
    linea_vars.user_but = CONTRL->ptr1;
}



/*
 * vdi_vex_motv
 *
 * This routine replaces the mouse coordinate change vector with the address
 * of a user-supplied routine.  The previous value is returned so that it
 * also may be called when there is a change in the mouse coordinates.
 *
 *  Inputs:
 *     contrl[7], contrl[8] - pointer to user routine
 *
 *  Outputs:
 *     contrl[9], contrl[10] - pointer to old routine
 */
void vdi_vex_motv(Vwk * vwk)
{
    CONTRL->ptr2 = linea_vars.user_mot;
    linea_vars.user_mot = CONTRL->ptr1;
}



/*
 * vdi_vex_curv
 *
 * This routine replaces the mouse draw vector with the
 * address of a user-supplied routine.  The previous value
 * is returned so that it also may be called when the mouse
 * is to be drawn.
 *
 * Inputs:
 *    contrl[7], contrl[8] - pointer to user routine
 *
 * Outputs:
 *    contrl[9], contrl[10] - pointer to old routine
 *
 */
void vdi_vex_curv(Vwk * vwk)
{
    CONTRL->ptr2 = linea_vars.user_cur;
    linea_vars.user_cur = CONTRL->ptr1;
}



#if CONF_WITH_VDI_EXTENSIONS
/*
 * vdi_vex_wheelv: a Milan VDI extension
 *
 * This routine replaces the mouse wheel vector with the
 * address of a user-supplied routine.  The previous value
 * is returned so that it also may be called when the mouse
 * wheel is used.
 *
 * Inputs:
 *    contrl[7], contrl[8] - pointer to user routine
 *
 * Outputs:
 *    contrl[9], contrl[10] - pointer to old routine
 *
 */
void vdi_vex_wheelv(Vwk * vwk)
{
    CONTRL->ptr2 = user_wheel;
    user_wheel = CONTRL->ptr1;
}
#endif



/* copies src mouse form to dst mouse sprite, constrains hotspot
 * position & colors and maps colors
 */
static void set_mouse_form(const MFORM *src, Mcdb *dst)
{
    int i;
    WORD col;
    UWORD * gmdt;                /* global mouse definition table */
    const UWORD * mask;
    const UWORD * data;

    linea_vars.mouse_flag += 1;            /* disable updates while redefining cursor */

    /* save x-offset of mouse hot spot */
    dst->xhot = src->mf_xhot & 0x000f;

    /* save y-offset of mouse hot spot */
    dst->yhot = src->mf_yhot & 0x000f;

    /* is background color index too high? */
    col = src->mf_bg;
    if (col >= linea_vars.DEV_TAB[13]) {
        col = 1;               /* yes - default to 1 */
    }
    dst->bg_col = MAP_COL[col];

    /* is foreground color index too high? */
    col = src->mf_fg;
    if (col >= linea_vars.DEV_TAB[13]) {
        col = 1;               /* yes - default to 1 */
    }
    dst->fg_col = MAP_COL[col];

    /*
     * Move the new mouse definition into the global mouse cursor definition
     * table.  The values for the mouse mask and data are supplied as two
     * separate 16-word entities.  They must be stored as a single array
     * starting with the first word of the mask followed by the first word
     * of the data and so on.
     */

    /* copy the data to the global mouse definition table */
    gmdt = dst->maskdata;
    mask = src->mf_mask;
    data = src->mf_data;
    for (i = 15; i >= 0; i--) {
        *gmdt++ = *mask++;              /* get next word of mask */
        *gmdt++ = *data++;              /* get next word of data */
    }

    linea_vars.mouse_flag -= 1;                    /* re-enable mouse drawing */
}



/*
 * vdi_vsc_form - Transforms user defined cursor to device specific format.
 *
 * Get the new values for the x and y-coordinates of the mouse hot
 * spot and the new color indices for the mouse mask and data.
 *
 * Inputs:
 *     intin[0] - x coordinate of hot spot
 *     intin[1] - y coordinate of hot spot
 *     intin[2] - reserved for future use. must be 1
 *     intin[3] - Mask color index
 *     intin[4] - Data color index
 *     intin[5-20]  - 16 words of cursor mask
 *     intin[21-36] - 16 words of cursor data
 *
 * Outputs:        None
 */
void vdi_vsc_form(Vwk * vwk)
{
    set_mouse_form((const MFORM *)INTIN, &linea_vars.mouse_cdb);
}


/*
 * vdi_mousex_handler - Handle additional mouse buttons
 */

static void vdi_mousex_handler (WORD scancode)
{
    WORD old_buttons = linea_vars.MOUSE_BT;

    if (scancode == 0x37)      /* Mouse button 3 press */
        linea_vars.MOUSE_BT |= 0x04;
    else if (scancode == 0xb7) /* Mouse button 3 release */
        linea_vars.MOUSE_BT &= ~0x04;
    else if (scancode == 0x5e) /* Mouse button 4 press */
        linea_vars.MOUSE_BT |= 0x08;
    else if (scancode == 0xde) /* Mouse button 4 release */
        linea_vars.MOUSE_BT &= ~0x08;
    else if (scancode == 0x5f) /* Mouse button 5 press */
        linea_vars.MOUSE_BT |= 0x10;
    else if (scancode == 0xdf) /* Mouse button 5 release */
        linea_vars.MOUSE_BT &= ~0x10;

    if (linea_vars.MOUSE_BT != old_buttons)
        call_user_but(linea_vars.MOUSE_BT);

    if (scancode == 0x59)      /* Wheel up */
        call_user_wheel(0, -1);
    else if (scancode == 0x5a) /* Wheel down */
        call_user_wheel(0, 1);
    else if (scancode == 0x5c) /* Wheel left */
        call_user_wheel(1, -1);
    else if (scancode == 0x5d) /* Wheel right */
        call_user_wheel(1, 1);
}



/*
 * vdimouse_init - Initializes the mouse (VDI part)
 *
 * entry:          none
 * exit:           none
 */

void vdimouse_init(void)
{
    struct kbdvecs *kbd_vectors;
    static const struct {
        BYTE topmode;
        BYTE buttons;
        BYTE xparam;
        BYTE yparam;
    } mouse_params = {0, 0, 1, 1};
    LONG *p;

    /* Input must be initialized here and not in init_wk */
    linea_vars.loc_mode = 0;               /* default is request mode  */
    linea_vars.val_mode = 0;               /* default is request mode  */
    linea_vars.chc_mode = 0;               /* default is request mode  */
    linea_vars.str_mode = 0;               /* default is request mode  */

    /* mouse settings */
    linea_vars.HIDE_CNT = 1;               /* mouse is initially hidden */
    linea_vars.GCURX = linea_vars.DEV_TAB[0] / 2;     /* initialize the mouse to center */
    linea_vars.GCURY = linea_vars.DEV_TAB[1] / 2;

    linea_vars.user_but = do_nothing_i;
    linea_vars.user_mot = do_nothing_v;
    linea_vars.user_cur = mov_cur;         /* initialize user_cur vector */
    user_wheel = do_nothing_ii;

    /* Move in the default mouse form (presently the arrow) */
    set_mouse_form(default_mform(), &linea_vars.mouse_cdb);

    linea_vars.MOUSE_BT = 0;    /* clear the mouse button state */
    linea_vars.cur_ms_stat = 0; /* clear the mouse status */
    linea_vars.mouse_flag = 0;             /* clear the mouse flag */
    linea_vars.draw_flag = 0;   /* clear the hide operations counter */
    linea_vars.newx = 0;        /* set cursor x-coordinate to 0 */
    linea_vars.newy = 0;        /* set cursor y-coordinate to 0 */

    /* vblqueue points to start of vbl_list[] */
    p = (LONG *)get_unaligned_ptr(vblqueue);
    set_unaligned(*p, (LONG)vb_draw);   /* set GEM VBL-routine to vbl_list[0] */

    /* Initialize mouse via XBIOS in relative mode */
    Initmous(1, &mouse_params, mouse_int);

    kbd_vectors = (struct kbdvecs *)Kbdvbase();
    old_statvec = kbd_vectors->statvec;
    kbd_vectors->statvec = wheel_int;
    mousexvec = vdi_mousex_handler;
}



/*
 * vdimouse_exit - deinitialize/disable mouse
 */

void vdimouse_exit(void)
{
    LONG * pointer;             /* help for storing LONGs in INTIN */
    struct kbdvecs *kbd_vectors;

    linea_vars.user_but = do_nothing_i;
    linea_vars.user_mot = do_nothing_v;
    linea_vars.user_cur = do_nothing_ii;
    user_wheel = do_nothing_ii;

    pointer = (LONG *)get_unaligned_ptr(vblqueue);         /* vblqueue points to start of vbl_list[] */
    set_unaligned(*pointer, (LONG)vb_draw);   /* set GEM VBL-routine to vbl_list[0] */

    /* disable mouse via XBIOS */
    Initmous(0, 0, 0);

    kbd_vectors = (struct kbdvecs *)Kbdvbase();
    kbd_vectors->statvec = old_statvec;
}



/*
 * vb_draw - moves mouse cursor, GEM VBL routine
 *
 * It removes the mouse cursor from its current location, if necessary,
 * and redraws it at a new location.
 *
 *      Inputs:
 *         draw_flag - signals need to redraw cursor
 *         newx - new cursor x-coordinate
 *         newy - new cursor y-coordinate
 *         mouse_flag - cursor hide/show flag
 *
 *      Outputs:
 *         draw_flag is cleared
 *
 *      Registers Modified:     d0, d1
 *
 */

/* If we do not need to draw the cursor now then just exit. */

static void vb_draw(void)
{
    disable_interrupts();
//    WORD old_sr = set_sr(0x2700);       /* disable interrupts */
    if (linea_vars.draw_flag) {
        linea_vars.draw_flag = FALSE;
        enable_interrupts();
        if (!linea_vars.mouse_flag) {
            cur_replace(mcs_ptr);       /* remove the old cursor from the screen */
            cur_display(&linea_vars.mouse_cdb, mcs_ptr, linea_vars.newx, linea_vars.newy);  /* display the cursor */
        }
    } else
        enable_interrupts();

}



#ifndef MACHINE_RPI
/*
 * cur_display_clip()
 *
 * handles cursor display for cursors that are subject to L/R clipping
 */
static void cur_display_clip(WORD op,Mcdb *sprite,MCS *mcs,UWORD *mask_start,UWORD shft)
{
    WORD dst_inc, plane;
    UWORD cdb_fg, cdb_bg;
    UWORD cdb_mask;             /* for checking cdb_bg/cdb_fg */
    UWORD *addr, *save;

    dst_inc = linea_vars.v_lin_wr >> 1;    /* calculate number of words in a scan line */

    addr = mcs->addr;           /* starting screen address */
    save = (UWORD *)mcs->area;  /* we save words, not longwords */

    cdb_bg = sprite->bg_col;    /* get mouse background color bits */
    cdb_fg = sprite->fg_col;    /* get mouse foreground color bits */

    /* plane controller, draw cursor in each graphic plane */
    for (plane = linea_vars.v_planes - 1, cdb_mask = 0x0001; plane >= 0; plane--) {
        WORD row;
        UWORD *src, *dst;

        /* setup the things we need for each plane again */
        src = mask_start;               /* calculated mask data begin */
        dst = addr++;                   /* current destination address */

        /* loop through rows */
        for (row = mcs->len - 1; row >= 0; row--) {
            ULONG bits;                 /* our graphics data */
            ULONG fg;                   /* the foreground color */
            ULONG bg;                   /* the background color */

            /*
             * first, save the existing data
             */
            *save++ = *dst;
            if (op == 1) {          /* right word only */
                bits = *dst;            /* dst already at right word */
            } else {                /* left word only  */
                bits = ((ULONG)*dst) << 16; /* move to left posn */
            }

            /*
             * align the forms with the cursor position on the screen
             */

            /* get and align background & foreground forms */
            bg = (ULONG)*src++ << shft;
            fg = (ULONG)*src++ << shft;

            /*
             * logical operation for cursor interaction with screen
             */

            /* select operation for mouse mask background color */
            if (cdb_bg & cdb_mask)
                bits |= bg;
            else
                bits &= ~bg;

            /* select operation for mouse mask foreground color */
            if (cdb_fg & cdb_mask)
                bits |= fg;
            else
                bits &= ~fg;

            /*
             * update the screen with the new data
             */
            if (op == 1) {          /* right word only */
                *dst = (UWORD)bits;
            } else {                /* left word only */
                *dst = (UWORD)(bits >> 16);
            }

            dst += dst_inc;             /* a1 -> next row of screen */
        } /* loop through rows */

        cdb_mask <<= 1;
    } /* loop through planes */
}
#endif


/*
 * cur_display() - blits a "cursor" to the destination
 *
 * before the destination is overwritten, the current contents are
 * saved to the user-provided save area (MCS).  then the cursor is
 * written, combining a background colour form, a foreground colour
 * form, and the current contents of the destination.
 *
 * some points to note:
 * the cursor is always 16x16 pixels.  in the general case, it will
 * overlap two adjacent screen words in each plane; thus the save area
 * requires 4 bytes per plane for each row of the cursor, or 64 bytes
 * in total per plane (plus some bookkeeping overhead).  if the cursor
 * is subject to left or right clipping, however, then it must lie
 * within one screen word (per plane), so we only save 32 bytes/plane.
 */

static void cur_display (Mcdb *sprite, MCS *mcs, WORD x, WORD y)
{
#ifdef MACHINE_RPI
    raspi_hw_cur_display(sprite, x, y);
#else
    int row_count, plane, inc, op, dst_inc;
    UWORD * addr, * mask_start;
    UWORD shft, cdb_fg, cdb_bg;
    UWORD cdb_mask;             /* for checking cdb_bg/cdb_fg */
    ULONG *save;

    x -= sprite->xhot;          /* x = left side of destination block */
    y -= sprite->yhot;          /* y = top of destination block */

    mcs->stat = 0x00;           /* reset status of save buffer */

    /*
     * clip x axis
     */
    if (x < 0) {            /* clip left */
        x += 16;                /* get address of right word */
        op = 1;                 /* remember we're clipping left */
    }
    else if (x >= (linea_vars.DEV_TAB[0]-15)) {    /* clip right */
        op = 2;                 /* remember we're clipping right */
    }
    else {                  /* no clipping */
        op = 0;                 /* longword save */
        mcs->stat |= MCS_LONGS; /* mark savearea as longword save */
    }

    /*
     * clip y axis
     */
    mask_start = sprite->maskdata;  /* MASK/DATA for cursor */
    if (y < 0) {            /* clip top */
        row_count = y + 16;
        mask_start -= y << 1;   /* point to first visible row of MASK/FORM */
        y = 0;                  /* and reset starting row */
    }
    else if (y > (linea_vars.DEV_TAB[1]-15)) { /* clip bottom */
        row_count = linea_vars.DEV_TAB[1] - y + 1;
    }
    else {
        row_count = 16;
    }

    /*
     *  Compute the bit offset into the desired word, save it, and remove
     *  these bits from the x-coordinate.
     */
    addr = get_start_addr(x, y);
    shft = 16 - (x&0x0f);       /* amount to shift forms by */

    /*
     *  Store values required by cur_replace()
     */
    mcs->len = row_count;       /* number of cursor rows */
    mcs->addr = addr;           /* save area: origin of material */
    mcs->stat |= MCS_VALID;     /* flag the buffer as being loaded */

    /*
     *  To allow performance optimisations in this function, we handle
     *  L/R clipping in a separate function
     */
    if (op) {
        cur_display_clip(op,sprite,mcs,mask_start,shft);
        return;
    }

    /*
     * The rest of this function handles the no-L/R clipping case
     */
    inc = linea_vars.v_planes;             /* # distance to next word in same plane */
    dst_inc = linea_vars.v_lin_wr >> 1;    /* calculate number of words in a scan line */

    save = mcs->area;           /* for long stores */

    cdb_bg = sprite->bg_col;    /* get mouse background color bits */
    cdb_fg = sprite->fg_col;    /* get mouse foreground color bits */

    /* plane controller, draw cursor in each graphic plane */
    for (plane = linea_vars.v_planes - 1, cdb_mask = 0x0001; plane >= 0; plane--) {
        int row;
        UWORD * src, * dst;

        /* setup the things we need for each plane again */
        src = mask_start;               /* calculated mask data begin */
        dst = addr++;                   /* current destination address */

        /* loop through rows */
        for (row = row_count - 1; row >= 0; row--) {
            ULONG bits;                 /* our graphics data */
            ULONG fg;                   /* the foreground color */
            ULONG bg;                   /* the background color */

            /*
             * first, save the existing data
             */
            bits = ((ULONG)*dst) << 16; /* bring to left pos. */
            bits |= *(dst + inc);
            *save++ = bits;

            /*
             * align the forms with the cursor position on the screen
             */

            /* get and align background & foreground forms */
            bg = (ULONG)*src++ << shft;
            fg = (ULONG)*src++ << shft;

            /*
             * logical operation for cursor interaction with screen
             * note that this only implements the "VDI" mode
             */

            /* select operation for mouse mask background color */
            if (cdb_bg & cdb_mask)
                bits |= bg;
            else
                bits &= ~bg;

            /* select operation for mouse mask foreground color */
            if (cdb_fg & cdb_mask)
                bits |= fg;
            else
                bits &= ~fg;

            /*
             * update the screen with the new data
             */
            *dst = (UWORD)(bits >> 16);
            *(dst + inc) = (UWORD)bits;
            dst += dst_inc;             /* next row of screen */
        } /* loop through rows */

        cdb_mask <<= 1;
    } /* loop through planes */
#endif
}


/*
 * cur_replace - replace cursor with data in save area
 *
 * note: the near-duplication of loops for the word and longword cases
 * is done deliberately for performance reasons
 *
 * input:
 *      mcs         ptr to mouse cursor save area
 *      v_planes    number of planes in destination
 *      v_lin_wr    line wrap (byte width of form)
 */
static void cur_replace (MCS *mcs)
{
#ifndef MACHINE_RPI
    WORD plane, row;
    UWORD *addr, *src, *dst;
    const WORD inc = linea_vars.v_planes;      /* # words to next word in same plane */
    const WORD dst_inc = linea_vars.v_lin_wr >> 1; /* # words in a scan line */

    if (!(mcs->stat & MCS_VALID))   /* does save area contain valid data ? */
        return;
    mcs->stat &= ~MCS_VALID;        /* yes but (like TOS) don't allow reuse */

    addr = mcs->addr;
    src = (UWORD *)mcs->area;

    /*
     * handle longword data
     */
    if (mcs->stat & MCS_LONGS) {
        /* plane controller, draw cursor in each graphic plane */
        for (plane = linea_vars.v_planes - 1; plane >= 0; plane--) {
            dst = addr++;           /* current destination address */
            /* loop through rows */
            for (row = mcs->len - 1; row >= 0; row--) {
                *dst = *src++;
                *(dst + inc) = *src++;
                dst += dst_inc;     /* next row of screen */
            }
        }
        return;
    }

    /*
     * handle word data
     */

    /* plane controller, draw cursor in each graphic plane */
    for (plane = linea_vars.v_planes - 1; plane >= 0; plane--) {
        dst = addr++;               /* current destination address */
        /* loop through rows */
        for (row = mcs->len - 1; row >= 0; row--) {
            *dst = *src++;
            dst += dst_inc;         /* next row of screen */
        }
    }
#endif
}


/* line-A support */

/* set by linea.S */
WORD sprite_x, sprite_y;
Mcdb *sprite_def;
MCS *sprite_sav;

void draw_sprite(void)
{
    cur_display(sprite_def, sprite_sav, sprite_x, sprite_y);
}

void undraw_sprite(void)
{
    cur_replace(sprite_sav);
}
