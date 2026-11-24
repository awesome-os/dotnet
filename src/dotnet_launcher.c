/**
 * @file src/dotnet_launcher.c
 * @description Zero-dependency native C11 Win64/POSIX/WASI self-hydrating Ghost .NET SDK launcher.
 * Hydrates rolling-latest .NET SDK once from awesome-os/b8g-ghost (cas-v1, Keep-Last-5)
 * into ~/.b8g/cas/dotnet/<sha256>/, sets DOTNET_ROOT and DOTNET_CLI_TELEMETRY_OPTOUT=1,
 * and execs verbatim (GetCommandLineW + CreateProcessW on Win32, execv on POSIX).
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
  #define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <direct.h>
  #define PATH_SEP '\\'
  #define OS_KEY "win64"
  #define DOTNET_REL_BIN "dotnet.exe"
#elif defined(__wasi__)
  #include <sys/stat.h>
  #define PATH_SEP '/'
  #define OS_KEY "wasm32-wasi"
  #define DOTNET_REL_BIN "dotnet"
#else
  #include <unistd.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #define PATH_SEP '/'
  #if defined(__APPLE__) && defined(__aarch64__)
    #define OS_KEY "macos-arm64"
  #elif defined(__aarch64__)
    #define OS_KEY "linux-arm64"
  #else
    #define OS_KEY "linux-x64"
  #endif
  #define DOTNET_REL_BIN "dotnet"
#endif

#define GHOST_DOTNET_LAUNCHER_VERSION "0.2.0"
#define INDEX_URL "https://raw.githubusercontent.com/awesome-os/b8g-ghost/main/manifests/index.json"
#define BLOB_BASE_URL "https://github.com/awesome-os/b8g-ghost/releases/download/cas-v1"
#define MAX_PATH_BUF 2048

static int get_cas_dir(char *out, size_t cap) {
  const char *env_dir = getenv("B8G_GHOST_CACHE_DIR");
  if (env_dir && env_dir[0] != '\0') {
    snprintf(out, cap, "%s", env_dir);
    return 0;
  }
#if defined(_WIN32)
  const char *home = getenv("USERPROFILE");
  if (!home || home[0] == '\0') home = getenv("HOME");
  if (!home || home[0] == '\0') return -1;
  snprintf(out, cap, "%s\\.b8g\\cas", home);
#else
  const char *home = getenv("HOME");
  if (!home || home[0] == '\0') return -1;
  snprintf(out, cap, "%s/.b8g/cas", home);
#endif
  return 0;
}

static char *read_file_text(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return NULL;
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  long sz = ftell(fp);
  if (sz <= 0 || sz > 16 * 1024 * 1024) {
    fclose(fp);
    return NULL;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(fp);
    return NULL;
  }
  size_t n = fread(buf, 1, (size_t)sz, fp);
  buf[n] = '\0';
  fclose(fp);
  return buf;
}

static int lookup_sha256_in_index(const char *json, const char *filename, char out_sha[65]) {
  char key[256];
  snprintf(key, sizeof(key), "\"%s\"", filename);
  const char *pos = strstr(json, key);
  if (!pos) return -1;
  const char *sha_key = strstr(pos, "\"sha256\"");
  if (!sha_key) return -1;
  const char *colon = strchr(sha_key + 8, ':');
  if (!colon) return -1;
  const char *q1 = strchr(colon, '"');
  if (!q1) return -1;
  q1++;
  for (int i = 0; i < 64; i++) {
    char c = q1[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      return -1;
    }
    out_sha[i] = (c >= 'A' && c <= 'F') ? (char)(c + 32) : c;
  }
  out_sha[64] = '\0';
  return 0;
}

static void print_help(const char *logical_key, const char *cas_dir) {
  printf("@awesome-os/dotnet %s - zero-installer native C11 .NET SDK launcher\n",
         GHOST_DOTNET_LAUNCHER_VERSION);
  printf("  target : %s\n", OS_KEY);
  printf("  key    : %s\n", logical_key);
  printf("  cas    : %s\n", cas_dir);
  printf("  index  : %s\n", INDEX_URL);
  printf("  env    : DOTNET_ROOT=<cas>/dotnet/<sha256>, DOTNET_CLI_TELEMETRY_OPTOUT=1\n");
  printf("Options:\n");
  printf("  --ghost-version   Print launcher version and target\n");
  printf("  --ghost-info      Inspect launcher and cached CAS manifest state\n");
  printf("  --ghost-help      Show this launcher help\n");
}

static void inspect_cas_manifest(const char *cas_dir, const char *logical_key) {
  char index_path[MAX_PATH_BUF];
  snprintf(index_path, sizeof(index_path), "%s%cindex.json", cas_dir, PATH_SEP);
  char sha[65] = {0};
  char *json = read_file_text(index_path);
  int found = (json != NULL && lookup_sha256_in_index(json, logical_key, sha) == 0);
  if (json) free(json);

  printf("@awesome-os/dotnet %s - target=%s - key=%s - cas=%s - sha256=%s\n",
         GHOST_DOTNET_LAUNCHER_VERSION, OS_KEY, logical_key, cas_dir,
         found ? sha : "not-cached");
}

#if !defined(__wasi__)

static int file_exists(const char *path) {
#if defined(_WIN32)
  DWORD attr = GetFileAttributesA(path);
  return (attr != INVALID_FILE_ATTRIBUTES);
#else
  struct stat st;
  return (stat(path, &st) == 0);
#endif
}

static void mkdir_p(const char *dir) {
  char tmp[MAX_PATH_BUF];
  snprintf(tmp, sizeof(tmp), "%s", dir);
  size_t len = strlen(tmp);
  if (len == 0) return;
  if (tmp[len - 1] == '/' || tmp[len - 1] == '\\') tmp[len - 1] = '\0';
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/' || *p == '\\') {
      char saved = *p;
      *p = '\0';
#if defined(_WIN32)
      if (!(p - tmp == 2 && tmp[1] == ':')) _mkdir(tmp);
#else
      mkdir(tmp, 0755);
#endif
      *p = saved;
    }
  }
#if defined(_WIN32)
  _mkdir(tmp);
#else
  mkdir(tmp, 0755);
#endif
}

static int run_cmd(const char *cmd) {
  return system(cmd);
}

static int refresh_index(const char *cas_dir, const char *index_path) {
  mkdir_p(cas_dir);
  char cmd[MAX_PATH_BUF * 2];
  snprintf(cmd, sizeof(cmd), "curl -fsSL \"%s\" -o \"%s.tmp\"", INDEX_URL, index_path);
  if (run_cmd(cmd) != 0) return -1;
  char tmp_path[MAX_PATH_BUF + 8];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", index_path);
#if defined(_WIN32)
  MoveFileExA(tmp_path, index_path, MOVEFILE_REPLACE_EXISTING);
#else
  rename(tmp_path, index_path);
#endif
  return 0;
}

static int hydrate_blob(const char *cas_dir, const char *sha, const char *target_dir) {
  mkdir_p(target_dir);
  char tmp_arc[MAX_PATH_BUF];
  snprintf(tmp_arc, sizeof(tmp_arc), "%s%cdotnet-%s.tar.zst", cas_dir, PATH_SEP, sha);

  char cmd[MAX_PATH_BUF * 3];
  fprintf(stderr, "[awesome-os/dotnet] Hydrating cas-v1 blob %.12s...\n", sha);
  snprintf(cmd, sizeof(cmd), "curl -fsSL \"%s/%s\" -o \"%s\"", BLOB_BASE_URL, sha, tmp_arc);
  if (run_cmd(cmd) != 0) {
    remove(tmp_arc);
    return -1;
  }

#if defined(_WIN32) || defined(__APPLE__)
  snprintf(cmd, sizeof(cmd), "tar -xf \"%s\" -C \"%s\"", tmp_arc, target_dir);
#else
  snprintf(cmd, sizeof(cmd), "tar --zstd -xf \"%s\" -C \"%s\"", tmp_arc, target_dir);
#endif
  int rc = run_cmd(cmd);
  remove(tmp_arc);
  return rc == 0 ? 0 : -1;
}

#if defined(_WIN32)
static const wchar_t *skip_argv0_w(const wchar_t *cmd) {
  if (!cmd) return L"";
  const wchar_t *p = cmd;
  while (*p == L' ' || *p == L'\t') p++;
  if (*p == L'"') {
    p++;
    while (*p && *p != L'"') p++;
    if (*p == L'"') p++;
  } else {
    while (*p && *p != L' ' && *p != L'\t') p++;
  }
  while (*p == L' ' || *p == L'\t') p++;
  return p;
}

static int exec_dotnet_win32(const char *dotnet_bin, const char *target_dir) {
  SetEnvironmentVariableA("DOTNET_ROOT", target_dir);
  SetEnvironmentVariableA("DOTNET_CLI_TELEMETRY_OPTOUT", "1");

  int wbin_len = MultiByteToWideChar(CP_UTF8, 0, dotnet_bin, -1, NULL, 0);
  if (wbin_len <= 0) return 127;
  wchar_t *wbin = (wchar_t *)malloc((size_t)wbin_len * sizeof(wchar_t));
  if (!wbin) return 127;
  MultiByteToWideChar(CP_UTF8, 0, dotnet_bin, -1, wbin, wbin_len);

  const wchar_t *raw_cmd = GetCommandLineW();
  const wchar_t *tail = skip_argv0_w(raw_cmd);
  size_t tail_len = wcslen(tail);

  size_t full_cap = (size_t)wbin_len + tail_len + 8;
  wchar_t *full_cmd = (wchar_t *)malloc(full_cap * sizeof(wchar_t));
  if (!full_cmd) {
    free(wbin);
    return 127;
  }

  size_t pos = 0;
  full_cmd[pos++] = L'"';
  size_t bin_chars = (size_t)(wbin_len - 1);
  memcpy(full_cmd + pos, wbin, bin_chars * sizeof(wchar_t));
  pos += bin_chars;
  full_cmd[pos++] = L'"';
  if (tail_len > 0) {
    full_cmd[pos++] = L' ';
    memcpy(full_cmd + pos, tail, tail_len * sizeof(wchar_t));
    pos += tail_len;
  }
  full_cmd[pos] = L'\0';

  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  ZeroMemory(&pi, sizeof(pi));
  si.cb = sizeof(si);

  if (!CreateProcessW(wbin, full_cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
    fprintf(stderr, "[awesome-os/dotnet] CreateProcessW failed (%lu) for %s\n",
            (unsigned long)GetLastError(), dotnet_bin);
    free(full_cmd);
    free(wbin);
    return 127;
  }

  free(full_cmd);
  free(wbin);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 1;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return (int)exit_code;
}
#endif

#endif /* !defined(__wasi__) */

int main(int argc, char **argv) {
  char logical_key[128];
  snprintf(logical_key, sizeof(logical_key), "dotnet-sdk-%s.tar.zst", OS_KEY);

  char cas_dir[MAX_PATH_BUF];
  int has_cas_dir = (get_cas_dir(cas_dir, sizeof(cas_dir)) == 0);
  if (!has_cas_dir) {
    snprintf(cas_dir, sizeof(cas_dir), ".b8g%ccas", PATH_SEP);
  }

  if (argc == 2 && strcmp(argv[1], "--ghost-version") == 0) {
    printf("@awesome-os/dotnet %s - target=%s - key=%s\n",
           GHOST_DOTNET_LAUNCHER_VERSION, OS_KEY, logical_key);
    return 0;
  }

  if (argc == 2 && strcmp(argv[1], "--ghost-info") == 0) {
    inspect_cas_manifest(cas_dir, logical_key);
    return 0;
  }

  if (argc == 2 && strcmp(argv[1], "--ghost-help") == 0) {
    print_help(logical_key, cas_dir);
    return 0;
  }

#if defined(__wasi__)
  if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
    print_help(logical_key, cas_dir);
    inspect_cas_manifest(cas_dir, logical_key);
    return 0;
  }
  fprintf(stderr,
          "[awesome-os/dotnet] WASI target supports --ghost-version, --ghost-info, and --help only.\n");
  return 1;
#else
  if (!has_cas_dir) {
    fprintf(stderr, "[awesome-os/dotnet] Could not resolve CAS cache dir (~/.b8g/cas).\n");
    return 1;
  }

  char index_path[MAX_PATH_BUF];
  snprintf(index_path, sizeof(index_path), "%s%cindex.json", cas_dir, PATH_SEP);

  char sha[65] = {0};
  char *json = read_file_text(index_path);
  int found = (json != NULL && lookup_sha256_in_index(json, logical_key, sha) == 0);
  if (json) free(json);

  if (!found) {
    if (refresh_index(cas_dir, index_path) != 0) {
      fprintf(stderr, "[awesome-os/dotnet] Failed to fetch cas-v1 index from %s\n", INDEX_URL);
      return 1;
    }
    json = read_file_text(index_path);
    found = (json != NULL && lookup_sha256_in_index(json, logical_key, sha) == 0);
    if (json) free(json);
    if (!found) {
      fprintf(stderr, "[awesome-os/dotnet] Entry '%s' not found in cas-v1 index.\n", logical_key);
      return 1;
    }
  }

  char target_dir[MAX_PATH_BUF];
  char dotnet_bin[MAX_PATH_BUF];
  snprintf(target_dir, sizeof(target_dir), "%s%cdotnet%c%s", cas_dir, PATH_SEP, PATH_SEP, sha);
  snprintf(dotnet_bin, sizeof(dotnet_bin), "%s%c%s", target_dir, PATH_SEP, DOTNET_REL_BIN);

  if (!file_exists(dotnet_bin)) {
    if (hydrate_blob(cas_dir, sha, target_dir) != 0 || !file_exists(dotnet_bin)) {
      fprintf(stderr, "[awesome-os/dotnet] Failed to hydrate %s (%s)\n", logical_key, sha);
      return 1;
    }
  }

#if defined(_WIN32)
  (void)argc;
  (void)argv;
  return exec_dotnet_win32(dotnet_bin, target_dir);
#else
  setenv("DOTNET_ROOT", target_dir, 1);
  setenv("DOTNET_CLI_TELEMETRY_OPTOUT", "1", 1);
  argv[0] = dotnet_bin;
  execv(dotnet_bin, argv);
  perror("[awesome-os/dotnet] execv");
  return 127;
#endif
#endif
}
