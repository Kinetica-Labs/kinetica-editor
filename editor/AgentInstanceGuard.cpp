// The unattended-process cap uses Qt's portable process-aware file locks, not a counter that
// remains occupied after a crash. RuntimeLocation is per-user and independent of editor settings.
#include "AgentInstanceGuard.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLockFile>
#include <QStandardPaths>

AgentInstanceGuard::AgentInstanceGuard() = default;
AgentInstanceGuard::~AgentInstanceGuard() = default;

bool AgentInstanceGuard::acquire(QString& error) {
    QString root = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (root.isEmpty()) {
        const auto user = QCryptographicHash::hash(QDir::homePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
        root = QDir::tempPath() + "/kinetica-user-" + QString::fromLatin1(user);
    }
    const QString folder = root + "/kinetica-agent-eval";
    if (!QDir().mkpath(folder)) return error = "cannot create the agent-evaluation lock directory", false;
    for (int slot = 0; slot < 2; ++slot) {
        auto candidate = std::make_unique<QLockFile>(folder + "/slot-" + QString::number(slot) + ".lock");
        candidate->setStaleLockTime(0); // no age-based stealing; Qt still detects a dead process owner
        if (candidate->tryLock(0)) { lock_ = std::move(candidate); return true; }
    }
    error = "two hidden agent-evaluation editors are already running; close one or wait for its idle lease";
    return false;
}
