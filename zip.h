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

#ifndef _ZIP_H_
#define _ZIP_H_

struct zip_local {
	uint32_t		zl_sig;
#define  ZLOCAL_SIG		 0x04034b50
	uint16_t		zl_ver;
	uint16_t		zl_flags;
	uint16_t		zl_comp;
	uint16_t		zl_mtime;
	uint16_t		zl_mdate;
	uint32_t		zl_crc;
	uint32_t		zl_csize;
	uint32_t		zl_usize;
	uint16_t		zl_fnlen;
	uint16_t		zl_extlen;
} __packed;

struct zip_central {
	uint32_t		zc_sig;
#define  ZCENTRAL_SIG		 0x02014b50
	uint16_t		zc_vcrea;
	uint16_t		zc_vextr;
	uint16_t		zc_flags;
	uint16_t		zc_comp;
	uint16_t		zc_mtime;
	uint16_t		zc_mdate;
	uint32_t		zc_crc;
	uint32_t		zc_csize;
	uint32_t		zc_usize;
	uint16_t		zc_fnlen;
	uint16_t		zc_extlen;
	uint16_t		zc_cmntlen;
	uint16_t		zc_dknum;
	uint16_t		zc_iattr;
	uint32_t		zc_eattr;
	uint32_t		zc_hofft;
} __packed;

struct zip_eocd {
	uint32_t		ze_sig;
#define  ZEOCD_SIG		 0x06054b50
	uint16_t		ze_dknum;
	uint16_t		ze_dksnum;
	uint16_t		ze_dknent;
	uint16_t		ze_nent;
	uint32_t		ze_size;
	uint32_t		ze_dofft;
	uint16_t		ze_cmntlen;
} __packed;

struct zip_dent {
	struct zip_central	zd_ce;
	char			zd_fname[MAXPATHLEN];
	size_t			zd_fnlen;
	TAILQ_ENTRY(zip_dent)	zd_entries;
};

struct zip {
	int			z_fd;
	TAILQ_HEAD(, zip_dent)	z_dlist;
};

int	zip_open(struct zip *, char *, int, mode_t);
int	zip_addfile(struct zip *, char *);
int	zip_adddir(struct zip *, char *);
int	zip_finalize(struct zip *);

#endif
