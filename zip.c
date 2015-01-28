/*
 * Copyright (c) 2010 Mike Belopuhov
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>
#include <fts.h>

#include "zip.h"

static uint16_t zip_dosdate(struct timespec *);
static uint16_t zip_dostime(struct timespec *);

static uint16_t
zip_dosdate(struct timespec *ts)
{
	struct tm *tm;
	uint16_t res;

	tm = localtime(&ts->tv_sec);

	res = tm->tm_mday | (tm->tm_mon + 1) << 5 | (tm->tm_year - 80) << 9;

	return (res);
}

static uint16_t
zip_dostime(struct timespec *ts)
{
	struct tm *tm;
	uint16_t res;

	tm = localtime(&ts->tv_sec);

	res = (tm->tm_sec & 0x1f) | tm->tm_min << 5 | tm->tm_hour << 11;

	return (res);
}

int
zip_open(struct zip *z, char *path, int flags, mode_t mode)
{
	if ((z->z_fd = open(path, flags, mode)) == -1)
		return (-1);

	TAILQ_INIT(&z->z_dlist);

	return (0);
}

int
zip_addfile(struct zip *z, char *path)
{
	struct iovec iov[3];
	struct stat sb;
	struct zip_local zl;
	struct zip_dent *zd;
	char *fmap;
	size_t flen;
	off_t loff;
	int fd, err = 0;

	if ((fd = open(path, O_RDONLY, 0)) < 0)
		return (-1);

	if (fstat(fd, &sb)) {
		close(fd);
		return (-1);
	}

	flen = sb.st_size;

	/* local file header */
	bzero(&zl, sizeof zl);
	zl.zl_sig = htole32(ZLOCAL_SIG);
	zl.zl_ver = htole16(10);
	zl.zl_mtime = htole16(zip_dostime(&sb.st_mtim));
	zl.zl_mdate = htole16(zip_dosdate(&sb.st_mtim));
	zl.zl_csize = zl.zl_usize = sb.st_size;
	zl.zl_fnlen = htole16(strlen(path));

	iov[0].iov_base = &zl;
	iov[0].iov_len = sizeof zl;

	/* append path */
	iov[1].iov_base = path;
	iov[1].iov_len = strlen(path);

	if ((fmap = mmap(NULL, flen, PROT_READ, MAP_FILE, fd, 0)) ==
	    MAP_FAILED) {
		close(fd);
		return (-1);
	}

	/* calculate crc32 */
	zl.zl_crc = htole32(crc32(0, fmap, flen));

	iov[2].iov_base = fmap;
	iov[2].iov_len = flen;

	loff = lseek(z->z_fd, 0, SEEK_CUR);
	err = writev(z->z_fd, iov, 3);

	munmap(fmap, flen);
	close(fd);

	/*
	 * compose a central directory entry for zip_finalize()
	 */

	zd = calloc(1, sizeof (*zd));
	if (!zd)
		return (-1);

	zd->zd_ce.zc_mtime = zl.zl_mtime;
	zd->zd_ce.zc_mdate = zl.zl_mdate;
	zd->zd_ce.zc_crc = zl.zl_crc;
	zd->zd_ce.zc_csize = zd->zd_ce.zc_usize = zl.zl_csize;
	zd->zd_ce.zc_fnlen = zl.zl_fnlen;
	zd->zd_ce.zc_eattr = htole32(sb.st_mode << 16);
	zd->zd_ce.zc_hofft = htole32(loff);

	zd->zd_fnlen = strlen(path);
	bcopy(path, zd->zd_fname, zd->zd_fnlen);

	TAILQ_INSERT_TAIL(&z->z_dlist, zd, zd_entries);

	return (err > 0 ? 0 : -1);
}

int
zip_adddir(struct zip *z, char *path)
{
	FTS		*fts;
	FTSENT		*p;
	static char	*fpath[] = { NULL, NULL };
	char		*ppath;

	fpath[0] = path;

	if ((fts = fts_open(fpath, FTS_PHYSICAL | FTS_NOCHDIR, NULL)) == NULL) {
		warn("fts_open %s", path);
		return (-1);
	}

	while ((p = fts_read(fts)) != NULL) {
		switch (p->fts_info) {
		case FTS_F:
			ppath = p->fts_accpath;
			if (strncmp(ppath, "./", 2) == 0)
				ppath += 2;
			if (zip_addfile(z, ppath) != 0) {
				warnx("zip_addfile %s failed", ppath);
				continue;
			}
			break;
		}
	}
	fts_close(fts);

	return (0);
}

int
zip_finalize(struct zip *z)
{
	struct iovec iov[2];
	struct zip_eocd ze;
	struct zip_dent *zd;
	off_t cdoff;
	int n, nent = 0, cdsz = 0;

	cdoff = lseek(z->z_fd, 0, SEEK_CUR);

	/* loop, write down, free */
	while ((zd = TAILQ_FIRST(&z->z_dlist)) != NULL) {
		TAILQ_REMOVE(&z->z_dlist, zd, zd_entries);

		zd->zd_ce.zc_sig = htole32(ZCENTRAL_SIG);
		zd->zd_ce.zc_vcrea = htole16(0x031e); /* UNIX */
		zd->zd_ce.zc_vextr = htole16(10); /* 1.0 */

		iov[0].iov_base = &zd->zd_ce;
		iov[0].iov_len = sizeof (zd->zd_ce);
		iov[1].iov_base = zd->zd_fname;
		iov[1].iov_len = zd->zd_fnlen;

		n = writev(z->z_fd, iov, 2);
		if (n < 0)
			return (-1);
		cdsz += n;

		free(zd);

		nent++;
	}

	bzero(&ze, sizeof ze);
	ze.ze_sig = htole32(ZEOCD_SIG);
	ze.ze_dknent = ze.ze_nent = htole16(nent);
	ze.ze_size = htole32(cdsz);
	ze.ze_dofft = htole32(cdoff);

	if (write(z->z_fd, &ze, sizeof ze) < 0)
		return (-1);

	close(z->z_fd);

	return (0);
}

int
main(int argc, char **argv)
{
	struct zip z;
	struct stat st;
	int i, rflag = 0;
	char ch;

	while ((ch = getopt(argc, argv, "qr")) != -1)
		switch (ch) {
		case 'q':
			break;
		case 'r':
			rflag = 1;
			break;
		}
	argc -= optind;
	argv += optind;

	if (argc < 2) 
		errx(1, "usage: zip [-r] archive file [file ...]");

	if (zip_open(&z, argv[0], O_RDWR|O_CREAT|O_TRUNC, 0666))
		err(1, "zip_open: %s", argv[0]);

	for (i = 1; i < argc; i++) {
		if (stat(argv[i], &st) == 0 && st.st_mode & S_IFDIR) {
			if (rflag == 0)
				continue;
			if (zip_adddir(&z, argv[i]))
				err(1, "zip_adddir: %s", argv[i]);
		} else if (zip_addfile(&z, argv[i])) {
			err(1, "zip_addfile: %s", argv[i]);
		}
	}

	if (zip_finalize(&z))
		err(1, "zip_finalize");

	return (0);
}
