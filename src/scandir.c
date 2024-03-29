/* Copyright (C) 1992-1998, 2000 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

/* This was modified for use in Hydra web server. --nmav
 */

/* $Id: scandir.c,v 1.2 2002/11/29 14:56:36 andreou Exp $ */

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "compat.h"

#ifndef HAVE_SCANDIR

int
scandir(const char *dir, struct dirent ***namelist,
	int (*select) (const struct dirent *),
	int (*cmp) (const void *, const void *))
{
	DIR *dp = opendir (dir);
	struct dirent **v = NULL;
	size_t vsize = 0, i;
	struct dirent *d;
	int save;

	if (dp == NULL)
		return -1;

	save = errno;
	errno = (0);

	i = 0;
	while ((d = readdir (dp)) != NULL)
		if (select == NULL || (*select) (d))
		{
			struct dirent *vnew;
			size_t dsize;

			/* Ignore errors from select or readdir */
			errno = (0);

			if ( i == vsize)
			{
				struct dirent **new;
				if (vsize == 0)
					vsize = 10;
				else
					vsize *= 2;
				new = (struct dirent **) realloc (v, vsize * sizeof (*v));
				if (new == NULL)
					break;
				v = new;
			}

/*	dsize = &d->d_name[_D_ALLOC_NAMLEN (d)] - (char *) d;
 */
			dsize = d->d_reclen;
			vnew = (struct dirent *) malloc (dsize);
			if (vnew == NULL)
				break;

			v[i++] = (struct dirent *) memcpy (vnew, d, dsize);
		}

	if ( errno != 0)
	{
		save = errno;
		(void) closedir (dp);
		while (i > 0)
			free (v[--i]);
		free (v);
		errno = (save);
		return -1;
	}

	(void) closedir (dp);
	errno = (save);

	/* Sort the list if we have a comparison function to sort with.  */
#ifdef HAVE_QSORT
	if (cmp != NULL)
		qsort (v, i, sizeof (*v), cmp);
#endif

	*namelist = v;
	return i;
} /* scandir() */

#endif
