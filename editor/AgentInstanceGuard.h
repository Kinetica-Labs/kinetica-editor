#pragma once
// Two per-user lock slots bound unattended agent-evaluation processes across editor instances.
// QLockFile validates stale owners; a surviving window keeps its slot until its workers are gone.
#include <QString>
#include <memory>

class QLockFile;

class AgentInstanceGuard {
public:
    AgentInstanceGuard();
    ~AgentInstanceGuard();
    bool acquire(QString& error);
private:
    std::unique_ptr<QLockFile> lock_;
};
