/*
 * vdi_col.h - VDI color palette functions and tables.
 *
 * Copyright 2005-2016 by The EmuTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

extern WORD MAP_COL[], REV_MAP_COL[];

extern void init_colors(void);
extern void vdi_vs_color(Vwk *vwk);
extern void vdi_vq_color(Vwk *vwk);
