/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

int _close(int fd)
{
	(void)fd;
	return -1;
}

void _exit(int status)
{
	(void)status;

	for (;;) {
	}
}

int _fstat(int fd, struct stat *st)
{
	if (st == NULL) {
		errno = EINVAL;
		return -1;
	}

	(void)fd;
	st->st_mode = S_IFCHR;
	return 0;
}

int _isatty(int fd)
{
	(void)fd;
	return 1;
}

off_t _lseek(int fd, off_t offset, int whence)
{
	(void)fd;
	(void)offset;
	(void)whence;
	return 0;
}

int _read(int fd, void *buf, size_t count)
{
	(void)fd;
	(void)buf;
	(void)count;
	return 0;
}

void *_sbrk(ptrdiff_t incr)
{
	(void)incr;
	errno = ENOMEM;
	return (void *)-1;
}

int _write(int fd, const void *buf, size_t count)
{
	(void)fd;
	(void)buf;
	return (int)count;
}
