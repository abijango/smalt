// smalt::log: the one logger every module uses (CLAUDE.md, "Code style").
//
// Writes plain-text lines to .adds/bookorbit/logs/smalt.log (see
// Paths::logsDir()). Calls never block on file I/O: lines are queued and a
// background thread appends them, so logging is safe on Nickel's UI thread
// (CLAUDE.md hard rule 2). If the log folder can't be written, lines are
// dropped silently; logging never fails the caller.
//
// The file rotates at about 1 MB, keeping one previous file (smalt.log.1),
// so the user partition can't fill up. Settings > Export log (SPEC, "Settings
// screen") copies these files.
//
// Line format: 2026-11-16T18:04:05.123Z INFO  [module] message
#pragma once

#include <QString>

namespace smalt {
namespace log {

enum class Level { Debug, Info, Warn, Error };

// Starts the writer thread and opens <logDir>/smalt.log. Safe to call more
// than once; later calls switch to the new folder. Before init(), lines go to
// stderr only.
void init(const QString &logDir);

// Queues one line. `module` is a short tag such as "ledger" or "sync".
void write(Level level, const char *module, const QString &message);

inline void debug(const char *module, const QString &message)
{
    write(Level::Debug, module, message);
}

inline void info(const char *module, const QString &message)
{
    write(Level::Info, module, message);
}

inline void warn(const char *module, const QString &message)
{
    write(Level::Warn, module, message);
}

inline void error(const char *module, const QString &message)
{
    write(Level::Error, module, message);
}

// Lines below this level are dropped. Default: Info (Debug on the desktop
// target when SMALT_DEBUG_LOG is set in the environment).
void setMinimumLevel(Level level);

// Blocks until every queued line is on disk. For tests, USB connect and
// shutdown only; never call it on a path that runs while the user reads.
void flush();

// Flushes and stops the writer thread. Further lines go to stderr.
void shutdown();

// Path of the current log file, or empty before init().
QString currentFile();

} // namespace log
} // namespace smalt
