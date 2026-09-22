/* Temporary diagnostic: mimics file_io_mkdir's component walk and reports the
   stat/mkdir result and errno for each component, to find which one fails. */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <direct.h>
#include <windows.h>

static int is_dir(const char *p)
{
    struct stat st;
    errno = 0;
    int r = stat(p, &st);
    printf("      stat(\"%s\") -> %d errno=%d%s\n", p, r, r ? errno : 0,
           (r == 0 && (st.st_mode & _S_IFDIR)) ? " [DIR]" : "");
    return r == 0 && (st.st_mode & _S_IFDIR);
}

int main(void)
{
    char cwd[1024];
    _getcwd(cwd, sizeof(cwd));
    printf("cwd = \"%s\"\n", cwd);

    char root[1024];
    DWORD n = GetTempPathA((DWORD)sizeof(root), root);
    printf("GetTempPathA -> %lu \"%s\"\n", (unsigned long)n, root);

    for (char *c = root; *c; c++) if (*c == '\\') *c = '/';
    size_t len = strlen(root);
    while (len > 1 && root[len - 1] == '/') root[--len] = '\0';
    printf("root = \"%s\"\n", root);

    char path[1024];
    snprintf(path, sizeof(path), "%s/rmmz_probe_dir", root);
    printf("target = \"%s\"\n\n", path);

    printf("-- component walk --\n");
    char tmp[1024];
    len = strlen(path);
    memcpy(tmp, path, len + 1);

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            tmp[i] = '\0';
            printf("  component[%zu] = \"%s\"\n", i, tmp);
            if (!is_dir(tmp)) {
                errno = 0;
                int r = _mkdir(tmp);
                printf("      _mkdir -> %d errno=%d\n", r, errno);
                if (r != 0 && errno != EEXIST) {
                    printf("  >>> file_io_mkdir would RETURN FALSE here\n");
                    return 1;
                }
            }
            tmp[i] = '/';
        }
    }

    errno = 0;
    int r = _mkdir(tmp);
    printf("  final _mkdir(\"%s\") -> %d errno=%d\n", tmp, r, errno);
    printf("  final is_dir -> %d\n", is_dir(path));
    _rmdir(path);
    return 0;
}
