#include "SessionManager.h"

#include <algorithm>

SessionManager::SessionManager(QObject *parent)
    : QObject(parent)
{
}

SessionManager::~SessionManager()
{
    closeAllSessions();
}

Session *SessionManager::createSession(const QString &workingDirectory)
{
    const int id = nextId_++;
    auto session = std::make_unique<Session>(id, QStringLiteral("CMD-%1").arg(id));
    Session *sessionPtr = session.get();

    connect(sessionPtr, &Session::terminalUpdated, this, &SessionManager::sessionTerminalUpdated);
    connect(sessionPtr, &Session::stateChanged, this, [this](int sessionId) {
        if (Session *session = sessionById(sessionId)) {
            emit sessionUpdated(session);
        }
    });
    connect(sessionPtr, &Session::titleChanged, this, [this](int sessionId) {
        if (Session *session = sessionById(sessionId)) {
            emit sessionUpdated(session);
        }
    });
    connect(sessionPtr, &Session::errorOccurred, this, [this](int sessionId, const QString &) {
        if (Session *session = sessionById(sessionId)) {
            emit sessionUpdated(session);
        }
    });

    sessions_.push_back(std::move(session));
    sessionPtr->start(workingDirectory);
    emit sessionAdded(sessionPtr);
    setCurrentSession(id);
    return sessionPtr;
}

void SessionManager::closeSession(int id)
{
    const int index = indexOfSession(id);
    if (index < 0) {
        return;
    }

    const bool wasCurrent = currentSessionId_ == id;
    sessions_[static_cast<size_t>(index)]->stop();
    sessions_.erase(sessions_.begin() + index);
    emit sessionRemoved(id);

    if (wasCurrent) {
        if (!sessions_.empty()) {
            const int newIndex = std::min(index, static_cast<int>(sessions_.size()) - 1);
            setCurrentSession(sessions_[static_cast<size_t>(newIndex)]->id());
        } else {
            currentSessionId_ = 0;
            emit currentSessionChanged(nullptr);
        }
    }
}

void SessionManager::closeAllSessions()
{
    while (!sessions_.empty()) {
        closeSession(sessions_.back()->id());
    }
}

void SessionManager::setCurrentSession(int id)
{
    if (currentSessionId_ == id && id != 0) {
        return;
    }

    currentSessionId_ = id;
    emit currentSessionChanged(currentSession());
}

Session *SessionManager::currentSession() const
{
    return sessionById(currentSessionId_);
}

Session *SessionManager::sessionById(int id) const
{
    for (const auto &session : sessions_) {
        if (session->id() == id) {
            return session.get();
        }
    }
    return nullptr;
}

QVector<Session *> SessionManager::sessions() const
{
    QVector<Session *> result;
    result.reserve(sessions_.size());
    for (const auto &session : sessions_) {
        result.append(session.get());
    }
    return result;
}

int SessionManager::currentSessionId() const
{
    return currentSessionId_;
}

int SessionManager::indexOfSession(int id) const
{
    for (int i = 0; i < sessions_.size(); ++i) {
        if (sessions_[i]->id() == id) {
            return i;
        }
    }
    return -1;
}
