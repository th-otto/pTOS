/*
 * font.h - font specific definitions
 *
 * Copyright (C) 2001 Lineo, Inc.
 * Copyright (C) 2004 by Authors (see below)
 * Copyright (C) 2015-2017 The EmuTOS development team
 *
 * Authors:
 *  MAD     Martin Doering
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef FONT_H
#define FONT_H

#include "portab.h"
#include "fonthdr.h"

/* prototypes */

void font_init(void);           /* initialize BIOS font ring */
void font_set_default(WORD cellheight); /* choose the default font */

#endif /* FONT_H */
