// smalt::log implementation. See include/smalt/log.h.
//
// One background thread owns the log file. Callers format a line and queue
// it; they never touch the filesystem. The logger object is deliberately
// leaked so no static destructor can run (and std::terminate on a joinable
// thread) while Nickel exits.
#include "smalt/log.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>

namespace smalt {
namespace log {
namespace {

constexpr qint64 kRotateBytes = 1024 * 1024;
constexpr size_t kMaxQueuedLines = 4096; // Bounds memory if the disk stalls.

struct Logger
{
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable idle;
    std::deque<QByteArray> queue;
    std::thread thread;
    QString dir;
    QString file;
    bool running = false;
    bool stopping = false;
    bool writing = false;
    quint64 dropped = 0;
    std::atomic<int> minimum{ int(Level::Info) };
};

Logger &logger()
{
    static Logger *instance = [] {
        Logger *l = new Logger;
        if (std::getenv("SMALT_DEBUG_LOG"))
            l->minimum = int(Level::Debug);
        return l;
    }();
    return *instance;
}

const char *levelName(Level level)
{
    switch (level) {
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO ";
    case Level::Warn:
        return "WARN ";
    case Level::Error:
        return "ERROR";
    }
    return "?    ";
}

// Runs on the writer thread only. Any failure drops the batch.
void appendBatch(const QString &dir, const QString &path, const std::deque<QByteArray> &batch)
{
    if (path.isEmpty() || !QDir().mkpath(dir))
        return;
    QFileInfo info(path);
    if (info.exists() && info.size() > kRotateBytes) {
        const QString previous = path + QStringLiteral(".1");
        QFile::remove(previous);
        QFile::rename(path, previous);
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Append))
        return;
    for (const QByteArray &line : batch) {
        if (out.write(line) != line.size())
            break;
    }
    out.close();
}

void run()
{
    Logger &l = logger();
    std::unique_lock<std::mutex> lock(l.mutex);
    for (;;) {
        l.wake.wait(lock, [&l] { return l.stopping || !l.queue.empty(); });
        if (l.queue.empty() && l.stopping)
            break;
        std::deque<QByteArray> batch;
        batch.swap(l.queue);
        if (l.dropped) {
            batch.push_front(QByteArray("[log] dropped ") + QByteArray::number(l.dropped)
                             + " lines while the disk was slow\n");
            l.dropped = 0;
        }
        const QString dir = l.dir;
        const QString path = l.file;
        l.writing = true;
        lock.unlock();
        appendBatch(dir, path, batch);
        lock.lock();
        l.writing = false;
        l.idle.notify_all();
    }
    l.idle.notify_all();
}

} // namespace

void init(const QString &logDir)
{
    Logger &l = logger();
    std::lock_guard<std::mutex> lock(l.mutex);
    l.dir = QDir::cleanPath(logDir);
    l.file = l.dir + QStringLiteral("/smalt.log");
    if (!l.running) {
        l.stopping = false;
        l.running = true;
        l.thread = std::thread(run);
    }
}

void write(Level level, const char *module, const QString &message)
{
    Logger &l = logger();
    if (int(level) < l.minimum.load())
        return;
    QByteArray line = QDateTime::currentDateTimeUtc()
                              .toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"))
                              .toUtf8();
    line += ' ';
    line += levelName(level);
    line += " [";
    line += module ? module : "?";
    line += "] ";
    line += message.toUtf8();
    line += '\n';

    std::lock_guard<std::mutex> lock(l.mutex);
    if (!l.running) {
        std::fwrite(line.constData(), 1, size_t(line.size()), stderr);
        return;
    }
    if (l.queue.size() >= kMaxQueuedLines) {
        l.queue.pop_front();
        ++l.dropped;
    }
    l.queue.push_back(line);
    l.wake.notify_one();
}

void setMinimumLevel(Level level)
{
    logger().minimum = int(level);
}

void flush()
{
    Logger &l = logger();
    std::unique_lock<std::mutex> lock(l.mutex);
    if (!l.running)
        return;
    l.idle.wait(lock, [&l] { return l.queue.empty() && !l.writing; });
}

void shutdown()
{
    Logger &l = logger();
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(l.mutex);
        if (!l.running)
            return;
        l.stopping = true;
        l.wake.notify_one();
        worker.swap(l.thread);
    }
    if (worker.joinable())
        worker.join();
    std::lock_guard<std::mutex> lock(l.mutex);
    l.running = false;
    l.stopping = false;
    l.file.clear();
}

QString currentFile()
{
    Logger &l = logger();
    std::lock_guard<std::mutex> lock(l.mutex);
    return l.running ? l.file : QString();
}

} // namespace log
} // namespace smalt
