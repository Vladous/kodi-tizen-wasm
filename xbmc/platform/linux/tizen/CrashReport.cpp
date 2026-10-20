/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CrashReport.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include <atomic>
#include <chrono>
#include <thread>

#include <dirent.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

// Nothing but the app reads its data directory on a TV, and a crash leaves no core dump
// there, so a crash writes where it happened to stderr, which the launcher can keep.
// Only async-signal-safe calls are used.

extern "C" char __executable_start;
extern "C" char __etext;

namespace
{
void WriteString(const char* text)
{
  [[maybe_unused]] auto ignored = write(STDERR_FILENO, text, strlen(text));
}

void WriteHex(const char* label, unsigned long value)
{
  char buffer[32];
  int pos = sizeof(buffer);
  buffer[--pos] = '\0';
  do
  {
    buffer[--pos] = "0123456789abcdef"[value & 0xf];
    value >>= 4;
  } while (value && pos > 2);
  buffer[--pos] = 'x';
  buffer[--pos] = '0';
  WriteString(label);
  WriteString(buffer + pos);
}

unsigned long ParseHex(const char*& text)
{
  unsigned long value = 0;
  for (;; ++text)
  {
    const char c = *text;
    if (c >= '0' && c <= '9')
      value = value * 16 + (c - '0');
    else if (c >= 'a' && c <= 'f')
      value = value * 16 + (c - 'a' + 10);
    else
      return value;
  }
}

// The end of the mapping that holds an address, from /proc/self/maps
unsigned long MappingEnd(unsigned long address)
{
  const int maps = open("/proc/self/maps", O_RDONLY);
  if (maps < 0)
    return 0;
  unsigned long end = 0;
  char line[512];
  size_t length = 0;
  char c;
  while (!end && read(maps, &c, 1) == 1)
  {
    if (c != '\n')
    {
      if (length < sizeof(line) - 1)
        line[length++] = c;
      continue;
    }
    line[length] = '\0';
    length = 0;
    const char* text = line;
    const unsigned long start = ParseHex(text);
    ++text;
    const unsigned long stop = ParseHex(text);
    if (address >= start && address < stop)
      end = stop;
  }
  close(maps);
  return end;
}

// Where a thread is, from the context a signal interrupted
void WriteContext(void* context)
{
#if defined(__arm__)
  if (const auto* uc = static_cast<const ucontext_t*>(context))
  {
    WriteHex("\npc ", uc->uc_mcontext.arm_pc);
    WriteHex(" lr ", uc->uc_mcontext.arm_lr);
    WriteHex(" sp ", uc->uc_mcontext.arm_sp);
  }
#endif
  WriteString("\nbacktrace:\n");
  void* frames[48];
  backtrace_symbols_fd(frames, backtrace(frames, 48), STDERR_FILENO);

#if defined(__arm__)
  // The unwinder stops at a frame without unwind tables, such as in libc, so the
  // addresses in Kodi's code found on the stack follow, which include the callers.
  if (const auto* uc = static_cast<const ucontext_t*>(context))
  {
    WriteString("stack:");
    const auto* word = reinterpret_cast<const unsigned long*>(uc->uc_mcontext.arm_sp);
    const auto* stackEnd = reinterpret_cast<const unsigned long*>(MappingEnd(uc->uc_mcontext.arm_sp));
    const auto start = reinterpret_cast<unsigned long>(&__executable_start);
    const auto end = reinterpret_cast<unsigned long>(&__etext);
    for (int i = 0; i < 4096 && word + i < stackEnd; ++i)
    {
      if (word[i] >= start && word[i] < end)
        WriteHex(" ", word[i]);
    }
    WriteString("\n");
  }
#endif
}

void WriteMaps()
{
  // The load addresses, to turn the addresses above into functions
  WriteString("maps:\n");
  const int maps = open("/proc/self/maps", O_RDONLY);
  if (maps >= 0)
  {
    char buffer[4096];
    ssize_t n;
    while ((n = read(maps, buffer, sizeof(buffer))) > 0)
      [[maybe_unused]] auto ignored = write(STDERR_FILENO, buffer, n);
    close(maps);
  }
}

void OnCrash(int sig, siginfo_t* info, void* context)
{
  WriteHex("\n*** Kodi crashed: signal ", static_cast<unsigned long>(sig));
  WriteHex(", address ", reinterpret_cast<unsigned long>(info ? info->si_addr : nullptr));
  WriteContext(context);
  WriteMaps();
  signal(sig, SIG_DFL);
  raise(sig);
}

const int DUMP_SIGNAL = SIGRTMIN + 5;
std::atomic<bool> dumped;

void OnDump(int sig, siginfo_t* info, void* context)
{
  WriteContext(context);
  dumped = true;
}
} // namespace

void KODI::PLATFORM::TIZEN::InstallCrashReport()
{
  // backtrace() loads libgcc on its first call, which must not happen in the handler.
  void* frame;
  backtrace(&frame, 1);

  struct sigaction action = {};
  action.sa_sigaction = OnCrash;
  action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
  for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT})
    sigaction(sig, &action, nullptr);

  struct sigaction dump = {};
  dump.sa_sigaction = OnDump;
  dump.sa_flags = SA_SIGINFO | SA_RESTART;
  sigaction(DUMP_SIGNAL, &dump, nullptr);
}

void KODI::PLATFORM::TIZEN::ReportThreads()
{
  using namespace std::chrono_literals;

  DIR* tasks = opendir("/proc/self/task");
  if (!tasks)
    return;
  const long self = syscall(SYS_gettid);
  while (const dirent* task = readdir(tasks))
  {
    const long tid = strtol(task->d_name, nullptr, 10);
    if (tid <= 0 || tid == self)
      continue;
    char name[32] = {};
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/task/%ld/comm", tid);
    if (FILE* comm = fopen(path, "r"))
    {
      if (!fgets(name, sizeof(name), comm))
        name[0] = '\0';
      name[strcspn(name, "\n")] = '\0';
      fclose(comm);
    }
    fprintf(stderr, "\n*** Kodi thread %ld %s:", tid, name);
    fflush(stderr);
    // One thread after the other, so that their reports do not mix
    dumped = false;
    if (syscall(SYS_tgkill, getpid(), tid, DUMP_SIGNAL) != 0)
      continue;
    for (int i = 0; i < 50 && !dumped; ++i)
      std::this_thread::sleep_for(10ms);
  }
  closedir(tasks);
  WriteMaps();
}
