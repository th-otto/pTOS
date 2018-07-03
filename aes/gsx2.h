/*
 * gsx2.h - VDI (GSX) bindings
 *
 * Copyright (C) 2014-2015 The EmuTOS development team
 *
 * Authors:
 *  VRI   Vincent Rivière
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef GSX2_H
#define GSX2_H

#include "portab.h"
#include "vdipb.h"
extern VDIPB vdipb;

#define i_intin(p)  (vdipb.intin = (p))
#define i_ptsin(p)  (vdipb.ptsin = (p))
#define i_intout(p) (vdipb.intout = (p))
#define i_ptsout(p) (vdipb.ptsout = (p))
#define i_ptr(p)    (contrl.ptr1 = (p))
#define i_ptr2(p)   (contrl.ptr2 = (p))
#define m_lptr2(p)  ((p) = contrl.ptr2)

void gsx2(void);

#endif /* GSX2_H */
