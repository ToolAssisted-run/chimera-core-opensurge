/* osfiles.c - every file the engine opens, answered by the core.
 *
 * The engine reads and writes its disk with the C library (through PhysicsFS
 * and Allegro: open, read, stat, opendir...); the link wraps those calls
 * (sources.mk), in both builds, and this file answers them from a disk that
 * exists only in the machine:
 *
 *   /os/share/...  the game's files: Surge the Rabbit's, one stored zip that
 *                  is part of the program (make-data-zip.py), read in place.
 *                  When the project brings a game of its own (its "game"
 *                  slot: a zip of the game's folder) that is /os/game
 *                  instead, and the engine is told to play it.
 *   /os/user/...   the engine's user folder: its settings, saves, logs.
 *                  Starts as the saves the project starts with (the savedata
 *                  slot); everything the engine writes is a file in guest
 *                  memory, empty when the machine starts and carried by
 *                  savestates like the rest of it.
 *   anything else  not there. The computer's disk is never read or written:
 *                  the only real files are the ones the host mounted by name
 *                  for the core (the settings and the slots), opened at Init.
 *
 * A file the engine opens is a handle over memory, so its state - the
 * position - is in the guest, which a savestate carries; a host file handle
 * would not survive a load.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <zlib.h>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "osurge-driver.h"

#define TEXT_ARG "\x1f"
#define SHARE_ROOT "/os/share"
#define GAME_ROOT "/os/game"
#define USER_ROOT "/os/user"

FILE *__real_fopen(const char *path, const char *mode);
int __real_open(const char *path, int flags, ...);

/* the game's files, as the program carries them (data.o, from surge-data.zip) */
extern const uint8_t _binary_surge_data_zip_start[];
extern const uint8_t _binary_surge_data_zip_end[];

/* ------------------------------------------------------------ settings */

void osurge_settings_read(struct osurge_settings *s)
{
	memset(s, 0, sizeof *s);
	s->start_time = wbx_setting_long("startTime", 1704067200L);
	s->resolution = (int)wbx_setting_long("resolution", 1);
	wbx_setting_str("language", s->language, (int)sizeof s->language);
	if (!strcmp(s->language, "default")) s->language[0] = '\0';
	wbx_setting_str("startLevel", s->start_level, (int)sizeof s->start_level);
	wbx_setting_str("quest", s->quest, (int)sizeof s->quest);
	{
		char q[16] = "medium";
		wbx_setting_str("quality", q, (int)sizeof q);
		s->quality = !strcmp(q, "low") ? 0 : !strcmp(q, "high") ? 2 : 1;
	}
	s->verbose = wbx_setting_bool("verboseLog", 0);
}

/* ------------------------------------------------------------ the tree */

/* a file of a zip: its place in the virtual disk and where its bytes are */
struct zfile
{
	const char *path;    /* absolute, '/' between parts, no trailing '/' */
	const uint8_t *data; /* the entry's bytes in the zip (stored or deflated) */
	uint32_t csize, usize;
	uint16_t method;     /* 0 stored, 8 deflated */
};

/* a file or folder the engine made: in the heap */
struct ufile
{
	char *path;
	uint8_t *data;
	size_t size, cap;
	int dir;
	int dead;
};

static struct zfile *g_zfiles;       /* sorted by path; sealed */
static size_t g_zfile_count;

static struct ufile *g_ufiles;       /* the machine's own files (heap) */
static size_t g_ufile_count, g_ufile_cap;

static int g_has_game;
static int g_real_ok;
static int g_log;

/* a path as the core compares it: '/' first, '/' between parts, no "." or
 * ".." part, no empty part, no trailing '/'; a relative path is from "/" */
static void norm_path(const char *in, char *out, size_t n)
{
	size_t o = 0;
	size_t part_start[64];
	int depth = 0;
	const char *p = in;
	if (n < 2) { if (n) out[0] = '\0'; return; }
	out[0] = '\0';
	while (*p)
	{
		while (*p == '/') p++;
		if (!*p) break;
		const char *q = p;
		while (*q && *q != '/') q++;
		size_t len = (size_t)(q - p);
		if (len == 1 && p[0] == '.')
		{
		}
		else if (len == 2 && p[0] == '.' && p[1] == '.')
		{
			if (depth > 0) o = part_start[--depth];
		}
		else if (depth < 63 && o + 1 + len < n)
		{
			part_start[depth++] = o;
			out[o++] = '/';
			memcpy(out + o, p, len);
			o += len;
		}
		p = q;
	}
	if (!o) { out[0] = '/'; o = 1; }
	out[o] = '\0';
}

static int cmp_zfile(const void *a, const void *b)
{
	return strcmp(((const struct zfile *)a)->path, ((const struct zfile *)b)->path);
}

static const struct zfile *find_zfile(const char *norm)
{
	size_t lo = 0, hi = g_zfile_count;
	while (lo < hi)
	{
		size_t mid = (lo + hi) / 2;
		int c = strcmp(norm, g_zfiles[mid].path);
		if (!c) return &g_zfiles[mid];
		if (c < 0) hi = mid; else lo = mid + 1;
	}
	return NULL;
}

static struct ufile *find_ufile(const char *norm)
{
	for (size_t i = 0; i < g_ufile_count; i++)
		if (!g_ufiles[i].dead && !strcmp(g_ufiles[i].path, norm)) return &g_ufiles[i];
	return NULL;
}

static struct ufile *add_ufile(const char *norm, int dir)
{
	struct ufile *u = find_ufile(norm);
	if (u) return u;
	for (size_t i = 0; i < g_ufile_count; i++)
		if (g_ufiles[i].dead) { u = &g_ufiles[i]; break; }
	if (!u)
	{
		if (g_ufile_count == g_ufile_cap)
		{
			g_ufile_cap = g_ufile_cap ? g_ufile_cap * 2 : 64;
			g_ufiles = (struct ufile *)realloc(g_ufiles, g_ufile_cap * sizeof *g_ufiles);
			if (!g_ufiles) { g_ufile_cap = 0; g_ufile_count = 0; return NULL; }
		}
		u = &g_ufiles[g_ufile_count++];
	}
	memset(u, 0, sizeof *u);
	u->path = strdup(norm);
	u->dir = dir;
	return u;
}

static int has_prefix_dir(const char *norm, const char *p, size_t len)
{
	return !strncmp(p, norm, len) && p[len] == '/';
}

/* does the folder exist: the roots do; any file's folders; any folder made */
static int path_is_dir(const char *norm)
{
	if (!strcmp(norm, "/") || !strcmp(norm, "/os") || !strcmp(norm, "/proc") || !strcmp(norm, USER_ROOT) ||
	    (!g_has_game && !strcmp(norm, SHARE_ROOT)) || (g_has_game && !strcmp(norm, GAME_ROOT)))
		return 1;
	size_t len = strlen(norm);
	for (size_t i = 0; i < g_ufile_count; i++)
	{
		const struct ufile *u = &g_ufiles[i];
		if (u->dead) continue;
		if (u->dir && !strcmp(u->path, norm)) return 1;
		if (has_prefix_dir(norm, u->path, len)) return 1;
	}
	size_t lo = 0, hi = g_zfile_count;
	char key[1100];
	if (len + 2 >= sizeof key) return 0;
	memcpy(key, norm, len);
	key[len] = '/';
	key[len + 1] = '\0';
	while (lo < hi)
	{
		size_t mid = (lo + hi) / 2;
		if (strcmp(g_zfiles[mid].path, key) < 0) lo = mid + 1; else hi = mid;
	}
	return lo < g_zfile_count && !strncmp(g_zfiles[lo].path, key, len + 1);
}

static int in_user(const char *norm)
{
	return !strncmp(norm, USER_ROOT, sizeof USER_ROOT - 1) &&
	       (norm[sizeof USER_ROOT - 1] == '/' || norm[sizeof USER_ROOT - 1] == '\0');
}

/* ------------------------------------------------------------- handles */

/* an open file: a file descriptor of the engine's (from FD_BASE up) or the
 * cookie of a stream. A zip's file is read in place when stored and
 * inflated into a buffer when not. */
struct handle
{
	const struct zfile *z;
	uint8_t *own;          /* the inflated bytes of a deflated file */
	const uint8_t *bytes;  /* the file's bytes, for a file of a zip */
	size_t usize;
	char *upath;           /* a file the engine made, by path (it may be reallocated meanwhile) */
	size_t pos;
	int write, append;
	int used;
};

#define FD_BASE 1000
#define FD_MAX 512
static struct handle g_fds[FD_MAX];

static void handle_free(struct handle *h)
{
	free(h->own);
	free(h->upath);
	memset(h, 0, sizeof *h);
}

static const uint8_t *handle_bytes(struct handle *h, size_t *size)
{
	if (h->z) { *size = h->usize; return h->bytes; }
	struct ufile *u = find_ufile(h->upath);
	if (!u) { *size = 0; return NULL; }
	*size = u->size;
	return u->data;
}

static ssize_t handle_read(struct handle *h, char *buf, size_t size)
{
	size_t have;
	const uint8_t *data = handle_bytes(h, &have);
	if (h->pos >= have) return 0;
	size_t n = have - h->pos;
	if (n > size) n = size;
	memcpy(buf, data + h->pos, n);
	h->pos += n;
	return (ssize_t)n;
}

static ssize_t handle_write(struct handle *h, const char *buf, size_t size)
{
	if (!h->write || h->z) { errno = EBADF; return -1; }
	struct ufile *u = find_ufile(h->upath);
	if (!u) { errno = ENOENT; return -1; }
	if (h->append) h->pos = u->size;
	if (h->pos + size > u->cap)
	{
		size_t cap = u->cap ? u->cap : 256;
		while (cap < h->pos + size) cap *= 2;
		uint8_t *d = (uint8_t *)realloc(u->data, cap);
		if (!d) { errno = ENOSPC; return -1; }
		u->data = d;
		u->cap = cap;
	}
	if (h->pos > u->size) memset(u->data + u->size, 0, h->pos - u->size);
	memcpy(u->data + h->pos, buf, size);
	h->pos += size;
	if (h->pos > u->size) u->size = h->pos;
	return (ssize_t)size;
}

static off64_t handle_seek(struct handle *h, off64_t offset, int whence)
{
	size_t have;
	handle_bytes(h, &have);
	long long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long long)h->pos : (long long)have;
	long long to = base + offset;
	if (to < 0) { errno = EINVAL; return -1; }
	h->pos = (size_t)to;
	return to;
}

/* opens a file of the machine's disk: 0 on failure with errno set, else the
 * handle (in `out`, which the caller owns) */
static int handle_open(const char *path, int writing, int create, int truncate, int append, int excl, struct handle *out)
{
	char norm[1100];
	norm_path(path, norm, sizeof norm);
	if (g_log > 1) fprintf(stderr, "osfiles: open %s%s\n", norm, writing ? " (write)" : "");
	memset(out, 0, sizeof *out);
	const struct zfile *z = find_zfile(norm);
	struct ufile *u = find_ufile(norm);
	if (u && u->dir) { errno = EISDIR; return 0; }
	if (!z && !u && path_is_dir(norm)) { errno = EISDIR; return 0; }
	if (writing)
	{
		if (!in_user(norm)) { errno = EACCES; return 0; }
		if (u && excl && create) { errno = EEXIST; return 0; }
		if (!u)
		{
			if (!create) { errno = ENOENT; return 0; }
			u = add_ufile(norm, 0);
			if (!u) { errno = ENOSPC; return 0; }
		}
		if (truncate) u->size = 0;
		out->upath = strdup(norm);
		out->write = 1;
		out->append = append;
		out->used = 1;
		return 1;
	}
	if (u)
	{
		out->upath = strdup(norm);
		out->used = 1;
		return 1;
	}
	if (!z)
	{
		if (g_log) fprintf(stderr, "osfiles: %s is not there\n", norm);
		errno = ENOENT;
		return 0;
	}
	out->z = z;
	out->usize = z->usize;
	if (z->method == 0)
	{
		out->bytes = z->data;
	}
	else
	{
		out->own = (uint8_t *)malloc(z->usize ? z->usize : 1);
		z_stream zs;
		memset(&zs, 0, sizeof zs);
		if (!out->own || inflateInit2(&zs, -15) != Z_OK) { free(out->own); out->own = NULL; errno = EIO; return 0; }
		zs.next_in = (Bytef *)z->data;
		zs.avail_in = z->csize;
		zs.next_out = out->own;
		zs.avail_out = z->usize;
		int r = inflate(&zs, Z_FINISH);
		inflateEnd(&zs);
		if (r != Z_STREAM_END || zs.total_out != z->usize) { free(out->own); out->own = NULL; errno = EIO; return 0; }
		out->bytes = out->own;
	}
	out->used = 1;
	return 1;
}

/* a stream over a handle */
static ssize_t cookie_read(void *c, char *buf, size_t size) { return handle_read((struct handle *)c, buf, size); }
static ssize_t cookie_write(void *c, const char *buf, size_t size) { return handle_write((struct handle *)c, buf, size); }
static int cookie_seek(void *c, off64_t *offset, int whence)
{
	off64_t r = handle_seek((struct handle *)c, *offset, whence);
	if (r < 0) return -1;
	*offset = r;
	return 0;
}
static int cookie_close(void *c)
{
	handle_free((struct handle *)c);
	free(c);
	return 0;
}
static const cookie_io_functions_t k_io = { cookie_read, cookie_write, cookie_seek, cookie_close };

/* a file the host mounted by name for the core: the settings and the slots
 * (opened at Init, never after) */
static int real_name(const char *path)
{
	if (!strcmp(path, "settings") || !strcmp(path, "slots")) return 1;
	return g_real_ok && !strchr(path, '/');
}

static FILE *open_stream(const char *path, const char *mode)
{
	if (!path || !mode) { errno = EINVAL; return NULL; }
	if (!strchr(path, '/') && real_name(path)) return __real_fopen(path, mode);
	int plus = strchr(mode, '+') != NULL;
	int writing = mode[0] == 'w' || mode[0] == 'a' || plus;
	struct handle tmp;
	if (!handle_open(path, writing, mode[0] != 'r', mode[0] == 'w', mode[0] == 'a', 0, &tmp)) return NULL;
	struct handle *h = (struct handle *)malloc(sizeof *h);
	if (!h) { handle_free(&tmp); errno = ENOMEM; return NULL; }
	*h = tmp;
	FILE *f = fopencookie(h, mode, k_io);
	if (!f) { cookie_close(h); errno = ENOMEM; }
	return f;
}

FILE *__wrap_fopen(const char *path, const char *mode) { return open_stream(path, mode); }
FILE *__wrap_fopen64(const char *path, const char *mode) { return open_stream(path, mode); }

/* ------------------------------------------------- file descriptors */

static struct handle *fd_handle(int fd)
{
	if (fd < FD_BASE || fd >= FD_BASE + FD_MAX || !g_fds[fd - FD_BASE].used) return NULL;
	return &g_fds[fd - FD_BASE];
}

static int do_open(const char *path, int flags, mode_t mode)
{
	(void)mode;
	if (!path) { errno = EFAULT; return -1; }
	if (!strchr(path, '/') && real_name(path)) return __real_open(path, flags, mode);
	int acc = flags & O_ACCMODE;
	int writing = acc == O_WRONLY || acc == O_RDWR;
	int slot;
	for (slot = 0; slot < FD_MAX; slot++) if (!g_fds[slot].used) break;
	if (slot == FD_MAX) { errno = EMFILE; return -1; }
	if (!handle_open(path, writing, (flags & O_CREAT) != 0, (flags & O_TRUNC) != 0, (flags & O_APPEND) != 0,
	                 (flags & O_EXCL) != 0, &g_fds[slot])) return -1;
	return FD_BASE + slot;
}

int __wrap_open(const char *path, int flags, ...)
{
	mode_t mode = 0;
	if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
	return do_open(path, flags, mode);
}
int __wrap_open64(const char *path, int flags, ...)
{
	mode_t mode = 0;
	if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
	return do_open(path, flags, mode);
}

extern int __real_close(int fd);
extern ssize_t __real_read(int fd, void *buf, size_t n);
extern ssize_t __real_write(int fd, const void *buf, size_t n);
extern ssize_t __real_pread(int fd, void *buf, size_t n, off_t off);
extern ssize_t __real_pread64(int fd, void *buf, size_t n, off64_t off);
extern off_t __real_lseek(int fd, off_t off, int whence);
extern off64_t __real_lseek64(int fd, off64_t off, int whence);
extern int __real_fstat(int fd, struct stat *st);
extern int __real_fsync(int fd);
extern int __real_ftruncate(int fd, off_t len);

int __wrap_close(int fd)
{
	struct handle *h = fd_handle(fd);
	if (!h) return __real_close(fd);
	handle_free(h);
	return 0;
}
ssize_t __wrap_read(int fd, void *buf, size_t n)
{
	struct handle *h = fd_handle(fd);
	return h ? handle_read(h, (char *)buf, n) : __real_read(fd, buf, n);
}
ssize_t __wrap_write(int fd, const void *buf, size_t n)
{
	struct handle *h = fd_handle(fd);
	return h ? handle_write(h, (const char *)buf, n) : __real_write(fd, buf, n);
}
ssize_t __wrap_pread(int fd, void *buf, size_t n, off_t off)
{
	struct handle *h = fd_handle(fd);
	if (!h) return __real_pread(fd, buf, n, off);
	size_t keep = h->pos, got;
	h->pos = (size_t)off;
	got = (size_t)handle_read(h, (char *)buf, n);
	h->pos = keep;
	return (ssize_t)got;
}
ssize_t __wrap_pread64(int fd, void *buf, size_t n, off64_t off)
{
	struct handle *h = fd_handle(fd);
	if (!h) return __real_pread64(fd, buf, n, off);
	return __wrap_pread(fd, buf, n, (off_t)off);
}
off_t __wrap_lseek(int fd, off_t off, int whence)
{
	struct handle *h = fd_handle(fd);
	return h ? (off_t)handle_seek(h, off, whence) : __real_lseek(fd, off, whence);
}
off64_t __wrap_lseek64(int fd, off64_t off, int whence)
{
	struct handle *h = fd_handle(fd);
	return h ? handle_seek(h, off, whence) : __real_lseek64(fd, off, whence);
}
int __wrap_fsync(int fd) { return fd_handle(fd) ? 0 : __real_fsync(fd); }
int __wrap_ftruncate(int fd, off_t len)
{
	struct handle *h = fd_handle(fd);
	if (!h) return __real_ftruncate(fd, len);
	struct ufile *u = h->upath ? find_ufile(h->upath) : NULL;
	if (!u || !h->write) { errno = EBADF; return -1; }
	if ((size_t)len <= u->size) u->size = (size_t)len;
	return 0;
}

/* --------------------------------------------------------------- stat */

static void fill_dir(struct stat *st)
{
	memset(st, 0, sizeof *st);
	st->st_mode = S_IFDIR | 0755;
	st->st_nlink = 2;
	st->st_dev = 1;
}

static unsigned long path_hash(const char *s)
{
	unsigned long h = 5381;
	while (*s) h = h * 33 + (unsigned char)*s++;
	return h;
}

static int fill_stat(const char *path, struct stat *st)
{
	if (!path) { errno = EFAULT; return -1; }
	char norm[1100];
	norm_path(path, norm, sizeof norm);
	const struct zfile *z = find_zfile(norm);
	struct ufile *u = find_ufile(norm);
	if (u && !u->dir)
	{
		memset(st, 0, sizeof *st);
		st->st_mode = S_IFREG | 0644;
		st->st_size = (off_t)u->size;
		st->st_nlink = 1;
		st->st_dev = 1;
		st->st_ino = path_hash(norm);
		st->st_mtime = 1700000000;
		return 0;
	}
	if (z)
	{
		memset(st, 0, sizeof *st);
		st->st_mode = S_IFREG | 0444;
		st->st_size = (off_t)z->usize;
		st->st_nlink = 1;
		st->st_dev = 1;
		st->st_ino = path_hash(norm);
		st->st_mtime = 1700000000;
		return 0;
	}
	if (path_is_dir(norm))
	{
		fill_dir(st);
		st->st_ino = path_hash(norm);
		st->st_mtime = 1700000000;
		return 0;
	}
	errno = ENOENT;
	return -1;
}

int __wrap_stat(const char *path, struct stat *st)
{
	if (path && !strchr(path, '/') && real_name(path)) { errno = ENOENT; return -1; }
	return fill_stat(path, st);
}
int __wrap_lstat(const char *path, struct stat *st) { return __wrap_stat(path, st); }
int __wrap_stat64(const char *path, struct stat64 *st) { return __wrap_stat(path, (struct stat *)st); }
int __wrap_lstat64(const char *path, struct stat64 *st) { return __wrap_stat(path, (struct stat *)st); }

int __wrap_fstat(int fd, struct stat *st)
{
	struct handle *h = fd_handle(fd);
	if (!h) return __real_fstat(fd, st);
	size_t size;
	handle_bytes(h, &size);
	memset(st, 0, sizeof *st);
	st->st_mode = S_IFREG | 0644;
	st->st_size = (off_t)size;
	st->st_nlink = 1;
	st->st_dev = 1;
	return 0;
}
int __wrap_fstat64(int fd, struct stat64 *st) { return __wrap_fstat(fd, (struct stat *)st); }

int __wrap_access(const char *path, int mode)
{
	struct stat st;
	if (fill_stat(path, &st) != 0) return -1;
	if ((mode & W_OK) != 0)
	{
		char norm[1100];
		norm_path(path, norm, sizeof norm);
		if (!in_user(norm)) { errno = EACCES; return -1; }
	}
	return 0;
}

char *__wrap_getcwd(char *buf, size_t size)
{
	if (!buf) return strdup("/os");
	if (size < 4) { errno = ERANGE; return NULL; }
	strcpy(buf, "/os");
	return buf;
}

char *__wrap_realpath(const char *path, char *resolved)
{
	char norm[1100];
	if (!path) { errno = EINVAL; return NULL; }
	norm_path(path, norm, sizeof norm);
	struct stat st;
	if (fill_stat(norm, &st) != 0) return NULL;
	return resolved ? strcpy(resolved, norm) : strdup(norm);
}

ssize_t __wrap_readlink(const char *path, char *buf, size_t size)
{
	/* the engine asks where its own program is (PhysicsFS) */
	if (path && !strcmp(path, "/proc/self/exe"))
	{
		static const char exe[] = "/os/bin/opensurge";
		size_t n = sizeof exe - 1 < size ? sizeof exe - 1 : size;
		memcpy(buf, exe, n);
		return (ssize_t)n;
	}
	errno = EINVAL;
	return -1;
}

/* ---------------------------------------------------------- folders */

struct mydir
{
	char **names;
	size_t count, pos;
	struct dirent ent;
};

static int names_add(char ***v, size_t *count, size_t *cap, const char *name, size_t len)
{
	for (size_t i = 0; i < *count; i++)
		if (strlen((*v)[i]) == len && !strncmp((*v)[i], name, len)) return 1;
	if (*count == *cap)
	{
		*cap = *cap ? *cap * 2 : 32;
		char **n = (char **)realloc(*v, *cap * sizeof **v);
		if (!n) return 0;
		*v = n;
	}
	char *c = (char *)malloc(len + 1);
	if (!c) return 0;
	memcpy(c, name, len);
	c[len] = '\0';
	(*v)[(*count)++] = c;
	return 1;
}

static int cmp_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

DIR *__wrap_opendir(const char *path)
{
	char norm[1100];
	if (!path) { errno = EFAULT; return NULL; }
	norm_path(path, norm, sizeof norm);
	if (find_zfile(norm) || (find_ufile(norm) && !find_ufile(norm)->dir)) { errno = ENOTDIR; return NULL; }
	if (!path_is_dir(norm)) { errno = ENOENT; return NULL; }
	struct mydir *d = (struct mydir *)calloc(1, sizeof *d);
	if (!d) { errno = ENOMEM; return NULL; }
	size_t cap = 0, len = strlen(norm);
	char prefix[1104];
	snprintf(prefix, sizeof prefix, "%s%s", norm, len == 1 ? "" : "/");
	size_t pl = strlen(prefix);
	/* the roots' own children */
	if (!strcmp(norm, "/")) names_add(&d->names, &d->count, &cap, "os", 2);
	if (!strcmp(norm, "/os"))
	{
		names_add(&d->names, &d->count, &cap, "user", 4);
		names_add(&d->names, &d->count, &cap, g_has_game ? "game" : "share", g_has_game ? 4 : 5);
	}
	for (size_t i = 0; i < g_zfile_count; i++)
	{
		const char *p = g_zfiles[i].path;
		if (strncmp(p, prefix, pl) != 0) continue;
		const char *rest = p + pl, *slash = strchr(rest, '/');
		names_add(&d->names, &d->count, &cap, rest, slash ? (size_t)(slash - rest) : strlen(rest));
	}
	for (size_t i = 0; i < g_ufile_count; i++)
	{
		const struct ufile *u = &g_ufiles[i];
		if (u->dead || strncmp(u->path, prefix, pl) != 0) continue;
		const char *rest = u->path + pl, *slash = strchr(rest, '/');
		names_add(&d->names, &d->count, &cap, rest, slash ? (size_t)(slash - rest) : strlen(rest));
	}
	qsort(d->names, d->count, sizeof *d->names, cmp_names);
	return (DIR *)d;
}

struct dirent *__wrap_readdir(DIR *dir)
{
	struct mydir *d = (struct mydir *)dir;
	if (!d || d->pos >= d->count) return NULL;
	memset(&d->ent, 0, sizeof d->ent);
	strncpy(d->ent.d_name, d->names[d->pos++], sizeof d->ent.d_name - 1);
	d->ent.d_type = DT_UNKNOWN;
	d->ent.d_reclen = sizeof d->ent;
	return &d->ent;
}
struct dirent *__wrap_readdir64(DIR *dir) { return __wrap_readdir(dir); }

int __wrap_closedir(DIR *dir)
{
	struct mydir *d = (struct mydir *)dir;
	if (!d) return -1;
	for (size_t i = 0; i < d->count; i++) free(d->names[i]);
	free(d->names);
	free(d);
	return 0;
}

int __wrap_mkdir(const char *path, mode_t mode)
{
	(void)mode;
	char norm[1100];
	norm_path(path, norm, sizeof norm);
	if (!in_user(norm)) { errno = EACCES; return -1; }
	if (find_ufile(norm) || find_zfile(norm) || path_is_dir(norm)) { errno = EEXIST; return -1; }
	return add_ufile(norm, 1) ? 0 : (errno = ENOSPC, -1);
}

int __wrap_rmdir(const char *path)
{
	char norm[1100];
	norm_path(path, norm, sizeof norm);
	if (!in_user(norm)) { errno = EACCES; return -1; }
	struct ufile *u = find_ufile(norm);
	size_t len = strlen(norm);
	for (size_t i = 0; i < g_ufile_count; i++)
		if (!g_ufiles[i].dead && has_prefix_dir(norm, g_ufiles[i].path, len)) { errno = ENOTEMPTY; return -1; }
	if (!u || !u->dir) { errno = ENOENT; return -1; }
	free(u->path); free(u->data);
	memset(u, 0, sizeof *u);
	u->dead = 1;
	return 0;
}

int __wrap_unlink(const char *path)
{
	char norm[1100];
	norm_path(path, norm, sizeof norm);
	if (!in_user(norm)) { errno = EACCES; return -1; }
	struct ufile *u = find_ufile(norm);
	if (!u || u->dir) { errno = ENOENT; return -1; }
	free(u->path); free(u->data);
	memset(u, 0, sizeof *u);
	u->dead = 1;
	return 0;
}

int __wrap_remove(const char *path)
{
	char norm[1100];
	norm_path(path, norm, sizeof norm);
	struct ufile *u = find_ufile(norm);
	return u && u->dir ? __wrap_rmdir(path) : __wrap_unlink(path);
}

int __wrap_rename(const char *from, const char *to)
{
	char a[1100], b[1100];
	norm_path(from, a, sizeof a);
	norm_path(to, b, sizeof b);
	if (!in_user(a) || !in_user(b)) { errno = EACCES; return -1; }
	struct ufile *u = find_ufile(a);
	if (!u) { errno = ENOENT; return -1; }
	struct ufile *t = find_ufile(b);
	if (t && t != u) { free(t->path); free(t->data); memset(t, 0, sizeof *t); t->dead = 1; }
	char *np = strdup(b);
	if (!np) { errno = ENOMEM; return -1; }
	free(u->path);
	u->path = np;
	return 0;
}

/* ------------------------------------------------- the environment */

char *__wrap_getenv(const char *name)
{
	/* the engine's user folder, and nothing else of the host's environment */
	if (name && !strcmp(name, "OPENSURGE_USER_PATH")) return (char *)USER_ROOT;
	if (name && !strcmp(name, "HOME")) return (char *)"/os";
	return NULL;
}
struct passwd *__wrap_getpwuid(uid_t uid) { (void)uid; return NULL; }
uid_t __wrap_getuid(void) { return 1000; }

/* ----------------------------------------------------------- loading */

/* a slot's file read whole, in sealed memory */
static int read_whole(const char *name, uint8_t **out, size_t *size)
{
	FILE *f = __real_fopen(name, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0) { fclose(f); return -1; }
	uint8_t *p = (uint8_t *)alloc_sealed((size_t)n + 1);
	if (!p) { fclose(f); return -1; }
	size_t got = fread(p, 1, (size_t)n, f);
	fclose(f);
	if (got != (size_t)n) return -1;
	*out = p;
	*size = (size_t)n;
	return 0;
}

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

struct zlist { struct zfile *v; size_t count, cap; };

static int zlist_add(struct zlist *l, const char *path, const uint8_t *data, uint32_t csize, uint32_t usize, uint16_t method)
{
	if (l->count == l->cap)
	{
		l->cap = l->cap ? l->cap * 2 : 256;
		struct zfile *n = (struct zfile *)realloc(l->v, l->cap * sizeof *n);
		if (!n) return 0;
		l->v = n;
	}
	struct zfile *z = &l->v[l->count++];
	z->path = strdup(path);
	z->data = data;
	z->csize = csize;
	z->usize = usize;
	z->method = method;
	return z->path != NULL;
}

/* the entries of a zip held in memory: each file as `root` + its name (less
 * `strip`, a leading folder that is not part of the game); 0 on a zip that
 * cannot be read */
static int read_zip(struct zlist *l, const uint8_t *zip, size_t size, const char *root, const char *strip)
{
	if (size < 22) return 0;
	size_t e = size - 22;
	while (e > 0 && rd32(zip + e) != 0x06054b50u) { if (size - e > 70000) return 0; e--; }
	if (rd32(zip + e) != 0x06054b50u) return 0;
	uint32_t count = rd16(zip + e + 10), cdsize = rd32(zip + e + 12), cdoff = rd32(zip + e + 16);
	if ((size_t)cdoff + cdsize > size) return 0;
	const uint8_t *p = zip + cdoff;
	size_t sl = strip ? strlen(strip) : 0;
	for (uint32_t i = 0; i < count; i++)
	{
		if ((size_t)(p - zip) + 46 > size || rd32(p) != 0x02014b50u) return 0;
		uint16_t method = (uint16_t)rd16(p + 10);
		uint32_t csize = rd32(p + 20), usize = rd32(p + 24);
		uint32_t nlen = rd16(p + 28), xlen = rd16(p + 30), clen = rd16(p + 32), lho = rd32(p + 42);
		const uint8_t *name = p + 46;
		p += 46 + nlen + xlen + clen;
		if (nlen == 0 || name[nlen - 1] == '/' || nlen > 900) continue;
		if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu) continue; /* zip64: not read */
		if ((size_t)lho + 30 > size || rd32(zip + lho) != 0x04034b50u) return 0;
		size_t doff = (size_t)lho + 30 + rd16(zip + lho + 26) + rd16(zip + lho + 28);
		if (doff + csize > size) return 0;
		char entry[1000], path[1100];
		memcpy(entry, name, nlen);
		entry[nlen] = '\0';
		const char *rel = entry;
		if (sl)
		{
			if (strncmp(entry, strip, sl) != 0) continue;
			rel = entry + sl;
		}
		snprintf(path, sizeof path, "%s/%s", root, rel);
		char norm[1100];
		norm_path(path, norm, sizeof norm);
		if (method != 0 && method != 8) continue;
		if (!zlist_add(l, norm, zip + doff, csize, usize, method)) return 0;
	}
	return 1;
}

/* the folder a game's zip keeps its files in: where its surge.cfg is, or "" */
static void game_strip(const uint8_t *zip, size_t size, char *strip, size_t n)
{
	struct zlist probe = { 0 };
	strip[0] = '\0';
	if (!read_zip(&probe, zip, size, "", NULL)) { free(probe.v); return; }
	size_t best = (size_t)-1;
	for (size_t i = 0; i < probe.count; i++)
	{
		const char *p = probe.v[i].path + 1; /* past the '/' */
		size_t l = strlen(p);
		if (l >= 9 && !strcmp(p + l - 9, "surge.cfg") && (l == 9 || p[l - 10] == '/') && l < best)
		{
			best = l;
			size_t k = l - 9;
			if (k < n) { memcpy(strip, p, k); strip[k] = '\0'; }
		}
	}
	for (size_t i = 0; i < probe.count; i++) free((void *)probe.v[i].path);
	free(probe.v);
}

/* the saves the project starts with: zips of files, laid in the user folder */
static int seed_saves(char *err, int errsize)
{
	char name[256];
	for (int k = 0; wbx_slot_name("savedata", k, name, (int)sizeof name); k++)
	{
		uint8_t *data;
		size_t size;
		if (read_whole(name, &data, &size) != 0) { snprintf(err, (size_t)errsize, "error.zip_could_not_be_read" TEXT_ARG "%s", name); return 0; }
		struct zlist l = { 0 };
		if (!read_zip(&l, data, size, USER_ROOT, NULL)) { snprintf(err, (size_t)errsize, "error.zip_could_not_be_read" TEXT_ARG "%s", name); free(l.v); return 0; }
		/* each entry becomes a file of the machine's own */
		for (size_t i = 0; i < l.count; i++)
		{
			const struct zfile *z = &l.v[i];
			uint8_t *buf = (uint8_t *)malloc(z->usize ? z->usize : 1);
			if (!buf) { free(l.v); return 0; }
			int ok = 0;
			if (z->method == 0) { memcpy(buf, z->data, z->usize); ok = 1; }
			else
			{
				z_stream zs;
				memset(&zs, 0, sizeof zs);
				if (inflateInit2(&zs, -15) == Z_OK)
				{
					zs.next_in = (Bytef *)z->data; zs.avail_in = z->csize; zs.next_out = buf; zs.avail_out = z->usize;
					ok = inflate(&zs, Z_FINISH) == Z_STREAM_END;
					inflateEnd(&zs);
				}
			}
			if (ok)
			{
				struct ufile *u = add_ufile(z->path, 0);
				if (u) { free(u->data); u->data = buf; u->size = u->cap = z->usize; buf = NULL; }
			}
			free(buf);
			free((void *)z->path);
		}
		free(l.v);
	}
	return 1;
}

int osfiles_load(const struct osurge_settings *s, char *err, int errsize)
{
	char name[256];
	g_real_ok = 1;
	g_log = s->verbose;
	{ char v[8] = ""; wbx_setting_str("verboseLog", v, (int)sizeof v); if (!strcmp(v, "2")) g_log = 2; }

	struct zlist list = { 0 };
	/* the game: the project's own (its "game" slot), else Surge the Rabbit's */
	if (wbx_slot_name("game", 0, name, (int)sizeof name))
	{
		uint8_t *data;
		size_t size;
		if (read_whole(name, &data, &size) != 0)
		{
			snprintf(err, (size_t)errsize, "error.zip_could_not_be_read" TEXT_ARG "%s", name);
			return 0;
		}
		char strip[512];
		game_strip(data, size, strip, sizeof strip);
		if (!read_zip(&list, data, size, GAME_ROOT, strip))
		{
			snprintf(err, (size_t)errsize, "error.zip_could_not_be_read" TEXT_ARG "%s", name);
			return 0;
		}
		g_has_game = 1;
	}
	else
	{
		size_t size = (size_t)(_binary_surge_data_zip_end - _binary_surge_data_zip_start);
		if (!read_zip(&list, _binary_surge_data_zip_start, size, SHARE_ROOT, NULL))
		{
			snprintf(err, (size_t)errsize, "error.data_could_not_be_read");
			return 0;
		}
	}

	if (!seed_saves(err, errsize)) return 0;

	/* the file table: sorted, the first of a path kept */
	qsort(list.v, list.count, sizeof *list.v, cmp_zfile);
	size_t kept = 0;
	for (size_t i = 0; i < list.count; i++)
		if (i == 0 || strcmp(list.v[i].path, list.v[i - 1].path) != 0) kept++;
	g_zfiles = (struct zfile *)alloc_sealed((kept ? kept : 1) * sizeof *g_zfiles);
	if (!g_zfiles) { snprintf(err, (size_t)errsize, "error.out_of_memory"); return 0; }
	size_t chars = 0;
	for (size_t i = 0; i < list.count; i++) chars += strlen(list.v[i].path) + 1;
	char *strings = (char *)alloc_sealed(chars ? chars : 1);
	if (!strings) { snprintf(err, (size_t)errsize, "error.out_of_memory"); return 0; }
	size_t k = 0;
	for (size_t i = 0; i < list.count; i++)
	{
		if (i && !strcmp(list.v[i].path, list.v[i - 1].path)) continue;
		size_t l = strlen(list.v[i].path) + 1;
		memcpy(strings, list.v[i].path, l);
		g_zfiles[k] = list.v[i];
		g_zfiles[k].path = strings;
		strings += l;
		k++;
	}
	g_zfile_count = kept;
	for (size_t i = 0; i < list.count; i++) free((void *)list.v[i].path);
	free(list.v);
	g_real_ok = 0;
	return 1;
}

int osfiles_has_game(void) { return g_has_game; }

/* Export Save Data: the files the engine wrote for the game's progress,
 * under its user folder, as the zip entries the person sees (the path from
 * the user folder). The list is made when asked, between steps, in invisible
 * memory: a tool's question leaves the machine as it was. */
#define SAVE_MAX 256
static int *g_save_list;
static int g_save_count;

int osfiles_saves_count(void)
{
	if (!g_save_list) g_save_list = (int *)alloc_invisible(SAVE_MAX * sizeof(int));
	if (!g_save_list) return 0;
	g_save_count = 0;
	for (size_t i = 0; i < g_ufile_count && g_save_count < SAVE_MAX; i++)
	{
		const struct ufile *u = &g_ufiles[i];
		/* what the player made: the saves and the preferences, not the log */
		if (!u->dead && !u->dir && u->size > 0 && !strstr(u->path, "/logs/") && !strstr(u->path, "logfile") && !strstr(u->path, ".log"))
			g_save_list[g_save_count++] = (int)i;
	}
	return g_save_count;
}
const char *osfiles_saves_name(int index)
{
	if (index < 0 || index >= g_save_count) return "";
	return g_ufiles[g_save_list[index]].path + sizeof USER_ROOT;
}
int64_t osfiles_saves_size(int index)
{
	return index < 0 || index >= g_save_count ? 0 : (int64_t)g_ufiles[g_save_list[index]].size;
}
const uint8_t *osfiles_saves_data(int index)
{
	return index < 0 || index >= g_save_count ? NULL : g_ufiles[g_save_list[index]].data;
}
