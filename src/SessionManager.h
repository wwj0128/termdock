#pragma once

#include "Session.h"

#include <QObject>
#include <QVector>

#include <memory>
#include <vector>

class SessionManager : public QObject
{
    Q_OBJECT

public:
    explicit SessionManager(QObject *parent = nullptr);
    ~SessionManager() override;

    Session *createSession(const QString &workingDirectory = QString());
    void closeSession(int id);
    void closeAllSessions();
    void setCurrentSession(int id);

    Session *currentSession() const;
    Session *sessionById(int id) const;
    QVector<Session *> sessions() const;
    int currentSessionId() const;

signals:
    void sessionAdded(Session *session);
    void sessionRemoved(int sessionId);
    void currentSessionChanged(Session *session);
    void sessionUpdated(Session *session);
    void sessionTerminalUpdated(int sessionId);

private:
    int indexOfSession(int id) const;

    std::vector<std::unique_ptr<Session>> sessions_;
    int nextId_ = 1;
    int currentSessionId_ = 0;
};
