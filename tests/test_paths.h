/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_TEST_PATHS_H
#define RMMZ_TEST_PATHS_H

/*
 * Portable scratch-directory helpers shared by the test executables.
 * Resolves a per-platform temp root, builds paths under it, and creates or
 * removes directory trees without spawning a shell. Paths always use forward
 * slashes, matching the JS shims.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <dirent.h>
#include <unistd.h>
#endif

#define TEST_PATH_MAX 1024

/* Directory of a real RPG Maker MZ game for integration tests, taken from the
   RMMZ_TEST_GAME_DIR environment variable. NULL when unset; tests that need
   it should skip. */
static inline const char *test_game_dir(void)
{
    const char *d = getenv("RMMZ_TEST_GAME_DIR");
    return (d && d[0] != '\0') ? d : NULL;
}

/* Temp root without trailing separator, e.g. "/tmp" or "C:/Users/me/AppData/Local/Temp". */
static inline const char *test_tmp_root(void)
{
    static char root[TEST_PATH_MAX] = "";
    if (root[0] != '\0') return root;

#ifdef _WIN32
    DWORD n = GetTempPathA((DWORD)sizeof(root), root);
    if (n == 0 || n >= sizeof(root)) {
        strcpy(root, "C:/Temp");
    }
    for (char *c = root; *c; c++) {
        if (*c == '\\') *c = '/';
    }
#else
    const char *env = getenv("TMPDIR");
    if (!env || env[0] == '\0') env = "/tmp";
    snprintf(root, sizeof(root), "%s", env);
#endif

    size_t len = strlen(root);
    while (len > 1 && root[len - 1] == '/') {
        root[--len] = '\0';
    }
    return root;
}

/* "<tmp root>/<rel>" in one of a few rotating static buffers, so several
   calls can appear in a single expression or statement sequence. */
static inline const char *test_tmp_path(const char *rel)
{
    static char bufs[8][TEST_PATH_MAX];
    static int next = 0;
    char *buf = bufs[next];
    next = (next + 1) % 8;
    snprintf(buf, TEST_PATH_MAX, "%s/%s", test_tmp_root(), rel);
    return buf;
}

static inline int test_path_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Create a directory and any missing parents. Returns 1 on success. */
static inline int test_mkdir_p(const char *path)
{
    char tmp[TEST_PATH_MAX];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return 0;
    memcpy(tmp, path, len + 1);

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            tmp[i] = '\0';
            /* Skip drive roots like "C:" */
            if (!(i == 2 && tmp[1] == ':')) {
#ifdef _WIN32
                _mkdir(tmp);
#else
                mkdir(tmp, 0755);
#endif
            }
            tmp[i] = '/';
        }
    }
#ifdef _WIN32
    _mkdir(tmp);
#else
    mkdir(tmp, 0755);
#endif
    return test_path_is_dir(path);
}

/* Recursively delete a file or directory tree. Missing paths are ignored. */
static inline void test_rm_rf(const char *path)
{
    if (!path || path[0] == '\0') return;

    if (!test_path_is_dir(path)) {
        remove(path);
        return;
    }

#ifdef _WIN32
    char pattern[TEST_PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s/*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            char child[TEST_PATH_MAX];
            snprintf(child, sizeof(child), "%s/%s", path, fd.cFileName);
            test_rm_rf(child);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    _rmdir(path);
#else
    DIR *d = opendir(path);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child[TEST_PATH_MAX];
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            test_rm_rf(child);
        }
        closedir(d);
    }
    rmdir(path);
#endif
}

#endif /* RMMZ_TEST_PATHS_H */
