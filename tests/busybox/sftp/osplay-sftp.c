#define _FILE_OFFSET_BITS 64

#include "osplay-sftp.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    FXP_INIT = 1, FXP_VERSION, FXP_OPEN, FXP_CLOSE, FXP_READ, FXP_WRITE,
    FXP_LSTAT, FXP_FSTAT, FXP_SETSTAT, FXP_FSETSTAT,
    FXP_REALPATH = 16, FXP_STAT,
    FXP_STATUS = 101, FXP_HANDLE, FXP_DATA, FXP_NAME, FXP_ATTRS
};
enum {
    FX_OK = 0, FX_EOF, FX_NO_SUCH_FILE, FX_PERMISSION_DENIED, FX_FAILURE,
    FX_BAD_MESSAGE, FX_UNSUPPORTED = 8
};
enum {
    OPEN_READ = 1, OPEN_WRITE = 2, OPEN_APPEND = 4,
    OPEN_CREATE = 8, OPEN_TRUNCATE = 16, OPEN_EXCLUSIVE = 32
};
enum { ATTR_SIZE = 1, ATTR_OWNER = 2, ATTR_MODE = 4, ATTR_TIME = 8 };

/* OpenSSH sends 32 KiB requests by default. The cap includes the framing
 * and leaves room for 64 KiB writes without tying memory to the file size. */
enum { MAX_DATA = 65536, MAX_PACKET = MAX_DATA + 64, MAX_PATH = 256, MAX_FILES = 4 };

struct sftp_file {
    uint32_t id;
    int fd;
    int writable;
};
struct osplay_sftp {
    const char *home;
    struct sftp_file files[MAX_FILES];
    uint32_t next_id;
    int initialized, failed;
    size_t used, need, out_size, out_pos;
    unsigned char input[MAX_PACKET], output[MAX_PACKET];
};
struct request {
    const unsigned char *pos;
    size_t left;
    int bad;
};

static uint32_t load32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void store32(unsigned char *p, uint32_t n)
{
    p[0] = n >> 24;
    p[1] = n >> 16;
    p[2] = n >> 8;
    p[3] = n;
}

static const unsigned char *take(struct request *req, size_t size)
{
    const unsigned char *p = req->pos;
    if (req->bad || size > req->left) {
        req->bad = 1;
        return NULL;
    }
    req->pos += size;
    req->left -= size;
    return p;
}

static uint32_t get32(struct request *req)
{
    const unsigned char *p = take(req, 4);
    return p ? load32(p) : 0;
}

static uint64_t get64(struct request *req)
{
    uint64_t hi = get32(req);
    return hi << 32 | get32(req);
}

static const unsigned char *get_string(struct request *req, uint32_t *size)
{
    *size = get32(req);
    return take(req, *size);
}

static int get_path(const struct osplay_sftp *sftp, struct request *req, char path[MAX_PATH])
{
    uint32_t size;
    const unsigned char *p = get_string(req, &size);
    size_t prefix = 0;
    if (!p || !size || memchr(p, '\0', size)) return 0;
    if (p[0] != '/') {
        prefix = strlen(sftp->home);
        if (prefix >= MAX_PATH - 1) return 0;
        memcpy(path, sftp->home, prefix);
        path[prefix++] = '/';
    }
    if (size >= MAX_PATH - prefix) return 0;
    memcpy(path + prefix, p, size);
    path[prefix + size] = '\0';
    return 1;
}

static int done(const struct request *req)
{
    return !req->bad && req->left == 0;
}

static void put32(struct osplay_sftp *sftp, uint32_t n)
{
    store32(sftp->output + sftp->out_size, n);
    sftp->out_size += 4;
}

static void put_string(struct osplay_sftp *sftp, const char *p, size_t size)
{
    put32(sftp, size);
    memcpy(sftp->output + sftp->out_size, p, size);
    sftp->out_size += size;
}

static void reply(struct osplay_sftp *sftp, unsigned char type, uint32_t id)
{
    sftp->output[4] = type;
    sftp->out_size = 5;
    put32(sftp, id);
}

static void status(struct osplay_sftp *sftp, uint32_t id, unsigned code)
{
    reply(sftp, FXP_STATUS, id);
    put32(sftp, code);
    put32(sftp, 0); /* message */
    put32(sftp, 0); /* language */
}

static unsigned file_error(void)
{
    switch (errno) {
    case ENOENT: case ENOTDIR: return FX_NO_SUCH_FILE;
    case EACCES: case EPERM: case EROFS: return FX_PERMISSION_DENIED;
    default: return FX_FAILURE;
    }
}

static void attrs(struct osplay_sftp *sftp, const struct stat *st)
{
    const uint64_t size = st->st_size;
    put32(sftp, ATTR_SIZE | ATTR_MODE);
    put32(sftp, size >> 32);
    put32(sftp, size);
    put32(sftp, st->st_mode);
}

static uint32_t get_attrs(struct request *req, uint64_t *size, uint32_t *mode)
{
    uint32_t flags = get32(req);
    if (flags & ~(ATTR_SIZE | ATTR_OWNER | ATTR_MODE | ATTR_TIME)) req->bad = 1;
    if (flags & ATTR_SIZE) *size = get64(req);
    if (flags & ATTR_OWNER) take(req, 8);
    if (flags & ATTR_MODE) *mode = get32(req);
    if (flags & ATTR_TIME) take(req, 8);
    return flags;
}

static struct sftp_file *get_file(struct osplay_sftp *sftp, struct request *req)
{
    uint32_t size;
    const unsigned char *p = get_string(req, &size);
    if (!p || size != 4) return NULL;
    uint32_t id = load32(p);
    for (unsigned i = 0; i < MAX_FILES; ++i)
        if (sftp->files[i].fd >= 0 && sftp->files[i].id == id) return &sftp->files[i];
    return NULL;
}

static unsigned open_file(struct osplay_sftp *sftp, struct request *req, uint32_t id)
{
    char path[MAX_PATH];
    struct stat st;
    struct sftp_file *file = NULL;
    uint64_t size = 0;
    uint32_t mode = 0666;
    if (!get_path(sftp, req, path)) return FX_BAD_MESSAGE;
    uint32_t flags = get32(req);
    get_attrs(req, &size, &mode);
    if (!done(req) || flags & ~63u || !(flags & (OPEN_READ | OPEN_WRITE)) ||
        ((flags & (OPEN_CREATE | OPEN_TRUNCATE | OPEN_APPEND)) && !(flags & OPEN_WRITE)) ||
        ((flags & OPEN_EXCLUSIVE) && !(flags & OPEN_CREATE)))
        return FX_BAD_MESSAGE;
    if (flags & OPEN_APPEND) return FX_UNSUPPORTED;
    for (unsigned i = 0; i < MAX_FILES && !file; ++i)
        if (sftp->files[i].fd < 0) file = &sftp->files[i];
    if (!file || sftp->next_id == UINT32_MAX) return FX_FAILURE;
    if (stat(path, &st) == 0) {
        if (!S_ISREG(st.st_mode)) return FX_PERMISSION_DENIED;
    } else if (errno != ENOENT || !(flags & OPEN_CREATE)) {
        return file_error();
    }
    int oflags = O_NONBLOCK;
    oflags |= (flags & OPEN_WRITE) ? ((flags & OPEN_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (flags & OPEN_CREATE) oflags |= O_CREAT;
    if (flags & OPEN_TRUNCATE) oflags |= O_TRUNC;
    if (flags & OPEN_EXCLUSIVE) oflags |= O_EXCL;
    int fd = open(path, oflags, mode & 0777);
    if (fd < 0) return file_error();
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return FX_FAILURE;
    }
    file->fd = fd;
    file->writable = (flags & OPEN_WRITE) != 0;
    file->id = ++sftp->next_id;
    reply(sftp, FXP_HANDLE, id);
    put32(sftp, 4);
    put32(sftp, file->id);
    return FX_OK;
}

static unsigned read_file(struct osplay_sftp *sftp, struct request *req, uint32_t id)
{
    struct sftp_file *file = get_file(sftp, req);
    uint64_t offset = get64(req);
    uint32_t size = get32(req);
    if (!done(req)) return FX_BAD_MESSAGE;
    if (!file || offset > UINT32_MAX) return FX_FAILURE;
    if (size > MAX_DATA) size = MAX_DATA;
    if (lseek(file->fd, (off_t)offset, SEEK_SET) < 0) return file_error();
    ssize_t count;
    do { count = read(file->fd, sftp->output + 13, size); } while (count < 0 && errno == EINTR);
    if (count < 0) return file_error();
    if (count == 0 && size != 0) return FX_EOF;
    reply(sftp, FXP_DATA, id);
    put32(sftp, count);
    sftp->out_size += count;
    return FX_OK;
}

static unsigned write_file(struct osplay_sftp *sftp, struct request *req)
{
    struct sftp_file *file = get_file(sftp, req);
    uint64_t offset = get64(req);
    uint32_t size;
    const unsigned char *data = get_string(req, &size);
    if (!data || !done(req)) return FX_BAD_MESSAGE;
    if (!file || !file->writable) return FX_PERMISSION_DENIED;
    if (size > MAX_DATA || offset > UINT32_MAX || size > UINT32_MAX - offset)
        return FX_FAILURE;
    if (lseek(file->fd, (off_t)offset, SEEK_SET) < 0) return file_error();
    while (size != 0) {
        ssize_t written = write(file->fd, data, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return written == 0 ? FX_FAILURE : file_error();
        data += written;
        size -= written;
    }
    return FX_OK;
}

static unsigned handle_request(struct osplay_sftp *sftp, unsigned char type,
                               struct request *req, uint32_t id)
{
    struct stat st;
    char path[MAX_PATH];
    switch (type) {
    case FXP_OPEN: return open_file(sftp, req, id);
    case FXP_READ: return read_file(sftp, req, id);
    case FXP_WRITE: return write_file(sftp, req);
    case FXP_CLOSE: case FXP_FSTAT: case FXP_FSETSTAT: {
        struct sftp_file *file = get_file(sftp, req);
        uint64_t size = 0;
        uint32_t mode = 0;
        uint32_t flags = type == FXP_FSETSTAT ? get_attrs(req, &size, &mode) : 0;
        if (!done(req)) return FX_BAD_MESSAGE;
        if (!file) return FX_FAILURE;
        if (type == FXP_CLOSE) {
            int fd = file->fd;
            file->fd = -1;
            return close(fd) == 0 ? FX_OK : file_error();
        }
        if (type == FXP_FSETSTAT) {
            if (flags & ~ATTR_SIZE) return FX_UNSUPPORTED;
            if (!flags) return FX_OK;
            if (!file->writable || size > UINT32_MAX) return FX_FAILURE;
            return ftruncate(file->fd, (off_t)size) == 0 ? FX_OK : file_error();
        }
        if (fstat(file->fd, &st) < 0) return file_error();
        reply(sftp, FXP_ATTRS, id);
        attrs(sftp, &st);
        return FX_OK;
    }
    case FXP_STAT: case FXP_LSTAT:
        if (!get_path(sftp, req, path) || !done(req)) return FX_BAD_MESSAGE;
        if ((type == FXP_STAT ? stat(path, &st) : lstat(path, &st)) < 0) return file_error();
        reply(sftp, FXP_ATTRS, id);
        attrs(sftp, &st);
        return FX_OK;
    case FXP_REALPATH: {
        if (!get_path(sftp, req, path) || !done(req)) return FX_BAD_MESSAGE;
        char *name = realpath(path, NULL);
        if (!name) return file_error();
        const size_t size = strlen(name);
        if (size >= MAX_PATH) {
            free(name);
            return FX_FAILURE;
        }
        reply(sftp, FXP_NAME, id);
        put32(sftp, 1);
        put_string(sftp, name, size);
        put_string(sftp, name, size);
        put32(sftp, 0);
        free(name);
        return FX_OK;
    }
    case FXP_SETSTAT:
    default:
        return FX_UNSUPPORTED;
    }
}

static void dispatch(struct osplay_sftp *sftp)
{
    struct request req = {sftp->input + 5, sftp->need - 5, 0};
    unsigned char type = sftp->input[4];
    uint32_t id = get32(&req); /* INIT carries the client version instead. */
    if (req.bad || (!sftp->initialized && (type != FXP_INIT || id < 3)) ||
        (sftp->initialized && type == FXP_INIT)) {
        sftp->failed = 1;
        return;
    }
    if (!sftp->initialized) {
        sftp->initialized = 1;
        reply(sftp, FXP_VERSION, 3);
    } else {
        unsigned code = handle_request(sftp, type, &req, id);
        if (sftp->out_size == 0) status(sftp, id, code);
    }
    store32(sftp->output, sftp->out_size - 4);
}

struct osplay_sftp *osplay_sftp_new(const char *home)
{
    struct osplay_sftp *sftp = malloc(sizeof(*sftp));
    if (!sftp) return NULL;
    sftp->home = home && home[0] == '/' ? home : "/";
    sftp->next_id = 0;
    sftp->initialized = sftp->failed = 0;
    sftp->used = sftp->out_size = sftp->out_pos = 0;
    sftp->need = 4;
    for (unsigned i = 0; i < MAX_FILES; ++i) sftp->files[i].fd = -1;
    return sftp;
}

void osplay_sftp_free(struct osplay_sftp *sftp)
{
    if (!sftp) return;
    for (unsigned i = 0; i < MAX_FILES; ++i)
        if (sftp->files[i].fd >= 0) close(sftp->files[i].fd);
    free(sftp);
}

size_t osplay_sftp_feed(struct osplay_sftp *sftp, const unsigned char *data, size_t size)
{
    size_t used = 0;
    while (size && !sftp->failed && sftp->out_size == 0) {
        size_t count = sftp->need - sftp->used;
        if (count > size) count = size;
        memcpy(sftp->input + sftp->used, data + used, count);
        used += count;
        size -= count;
        sftp->used += count;
        if (sftp->used < sftp->need) break;
        if (sftp->need == 4) {
            uint32_t length = load32(sftp->input);
            if (length < 5 || length > MAX_PACKET - 4) sftp->failed = 1;
            else sftp->need = 4 + length;
        } else {
            dispatch(sftp);
            sftp->used = 0;
            sftp->need = 4;
        }
    }
    return used;
}

size_t osplay_sftp_output(const struct osplay_sftp *sftp, const unsigned char **data)
{
    *data = sftp->output + sftp->out_pos;
    return sftp->out_size - sftp->out_pos;
}

void osplay_sftp_sent(struct osplay_sftp *sftp, size_t size)
{
    sftp->out_pos += size;
    if (sftp->out_pos == sftp->out_size) sftp->out_pos = sftp->out_size = 0;
}

int osplay_sftp_failed(const struct osplay_sftp *sftp, int eof)
{
    return sftp->failed || (eof && sftp->used != 0);
}
