#pragma once

#include "SessionManager.h"
#include "TerminalView.h"

#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QStringList>
#include <QVector>

struct SavedDirectoryEntry {
    QString path;
    QString label;
};

class QAction;
class QCloseEvent;
class QEvent;
class QIcon;
class QMenu;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QSystemTrayIcon;
class QVBoxLayout;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    void closeEvent(QCloseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    void buildTrayIcon();
    QIcon createAppIcon() const;
    void showFromTray();
    void quitFromTray();
    void buildTitleBar(QVBoxLayout *rootLayout);
    void connectSignals();
    void installResizeEventFilters();
    int resizeEdgesAt(const QPoint &globalPos) const;
    void updateResizeCursor(int edges);
    void beginManualResize(int edges, const QPoint &globalPos);
    void updateManualResize(const QPoint &globalPos);
    void endManualResize();
    void createSession(const QString &workingDirectory = QString());
    void closeCurrentSession();
    void clearCurrentSession();
    void updateSessionListItem(Session *session);
    void selectSessionInList(int sessionId);
    void refreshSessionDirectoryList();
    void chooseSessionDirectory();
    void switchToDirectory(const QString &workingDirectory);
    void persistSessionDirectory(const QString &workingDirectory);
    void renameSessionDirectory(const QString &workingDirectory, const QString &label);
    void removeSessionDirectory(const QString &workingDirectory);
    QStringList savedSessionDirectories() const;
    void loadSavedSessionDirectories();
    void saveSessionDirectories() const;
    void activateSessionListItem(QListWidgetItem *item);
    void showSessionListMenu(const QPoint &pos);
    void renameSessionListItem(QListWidgetItem *item);
    void deleteSessionListItem(QListWidgetItem *item);
    void closeSessionListItem(QListWidgetItem *item);
    void buildSessionListItemWidget(QListWidgetItem *item, const QString &title, const QString &detail, const QString &badge, bool live);
    void refreshSessionCardSelection();
    void refreshStatus();
    QString sessionTitle(Session *session) const;
    QString directoryItemText(const QString &path, const QString &label) const;
    QString compactDirectoryContext(const QString &path) const;
    QString stateText(Session::State state) const;

    void updateStatusBadge();
    void updateMaximizeButton();
    bool isInTitleDragArea(const QPoint &position) const;
    void syncWindowTitle();

    SessionManager sessionManager_;
    QVector<SavedDirectoryEntry> savedSessionDirectories_;

    QWidget *titleBar_ = nullptr;
    QLabel *windowTitleLabel_ = nullptr;
    QPushButton *minimizeButton_ = nullptr;
    QPushButton *maximizeButton_ = nullptr;
    QPushButton *windowCloseButton_ = nullptr;
    bool draggingWindow_ = false;
    QPoint dragPosition_;
    bool resizingWindow_ = false;
    bool resizeCursorActive_ = false;
    int resizeEdges_ = 0;
    QRect resizeStartGeometry_;
    QPoint resizeStartGlobalPos_;
    QVector<QWidget *> resizeHandles_;

    QLineEdit *searchEdit_ = nullptr;
    QListWidget *sessionList_ = nullptr;
    QPushButton *newButton_ = nullptr;
    QLabel *currentLabel_ = nullptr;
    QLabel *terminalMetaLabel_ = nullptr;
    QLabel *statusBadge_ = nullptr;
    QPushButton *chooseDirectoryButton_ = nullptr;
    TerminalView *terminalView_ = nullptr;
    QLabel *terminalHintLabel_ = nullptr;
    QPushButton *clearButton_ = nullptr;
    QPushButton *copyButton_ = nullptr;
    QPushButton *pasteButton_ = nullptr;
    QPushButton *closeButton_ = nullptr;
    QSystemTrayIcon *trayIcon_ = nullptr;
    QMenu *trayMenu_ = nullptr;
    QAction *trayShowAction_ = nullptr;
    QAction *trayNewSessionAction_ = nullptr;
    QAction *trayQuitAction_ = nullptr;
    bool quittingFromTray_ = false;
};
