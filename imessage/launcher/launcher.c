// launcher.c — the program inside iterate-imessage.app, the one thing on the Mac that holds Full
// Disk Access for the iMessage lend. It runs the lend and nothing else:
//
//   LEND_NODE LEND_ITERATE provide LEND_ENTRY --mind <MIND> --key <home>/.config/iterate-imessage/key
//     --paths /integrations/imessage/
//
// The three programs are fixed when install.sh compiles this file; nothing at run time can name
// another. The launcher takes no arguments and reads no environment: its only input is the file
// <home>/.config/iterate-imessage/launcher.conf, which may set MIND and IMESSAGE_ACCOUNT and
// nothing else. The lend gets a fresh environment built here, so variables such as NODE_OPTIONS or
// DYLD_INSERT_LIBRARIES that reach the launcher never reach node.
//
// It SPAWNS node and waits; it never execs it. macOS charges a child's Full Disk Access to its
// responsible process, which for a launchd agent is the agent itself, and checks that process's
// code identity at each access. Exec would keep the process but swap its identity for node's, so
// the grant would have to be node's. Spawning keeps this launcher, with its own identity, running
// as the responsible process for node and for the `osascript` that node starts.
#ifndef __APPLE__
#define _POSIX_C_SOURCE 200809L  // Linux, where the tests also run
#endif
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(LEND_NODE) || !defined(LEND_ITERATE) || !defined(LEND_ENTRY)
#error "Build with install.sh, which defines LEND_NODE, LEND_ITERATE and LEND_ENTRY."
#endif

// sysexits.h's codes: launchd logs them and KeepAlive starts the job again 15 s later.
#define EXIT_USAGE 64
#define EXIT_CONFIG 78
#define EXIT_OS 71

#define CONFIG_DIR "/.config/iterate-imessage"
#define VALUE_MAX 512
#define PATH_BYTES 1024

static const char *const PATHS = "/integrations/imessage/";
static volatile sig_atomic_t child = 0;

static void forward(int signal) {
  if (child > 0) kill(child, signal);
}

static int fail(int code, const char *message, const char *detail) {
  fprintf(stderr, "iterate-imessage launcher: %s%s%s\n", message, detail ? ": " : "", detail ? detail : "");
  return code;
}

// The owner's home folder, from the user database, not $HOME. Tests compile in LEND_HOME instead.
static const char *home_folder(void) {
#ifdef LEND_HOME
  return LEND_HOME;
#else
  struct passwd *entry = getpwuid(getuid());
  return entry && entry->pw_dir && entry->pw_dir[0] == '/' ? entry->pw_dir : NULL;
#endif
}

static const char *user_name(void) {
  struct passwd *entry = getpwuid(getuid());
  return entry && entry->pw_name ? entry->pw_name : "";
}

// A value is one line of printable ASCII, without spaces, quotes or a backslash, and never begins
// with '-', so it cannot become an option of the iterate CLI.
static int valid_value(const char *value) {
  size_t length = strlen(value);
  if (length == 0 || length > VALUE_MAX || value[0] == '-') return 0;
  for (const char *c = value; *c; c++)
    if (*c <= ' ' || *c > '~' || *c == '"' || *c == '\'' || *c == '\\') return 0;
  return 1;
}

static char *trim(char *text) {
  while (*text == ' ' || *text == '\t') text++;
  char *end = text + strlen(text);
  while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = '\0';
  return text;
}

// launcher.conf: lines of KEY=value, blank lines and # comments. MIND and IMESSAGE_ACCOUNT are both
// required, each once; any other key stops the launcher. The file must be the owner's and writable
// by nobody else, as ssh asks of its own configuration.
static int read_config(const char *file, char mind[VALUE_MAX + 1], char account[VALUE_MAX + 1]) {
  int fd = open(file, O_RDONLY | O_NOFOLLOW);
  if (fd < 0) return fail(EXIT_CONFIG, "cannot read the configuration", file);
  struct stat info;
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
    close(fd);
    return fail(EXIT_CONFIG, "the configuration is not a regular file", file);
  }
  if (info.st_uid != getuid() || (info.st_mode & (S_IWGRP | S_IWOTH))) {
    close(fd);
    return fail(EXIT_CONFIG, "the configuration must be the owner's and writable by nobody else (chmod 600)", file);
  }
  FILE *in = fdopen(fd, "r");
  if (!in) {
    close(fd);
    return fail(EXIT_OS, "cannot read the configuration", strerror(errno));
  }
  mind[0] = account[0] = '\0';
  char line[VALUE_MAX + 64];
  int status = 0;
  while (status == 0 && fgets(line, sizeof line, in)) {
    if (!strchr(line, '\n') && !feof(in)) {
      status = fail(EXIT_CONFIG, "a line of the configuration is too long", file);
      break;
    }
    line[strcspn(line, "\n")] = '\0';
    char *text = trim(line);
    if (*text == '\0' || *text == '#') continue;
    char *equals = strchr(text, '=');
    if (!equals) {
      status = fail(EXIT_CONFIG, "a configuration line is not KEY=value", text);
      break;
    }
    *equals = '\0';
    char *key = trim(text), *value = trim(equals + 1);
    char *slot = !strcmp(key, "MIND") ? mind : !strcmp(key, "IMESSAGE_ACCOUNT") ? account : NULL;
    if (!slot)
      status = fail(EXIT_CONFIG, "unknown configuration key (only MIND and IMESSAGE_ACCOUNT)", key);
    else if (slot[0])
      status = fail(EXIT_CONFIG, "a configuration key is set twice", key);
    else if (!valid_value(value))
      status = fail(EXIT_CONFIG, "a configuration value is empty, too long, starts with '-' or has a space, quote or control character", key);
    else
      strcpy(slot, value);
  }
  fclose(in);
  if (status == 0 && !mind[0]) status = fail(EXIT_CONFIG, "the configuration does not set MIND", file);
  if (status == 0 && !account[0])
    status = fail(EXIT_CONFIG, "the configuration does not set IMESSAGE_ACCOUNT", file);
  return status;
}

int main(int argc, char **argv) {
  (void)argv;
  if (argc > 1) return fail(EXIT_USAGE, "takes no arguments; it reads ~/.config/iterate-imessage/launcher.conf", NULL);

  const char *home = home_folder();
  if (!home) return fail(EXIT_OS, "cannot find the owner's home folder", NULL);
  char dir[PATH_BYTES], config[PATH_BYTES + 64], key[PATH_BYTES + 64];
  if (snprintf(dir, sizeof dir, "%s%s", home, CONFIG_DIR) >= (int)sizeof dir)
    return fail(EXIT_OS, "the home folder's path is too long", NULL);
  snprintf(config, sizeof config, "%s/launcher.conf", dir);
  snprintf(key, sizeof key, "%s/key", dir);

  char mind[VALUE_MAX + 1], account[VALUE_MAX + 1];
  int status = read_config(config, mind, account);
  if (status) return status;

  // The lend's whole environment. The iterate CLI picks its own code by the working folder, so it
  // runs from the configuration folder, which holds no checkout.
  char tmp[PATH_BYTES] = "/tmp/";
#ifdef _CS_DARWIN_USER_TEMP_DIR
  if (confstr(_CS_DARWIN_USER_TEMP_DIR, tmp, sizeof tmp) == 0) strcpy(tmp, "/tmp/");
#endif
  char e_home[PATH_BYTES + 64], e_user[300], e_logname[300], e_tmp[PATH_BYTES + 64];
  char e_account[VALUE_MAX + 64], e_allow[PATH_BYTES + 64], e_state[PATH_BYTES + 64];
  snprintf(e_home, sizeof e_home, "HOME=%s", home);
  snprintf(e_user, sizeof e_user, "USER=%s", user_name());
  snprintf(e_logname, sizeof e_logname, "LOGNAME=%s", user_name());
  snprintf(e_tmp, sizeof e_tmp, "TMPDIR=%s", tmp);
  snprintf(e_account, sizeof e_account, "IMESSAGE_ACCOUNT=%s", account);
  snprintf(e_allow, sizeof e_allow, "IMESSAGE_ALLOW_FILE=%s/allow", dir);
  snprintf(e_state, sizeof e_state, "IMESSAGE_STATE_FILE=%s/state.json", dir);
  char *environment[] = {e_home,  e_user,    e_logname, "PATH=/usr/bin:/bin:/usr/sbin:/sbin",
                         e_tmp,   e_account, e_allow,   e_state,
                         NULL};
  char *arguments[] = {LEND_NODE, LEND_ITERATE, "provide", LEND_ENTRY, "--mind", mind, "--key", key,
                       "--paths", (char *)PATHS, NULL};

  if (chdir(dir) != 0) return fail(EXIT_CONFIG, "cannot enter the configuration folder", dir);

  // Forward launchd's stop (SIGTERM) and a terminal's Ctrl-C to the lend; the child starts with the
  // default handlers and no blocked signals. The three wait, blocked, until the child is known.
  // When this launcher is gone, launchd ends the rest of its process group, the lend with it.
  sigset_t stops, previous;
  sigemptyset(&stops);
  sigaddset(&stops, SIGTERM);
  sigaddset(&stops, SIGINT);
  sigaddset(&stops, SIGHUP);
  sigprocmask(SIG_BLOCK, &stops, &previous);
  struct sigaction action = {0};
  action.sa_handler = forward;
  sigemptyset(&action.sa_mask);
  sigaction(SIGTERM, &action, NULL);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGHUP, &action, NULL);
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
  sigset_t none;
  sigemptyset(&none);
  posix_spawnattr_setsigmask(&attributes, &none);
  posix_spawnattr_setsigdefault(&attributes, &stops);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

  pid_t pid;
  int spawned = posix_spawn(&pid, LEND_NODE, NULL, &attributes, arguments, environment);
  posix_spawnattr_destroy(&attributes);
  if (spawned != 0) return fail(EXIT_OS, "cannot start node at " LEND_NODE, strerror(spawned));
  child = pid;
  sigprocmask(SIG_SETMASK, &previous, NULL);

  int wait_status;
  while (waitpid(pid, &wait_status, 0) < 0)
    if (errno != EINTR) return fail(EXIT_OS, "lost the lend", strerror(errno));
  if (WIFEXITED(wait_status)) return WEXITSTATUS(wait_status);
  return 128 + WTERMSIG(wait_status);
}
