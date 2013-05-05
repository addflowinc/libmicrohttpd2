/*
    This file is part of libmicrospdy
    Copyright (C) 2012 Andrey Uzunov

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file internal.c
 * @brief  internal functions and macros for the framing layer
 * @author Andrey Uzunov
 */

#include "platform.h"
#include "structures.h"


time_t
SPDYF_monotonic_time(void)
{
#ifdef HAVE_CLOCK_GETTIME
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
	return ts.tv_sec;
#endif
    return time(NULL);
}
