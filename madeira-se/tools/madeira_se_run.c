/*
 * Madeira-SE standalone Wine launcher.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se.h"

#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s --host-dir DIR --guest-dir DIR --qemu LIB "
            "--runtime LIB --prefix DIR [--dxmt-dir DIR] [--workdir DIR] "
            "[--appdata-dir DIR] [--d3d9-backend auto|dxmt|wined3d] "
            "[--d3d9-virtual-mode WxH] "
            "[--window-size WxH] [--fps-cap FPS] "
            "[--desktop] EXE [ARG ...]\n",
            program);
}

static int valid_d3d9_virtual_mode(const char *mode)
{
    uint32_t width, height;

    if (mode == NULL || !strcmp(mode, "1")) return 1;
    return madeira_se_parse_window_size(mode, &width, &height) == MADEIRA_SE_OK;
}

static int valid_d3d9_backend(const char *backend)
{
    return backend == NULL || !strcmp(backend, "auto") ||
           !strcmp(backend, "dxmt") || !strcmp(backend, "wined3d");
}

static int valid_fps_cap(const char *value)
{
    char *end;
    unsigned long cap;

    if (value == NULL || value[0] == '\0') return 0;
    errno = 0;
    cap = strtoul(value, &end, 10);
    return errno == 0 && end != value && *end == '\0' && cap <= 240ul;
}

static int regular_file(const char *path)
{
    struct stat info;

    return path != NULL && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static int directory(const char *path)
{
    struct stat info;

    return path != NULL && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

/* Wine changes its internal loader working directory while bootstrapping a
 * prefix.  Resolve command-line resources before exec so a direct invocation
 * from the repository also works with relative paths. */
static int resolve_existing_path(const char *path, char *resolved,
                                 size_t capacity)
{
    char *real_path;
    size_t length;

    if (path == NULL || resolved == NULL || capacity == 0u) return 0;
    real_path = realpath(path, NULL);
    if (real_path == NULL) return 0;
    length = strlen(real_path);
    if (length >= capacity) {
        free(real_path);
        return 0;
    }
    memcpy(resolved, real_path, length + 1u);
    free(real_path);
    return 1;
}

static int ensure_directory(const char *path)
{
    char *copy;
    char *cursor;
    int result = 0;

    if (directory(path)) return 1;
    if (path == NULL || path[0] == '\0') return 0;
    copy = strdup(path);
    if (copy == NULL) return 0;
    for (cursor = copy + 1; *cursor != '\0'; ++cursor) {
        if (*cursor != '/') continue;
        *cursor = '\0';
        if (copy[0] != '\0' && mkdir(copy, 0700) != 0 && errno != EEXIST)
            goto done;
        *cursor = '/';
    }
    if (mkdir(copy, 0700) != 0 && errno != EEXIST) goto done;
    result = directory(path);
done:
    free(copy);
    return result;
}

static int join_path(char *destination, size_t capacity,
                     const char *directory_name, const char *basename);

/* Convert a host path to Wine's Z: mapping.  The standalone launcher maps
 * the host filesystem through dosdevices/z:, so this keeps game-owned data
 * beside the game while still presenting the Windows path expected by the
 * application. */
static int host_path_to_wine_path(const char *host_path, char *wine_path,
                                  size_t capacity)
{
    size_t source_index, destination_index = 0u;

    if (host_path == NULL || wine_path == NULL || capacity < 3u ||
        host_path[0] != '/')
        return 0;
    wine_path[destination_index++] = 'Z';
    wine_path[destination_index++] = ':';
    for (source_index = 0u; host_path[source_index] != '\0'; ++source_index)
    {
        if (destination_index + 1u >= capacity) return 0;
        wine_path[destination_index++] = host_path[source_index] == '/' ?
                                         '\\' : host_path[source_index];
    }
    wine_path[destination_index] = '\0';
    return 1;
}

static int read_entire_file(const char *path, char **contents, size_t *size)
{
    struct stat info;
    char *buffer;
    size_t offset = 0u;
    int descriptor;

    if (path == NULL || contents == NULL || size == NULL ||
        stat(path, &info) != 0 || !S_ISREG(info.st_mode) ||
        (uintmax_t)info.st_size > SIZE_MAX - 1u)
        return 0;
    descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return 0;
    buffer = malloc((size_t)info.st_size + 1u);
    if (buffer == NULL) {
        close(descriptor);
        return 0;
    }
    while (offset < (size_t)info.st_size)
    {
        ssize_t count = read(descriptor, buffer + offset,
                             (size_t)info.st_size - offset);
        if (count <= 0) {
            free(buffer);
            close(descriptor);
            return 0;
        }
        offset += (size_t)count;
    }
    close(descriptor);
    buffer[offset] = '\0';
    *contents = buffer;
    *size = offset;
    return 1;
}

/* A D3D9 title often imports d3dx9_*.dll and calls Direct3DCreate9 through
 * that helper instead of importing d3d9.dll directly.  Keep the launcher
 * policy independent of a full PE import parser: the import table stores DLL
 * names as ASCII strings, so a bounded case-insensitive scan is sufficient to
 * decide whether the D3D9 compatibility defaults should be enabled.  An
 * explicit --d3d9-backend still overrides this heuristic. */
static int file_contains_ascii_ci(const char *data, size_t size,
                                  const char *needle)
{
    size_t needle_size;
    size_t index;

    if (data == NULL || needle == NULL || needle[0] == '\0') return 0;
    needle_size = strlen(needle);
    if (needle_size > size) return 0;
    for (index = 0u; index + needle_size <= size; ++index) {
        size_t n;
        for (n = 0u; n < needle_size; ++n) {
            unsigned char left = (unsigned char)data[index + n];
            unsigned char right = (unsigned char)needle[n];
            if (tolower(left) != tolower(right)) break;
        }
        if (n == needle_size) return 1;
    }
    return 0;
}

static int executable_references_d3d9(const char *path)
{
    char *contents = NULL;
    size_t size = 0u;
    int result;

    if (!read_entire_file(path, &contents, &size)) return 0;
    result = file_contains_ascii_ci(contents, size, "d3d9") ||
             file_contains_ascii_ci(contents, size, "d3dx9");
    free(contents);
    return result;
}

static int replace_text_range(char **contents, size_t *size,
                              size_t start, size_t end,
                              const char *replacement)
{
    size_t replacement_size;
    size_t old_size;
    size_t new_size;
    char *updated;

    if (contents == NULL || *contents == NULL || size == NULL ||
        start > end || end > *size || replacement == NULL)
        return 0;
    replacement_size = strlen(replacement);
    old_size = *size;
    if (end - start > old_size ||
        replacement_size > SIZE_MAX - (old_size - (end - start)))
        return 0;
    new_size = old_size - (end - start) + replacement_size;
    if (new_size == SIZE_MAX) return 0;
    updated = malloc(new_size + 1u);
    if (updated == NULL) return 0;
    memcpy(updated, *contents, start);
    memcpy(updated + start, replacement, replacement_size);
    memcpy(updated + start + replacement_size, *contents + end,
           old_size - end);
    updated[new_size] = '\0';
    free(*contents);
    *contents = updated;
    *size = new_size;
    return 1;
}

/* Set one REG_SZ value in a Wine text registry.  Updating the existing line
 * instead of blindly appending duplicate sections matters for prefixes that
 * already ran once: wineserver will otherwise retain the older value. */
static int registry_set_text_value(char **contents, size_t *size,
                                   const char *section, const char *name,
                                   const char *value)
{
    char header[PATH_MAX];
    char value_line[PATH_MAX * 2u];
    const char *section_start;
    const char *section_end;
    const char *cursor;
    size_t header_size;
    size_t name_size;
    int length;

    if (contents == NULL || *contents == NULL || size == NULL ||
        section == NULL || value == NULL)
        return 0;
    length = snprintf(header, sizeof(header), "[%s]", section);
    if (length < 0 || (size_t)length >= sizeof(header)) return 0;
    header_size = (size_t)length;
    /* A NULL name denotes the key's default value.  Wine's registry text
     * format writes that value as @=..., while named values use quotes. */
    if (name == NULL)
        length = snprintf(value_line, sizeof(value_line), "@=\"%s\"\n", value);
    else
        length = snprintf(value_line, sizeof(value_line), "\"%s\"=\"%s\"\n",
                          name, value);
    if (length < 0 || (size_t)length >= sizeof(value_line)) return 0;
    name_size = name != NULL ? strlen(name) + 3u : 2u;

    section_start = strstr(*contents, header);
    while (section_start != NULL && section_start != *contents &&
           section_start[-1] != '\n')
        section_start = strstr(section_start + 1u, header);
    if (section_start == NULL)
    {
        char section_block[PATH_MAX * 2u];

        length = snprintf(section_block, sizeof(section_block),
                          "\n%s\n%s", header, value_line);
        if (length < 0 || (size_t)length >= sizeof(section_block)) return 0;
        return replace_text_range(contents, size, *size, *size,
                                  section_block);
    }

    section_end = strstr(section_start + header_size, "\n[");
    if (section_end == NULL) section_end = *contents + *size;
    cursor = section_start + header_size;
    while (cursor < section_end)
    {
        const char *line_start = cursor;
        const char *line_end;

        if (*cursor == '\n') ++line_start;
        if (line_start >= section_end) break;
        line_end = memchr(line_start, '\n', (size_t)(section_end - line_start));
        if (line_end == NULL) line_end = section_end;
        if (name == NULL ?
            ((size_t)(line_end - line_start) >= name_size &&
             line_start[0] == '@' && line_start[1] == '=') :
            ((size_t)(line_end - line_start) >= name_size &&
             line_start[0] == '"' &&
             !strncmp(line_start + 1u, name, strlen(name)) &&
             line_start[strlen(name) + 1u] == '"' &&
             line_start[strlen(name) + 2u] == '='))
            return replace_text_range(contents, size,
                                      (size_t)(line_start - *contents),
                                      (size_t)(line_end - *contents) +
                                          (line_end < *contents + *size),
                                      value_line);
        cursor = line_end < section_end ? line_end + 1u : section_end;
    }

    /* Insert before the next section.  Existing registry files conventionally
     * have a blank line there; inserting after the first newline preserves it. */
    {
        size_t insertion = (size_t)(section_end - *contents);
        if (insertion < *size && (*contents)[insertion] == '\n') ++insertion;
        return replace_text_range(contents, size, insertion, insertion,
                                  value_line);
    }
}

static int write_entire_file(const char *path, const char *contents, size_t size)
{
    char temporary[PATH_MAX];
    int descriptor;
    size_t offset = 0u;

    {
        size_t length = strlen(path);
        if (length + strlen(".madeira-tmp") + 1u >= sizeof(temporary))
            return 0;
        snprintf(temporary, sizeof(temporary), "%s.madeira-tmp", path);
    }
    descriptor = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0) return 0;
    while (offset < size)
    {
        ssize_t count = write(descriptor, contents + offset, size - offset);
        if (count <= 0) {
            close(descriptor);
            unlink(temporary);
            return 0;
        }
        offset += (size_t)count;
    }
    if (close(descriptor) != 0 || rename(temporary, path) != 0) {
        unlink(temporary);
        return 0;
    }
    return 1;
}

/* The standalone launcher intentionally skips wineboot's registration pass.
 * DxDiag is a builtin DLL, but its COM class still needs the normal registry
 * entries before CoCreateInstance can find it.  Register the small set of
 * builtin classes used while DxDiag builds its SystemInfo tree directly in
 * system.reg.  This keeps startup deterministic and avoids launching a
 * visible regsvr32 helper for every game. */
static int configure_dxdiag_registry(const char *prefix)
{
    static const struct {
        const char *section;
        const char *name;
        const char *value;
    } values[] = {
        { "Software\\\\Classes\\\\CLSID\\\\{4590F811-1D3A-11D0-891F-00AA004B2E24}",
          NULL, "WBEM Locator" },
        { "Software\\\\Classes\\\\CLSID\\\\{4590F811-1D3A-11D0-891F-00AA004B2E24}\\\\InprocServer32",
          NULL, "C:\\\\windows\\\\system32\\\\wbemprox.dll" },
        { "Software\\\\Classes\\\\CLSID\\\\{4590F811-1D3A-11D0-891F-00AA004B2E24}\\\\InprocServer32",
          "ThreadingModel", "Both" },
        { "Software\\\\Classes\\\\CLSID\\\\{674B6698-EE92-11D0-AD71-00C04FD8FDFF}",
          NULL, "WBEM Call Context" },
        { "Software\\\\Classes\\\\CLSID\\\\{674B6698-EE92-11D0-AD71-00C04FD8FDFF}\\\\InprocServer32",
          NULL, "C:\\\\windows\\\\system32\\\\wbemprox.dll" },
        { "Software\\\\Classes\\\\CLSID\\\\{674B6698-EE92-11D0-AD71-00C04FD8FDFF}\\\\InprocServer32",
          "ThreadingModel", "Both" },
        { "Software\\\\Classes\\\\CLSID\\\\{CB8555CC-9128-11D1-AD9B-00C04FD8FDFF}",
          NULL, "WBEM Administrative Locator" },
        { "Software\\\\Classes\\\\CLSID\\\\{CB8555CC-9128-11D1-AD9B-00C04FD8FDFF}\\\\InprocServer32",
          NULL, "C:\\\\windows\\\\system32\\\\wbemprox.dll" },
        { "Software\\\\Classes\\\\CLSID\\\\{CB8555CC-9128-11D1-AD9B-00C04FD8FDFF}\\\\InprocServer32",
          "ThreadingModel", "Both" },
        { "Software\\\\Classes\\\\CLSID\\\\{A65B8071-3BFE-4213-9A5B-491DA4461CA7}",
          NULL, "DxDiagProvider Class" },
        { "Software\\\\Classes\\\\CLSID\\\\{A65B8071-3BFE-4213-9A5B-491DA4461CA7}\\\\InprocServer32",
          NULL, "C:\\\\windows\\\\system32\\\\dxdiagn.dll" },
        { "Software\\\\Classes\\\\CLSID\\\\{A65B8071-3BFE-4213-9A5B-491DA4461CA7}\\\\InprocServer32",
          "ThreadingModel", "Apartment" },
        { "Software\\\\Classes\\\\CLSID\\\\{A65B8071-3BFE-4213-9A5B-491DA4461CA7}\\\\ProgId",
          NULL, "DxDiag.DxDiagProvider.1" },
        { "Software\\\\Classes\\\\CLSID\\\\{A65B8071-3BFE-4213-9A5B-491DA4461CA7}\\\\VersionIndependentProgId",
          NULL, "DxDiag.DxDiagProvider" }
    };
    char registry_path[PATH_MAX];
    char *contents = NULL;
    size_t size = 0u;
    size_t i;

    if (!join_path(registry_path, sizeof(registry_path), prefix, "system.reg"))
        return 0;
    if (regular_file(registry_path)) {
        if (!read_entire_file(registry_path, &contents, &size)) return 0;
    } else {
        static const char registry_header[] =
            "WINE REGISTRY Version 2\n"
            ";; All keys relative to REGISTRY\\\\Machine\n\n"
            "#arch=win64\n\n";
        contents = strdup(registry_header);
        if (contents == NULL) return 0;
        size = strlen(registry_header);
    }
    for (i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
        if (!registry_set_text_value(&contents, &size, values[i].section,
                                     values[i].name, values[i].value)) {
            free(contents);
            return 0;
        }
    }
    if (!write_entire_file(registry_path, contents, size)) {
        free(contents);
        return 0;
    }
    free(contents);
    return 1;
}

static int configure_appdata_registry(const char *prefix,
                                      const char *appdata_directory,
                                      char *wine_path, size_t wine_capacity)
{
    static const struct {
        const char *section;
        const char *name;
    } values[] = {
        { "Environment", "APPDATA" },
        { "Volatile Environment", "APPDATA" },
        { "Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\Shell Folders", "AppData" },
        { "Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\User Shell Folders", "AppData" },
        { "Software\\\\Wow6432Node\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\Shell Folders", "AppData" },
        { "Software\\\\Wow6432Node\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\User Shell Folders", "AppData" }
    };
    char registry_path[PATH_MAX];
    char escaped_path[PATH_MAX * 2u];
    char *contents = NULL;
    size_t size = 0u;
    size_t escaped_size = 0u;
    size_t i;

    if (!directory(appdata_directory) ||
        access(appdata_directory, R_OK | W_OK) != 0 ||
        !host_path_to_wine_path(appdata_directory, wine_path, wine_capacity) ||
        !join_path(registry_path, sizeof(registry_path), prefix, "user.reg"))
        return 0;
    for (i = 0u; wine_path[i] != '\0'; ++i)
    {
        if (escaped_size + (wine_path[i] == '\\' ? 2u : 1u) + 1u >=
            sizeof(escaped_path))
            return 0;
        if (wine_path[i] == '\\') escaped_path[escaped_size++] = '\\';
        escaped_path[escaped_size++] = wine_path[i];
    }
    escaped_path[escaped_size] = '\0';

    if (regular_file(registry_path)) {
        if (!read_entire_file(registry_path, &contents, &size)) return 0;
    } else {
        static const char registry_header[] = "WINE REGISTRY Version 2\n\n";
        contents = strdup(registry_header);
        if (contents == NULL) return 0;
        size = strlen(registry_header);
    }
    for (i = 0u; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        if (!registry_set_text_value(&contents, &size, values[i].section,
                                     values[i].name, escaped_path)) {
            free(contents);
            return 0;
        }
    }
    if (!write_entire_file(registry_path, contents, size)) {
        free(contents);
        return 0;
    }
    free(contents);
    return 1;
}

static int path_directory(const char *path, char *directory_path,
                          size_t capacity)
{
    const char *separator;
    size_t length;

    if (path == NULL || directory_path == NULL || capacity == 0u) return 0;
    separator = strrchr(path, '/');
    if (separator == NULL) return 0;
    length = separator == path ? 1u : (size_t)(separator - path);
    if (length >= capacity) return 0;
    memcpy(directory_path, path, length);
    directory_path[length] = '\0';
    return 1;
}

static int join_path(char *destination, size_t capacity,
                     const char *directory_name, const char *basename)
{
    int length;
    const char *separator;
    size_t directory_length;

    if (destination == NULL || capacity == 0u || directory_name == NULL ||
        basename == NULL)
        return 0;
    directory_length = strlen(directory_name);
    separator = directory_length != 0u &&
                directory_name[directory_length - 1u] == '/' ? "" : "/";
    length = snprintf(destination, capacity, "%s%s%s", directory_name,
                      separator, basename);
    return length >= 0 && (size_t)length < capacity;
}

static int prepend_environment_path(const char *name, const char *entry)
{
    const char *current;
    char *value;
    size_t length;
    int result;

    if (!directory(entry)) return 1;
    current = getenv(name);
    if (current == NULL || current[0] == '\0') return setenv(name, entry, 1) == 0;
    if (!strcmp(current, entry) ||
        (!strncmp(current, entry, strlen(entry)) && current[strlen(entry)] == ':'))
        return 1;
    if (strlen(entry) > SIZE_MAX - strlen(current) - 2u) return 0;
    length = strlen(entry) + strlen(current) + 2u;
    value = malloc(length);
    if (value == NULL) return 0;
    result = snprintf(value, length, "%s:%s", entry, current);
    if (result < 0 || (size_t)result >= length) result = 0;
    else result = setenv(name, value, 1) == 0;
    free(value);
    return result;
}

static const char *architecture_name(madeira_se_architecture_t architecture)
{
    return architecture == MADEIRA_SE_ARCH_X86_64 ? "x86_64" : "i386";
}

static int guest_runtime_file(const char *path)
{
    static const char *const extensions[] = {
        ".acm", ".ax", ".cpl", ".dll", ".dll16", ".drv", ".exe",
        ".exe16", ".ocx", ".sys", ".tlb", ".vxd", ".winmd"
    };
    const char *basename = strrchr(path, '/');
    const char *extension;
    size_t i;

    basename = basename ? basename + 1 : path;
    extension = strrchr(basename, '.');
    if (extension == NULL) return 0;
    for (i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i)
        if (!strcasecmp(extension, extensions[i])) return 1;
    return 0;
}

static int stage_guest_file(const char *source, const char *destination_dir)
{
    const char *basename = strrchr(source, '/');
    char destination[PATH_MAX];
    char resolved_source[PATH_MAX];
    struct stat info;

    basename = basename ? basename + 1 : source;
    if (!join_path(destination, sizeof(destination), destination_dir, basename)) return 0;
    if (lstat(destination, &info) == 0) {
        if (!S_ISLNK(info.st_mode) || stat(destination, &info) == 0) return 1;
        if (errno != ENOENT || unlink(destination) != 0) return 0;
    }
    else if (errno != ENOENT) return 0;
    if (realpath(source, resolved_source) == NULL) return 0;
    return symlink(resolved_source, destination) == 0 || errno == EEXIST;
}

static int stage_guest_alias(const char *source, const char *destination_dir,
                             const char *alias)
{
    char destination[PATH_MAX];
    char resolved_source[PATH_MAX];
    char existing_source[PATH_MAX];
    struct stat info;

    if (!join_path(destination, sizeof(destination), destination_dir, alias) ||
        realpath(source, resolved_source) == NULL)
        return 0;
    if (realpath(destination, existing_source) != NULL) {
        struct stat source_info, existing_info, existing_link;
        if (lstat(destination, &existing_link) != 0 || S_ISLNK(existing_link.st_mode))
            goto replace_alias;
        if (stat(resolved_source, &source_info) == 0 &&
            stat(destination, &existing_info) == 0 &&
            source_info.st_size == existing_info.st_size &&
            !strcmp(existing_source, resolved_source))
            return 1;
    }
replace_alias:
    if (lstat(destination, &info) == 0) {
        if (unlink(destination) != 0) return 0;
    } else if (errno != ENOENT) return 0;
    {
        int source_fd = open(resolved_source, O_RDONLY);
        int destination_fd;
        char buffer[65536];
        ssize_t bytes_read;
        int ok = source_fd >= 0;

        if (!ok) return 0;
        destination_fd = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0700);
        if (destination_fd < 0) {
            close(source_fd);
            return 0;
        }
        while ((bytes_read = read(source_fd, buffer, sizeof(buffer))) > 0) {
            ssize_t offset = 0;
            while (offset < bytes_read) {
                ssize_t written = write(destination_fd, buffer + offset,
                                         (size_t)(bytes_read - offset));
                if (written <= 0) {
                    ok = 0;
                    break;
                }
                offset += written;
            }
            if (!ok) break;
        }
        if (bytes_read < 0) ok = 0;
        close(destination_fd);
        close(source_fd);
        return ok;
    }
}

static int stage_guest_override(const char *source, const char *destination_dir)
{
    const char *basename = strrchr(source, '/');
    char destination[PATH_MAX];
    char resolved_source[PATH_MAX];
    char existing_source[PATH_MAX];
    struct stat info;

    basename = basename ? basename + 1 : source;
    if (!join_path(destination, sizeof(destination), destination_dir, basename) ||
        realpath(source, resolved_source) == NULL)
        return 0;
    if (realpath(destination, existing_source) != NULL &&
        !strcmp(existing_source, resolved_source))
        return 1;
    if (lstat(destination, &info) == 0)
    {
        if (unlink(destination) != 0) return 0;
    }
    else if (errno != ENOENT) return 0;
    return symlink(resolved_source, destination) == 0;
}

static int stage_dxmt_system(const char *dxmt_dir, const char *prefix,
                             madeira_se_architecture_t architecture,
                             int include_d3d9)
{
    static const char *const modules[] = {
        "d3d10core.dll", "d3d11.dll", "dxgi.dll", "winemetal.dll"
    };
    const char *machine = architecture == MADEIRA_SE_ARCH_X86_64 ?
                          "x86_64-windows" : "i386-windows";
    char windows_dir[PATH_MAX];
    char system32[PATH_MAX];
    char syswow64[PATH_MAX];
    char machine_dir[PATH_MAX];
    char source[PATH_MAX];
    size_t i;

    if (!join_path(windows_dir, sizeof(windows_dir), prefix, "drive_c/windows") ||
        !join_path(system32, sizeof(system32), windows_dir, "system32") ||
        !join_path(syswow64, sizeof(syswow64), windows_dir, "syswow64") ||
        !join_path(machine_dir, sizeof(machine_dir), dxmt_dir, machine))
        return 0;
    for (i = 0; i < sizeof(modules) / sizeof(modules[0]); ++i)
    {
        if (!join_path(source, sizeof(source), machine_dir, modules[i]) ||
            !regular_file(source) || !stage_guest_override(source, system32) ||
            !stage_guest_override(source, syswow64))
            return 0;
    }
    if (include_d3d9)
    {
        if (!join_path(source, sizeof(source), machine_dir, "d3d9.dll") ||
            !regular_file(source) || !stage_guest_override(source, system32) ||
            !stage_guest_override(source, syswow64))
            return 0;
    }
    return 1;
}

static int configure_dxmt_overrides(int include_d3d9)
{
    static const char base_overrides[] =
        /* DXMT's PE side is a Wine builtin so that Wine associates it with
         * the ARM64 winemetal.so Unix bridge.  A native-first override loads
         * the PE without that association and leaves the dispatcher unset. */
        "d3d10core,d3d11,dxgi,winemetal=b";
    static const char d3d9_overrides[] =
        "d3d9,d3d10core,d3d11,dxgi,winemetal=b";
    const char *overrides = include_d3d9 ? d3d9_overrides : base_overrides;
    const char *current = getenv("WINEDLLOVERRIDES");
    char *combined;
    size_t length;
    int result;

    if (current == NULL || current[0] == '\0')
        return setenv("WINEDLLOVERRIDES", overrides, 1) == 0;
    if (strstr(current, "d3d11") != NULL &&
        strstr(current, "winemetal") != NULL &&
        (!include_d3d9 || strstr(current, "d3d9") != NULL))
        return 1;
    if (strlen(overrides) > SIZE_MAX - strlen(current) - 2u) return 0;
    length = strlen(overrides) + strlen(current) + 2u;
    combined = malloc(length);
    if (combined == NULL) return 0;
    result = snprintf(combined, length, "%s;%s", overrides, current);
    if (result < 0 || (size_t)result >= length) result = 0;
    else result = setenv("WINEDLLOVERRIDES", combined, 1) == 0;
    free(combined);
    return result;
}

static int stage_guest_builtin_module(const char *guest_dir,
                                      const char *prefix,
                                      madeira_se_architecture_t architecture,
                                      const char *module_directory,
                                      const char *module)
{
    const char *machine = architecture == MADEIRA_SE_ARCH_X86_64 ?
                          "x86_64-windows" : "i386-windows";
    char dlls_dir[PATH_MAX];
    char source_dir[PATH_MAX];
    char machine_dir[PATH_MAX];
    char source[PATH_MAX];
    char windows_dir[PATH_MAX];
    char system32[PATH_MAX];
    char syswow64[PATH_MAX];

    if (!join_path(dlls_dir, sizeof(dlls_dir), guest_dir, "dlls") ||
        !join_path(source_dir, sizeof(source_dir), dlls_dir, module_directory) ||
        !join_path(machine_dir, sizeof(machine_dir), source_dir, machine) ||
        !join_path(source, sizeof(source), machine_dir, module) ||
        !regular_file(source) ||
        !join_path(windows_dir, sizeof(windows_dir), prefix,
                   "drive_c/windows") ||
        !join_path(system32, sizeof(system32), windows_dir, "system32") ||
        !join_path(syswow64, sizeof(syswow64), windows_dir, "syswow64"))
        return 0;
    return stage_guest_override(source, system32) &&
           stage_guest_override(source, syswow64);
}

static int dxmt_has_d3d9(const char *dxmt_dir,
                         madeira_se_architecture_t architecture)
{
    const char *machine = architecture == MADEIRA_SE_ARCH_X86_64 ?
                          "x86_64-windows" : "i386-windows";
    char machine_dir[PATH_MAX];
    char module[PATH_MAX];

    return dxmt_dir != NULL &&
           join_path(machine_dir, sizeof(machine_dir), dxmt_dir, machine) &&
           join_path(module, sizeof(module), machine_dir, "d3d9.dll") &&
           regular_file(module);
}

/* wine.inf normally copies fake DLL/driver entries into the prefix.  The
 * standalone path intentionally skips its explorer/service transaction, so
 * materialize the architecture-specific PE files directly.  Symlinks keep a
 * development prefix small; the bundle staging script resolves them for a
 * distributable copy. */
static int stage_guest_system(const char *guest_dir, const char *prefix,
                              madeira_se_architecture_t architecture)
{
    const char *machine = architecture == MADEIRA_SE_ARCH_X86_64 ?
                          "x86_64-windows" : "i386-windows";
    char pattern[PATH_MAX];
    char windows_dir[PATH_MAX];
    char system32[PATH_MAX];
    char syswow64[PATH_MAX];
    char sysarm32[PATH_MAX];
    glob_t matches;
    size_t i;
    int length, result = 1;

    if (!join_path(windows_dir, sizeof(windows_dir), prefix, "drive_c/windows") ||
        !join_path(system32, sizeof(system32), windows_dir, "system32") ||
        !join_path(syswow64, sizeof(syswow64), windows_dir, "syswow64") ||
        !join_path(sysarm32, sizeof(sysarm32), windows_dir, "sysarm32") ||
        !ensure_directory(system32) || !ensure_directory(syswow64) ||
        !ensure_directory(sysarm32))
        return 0;

    length = snprintf(pattern, sizeof(pattern), "%s/dlls/*/%s/*", guest_dir, machine);
    if (length < 0 || (size_t)length >= sizeof(pattern) ||
        glob(pattern, 0, NULL, &matches))
        return 0;
    for (i = 0; i < matches.gl_pathc; ++i)
    {
        struct stat info;
        if (stat(matches.gl_pathv[i], &info) != 0 || !S_ISREG(info.st_mode) ||
            !guest_runtime_file(matches.gl_pathv[i])) continue;
        if (!stage_guest_file(matches.gl_pathv[i], system32) ||
            !stage_guest_file(matches.gl_pathv[i], syswow64)) result = 0;
    }
    globfree(&matches);

    length = snprintf(pattern, sizeof(pattern), "%s/programs/*/%s/*", guest_dir, machine);
    if (length < 0 || (size_t)length >= sizeof(pattern) ||
        glob(pattern, 0, NULL, &matches))
        return 0;
    for (i = 0; i < matches.gl_pathc; ++i)
    {
        struct stat info;
        if (stat(matches.gl_pathv[i], &info) != 0 || !S_ISREG(info.st_mode) ||
            !guest_runtime_file(matches.gl_pathv[i])) continue;
        if (!stage_guest_file(matches.gl_pathv[i], system32) ||
            !stage_guest_file(matches.gl_pathv[i], syswow64)) result = 0;
    }
    globfree(&matches);

    /* llvm-mingw's UCRT import libraries name the API-set DLLs explicitly.
     * Normal WineFakeDlls creates these aliases during a desktop bootstrap;
     * the standalone prefix intentionally skips that large transaction, so
     * provide the small CRT alias set needed by native DXMT and game DLLs. */
    {
        static const char *const ucrt_api_sets[] = {
            "api-ms-win-crt-convert-l1-1-0.dll",
            "api-ms-win-crt-environment-l1-1-0.dll",
            "api-ms-win-crt-filesystem-l1-1-0.dll",
            "api-ms-win-crt-heap-l1-1-0.dll",
            "api-ms-win-crt-locale-l1-1-0.dll",
            "api-ms-win-crt-math-l1-1-0.dll",
            "api-ms-win-crt-multibyte-l1-1-0.dll",
            "api-ms-win-crt-private-l1-1-0.dll",
            "api-ms-win-crt-runtime-l1-1-0.dll",
            "api-ms-win-crt-stdio-l1-1-0.dll",
            "api-ms-win-crt-string-l1-1-0.dll",
            "api-ms-win-crt-time-l1-1-0.dll",
            "api-ms-win-crt-utility-l1-1-0.dll"
        };
        char ucrt[PATH_MAX];
        size_t alias_index;

        if (!join_path(ucrt, sizeof(ucrt), system32, "ucrtbase.dll") ||
            !regular_file(ucrt))
            return 0;
        for (alias_index = 0;
             alias_index < sizeof(ucrt_api_sets) / sizeof(ucrt_api_sets[0]);
             ++alias_index) {
            if (!stage_guest_alias(ucrt, system32, ucrt_api_sets[alias_index]) ||
                !stage_guest_alias(ucrt, syswow64, ucrt_api_sets[alias_index]) ||
                !stage_guest_alias(ucrt, sysarm32, ucrt_api_sets[alias_index]))
                return 0;
        }
    }
    return result;
}

int main(int argc, char **argv)
{
    const char *host_dir = NULL;
    const char *guest_dir = NULL;
    const char *qemu_library = NULL;
    const char *runtime_library = NULL;
    const char *prefix = NULL;
    const char *dxmt_dir = NULL;
    const char *workdir = NULL;
    const char *appdata_dir = NULL;
    const char *d3d9_backend = NULL;
    const char *d3d9_virtual_mode = NULL;
    const char *window_size = NULL;
    const char *fps_cap = NULL;
    const char *executable;
    int desktop_mode = 0;
    int executable_uses_d3d9 = 0;
    int use_dxmt_d3d9 = 0;
    madeira_se_architecture_t architecture;
    madeira_se_status_t status;
    char loader[PATH_MAX];
    char wineserver[PATH_MAX];
    char native_library_dir[PATH_MAX];
    char host_library_dir[PATH_MAX];
    char host_ntdll_dir[PATH_MAX];
    char host_ntdll_library[PATH_MAX];
    char host_winemac_dir[PATH_MAX];
    char bundled_dxmt_dir[PATH_MAX];
    char dxmt_unix_dir[PATH_MAX];
    char dxmt_unix_library[PATH_MAX];
    char resolved_host_dir[PATH_MAX];
    char resolved_guest_dir[PATH_MAX];
    char resolved_qemu_library[PATH_MAX];
    char resolved_runtime_library[PATH_MAX];
    char resolved_prefix[PATH_MAX];
    char resolved_dxmt_dir[PATH_MAX];
    char resolved_workdir[PATH_MAX];
    char resolved_appdata_dir[PATH_MAX];
    char derived_appdata_dir[PATH_MAX];
    char resolved_executable[PATH_MAX];
    char wine_appdata[PATH_MAX * 2u];
    char architecture_buffer[16];
    char screen_width_buffer[16];
    char screen_height_buffer[16];
    char **child_argv;
    int child_argc;
    int index;
    uint32_t screen_width = 0u;
    uint32_t screen_height = 0u;

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    for (index = 1; index < argc; ++index) {
        const char *option = argv[index];

        if (!strcmp(option, "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (!strcmp(option, "--desktop")) {
            desktop_mode = 1;
            continue;
        }
        if (!strcmp(option, "--host-dir") || !strcmp(option, "--guest-dir") ||
            !strcmp(option, "--qemu") || !strcmp(option, "--runtime") ||
            !strcmp(option, "--prefix") || !strcmp(option, "--dxmt-dir") ||
            !strcmp(option, "--workdir") || !strcmp(option, "--appdata-dir") ||
            !strcmp(option, "--d3d9-backend") ||
            !strcmp(option, "--d3d9-virtual-mode") ||
            !strcmp(option, "--window-size") ||
            !strcmp(option, "--fps-cap")) {
            const char **destination = NULL;

            if (++index >= argc) {
                usage(argv[0]);
                return 2;
            }
            if (!strcmp(option, "--host-dir")) destination = &host_dir;
            else if (!strcmp(option, "--guest-dir")) destination = &guest_dir;
            else if (!strcmp(option, "--qemu")) destination = &qemu_library;
            else if (!strcmp(option, "--runtime")) destination = &runtime_library;
            else if (!strcmp(option, "--prefix")) destination = &prefix;
            else if (!strcmp(option, "--dxmt-dir")) destination = &dxmt_dir;
            else if (!strcmp(option, "--workdir")) destination = &workdir;
            else if (!strcmp(option, "--appdata-dir")) destination = &appdata_dir;
            else if (!strcmp(option, "--d3d9-backend")) destination = &d3d9_backend;
            else if (!strcmp(option, "--d3d9-virtual-mode")) destination = &d3d9_virtual_mode;
            else if (!strcmp(option, "--window-size")) destination = &window_size;
            else destination = &fps_cap;
            *destination = argv[index];
            continue;
        }
        if (option[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", option);
            usage(argv[0]);
            return 2;
        }
        break;
    }

    if (d3d9_backend == NULL) d3d9_backend = getenv("MADEIRA_SE_D3D9_BACKEND");
    if (d3d9_backend == NULL || d3d9_backend[0] == '\0') d3d9_backend = "auto";
    if (!valid_d3d9_backend(d3d9_backend)) {
        fprintf(stderr, "unsupported D3D9 backend %s (expected auto, dxmt, or wined3d)\n",
                d3d9_backend);
        return 2;
    }
    if (fps_cap == NULL) fps_cap = getenv("MADEIRA_SE_FPS_CAP");
    if (fps_cap != NULL && !valid_fps_cap(fps_cap)) {
        fprintf(stderr, "unsupported FPS cap %s (expected an integer from 0 to 240)\n",
                fps_cap);
        return 2;
    }

    if (host_dir == NULL || guest_dir == NULL || qemu_library == NULL ||
        runtime_library == NULL || prefix == NULL || index >= argc) {
        usage(argv[0]);
        return 2;
    }
    executable = argv[index];
    if (!valid_d3d9_virtual_mode(d3d9_virtual_mode)) {
        fprintf(stderr, "unsupported D3D9 virtual mode %s (expected WxH between 320x200 and 7680x4320)\n",
                d3d9_virtual_mode);
        return 2;
    }
    if (window_size == NULL) window_size = getenv("MADEIRA_SE_WINDOW_SIZE");
    /* A compatibility mode represents the dimensions exposed to Direct3D.
     * Keep the native client area at the same size unless the caller
     * explicitly requests host-side scaling.  Without either setting, let
     * the application choose its own window dimensions. */
    if (window_size == NULL && d3d9_virtual_mode != NULL &&
        strcmp(d3d9_virtual_mode, "1"))
        window_size = d3d9_virtual_mode;
    if (window_size != NULL) {
        if (madeira_se_parse_window_size(window_size, &screen_width, &screen_height) != MADEIRA_SE_OK) {
            fprintf(stderr, "unsupported window size %s (expected WxH between %ux%u and %ux%u)\n",
                    window_size, MADEIRA_SE_WINDOW_MIN_WIDTH, MADEIRA_SE_WINDOW_MIN_HEIGHT,
                    MADEIRA_SE_WINDOW_MAX_WIDTH, MADEIRA_SE_WINDOW_MAX_HEIGHT);
            return 2;
        }
    }
    if (!resolve_existing_path(executable, resolved_executable,
                               sizeof(resolved_executable))) {
        fprintf(stderr, "cannot resolve executable %s: %s\n", executable,
                strerror(errno));
        return 1;
    }
    executable = resolved_executable;
    if (!resolve_existing_path(host_dir, resolved_host_dir,
                               sizeof(resolved_host_dir)) ||
        !resolve_existing_path(guest_dir, resolved_guest_dir,
                               sizeof(resolved_guest_dir)) ||
        !resolve_existing_path(qemu_library, resolved_qemu_library,
                               sizeof(resolved_qemu_library)) ||
        !resolve_existing_path(runtime_library, resolved_runtime_library,
                               sizeof(resolved_runtime_library))) {
        fprintf(stderr, "cannot resolve Madeira-SE runtime resources\n");
        return 1;
    }
    host_dir = resolved_host_dir;
    guest_dir = resolved_guest_dir;
    qemu_library = resolved_qemu_library;
    runtime_library = resolved_runtime_library;
    if (dxmt_dir != NULL) {
        if (!resolve_existing_path(dxmt_dir, resolved_dxmt_dir,
                                   sizeof(resolved_dxmt_dir))) {
            fprintf(stderr, "cannot resolve DXMT directory %s\n", dxmt_dir);
            return 1;
        }
        dxmt_dir = resolved_dxmt_dir;
    }
    if (workdir != NULL) {
        if (!resolve_existing_path(workdir, resolved_workdir,
                                   sizeof(resolved_workdir))) {
            fprintf(stderr, "cannot resolve working directory %s\n", workdir);
            return 1;
        }
        workdir = resolved_workdir;
    }
    if (appdata_dir == NULL) {
        if (workdir != NULL) appdata_dir = workdir;
        else if (!path_directory(executable, derived_appdata_dir,
                                 sizeof(derived_appdata_dir))) {
            fprintf(stderr, "cannot derive game directory for AppData\n");
            return 1;
        } else {
            appdata_dir = derived_appdata_dir;
        }
    }
    if (!directory(appdata_dir) && !ensure_directory(appdata_dir)) {
        fprintf(stderr, "cannot create AppData directory %s\n", appdata_dir);
        return 1;
    }
    if (!resolve_existing_path(appdata_dir, resolved_appdata_dir,
                               sizeof(resolved_appdata_dir))) {
        fprintf(stderr, "cannot resolve AppData directory %s\n", appdata_dir);
        return 1;
    }
    appdata_dir = resolved_appdata_dir;
    status = madeira_se_probe_pe_file(executable, &architecture);
    if (status != MADEIRA_SE_OK) {
        fprintf(stderr, "cannot identify %s: %s\n", executable,
                madeira_se_status_string(status));
        return 1;
    }
    executable_uses_d3d9 = executable_references_d3d9(executable);
    if (!directory(host_dir) || !directory(guest_dir) || !ensure_directory(prefix) ||
        !regular_file(qemu_library) || !regular_file(runtime_library) ||
        !join_path(loader, sizeof(loader), host_dir, "loader/wine") ||
        !join_path(wineserver, sizeof(wineserver), host_dir, "server/wineserver") ||
        !regular_file(loader) || !regular_file(wineserver)) {
        fprintf(stderr, "Madeira-SE runtime resources are incomplete\n");
        return 1;
    }
    if (!resolve_existing_path(prefix, resolved_prefix, sizeof(resolved_prefix))) {
        fprintf(stderr, "cannot resolve prefix %s\n", prefix);
        return 1;
    }
    prefix = resolved_prefix;
    if (!configure_appdata_registry(prefix, appdata_dir, wine_appdata,
                                    sizeof(wine_appdata))) {
        fprintf(stderr, "cannot configure AppData directory %s\n", appdata_dir);
        return 1;
    }
    fprintf(stderr, "Madeira-SE: AppData=%s (%s)\n", appdata_dir,
            wine_appdata);
    if (!stage_guest_system(guest_dir, prefix, architecture)) {
        fprintf(stderr, "cannot materialize Madeira-SE guest system files in %s\n", prefix);
        return 1;
    }
    if (!configure_dxdiag_registry(prefix)) {
        fprintf(stderr, "cannot register Madeira-SE DxDiag/WBEM compatibility classes\n");
        return 1;
    }
    fprintf(stderr, "Madeira-SE: registered DxDiag/WBEM compatibility classes\n");

    if (dxmt_dir == NULL &&
        join_path(bundled_dxmt_dir, sizeof(bundled_dxmt_dir), host_dir, "../dxmt") &&
        directory(bundled_dxmt_dir))
        dxmt_dir = bundled_dxmt_dir;
    use_dxmt_d3d9 = !strcmp(d3d9_backend, "dxmt") ||
                    (!strcmp(d3d9_backend, "auto") && executable_uses_d3d9 &&
                     dxmt_has_d3d9(dxmt_dir, architecture));
    if (use_dxmt_d3d9 && !dxmt_has_d3d9(dxmt_dir, architecture)) {
        fprintf(stderr, "D3D9 backend dxmt requires %s-windows/d3d9.dll in the DXMT runtime\n",
                architecture_name(architecture));
        return 1;
    }
    if (!use_dxmt_d3d9 &&
        !stage_guest_builtin_module(guest_dir, prefix, architecture,
                                    "d3d9", "d3d9.dll")) {
        fprintf(stderr, "cannot restore Wine's D3D9 module in %s\n", prefix);
        return 1;
    }
    if (dxmt_dir != NULL)
    {
        if (!join_path(dxmt_unix_dir, sizeof(dxmt_unix_dir), dxmt_dir,
                       "aarch64-unix") ||
            !join_path(dxmt_unix_library, sizeof(dxmt_unix_library),
                       dxmt_unix_dir, "winemetal.so") ||
            !regular_file(dxmt_unix_library) ||
            !stage_dxmt_system(dxmt_dir, prefix, architecture,
                               use_dxmt_d3d9) ||
            !prepend_environment_path("WINEDLLPATH", dxmt_dir) ||
            !configure_dxmt_overrides(use_dxmt_d3d9)) {
            fprintf(stderr, "Madeira-SE DXMT resources are incomplete in %s\n",
                    dxmt_dir);
            return 1;
        }
        fprintf(stderr, "Madeira-SE: DXMT=%s overrides=%s dllpath=%s\n",
                dxmt_dir, getenv("WINEDLLOVERRIDES") ?: "",
                getenv("WINEDLLPATH") ?: "");
    }
    fprintf(stderr, "Madeira-SE: D3D9 backend=%s%s\n",
            use_dxmt_d3d9 ? "DXMT" : "wined3d",
            use_dxmt_d3d9 ? " (d9mt Metal layer)" : " (OpenGL fallback)");
    fprintf(stderr, "Madeira-SE: D3D9 import heuristic=%s\n",
            executable_uses_d3d9 ? "present" : "absent");

    /* A packaged runtime keeps its redistributable dylibs next to the host
     * tree.  The Homebrew paths make an un-staged development build usable;
     * copy-mode staging rewrites all bundled install names to @loader_path. */
    if (!join_path(host_library_dir, sizeof(host_library_dir), host_dir, "lib") ||
        !join_path(host_ntdll_dir, sizeof(host_ntdll_dir), host_dir, "dlls/ntdll") ||
        !join_path(host_ntdll_library, sizeof(host_ntdll_library), host_ntdll_dir, "ntdll.so") ||
        !join_path(host_winemac_dir, sizeof(host_winemac_dir), host_dir,
                   "dlls/winemac.drv") ||
        !join_path(native_library_dir, sizeof(native_library_dir), host_dir,
                   "../native-libs") ||
        !prepend_environment_path("DYLD_FALLBACK_LIBRARY_PATH",
                                  "/usr/local/opt/libpng/lib") ||
        !prepend_environment_path("DYLD_FALLBACK_LIBRARY_PATH",
                                  "/usr/local/opt/freetype/lib") ||
        !prepend_environment_path("DYLD_FALLBACK_LIBRARY_PATH",
                                  "/opt/homebrew/opt/libpng/lib") ||
        !prepend_environment_path("DYLD_FALLBACK_LIBRARY_PATH",
                                  "/opt/homebrew/opt/freetype/lib") ||
        !prepend_environment_path("DYLD_FALLBACK_LIBRARY_PATH",
                                  native_library_dir) ||
        !prepend_environment_path("DYLD_FALLBACK_LIBRARY_PATH",
                                  host_library_dir) ||
        !prepend_environment_path("DYLD_FALLBACK_LIBRARY_PATH",
                                  host_ntdll_dir) ||
        !prepend_environment_path("DYLD_FALLBACK_LIBRARY_PATH",
                                  host_winemac_dir) ||
        (dxmt_dir != NULL &&
         !prepend_environment_path("DYLD_FALLBACK_LIBRARY_PATH",
                                   dxmt_unix_dir))) {
        fprintf(stderr, "cannot configure native library search path: %s\n",
                strerror(errno));
        return 1;
    }

    if (workdir != NULL && chdir(workdir) != 0) {
        fprintf(stderr, "cannot change to %s: %s\n", workdir, strerror(errno));
        return 1;
    }
    if (setenv("MADEIRA_SE_RUNTIME_LIBRARY", runtime_library, 1) != 0 ||
        setenv("MADEIRA_SE_QEMU_LIBRARY", qemu_library, 1) != 0 ||
        setenv("MADEIRA_SE_GUEST_BUILD_DIR", guest_dir, 1) != 0 ||
        setenv("MADEIRA_SE_HOST_NTDLL", host_ntdll_library, 1) != 0 ||
        setenv("MADEIRA_SE_GUEST_ARCH", architecture_name(architecture), 1) != 0 ||
        setenv("MADEIRA_SE_D3D9_BACKEND",
               use_dxmt_d3d9 ? "dxmt" : "wined3d", 1) != 0 ||
        setenv("APPDATA", wine_appdata, 1) != 0 ||
        setenv("MADEIRA_SE_APPDATA_DIR", wine_appdata, 1) != 0 ||
        setenv("MADEIRA_SE_DXDIAG_COMPAT", "1", 1) != 0 ||
        setenv("WINESERVER", wineserver, 1) != 0 ||
        setenv("WINEPREFIX", prefix, 1) != 0 ||
        setenv("WINELOADERNOEXEC", "1", 1) != 0) {
        fprintf(stderr, "cannot configure Madeira-SE environment: %s\n",
                strerror(errno));
        return 1;
    }
    if (window_size != NULL &&
        setenv("MADEIRA_SE_WINDOW_SIZE", window_size, 1) != 0) {
        fprintf(stderr, "cannot configure standalone window size: %s\n",
                strerror(errno));
        return 1;
    }
    if (window_size != NULL) {
        snprintf(screen_width_buffer, sizeof(screen_width_buffer), "%u", screen_width);
        snprintf(screen_height_buffer, sizeof(screen_height_buffer), "%u", screen_height);
        if (setenv("MADEIRA_SCREEN_W", screen_width_buffer, 1) != 0 ||
            setenv("MADEIRA_SCREEN_H", screen_height_buffer, 1) != 0) {
            fprintf(stderr, "cannot configure standalone screen metrics: %s\n",
                    strerror(errno));
            return 1;
        }
    }
    if (fps_cap != NULL && setenv("MADEIRA_SE_FPS_CAP", fps_cap, 1) != 0) {
        fprintf(stderr, "cannot configure standalone FPS cap: %s\n",
                strerror(errno));
        return 1;
    }
    /* D3D9 applications commonly recreate their swapchain when the user
     * chooses windowed mode.  The standalone Cocoa drawable is already the
     * live presentation target, so keep it across that Reset() call.  Also
     * use the single threaded command stream by default for a D3D9 title: it
     * avoids a race between the 32-bit WoW64 guest and the native presentation
     * thread during the first frame.  Do not force that global Wine setting
     * for D3D11/OpenGL-only programs; DXMT's x86-64 D3D11 path needs the
     * normal command stream.  MADEIRA_SE_D3D9_CSMT=0|1 and an explicit
     * WINE_D3D_CONFIG remain diagnostic overrides. */
    if (executable_uses_d3d9) {
        const char *requested_csmt = getenv("MADEIRA_SE_D3D9_CSMT");
        const char *wine_d3d_config = getenv("WINE_D3D_CONFIG");
        char csmt_config[16];

        if (setenv("MADEIRA_SE_D3D9_SOFT_RESET", "1", 0) != 0) {
            fprintf(stderr, "cannot configure standalone D3D9 compatibility: %s\n",
                    strerror(errno));
            return 1;
        }
        if (requested_csmt != NULL && strcmp(requested_csmt, "0") != 0 &&
            strcmp(requested_csmt, "1") != 0) {
            fprintf(stderr, "MADEIRA_SE_D3D9_CSMT must be 0 or 1\n");
            return 1;
        }
        if (wine_d3d_config == NULL) {
            if (requested_csmt == NULL) requested_csmt = "0";
            snprintf(csmt_config, sizeof(csmt_config), "csmt=%s", requested_csmt);
            if (setenv("WINE_D3D_CONFIG", csmt_config, 1) != 0) {
                fprintf(stderr, "cannot configure standalone D3D9 compatibility: %s\n",
                        strerror(errno));
                return 1;
            }
            wine_d3d_config = csmt_config;
        }
        fprintf(stderr, "Madeira-SE: D3D9 command stream=%s\n", wine_d3d_config);
    }
    if (d3d9_virtual_mode != NULL &&
        setenv("MADEIRA_SE_D3D9_VIRTUAL_MODE", d3d9_virtual_mode, 1) != 0) {
        fprintf(stderr, "cannot configure D3D9 virtual mode: %s\n",
                strerror(errno));
        return 1;
    }
    if (d3d9_virtual_mode != NULL)
        fprintf(stderr, "Madeira-SE: D3D9 virtual mode=%s (compatibility shim)\n",
                d3d9_virtual_mode);
    if (window_size != NULL)
        fprintf(stderr, "Madeira-SE: window size=%s (client area)\n", window_size);
    else
        fprintf(stderr, "Madeira-SE: window size=application controlled\n");
    if (fps_cap != NULL)
        fprintf(stderr, "Madeira-SE: FPS cap=%s (present pacing)\n", fps_cap);
    if (dxmt_dir != NULL && setenv("MADEIRA_SE_DXMT_DIR", dxmt_dir, 1) != 0) {
        fprintf(stderr, "cannot configure Madeira-SE DXMT overlay: %s\n",
                strerror(errno));
        return 1;
    }
    /* A standalone Windows application does not need Wine's explorer shell,
     * services, or root PnP pass.  Keep the shell available as an explicit
     * opt-in for applications that depend on it. */
    if (desktop_mode) {
        if (unsetenv("MADEIRA_SE_NO_DESKTOP") != 0) {
            fprintf(stderr, "cannot enable Wine desktop mode: %s\n",
                    strerror(errno));
            return 1;
        }
    } else if (setenv("MADEIRA_SE_NO_DESKTOP", "1", 1) != 0) {
        fprintf(stderr, "cannot enable standalone mode: %s\n", strerror(errno));
        return 1;
    }
    if (getenv("WINEDEBUG") == NULL && setenv("WINEDEBUG", "-all", 1) != 0) {
        fprintf(stderr, "cannot configure WINEDEBUG: %s\n", strerror(errno));
        return 1;
    }
    /* The split Madeira loader selects the guest machine from the PE header;
     * Wine's normal WINEARCH=win32/win64 prefix switch is a WoW64 policy and
     * rejects this in-process TCTI profile. */
    if (unsetenv("WINEARCH") != 0) {
        fprintf(stderr, "cannot clear WINEARCH: %s\n", strerror(errno));
        return 1;
    }
    snprintf(architecture_buffer, sizeof(architecture_buffer), "%s",
             architecture_name(architecture));
    fprintf(stderr, "Madeira-SE: launching %s (%s)\n", executable,
            architecture_buffer);

    child_argc = argc - index + 1;
    child_argv = calloc((size_t)child_argc + 1u, sizeof(*child_argv));
    if (child_argv == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    child_argv[0] = loader;
    /* The launcher changes to --workdir before exec.  Pass the already
     * resolved image path to Wine as well as using it for PE probing; keeping
     * argv[index] here would make a relative invocation fail after chdir. */
    child_argv[1] = (char *)executable;
    for (int child_index = 2; child_index < child_argc; ++child_index)
        child_argv[child_index] = argv[index + child_index - 1];
    child_argv[child_argc] = NULL;
    execv(loader, child_argv);
    fprintf(stderr, "cannot start Wine loader %s: %s\n", loader,
            strerror(errno));
    free(child_argv);
    return 127;
}
