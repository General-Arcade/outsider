/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "io/file_io.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#define mkdir_p(p) _mkdir(p)
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <dirent.h>
#include <unistd.h>
#define mkdir_p(p) mkdir(p, 0755)
#endif

#define MAX_PATH_LEN 4096

static char s_game_root[MAX_PATH_LEN] = "";

/* Path resolution */

void file_io_set_game_root(const char *root)
{
    if (!root || root[0] == '\0') {
        s_game_root[0] = '\0';
        return;
    }

    /* Resolve to an absolute path so paths derived from the root contain no
       ".." and are not rejected by the traversal check. */
#ifdef _WIN32
    char *resolved = _fullpath(NULL, root, 0);
#else
    char *resolved = realpath(root, NULL);
#endif
    if (resolved) {
        size_t len = strlen(resolved);
        if (len >= MAX_PATH_LEN) len = MAX_PATH_LEN - 1;
        memcpy(s_game_root, resolved, len);
        s_game_root[len] = '\0';
        free(resolved);
    } else {
        size_t len = strlen(root);
        if (len >= MAX_PATH_LEN) len = MAX_PATH_LEN - 1;
        memcpy(s_game_root, root, len);
        s_game_root[len] = '\0';
    }

#ifdef _WIN32
    /* Normalise to forward slashes: Win32 accepts either, and the JS path
       shims always produce '/', which keeps prefix comparisons simple. */
    for (char *c = s_game_root; *c; c++)
        if (*c == '\\') *c = '/';
#endif

    /* Strip trailing separator (but keep a bare drive root like "C:/"). */
    size_t len = strlen(s_game_root);
    while (len > 1 && (s_game_root[len - 1] == '/' || s_game_root[len - 1] == '\\')) {
        if (len == 3 && s_game_root[1] == ':') break;
        len--;
    }
    s_game_root[len] = '\0';
}

/* Path character equality: '/' == '\\', and case-insensitive on Windows. */
static bool path_char_eq(char a, char b)
{
    if (a == '\\') a = '/';
    if (b == '\\') b = '/';
#ifdef _WIN32
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
#endif
    return a == b;
}

static bool path_has_prefix(const char *path, const char *prefix)
{
    while (*prefix) {
        if (*path == '\0' || !path_char_eq(*path, *prefix)) return false;
        path++;
        prefix++;
    }
    return true;
}

const char *file_io_get_game_root(void)
{
    return s_game_root;
}

/* True if any path component is "..". */
static bool path_has_traversal(const char *path)
{
    if (!path) return false;
    const char *p = path;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' &&
            (p[2] == '/' || p[2] == '\\' || p[2] == '\0')) {
            if (p == path || p[-1] == '/' || p[-1] == '\\') {
                return true;
            }
        }
        p++;
    }
    return false;
}

/* Lexically resolve "." and ".." components of a relative path, writing the
   result with '/' separators and no leading or trailing separator. Returns
   false if ".." would climb above the starting directory or the result does
   not fit. Games do request paths like "data/../dataEx/x.json", which stay
   inside the game root and must work. */
static bool normalize_components(const char *in, char *out, size_t out_size)
{
    size_t out_len = 0;
    const char *p = in;
    while (*p) {
        const char *start = p;
        while (*p && *p != '/' && *p != '\\') p++;
        size_t n = (size_t)(p - start);
        if (*p) p++;

        if (n == 0 || (n == 1 && start[0] == '.')) continue;

        if (n == 2 && start[0] == '.' && start[1] == '.') {
            if (out_len == 0) return false;
            while (out_len > 0 && out[out_len - 1] != '/') out_len--;
            if (out_len > 0) out_len--;   /* drop the separator too */
            continue;
        }

        size_t need = out_len + (out_len > 0 ? 1 : 0) + n;
        if (need >= out_size) return false;
        if (out_len > 0) out[out_len++] = '/';
        memcpy(out + out_len, start, n);
        out_len += n;
    }
    out[out_len] = '\0';
    return true;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decode %XX escapes in place-compatible fashion (out may not alias in).
   Sequences that are not two hex digits are copied through unchanged. */
static bool percent_decode(const char *in, char *out, size_t out_size)
{
    size_t o = 0;
    for (const char *p = in; *p; p++) {
        char c = *p;
        if (c == '%' && p[1] && p[2]) {
            int hi = hex_value(p[1]), lo = hex_value(p[2]);
            if (hi >= 0 && lo >= 0) {
                c = (char)((hi << 4) | lo);
                p += 2;
            }
        }
        if (o + 1 >= out_size) return false;
        out[o++] = c;
    }
    out[o] = '\0';
    return true;
}

bool file_io_resolve_path(const char *relative_path, char *out, size_t out_size)
{
    if (!relative_path || !out || out_size == 0) return false;

    /* The engine hands out URLs: Utils.encodeURI() turns "title screen.png"
       into "title%20screen.png". A browser decodes that before touching the
       file system, so do the same here. */
    char decoded[1024];
    if (strchr(relative_path, '%')) {
        if (!percent_decode(relative_path, decoded, sizeof(decoded))) return false;
        relative_path = decoded;
    }

    /* Absolute paths are accepted only inside the game root (when one is set). */
    if (relative_path[0] == '/' || relative_path[0] == '\\' ||
        (relative_path[0] != '\0' && relative_path[1] == ':')) {
        if (s_game_root[0] != '\0') {
            size_t root_len = strlen(s_game_root);
            if (!path_has_prefix(relative_path, s_game_root)) {
                return false;
            }
            /* root="/game" must reject "/game2/...". */
            char next = relative_path[root_len];
            if (next != '\0' && next != '/' && next != '\\') {
                return false;
            }
            if (path_has_traversal(relative_path + root_len)) {
                /* ".." is fine as long as the path never leaves the root. */
                char norm[1024];
                if (!normalize_components(relative_path + root_len, norm, sizeof(norm))) {
                    return false;
                }
                int written = snprintf(out, out_size, "%s/%s", s_game_root, norm);
                return written > 0 && (size_t)written < out_size;
            }
        }
        size_t len = strlen(relative_path);
        if (len >= out_size) return false;
        memcpy(out, relative_path, len + 1);
        return true;
    }

    if (s_game_root[0] != '\0') {
        char norm[1024];
        if (path_has_traversal(relative_path)) {
            /* ".." is fine as long as the path never leaves the root. */
            if (!normalize_components(relative_path, norm, sizeof(norm))) {
                return false;
            }
            relative_path = norm;
        }
        int written = snprintf(out, out_size, "%s/%s", s_game_root, relative_path);
        return written > 0 && (size_t)written < out_size;
    }

    size_t len = strlen(relative_path);
    if (len >= out_size) return false;
    memcpy(out, relative_path, len + 1);
    return true;
}

/* File read */

char *file_io_read_text(const char *path, size_t *out_size)
{
    if (!path) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)fsize + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[nread] = '\0';

    if (out_size) *out_size = nread;
    return buf;
}

uint8_t *file_io_read_binary(const char *path, size_t *out_size)
{
    if (!path || !out_size) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 0) {
        fclose(f);
        return NULL;
    }

    /* +1 so an empty file still yields a non-NULL buffer (malloc(0) may be NULL). */
    uint8_t *buf = malloc((size_t)fsize + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);

    *out_size = nread;
    return buf;
}

/* File write */

bool file_io_write_text(const char *path, const char *data)
{
    if (!path || !data) return false;

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    size_t len = strlen(data);
    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    return written == len;
}

bool file_io_write_binary(const char *path, const uint8_t *data, size_t size)
{
    if (!path || (!data && size > 0)) return false;

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    if (size > 0) {
        size_t written = fwrite(data, 1, size, f);
        fclose(f);
        return written == size;
    }

    fclose(f);
    return true;
}

/* File/directory queries */

/* On Windows, stat("dir/") with a trailing separator fails (except for a
   drive root), so the separator is stripped first. */
static int portable_stat(const char *path, struct stat *st)
{
#ifdef _WIN32
    size_t len = strlen(path);
    if (len > 1 && (path[len - 1] == '/' || path[len - 1] == '\\') &&
        !(len == 3 && path[1] == ':') && len < MAX_PATH_LEN) {
        char tmp[MAX_PATH_LEN];
        memcpy(tmp, path, len - 1);
        tmp[len - 1] = '\0';
        return stat(tmp, st);
    }
#endif
    return stat(path, st);
}

bool file_io_exists(const char *path)
{
    if (!path) return false;
    struct stat st;
    return portable_stat(path, &st) == 0;
}

bool file_io_is_directory(const char *path)
{
    if (!path) return false;
    struct stat st;
    return portable_stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Directory operations */

bool file_io_mkdir(const char *path)
{
    if (!path) return false;

    if (file_io_is_directory(path)) return true;

    char tmp[MAX_PATH_LEN];
    size_t len = strlen(path);
    if (len >= MAX_PATH_LEN) return false;
    memcpy(tmp, path, len + 1);

    /* Create each intermediate component in turn. */
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            tmp[i] = '\0';
#ifdef _WIN32
            /* "C:" is a drive designator rather than a directory. Windows
               reads it as that drive's *current* directory, which does not
               exist when the process is running from another drive: stat()
               then fails with ENOENT and _mkdir() with EACCES, and a game on
               a different drive from the process could not create its save
               folder. The drive itself always exists, so skip it. */
            if (i == 2 && tmp[1] == ':') {
                tmp[i] = '/';
                continue;
            }
#endif
            if (!file_io_is_directory(tmp)) {
                if (mkdir_p(tmp) != 0 && errno != EEXIST) return false;
            }
            tmp[i] = '/';
        }
    }

    if (mkdir_p(tmp) != 0 && errno != EEXIST) return false;
    return file_io_is_directory(path);
}

#ifdef _WIN32
char **file_io_readdir(const char *path, size_t *out_count)
{
    if (!path) return NULL;

    /* FindFirstFile wants a "<path>\*" pattern. */
    size_t plen = strlen(path);
    if (plen == 0 || plen + 3 >= MAX_PATH_LEN) return NULL;
    char pattern[MAX_PATH_LEN];
    memcpy(pattern, path, plen);
    if (pattern[plen - 1] != '/' && pattern[plen - 1] != '\\')
        pattern[plen++] = '\\';
    pattern[plen++] = '*';
    pattern[plen] = '\0';

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    size_t count = 0, cap = 32;
    char **list = calloc(cap + 1, sizeof(char *));
    if (!list) {
        FindClose(h);
        return NULL;
    }

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        if (count == cap) {
            size_t new_cap = cap * 2;
            char **grown = realloc(list, (new_cap + 1) * sizeof(char *));
            if (!grown) goto fail;
            list = grown;
            cap = new_cap;
        }
        list[count] = strdup(fd.cFileName);
        if (!list[count]) goto fail;
        count++;
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    list[count] = NULL;
    if (out_count) *out_count = count;
    return list;

fail:
    for (size_t j = 0; j < count; j++) free(list[j]);
    free(list);
    FindClose(h);
    return NULL;
}
#else
char **file_io_readdir(const char *path, size_t *out_count)
{
    if (!path) return NULL;

    DIR *d = opendir(path);
    if (!d) return NULL;

    /* First pass counts entries so the array can be sized exactly. */
    size_t count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        count++;
    }

    rewinddir(d);

    char **list = calloc(count + 1, sizeof(char *));
    if (!list) {
        closedir(d);
        return NULL;
    }

    size_t idx = 0;
    while ((ent = readdir(d)) != NULL && idx < count) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        list[idx] = strdup(ent->d_name);
        if (!list[idx]) {
            for (size_t j = 0; j < idx; j++) free(list[j]);
            free(list);
            closedir(d);
            return NULL;
        }
        idx++;
    }
    list[idx] = NULL;
    closedir(d);

    if (out_count) *out_count = idx;
    return list;
}
#endif /* _WIN32 */

void file_io_free_dirlist(char **list)
{
    if (!list) return;
    for (size_t i = 0; list[i] != NULL; i++) {
        free(list[i]);
    }
    free(list);
}

bool file_io_unlink(const char *path)
{
    if (!path) return false;
#ifdef _WIN32
    return _unlink(path) == 0;
#else
    return unlink(path) == 0;
#endif
}

bool file_io_rename(const char *old_path, const char *new_path)
{
    if (!old_path || !new_path) return false;
    return rename(old_path, new_path) == 0;
}
