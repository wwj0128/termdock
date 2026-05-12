#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QSignalBlocker>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
constexpr int ResizeBorderWidth = 9;
constexpr int ResizeLeft = 0x1;
constexpr int ResizeRight = 0x2;
constexpr int ResizeTop = 0x4;
constexpr int ResizeBottom = 0x8;
constexpr int TitleBarHeight = 56;
constexpr int SessionIdRole = Qt::UserRole;
constexpr int ItemKindRole = Qt::UserRole + 1;
constexpr int DirectoryRole = Qt::UserRole + 2;
constexpr int LiveSessionItem = 1;
constexpr int SavedDirectoryItem = 2;
const QColor BackgroundTopColor(31, 33, 46);
const QColor BackgroundBottomColor(24, 26, 37);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    setWindowIcon(createAppIcon());
    buildTrayIcon();
    loadSavedSessionDirectories();
    refreshSessionDirectoryList();
    installEventFilter(this);
    connectSignals();
    refreshStatus();
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint);
}

MainWindow::~MainWindow()
{
    for (Session *session : sessionManager_.sessions()) {
        persistSessionDirectory(session->workingDirectory());
    }
    saveSessionDirectories();
    sessionManager_.closeAllSessions();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (sessionList_) {
        auto *card = qobject_cast<QWidget *>(watched);
        if (card && card->parentWidget() == sessionList_) {
            const int row = card->property("listRow").toInt();
            QListWidgetItem *item = row >= 0 ? sessionList_->item(row) : nullptr;
            if (item && event->type() == QEvent::MouseButtonPress) {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                sessionList_->setCurrentItem(item);
                if (mouseEvent->button() == Qt::LeftButton) {
                    activateSessionListItem(item);
                    return true;
                }
                if (mouseEvent->button() == Qt::RightButton) {
                    showSessionListMenu(sessionList_->visualItemRect(item).center());
                    return true;
                }
            }
        }
    }

    if (resizingWindow_) {
        if (event->type() == QEvent::MouseMove) {
            updateManualResize(static_cast<QMouseEvent *>(event)->globalPos());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                endManualResize();
                return true;
            }
        }
    }

    if (watched == titleBar_) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && isInTitleDragArea(mouseEvent->pos())) {
                isMaximized() ? showNormal() : showMaximized();
                updateMaximizeButton();
                return true;
            }
        }

        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const int edges = resizeEdgesAt(mouseEvent->globalPos());
            if (mouseEvent->button() == Qt::LeftButton && edges != 0) {
                beginManualResize(edges, mouseEvent->globalPos());
                return true;
            }
            if (mouseEvent->button() == Qt::LeftButton && isInTitleDragArea(mouseEvent->pos())) {
                draggingWindow_ = true;
                dragPosition_ = mouseEvent->globalPos() - frameGeometry().topLeft();
                return true;
            }
        }

        if (event->type() == QEvent::MouseMove && draggingWindow_) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (isMaximized()) {
                showNormal();
                updateMaximizeButton();
                dragPosition_ = QPoint(width() / 2, TitleBarHeight / 2);
            }
            move(mouseEvent->globalPos() - dragPosition_);
            return true;
        }

        if (event->type() == QEvent::MouseButtonRelease) {
            draggingWindow_ = false;
            return true;
        }
    }

    if (event->type() == QEvent::MouseMove) {
        updateResizeCursor(resizeEdgesAt(static_cast<QMouseEvent *>(event)->globalPos()));
    } else if (event->type() == QEvent::Leave && !resizingWindow_) {
        updateResizeCursor(0);
    } else if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const int edges = resizeEdgesAt(mouseEvent->globalPos());
        if (mouseEvent->button() == Qt::LeftButton && edges != 0) {
            beginManualResize(edges, mouseEvent->globalPos());
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        draggingWindow_ = false;
    }

    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::nativeEvent(const QByteArray &, void *message, long *result)
{
#ifdef Q_OS_WIN
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == WM_NCHITTEST && !isMaximized()) {
        RECT windowRect{};
        GetWindowRect(reinterpret_cast<HWND>(winId()), &windowRect);
        const LONG x = GET_X_LPARAM(msg->lParam);
        const LONG y = GET_Y_LPARAM(msg->lParam);
        const bool left = x >= windowRect.left && x < windowRect.left + ResizeBorderWidth;
        const bool right = x < windowRect.right && x >= windowRect.right - ResizeBorderWidth;
        const bool top = y >= windowRect.top && y < windowRect.top + ResizeBorderWidth;
        const bool bottom = y < windowRect.bottom && y >= windowRect.bottom - ResizeBorderWidth;

        if (top && left) {
            *result = HTTOPLEFT;
            return true;
        }
        if (top && right) {
            *result = HTTOPRIGHT;
            return true;
        }
        if (bottom && left) {
            *result = HTBOTTOMLEFT;
            return true;
        }
        if (bottom && right) {
            *result = HTBOTTOMRIGHT;
            return true;
        }
        if (left) {
            *result = HTLEFT;
            return true;
        }
        if (right) {
            *result = HTRIGHT;
            return true;
        }
        if (top) {
            *result = HTTOP;
            return true;
        }
        if (bottom) {
            *result = HTBOTTOM;
            return true;
        }
    }
#else
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}

void MainWindow::updateStatusBadge()
{
    if (!statusBadge_) {
        return;
    }

    const bool active = sessionManager_.currentSession() && sessionManager_.currentSession()->state() == Session::State::Running;
    statusBadge_->setText(active ? QStringLiteral("活跃") : QStringLiteral("空闲"));
    statusBadge_->setStyleSheet(QStringLiteral(
        "#statusBadge { color: #cfd5e8; border: 1px solid #44485c; border-radius: 12px; padding: 5px 10px; font-size: 10px; font-weight: 800; background: #2b2e3d; }"
    ));
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    QLinearGradient background(rect().topLeft(), rect().bottomLeft());
    background.setColorAt(0.0, BackgroundTopColor);
    background.setColorAt(1.0, BackgroundBottomColor);
    painter.fillRect(rect(), background);

    painter.setPen(QPen(QColor(59, 62, 78), 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    installResizeEventFilters();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!quittingFromTray_ && trayIcon_ && trayIcon_->isVisible()) {
        hide();
        event->ignore();
        return;
    }

    QMainWindow::closeEvent(event);
}

QIcon MainWindow::createAppIcon() const
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card(6, 6, 52, 52);
    QLinearGradient fill(card.topLeft(), card.bottomRight());
    fill.setColorAt(0.0, QColor(28, 34, 50));
    fill.setColorAt(1.0, QColor(14, 142, 218));
    painter.setBrush(fill);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(card, 13, 13);

    QPen promptPen(QColor(255, 255, 255, 235), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(promptPen);
    painter.drawLine(QPointF(20, 24), QPointF(29, 32));
    painter.drawLine(QPointF(20, 40), QPointF(29, 32));
    painter.drawLine(QPointF(36, 41), QPointF(48, 41));

    return QIcon(pixmap);
}

void MainWindow::buildTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    trayMenu_ = new QMenu(this);
    trayMenu_->setStyleSheet(QStringLiteral(
        "QMenu { background: #222431; border: 1px solid #3c4052; padding: 7px; color: #edf1ff; }"
        "QMenu::item { padding: 8px 28px 8px 13px; border-radius: 7px; margin: 1px; }"
        "QMenu::item:selected { background: #35394d; color: #ffffff; }"
        "QMenu::separator { height: 1px; background: #3c4052; margin: 5px 8px; }"
    ));

    trayShowAction_ = trayMenu_->addAction(QStringLiteral("显示主窗口"));
    trayNewSessionAction_ = trayMenu_->addAction(QStringLiteral("新建终端会话"));
    trayMenu_->addSeparator();
    trayQuitAction_ = trayMenu_->addAction(QStringLiteral("退出"));

    trayIcon_ = new QSystemTrayIcon(createAppIcon(), this);
    trayIcon_->setToolTip(QStringLiteral("终端舱"));
    trayIcon_->setContextMenu(trayMenu_);
    trayIcon_->show();

    connect(trayShowAction_, &QAction::triggered, this, &MainWindow::showFromTray);
    connect(trayNewSessionAction_, &QAction::triggered, this, [this] {
        showFromTray();
        createSession();
    });
    connect(trayQuitAction_, &QAction::triggered, this, &MainWindow::quitFromTray);
    connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            showFromTray();
        }
    });
}

void MainWindow::showFromTray()
{
    show();
    if (isMinimized()) {
        showNormal();
    }
    raise();
    activateWindow();
    terminalView_->setFocus();
}

void MainWindow::quitFromTray()
{
    quittingFromTray_ = true;
    QApplication::quit();
}

void MainWindow::buildTitleBar(QVBoxLayout *rootLayout)
{
    titleBar_ = new QWidget(this);
    titleBar_->setObjectName(QStringLiteral("titleBar"));
    titleBar_->setFixedHeight(TitleBarHeight);
    titleBar_->setStyleSheet(QStringLiteral(
        "#titleBar { background: #222431; border-bottom: 1px solid #373a49; }"
        "#windowTitleLabel { color: #ffffff; font-size: 20px; font-weight: 800; }"
        "#titleBar QPushButton { min-width: 36px; min-height: 28px; border: 0; border-radius: 8px; background: transparent; color: #c5c8d6; font-size: 13px; font-weight: 700; }"
        "#titleBar QPushButton:hover { background: #343749; color: #ffffff; }"
        "#titleBar QPushButton:pressed { background: #191b25; }"
        "#titleBar QPushButton#windowCloseButton:hover { background: #8d3945; color: #ffffff; }"
    ));
    titleBar_->installEventFilter(this);
    setMouseTracking(true);

    auto *titleLayout = new QHBoxLayout(titleBar_);
    titleLayout->setContentsMargins(24, 8, 12, 8);
    titleLayout->setSpacing(10);

    auto *markLabel = new QLabel(QStringLiteral("▣"), titleBar_);
    markLabel->setStyleSheet(QStringLiteral("color: #7bc7ff; font-size: 18px;"));
    titleLayout->addWidget(markLabel);

    windowTitleLabel_ = new QLabel(QStringLiteral("终端舱"), titleBar_);
    windowTitleLabel_->setObjectName(QStringLiteral("windowTitleLabel"));
    titleLayout->addWidget(windowTitleLabel_, 1);

    minimizeButton_ = new QPushButton(QStringLiteral("—"), titleBar_);
    maximizeButton_ = new QPushButton(QStringLiteral("□"), titleBar_);
    windowCloseButton_ = new QPushButton(QStringLiteral("×"), titleBar_);
    windowCloseButton_->setObjectName(QStringLiteral("windowCloseButton"));
    titleLayout->addWidget(minimizeButton_);
    titleLayout->addWidget(maximizeButton_);
    titleLayout->addWidget(windowCloseButton_);

    rootLayout->addWidget(titleBar_);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("终端舱"));
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("rootWindow"));
    root->setStyleSheet(QStringLiteral("#rootWindow { background: transparent; }"));
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    setCentralWidget(root);
    buildTitleBar(rootLayout);

    auto *splitter = new QSplitter(Qt::Horizontal, root);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    rootLayout->addWidget(splitter, 1);

    auto *leftPanel = new QFrame(splitter);
    leftPanel->setObjectName(QStringLiteral("sessionPanel"));
    leftPanel->setStyleSheet(QStringLiteral(
        "#sessionPanel { background: #1b1d29; border-right: 1px solid #373a49; }"
        "#brandLabel { color: #ffffff; font-size: 13px; font-weight: 700; }"
        "#brandSubLabel { color: #8d92a6; font-size: 11px; font-weight: 600; }"
        "#sectionLabel { color: #cfd4e6; font-size: 12px; font-weight: 700; padding-top: 8px; }"
        "#panelMetric { background: #2b2e3f; border-radius: 10px; color: #c3c8d9; padding: 7px 10px; font-size: 11px; }"
        "QLineEdit { padding: 9px 11px; border: 1px solid #383c4f; border-radius: 10px; background: #151720; color: #eef2ff; selection-background-color: #1989d8; }"
        "QLineEdit:focus { border-color: #168be0; background: #181b27; }"
        "QLineEdit::placeholder { color: #74798c; }"
        "QListWidget { background: transparent; border: 0; outline: 0; color: #e5e9f6; }"
        "QListWidget::item { padding: 0; border: 0; margin: 4px 0; background: transparent; }"
        "QListWidget::item:hover { background: transparent; }"
        "QListWidget::item:selected { background: transparent; }"
        "QListWidget QScrollBar:vertical { width: 10px; background: #1b1d29; border: 0; margin: 2px 0; }"
        "QListWidget QScrollBar::handle:vertical { min-height: 34px; border-radius: 5px; background: #4b5064; }"
        "QListWidget QScrollBar::handle:vertical:hover { background: #62687d; }"
        "QListWidget QScrollBar::add-line:vertical, QListWidget QScrollBar::sub-line:vertical { height: 0; border: 0; background: transparent; }"
        "QListWidget QScrollBar::add-page:vertical, QListWidget QScrollBar::sub-page:vertical { background: transparent; }"
        "QListWidget QScrollBar:horizontal { height: 0; background: transparent; }"
        "QPushButton { padding: 10px 12px; border: 0; border-radius: 18px; background: #1287d8; color: #ffffff; font-weight: 800; }"
        "QPushButton:hover { background: #2097ec; }"
        "QPushButton:pressed { background: #0f70b5; }"
    ));
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(14, 16, 14, 14);
    leftLayout->setSpacing(8);

    auto *brandLabel = new QLabel(QStringLiteral("活动会话"), leftPanel);
    brandLabel->setObjectName(QStringLiteral("brandLabel"));
    auto *brandSubLabel = new QLabel(QStringLiteral("活动会话实时运行，保存目录点击后启动"), leftPanel);
    brandSubLabel->setObjectName(QStringLiteral("brandSubLabel"));
    leftLayout->addWidget(brandLabel);
    leftLayout->addWidget(brandSubLabel);

    searchEdit_ = new QLineEdit(leftPanel);
    searchEdit_->setPlaceholderText(QStringLiteral("搜索会话 / 目录 / PID"));
    leftLayout->addWidget(searchEdit_);

    auto *sectionLabel = new QLabel(QStringLiteral("LIVE / SAVED"), leftPanel);
    sectionLabel->setObjectName(QStringLiteral("sectionLabel"));
    leftLayout->addWidget(sectionLabel);

    sessionList_ = new QListWidget(leftPanel);
    sessionList_->setSelectionMode(QAbstractItemView::SingleSelection);
    sessionList_->setSpacing(2);
    sessionList_->setContextMenuPolicy(Qt::CustomContextMenu);
    leftLayout->addWidget(sessionList_, 1);

    newButton_ = new QPushButton(QStringLiteral("+ 新建终端会话"), leftPanel);
    leftLayout->addWidget(newButton_);

    auto *rightPanel = new QWidget(splitter);
    rightPanel->setObjectName(QStringLiteral("terminalPanel"));
    rightPanel->setStyleSheet(QStringLiteral(
        "#terminalPanel { background: #171923; }"
        "#terminalHeader { background: #202230; border-bottom: 1px solid #363949; }"
        "#terminalDeck { background: #0a0b10; border: 1px solid #343746; border-radius: 10px; }"
        "#deckRail { background: transparent; border: 0; color: #b4bad0; font-size: 11px; font-weight: 700; padding: 0; }"
        "#currentSessionLabel { color: #f8fbff; font-size: 13px; font-weight: 800; }"
        "#terminalMetaLabel { color: #a0a6bb; font-size: 11px; font-weight: 600; }"
        "#statusBadge { color: #bfc6dc; border: 1px solid #44485c; border-radius: 12px; padding: 5px 10px; font-size: 10px; font-weight: 800; background: #2b2e3d; }"
        "#terminalHintLabel { color: #858ba0; padding: 4px 8px; font-size: 11px; }"
        "#terminalPanel QPushButton { padding: 7px 12px; border: 0; border-radius: 14px; background: #303343; color: #eef2ff; font-weight: 700; }"
        "#terminalPanel QPushButton:hover { background: #3b4053; }"
        "#terminalPanel QPushButton:pressed { background: #252838; }"
        "#terminalPanel QPushButton:disabled { color: #70768a; background: #242633; }"
        "#terminalPanel QPushButton#dangerButton { color: #ffe5e8; background: #373040; }"
    ));
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(12, 12, 12, 8);
    rightLayout->setSpacing(10);

    auto *headerFrame = new QFrame(rightPanel);
    headerFrame->setObjectName(QStringLiteral("terminalHeader"));
    auto *headerLayout = new QHBoxLayout(headerFrame);
    headerLayout->setContentsMargins(18, 9, 12, 9);
    headerLayout->setSpacing(10);

    auto *titleLayout = new QVBoxLayout;
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(2);
    currentLabel_ = new QLabel(QStringLiteral("未选择会话"), headerFrame);
    currentLabel_->setObjectName(QStringLiteral("currentSessionLabel"));
    terminalMetaLabel_ = new QLabel(QStringLiteral("选择保存目录不会自动启动；点击目录卡片后创建会话。"), headerFrame);
    terminalMetaLabel_->setObjectName(QStringLiteral("terminalMetaLabel"));
    titleLayout->addWidget(currentLabel_);
    titleLayout->addWidget(terminalMetaLabel_);
    headerLayout->addLayout(titleLayout, 1);

    statusBadge_ = new QLabel(QStringLiteral("就绪"), headerFrame);
    statusBadge_->setObjectName(QStringLiteral("statusBadge"));
    headerLayout->addWidget(statusBadge_);

    chooseDirectoryButton_ = new QPushButton(QStringLiteral("选择目录"), headerFrame);
    chooseDirectoryButton_->setToolTip(QStringLiteral("选择目录并切换到该工作目录"));
    headerLayout->addWidget(chooseDirectoryButton_);

    clearButton_ = new QPushButton(QStringLiteral("清屏"), headerFrame);
    copyButton_ = new QPushButton(QStringLiteral("复制"), headerFrame);
    pasteButton_ = new QPushButton(QStringLiteral("粘贴"), headerFrame);
    closeButton_ = new QPushButton(QStringLiteral("关闭"), headerFrame);
    clearButton_->setToolTip(QStringLiteral("清空当前终端显示"));
    copyButton_->setToolTip(QStringLiteral("复制当前选择的文本"));
    pasteButton_->setToolTip(QStringLiteral("发送剪贴板内容到终端"));
    closeButton_->setToolTip(QStringLiteral("关闭当前会话"));
    closeButton_->setObjectName(QStringLiteral("dangerButton"));
    headerLayout->addWidget(clearButton_);
    headerLayout->addWidget(copyButton_);
    headerLayout->addWidget(pasteButton_);
    headerLayout->addWidget(closeButton_);
    rightLayout->addWidget(headerFrame);

    terminalView_ = new TerminalView(rightPanel);
    auto *terminalDeck = new QFrame(rightPanel);
    terminalDeck->setObjectName(QStringLiteral("terminalDeck"));
    auto *deckLayout = new QVBoxLayout(terminalDeck);
    deckLayout->setContentsMargins(10, 10, 10, 10);
    deckLayout->setSpacing(8);
    auto *deckRail = new QLabel(QStringLiteral("终端输出"), terminalDeck);
    deckRail->setObjectName(QStringLiteral("deckRail"));
    deckLayout->addWidget(deckRail);
    deckLayout->addWidget(terminalView_, 1);
    rightLayout->addWidget(terminalDeck, 1);

    terminalHintLabel_ = new QLabel(QStringLiteral("左键拖选文本，右键复制；无选择时右键粘贴。Ctrl+C 可复制选择或中断命令。"), rightPanel);
    terminalHintLabel_->setObjectName(QStringLiteral("terminalHintLabel"));
    rightLayout->addWidget(terminalHintLabel_);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setHandleWidth(1);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({282, 1020});

    statusBar()->showMessage(QStringLiteral("共 0 个会话"));

    auto *newShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+T")), this);
    connect(newShortcut, &QShortcut::activated, this, [this] {
        createSession();
    });

    auto *closeShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+W")), this);
    connect(closeShortcut, &QShortcut::activated, this, &MainWindow::closeCurrentSession);

    installResizeEventFilters();
}

void MainWindow::installResizeEventFilters()
{
    if (resizeHandles_.isEmpty()) {
        resizeHandles_.reserve(8);
        for (int i = 0; i < 8; ++i) {
            auto *handle = new QWidget(this);
            handle->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            handle->setAttribute(Qt::WA_NoSystemBackground, true);
            handle->setMouseTracking(true);
            handle->installEventFilter(this);
            handle->raise();
            resizeHandles_.append(handle);
        }
    }

    const int edge = ResizeBorderWidth;
    const int corner = ResizeBorderWidth * 2;
    const int w = width();
    const int h = height();
    const int middleWidth = qMax(0, w - corner * 2);
    const int middleHeight = qMax(0, h - corner * 2);

    resizeHandles_[0]->setGeometry(0, 0, corner, corner);
    resizeHandles_[1]->setGeometry(corner, 0, middleWidth, edge);
    resizeHandles_[2]->setGeometry(w - corner, 0, corner, corner);
    resizeHandles_[3]->setGeometry(0, corner, edge, middleHeight);
    resizeHandles_[4]->setGeometry(w - edge, corner, edge, middleHeight);
    resizeHandles_[5]->setGeometry(0, h - corner, corner, corner);
    resizeHandles_[6]->setGeometry(corner, h - edge, middleWidth, edge);
    resizeHandles_[7]->setGeometry(w - corner, h - corner, corner, corner);

    const bool visible = !isMaximized();
    for (QWidget *handle : resizeHandles_) {
        handle->setVisible(visible);
        handle->raise();
    }
    resizeHandles_[0]->setCursor(Qt::SizeFDiagCursor);
    resizeHandles_[1]->setCursor(Qt::SizeVerCursor);
    resizeHandles_[2]->setCursor(Qt::SizeBDiagCursor);
    resizeHandles_[3]->setCursor(Qt::SizeHorCursor);
    resizeHandles_[4]->setCursor(Qt::SizeHorCursor);
    resizeHandles_[5]->setCursor(Qt::SizeBDiagCursor);
    resizeHandles_[6]->setCursor(Qt::SizeVerCursor);
    resizeHandles_[7]->setCursor(Qt::SizeFDiagCursor);
}

int MainWindow::resizeEdgesAt(const QPoint &globalPos) const
{
    if (isMaximized()) {
        return 0;
    }

    const QRect windowRect = frameGeometry();
    if (!windowRect.adjusted(-ResizeBorderWidth, -ResizeBorderWidth, ResizeBorderWidth, ResizeBorderWidth).contains(globalPos)) {
        return 0;
    }

    int edges = 0;
    if (globalPos.x() <= windowRect.left() + ResizeBorderWidth) {
        edges |= ResizeLeft;
    } else if (globalPos.x() >= windowRect.right() - ResizeBorderWidth) {
        edges |= ResizeRight;
    }
    if (globalPos.y() <= windowRect.top() + ResizeBorderWidth) {
        edges |= ResizeTop;
    } else if (globalPos.y() >= windowRect.bottom() - ResizeBorderWidth) {
        edges |= ResizeBottom;
    }
    return edges;
}

void MainWindow::updateResizeCursor(int edges)
{
    if (isMaximized()) {
        edges = 0;
    }

    Qt::CursorShape shape = Qt::ArrowCursor;
    if ((edges & ResizeLeft && edges & ResizeTop) || (edges & ResizeRight && edges & ResizeBottom)) {
        shape = Qt::SizeFDiagCursor;
    } else if ((edges & ResizeRight && edges & ResizeTop) || (edges & ResizeLeft && edges & ResizeBottom)) {
        shape = Qt::SizeBDiagCursor;
    } else if (edges & (ResizeLeft | ResizeRight)) {
        shape = Qt::SizeHorCursor;
    } else if (edges & (ResizeTop | ResizeBottom)) {
        shape = Qt::SizeVerCursor;
    }

    if (edges == 0) {
        if (resizeCursorActive_) {
            unsetCursor();
            resizeCursorActive_ = false;
        }
        return;
    }

    setCursor(shape);
    resizeCursorActive_ = true;
}

void MainWindow::beginManualResize(int edges, const QPoint &globalPos)
{
    if (edges == 0 || isMaximized()) {
        return;
    }

    resizingWindow_ = true;
    resizeEdges_ = edges;
    resizeStartGeometry_ = geometry();
    resizeStartGlobalPos_ = globalPos;
    updateResizeCursor(edges);
    grabMouse(cursor());
}

void MainWindow::updateManualResize(const QPoint &globalPos)
{
    if (!resizingWindow_) {
        return;
    }

    const QPoint delta = globalPos - resizeStartGlobalPos_;
    QRect nextGeometry = resizeStartGeometry_;
    const QSize minSize = minimumSize().expandedTo(QSize(720, 440));

    if (resizeEdges_ & ResizeLeft) {
        const int maxLeft = resizeStartGeometry_.right() - minSize.width() + 1;
        nextGeometry.setLeft(qMin(resizeStartGeometry_.left() + delta.x(), maxLeft));
    }
    if (resizeEdges_ & ResizeRight) {
        nextGeometry.setRight(qMax(resizeStartGeometry_.right() + delta.x(), resizeStartGeometry_.left() + minSize.width() - 1));
    }
    if (resizeEdges_ & ResizeTop) {
        const int maxTop = resizeStartGeometry_.bottom() - minSize.height() + 1;
        nextGeometry.setTop(qMin(resizeStartGeometry_.top() + delta.y(), maxTop));
    }
    if (resizeEdges_ & ResizeBottom) {
        nextGeometry.setBottom(qMax(resizeStartGeometry_.bottom() + delta.y(), resizeStartGeometry_.top() + minSize.height() - 1));
    }

    setGeometry(nextGeometry);
}

void MainWindow::endManualResize()
{
    if (!resizingWindow_) {
        return;
    }

    releaseMouse();
    resizingWindow_ = false;
    resizeEdges_ = 0;
    updateResizeCursor(resizeEdgesAt(QCursor::pos()));
}

void MainWindow::connectSignals()
{
    connect(newButton_, &QPushButton::clicked, this, [this] {
        createSession();
    });
    connect(minimizeButton_, &QPushButton::clicked, this, &MainWindow::showMinimized);
    connect(maximizeButton_, &QPushButton::clicked, this, [this] {
        isMaximized() ? showNormal() : showMaximized();
        updateMaximizeButton();
    });
    connect(windowCloseButton_, &QPushButton::clicked, this, &MainWindow::close);
    connect(closeButton_, &QPushButton::clicked, this, &MainWindow::closeCurrentSession);
    connect(chooseDirectoryButton_, &QPushButton::clicked, this, &MainWindow::chooseSessionDirectory);
    connect(clearButton_, &QPushButton::clicked, this, &MainWindow::clearCurrentSession);
    connect(copyButton_, &QPushButton::clicked, this, [this] {
        terminalView_->copySelection();
        terminalView_->setFocus();
    });
    connect(pasteButton_, &QPushButton::clicked, this, [this] {
        terminalView_->pasteFromClipboard();
        terminalView_->setFocus();
    });
    connect(terminalView_, &TerminalView::interactionMessage, this, [this](const QString &message) {
        statusBar()->showMessage(message, 2500);
    });
    connect(terminalView_, &TerminalView::rawInputRequested, this, [this](const QString &text) {
        terminalView_->syncSizeToPty();
        if (Session *session = sessionManager_.currentSession()) {
            session->sendRawText(text);
        }
    });
    connect(terminalView_, &TerminalView::terminalSizeChanged, this, [this](int columns, int rows) {
        if (Session *session = sessionManager_.currentSession()) {
            session->resizeTerminal(columns, rows);
        }
    });

    connect(sessionList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        activateSessionListItem(item);
    });
    connect(sessionList_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *, QListWidgetItem *) {
        refreshSessionCardSelection();
    });
    connect(sessionList_, &QListWidget::customContextMenuRequested, this, &MainWindow::showSessionListMenu);

    connect(searchEdit_, &QLineEdit::textChanged, this, [this](const QString &filter) {
        for (int row = 0; row < sessionList_->count(); ++row) {
            QListWidgetItem *item = sessionList_->item(row);
            item->setHidden(!item->text().contains(filter, Qt::CaseInsensitive));
        }
    });

    connect(&sessionManager_, &SessionManager::sessionAdded, this, [this](Session *session) {
        persistSessionDirectory(session->workingDirectory());
        refreshSessionDirectoryList();
        refreshStatus();
    });

    connect(&sessionManager_, &SessionManager::sessionRemoved, this, [this](int) {
        refreshSessionDirectoryList();
        refreshStatus();
    });

    connect(&sessionManager_, &SessionManager::currentSessionChanged, this, [this](Session *session) {
        if (!session) {
            currentLabel_->setText(QStringLiteral("未选择会话"));
            terminalMetaLabel_->setText(QStringLiteral("选择保存目录不会自动启动；点击目录卡片后创建会话。"));
            terminalView_->setBuffer(nullptr);
            syncWindowTitle();
            refreshStatus();
            return;
        }

        selectSessionInList(session->id());
        currentLabel_->setText(session->terminalTitle().isEmpty() ? session->name() : session->terminalTitle());
        terminalMetaLabel_->setText(QStringLiteral("PID %1 · %2 · %3x%4")
            .arg(session->processId())
            .arg(stateText(session->state()))
            .arg(session->terminalBuffer()->columns())
            .arg(session->terminalBuffer()->rows()));
        terminalView_->setBuffer(session->terminalBuffer());
        terminalView_->syncSizeToPty();
        terminalView_->setFocus();
        syncWindowTitle();
        refreshStatus();
    });

    connect(&sessionManager_, &SessionManager::sessionUpdated, this, &MainWindow::updateSessionListItem);

    connect(&sessionManager_, &SessionManager::sessionTerminalUpdated, this, [this](int sessionId) {
        if (sessionManager_.currentSessionId() == sessionId) {
            terminalView_->refresh();
            if (Session *session = sessionManager_.currentSession()) {
                terminalMetaLabel_->setText(QStringLiteral("PID %1 · %2 · %3x%4")
                    .arg(session->processId())
                    .arg(stateText(session->state()))
                    .arg(session->terminalBuffer()->columns())
                    .arg(session->terminalBuffer()->rows()));
            }
        }
    });
}

void MainWindow::createSession(const QString &workingDirectory)
{
    const QString directory = workingDirectory.isEmpty()
        ? (sessionManager_.currentSession() ? sessionManager_.currentSession()->workingDirectory() : QDir::currentPath())
        : workingDirectory;
    sessionManager_.createSession(directory);
    persistSessionDirectory(directory);
    refreshSessionDirectoryList();
    terminalView_->setFocus();
}

void MainWindow::refreshSessionDirectoryList()
{
    const int currentSessionId = sessionManager_.currentSessionId();
    QSignalBlocker blocker(sessionList_);
    sessionList_->clear();

    QSet<QString> activeDirectories;
    const QVector<Session *> sessions = sessionManager_.sessions();
    for (Session *session : sessions) {
        const QString directory = QDir::cleanPath(session->workingDirectory().isEmpty() ? QDir::currentPath() : session->workingDirectory());
        activeDirectories.insert(directory);

        auto *item = new QListWidgetItem(sessionTitle(session));
        item->setToolTip(directory);
        item->setData(SessionIdRole, session->id());
        item->setData(ItemKindRole, LiveSessionItem);
        item->setData(DirectoryRole, directory);
        item->setData(Qt::UserRole + 3, QString());
        item->setSizeHint(QSize(0, 74));
        sessionList_->addItem(item);
        buildSessionListItemWidget(
            item,
            session->terminalTitle().isEmpty()
                ? (QFileInfo(directory).fileName().isEmpty() ? session->name() : QFileInfo(directory).fileName())
                : session->terminalTitle(),
            QStringLiteral("%1    PID %2").arg(compactDirectoryContext(directory)).arg(session->processId()),
            stateText(session->state()),
            true);
    }

    for (const SavedDirectoryEntry &entry : std::as_const(savedSessionDirectories_)) {
        const QString directory = QDir::cleanPath(entry.path);
        if (directory.isEmpty() || activeDirectories.contains(directory)) {
            continue;
        }

        auto *item = new QListWidgetItem(directoryItemText(directory, entry.label));
        item->setToolTip(directory);
        item->setData(SessionIdRole, 0);
        item->setData(ItemKindRole, SavedDirectoryItem);
        item->setData(DirectoryRole, directory);
        item->setData(Qt::UserRole + 3, entry.label);
        item->setSizeHint(QSize(0, 70));
        sessionList_->addItem(item);
        const QString title = entry.label.isEmpty() || entry.label == directory
            ? (QFileInfo(directory).fileName().isEmpty() ? directory : QFileInfo(directory).fileName())
            : entry.label;
        buildSessionListItemWidget(item, title, QStringLiteral("%1    点击启动").arg(compactDirectoryContext(directory)), QStringLiteral("保存"), false);
    }

    if (currentSessionId != 0) {
        selectSessionInList(currentSessionId);
    } else {
        sessionList_->clearSelection();
        sessionList_->setCurrentItem(nullptr);
    }
    refreshSessionCardSelection();
}

void MainWindow::buildSessionListItemWidget(QListWidgetItem *item, const QString &title, const QString &detail, const QString &badge, bool live)
{
    auto *card = new QFrame(sessionList_);
    card->setObjectName(live ? QStringLiteral("liveSessionCard") : QStringLiteral("savedSessionCard"));
    card->setCursor(Qt::PointingHandCursor);
    card->setStyleSheet(QStringLiteral(
        "#liveSessionCard, #savedSessionCard { background: #242635; border: 1px solid #35384a; border-radius: 11px; }"
        "#liveSessionCard[selected='true'], #savedSessionCard[selected='true'] { background: #213247; border: 1px solid #168be0; }"
        "#liveSessionCard:hover, #savedSessionCard:hover { background: #2a2d3f; border-color: #4d5368; }"
        "#liveSessionCard QLabel#sessionTitle, #savedSessionCard QLabel#sessionTitle { color: #f4f7ff; font-size: 13px; font-weight: 800; }"
        "#liveSessionCard QLabel#sessionDetail, #savedSessionCard QLabel#sessionDetail { color: #9fa6bb; font-size: 11px; }"
        "#liveSessionCard QLabel#sessionBadge { color: #7bf0a6; background: #173325; border-radius: 9px; padding: 3px 7px; font-size: 10px; font-weight: 800; }"
        "#savedSessionCard QLabel#sessionBadge { color: #b9c3dd; background: #303343; border-radius: 9px; padding: 3px 7px; font-size: 10px; font-weight: 800; }"
        "#liveSessionCard QPushButton { min-width: 24px; max-width: 24px; min-height: 24px; max-height: 24px; border: 0; border-radius: 12px; background: transparent; color: #8d93a8; font-size: 16px; font-weight: 700; }"
        "#liveSessionCard QPushButton:hover { background: #3b3040; color: #ffb4bf; }"
    ));

    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(12, 9, 10, 9);
    layout->setSpacing(9);

    auto *statusDot = new QLabel(live ? QStringLiteral("●") : QStringLiteral("○"), card);
    statusDot->setStyleSheet(live ? QStringLiteral("color: #54df86; font-size: 12px;") : QStringLiteral("color: #687085; font-size: 12px;"));
    layout->addWidget(statusDot);

    auto *textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(4);
    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("sessionTitle"));
    titleLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    auto *detailLabel = new QLabel(detail, card);
    detailLabel->setObjectName(QStringLiteral("sessionDetail"));
    detailLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    titleLabel->setToolTip(item->toolTip());
    detailLabel->setToolTip(item->toolTip());
    textLayout->addWidget(titleLabel);
    textLayout->addWidget(detailLabel);
    layout->addLayout(textLayout, 1);

    auto *badgeLabel = new QLabel(badge, card);
    badgeLabel->setObjectName(QStringLiteral("sessionBadge"));
    layout->addWidget(badgeLabel);

    if (live) {
        auto *closeButton = new QPushButton(QStringLiteral("×"), card);
        closeButton->setToolTip(QStringLiteral("关闭会话"));
        connect(closeButton, &QPushButton::clicked, this, [this, item] {
            closeSessionListItem(item);
        });
        layout->addWidget(closeButton);
    }

    sessionList_->setItemWidget(item, card);
    card->installEventFilter(this);
    card->setProperty("listRow", sessionList_->row(item));
    card->setProperty("selected", item == sessionList_->currentItem());
    card->style()->unpolish(card);
    card->style()->polish(card);
}

void MainWindow::refreshSessionCardSelection()
{
    for (int row = 0; row < sessionList_->count(); ++row) {
        QListWidgetItem *item = sessionList_->item(row);
        QWidget *card = sessionList_->itemWidget(item);
        if (!card) {
            continue;
        }
        card->setProperty("selected", item == sessionList_->currentItem());
        card->setProperty("listRow", row);
        card->style()->unpolish(card);
        card->style()->polish(card);
        card->update();
    }
}

void MainWindow::chooseSessionDirectory()
{
    const QString startDirectory = sessionManager_.currentSession()
        ? sessionManager_.currentSession()->workingDirectory()
        : QDir::currentPath();
    const QString directory = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择工作目录"),
        startDirectory,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (directory.isEmpty()) {
        terminalView_->setFocus();
        return;
    }

    switchToDirectory(directory);
}

void MainWindow::switchToDirectory(const QString &workingDirectory)
{
    const QString directory = QDir::cleanPath(workingDirectory);
    if (directory.isEmpty()) {
        return;
    }

    for (Session *session : sessionManager_.sessions()) {
        if (QDir::cleanPath(session->workingDirectory()) == directory) {
            sessionManager_.setCurrentSession(session->id());
            terminalView_->setFocus();
            return;
        }
    }

    createSession(directory);
}

void MainWindow::persistSessionDirectory(const QString &workingDirectory)
{
    const QString directory = QDir::cleanPath(workingDirectory);
    if (directory.isEmpty()) {
        return;
    }

    for (SavedDirectoryEntry &entry : savedSessionDirectories_) {
        if (QDir::cleanPath(entry.path) == directory) {
            if (entry.label.isEmpty()) {
                entry.label = QFileInfo(directory).fileName().isEmpty() ? directory : QFileInfo(directory).fileName();
            }
            saveSessionDirectories();
            return;
        }
    }

    SavedDirectoryEntry entry;
    entry.path = directory;
    entry.label = QFileInfo(directory).fileName().isEmpty() ? directory : QFileInfo(directory).fileName();
    savedSessionDirectories_.prepend(entry);
    while (savedSessionDirectories_.size() > 8) {
        savedSessionDirectories_.removeLast();
    }
    saveSessionDirectories();
}

void MainWindow::renameSessionDirectory(const QString &workingDirectory, const QString &label)
{
    const QString directory = QDir::cleanPath(workingDirectory);
    for (SavedDirectoryEntry &entry : savedSessionDirectories_) {
        if (QDir::cleanPath(entry.path) == directory) {
            entry.label = label.trimmed();
            saveSessionDirectories();
            return;
        }
    }
}

void MainWindow::removeSessionDirectory(const QString &workingDirectory)
{
    const QString directory = QDir::cleanPath(workingDirectory);
    for (int i = 0; i < savedSessionDirectories_.size(); ++i) {
        if (QDir::cleanPath(savedSessionDirectories_[i].path) == directory) {
            savedSessionDirectories_.removeAt(i);
            saveSessionDirectories();
            return;
        }
    }
}

QStringList MainWindow::savedSessionDirectories() const
{
    QStringList result;
    result.reserve(savedSessionDirectories_.size());
    for (const SavedDirectoryEntry &entry : savedSessionDirectories_) {
        result.append(entry.path);
    }
    return result;
}

void MainWindow::loadSavedSessionDirectories()
{
    QSettings settings(QStringLiteral("soso"), QStringLiteral("QTermWidget"));
    const auto raw = settings.value(QStringLiteral("recentSessionDirectories")).toList();
    savedSessionDirectories_.clear();

    for (const QVariant &value : raw) {
        const QVariantMap map = value.toMap();
        const QString path = QDir::cleanPath(map.value(QStringLiteral("path")).toString());
        if (path.isEmpty()) {
            continue;
        }

        SavedDirectoryEntry entry;
        entry.path = path;
        entry.label = map.value(QStringLiteral("label")).toString().trimmed();
        if (entry.label.isEmpty()) {
            entry.label = QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName();
        }
        savedSessionDirectories_.append(entry);
    }
}

void MainWindow::saveSessionDirectories() const
{
    QSettings settings(QStringLiteral("soso"), QStringLiteral("QTermWidget"));
    QVariantList values;
    values.reserve(savedSessionDirectories_.size());
    for (const SavedDirectoryEntry &entry : savedSessionDirectories_) {
        QVariantMap map;
        map.insert(QStringLiteral("path"), entry.path);
        map.insert(QStringLiteral("label"), entry.label);
        values.append(map);
    }
    settings.setValue(QStringLiteral("recentSessionDirectories"), values);
}

void MainWindow::activateSessionListItem(QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    const int kind = item->data(ItemKindRole).toInt();
    const QString directory = QDir::cleanPath(item->data(DirectoryRole).toString());
    const int sessionId = item->data(SessionIdRole).toInt();

    if (kind == LiveSessionItem && sessionId != 0) {
        sessionManager_.setCurrentSession(sessionId);
        return;
    }

    if (directory.isEmpty()) {
        return;
    }

    for (Session *session : sessionManager_.sessions()) {
        if (QDir::cleanPath(session->workingDirectory()) == directory) {
            sessionManager_.setCurrentSession(session->id());
            return;
        }
    }

    createSession(directory);
}

void MainWindow::showSessionListMenu(const QPoint &pos)
{
    QListWidgetItem *item = sessionList_->itemAt(pos);
    if (!item) {
        return;
    }

    QMenu menu(this);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setStyleSheet(QStringLiteral(
        "QMenu {"
        " background: #222431;"
        " border: 1px solid #3c4052;"
        " border-radius: 10px;"
        " padding: 7px;"
        " color: #edf1ff;"
        "}"
        "QMenu::item {"
        " padding: 8px 28px 8px 13px;"
        " border-radius: 7px;"
        " margin: 1px;"
        " background: transparent;"
        "}"
        "QMenu::item:selected {"
        " background: #35394d;"
        " color: #ffffff;"
        "}"
        "QMenu::separator {"
        " height: 1px;"
        " background: #3c4052;"
        " margin: 5px 8px;"
        "}"
    ));
    QAction *openAction = menu.addAction(QStringLiteral("打开"));
    QAction *renameAction = nullptr;
    QAction *deleteAction = nullptr;
    QAction *closeAction = nullptr;

    const int kind = item->data(ItemKindRole).toInt();
    if (kind == SavedDirectoryItem) {
        renameAction = menu.addAction(QStringLiteral("重命名"));
        deleteAction = menu.addAction(QStringLiteral("删除"));
    } else if (kind == LiveSessionItem) {
        closeAction = menu.addAction(QStringLiteral("关闭会话"));
    }

    QAction *chosen = menu.exec(sessionList_->viewport()->mapToGlobal(pos));
    if (!chosen) {
        return;
    }

    if (chosen == openAction) {
        activateSessionListItem(item);
    } else if (chosen == renameAction) {
        renameSessionListItem(item);
    } else if (chosen == deleteAction) {
        deleteSessionListItem(item);
    } else if (chosen == closeAction) {
        closeSessionListItem(item);
    }
}

void MainWindow::renameSessionListItem(QListWidgetItem *item)
{
    if (!item || item->data(ItemKindRole).toInt() != SavedDirectoryItem) {
        return;
    }

    const QString directory = item->data(DirectoryRole).toString();
    const QString currentLabel = item->data(Qt::UserRole + 3).toString();
    auto *editor = new QLineEdit(sessionList_);
    editor->setText(currentLabel.isEmpty() ? directory : currentLabel);
    editor->setFrame(false);
    editor->setMinimumHeight(38);
    item->setSizeHint(QSize(0, 68));
    editor->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        " background: #252838;"
        " color: #f5f7ff;"
        " border: 1px solid #168be0;"
        " border-radius: 10px;"
        " padding: 9px 12px;"
        " selection-background-color: #1989d8;"
        "}"
    ));
    sessionList_->setItemWidget(item, editor);
    editor->selectAll();
    editor->setFocus();

    const auto finishEdit = [this, item, editor, directory] {
        const QString label = editor->text().trimmed();
        renameSessionDirectory(directory, label);
        sessionList_->removeItemWidget(item);
        refreshSessionDirectoryList();
    };

    connect(editor, &QLineEdit::editingFinished, this, finishEdit);
}

void MainWindow::deleteSessionListItem(QListWidgetItem *item)
{
    if (!item || item->data(ItemKindRole).toInt() != SavedDirectoryItem) {
        return;
    }

    removeSessionDirectory(item->data(DirectoryRole).toString());
    refreshSessionDirectoryList();
}

void MainWindow::closeSessionListItem(QListWidgetItem *item)
{
    if (!item || item->data(ItemKindRole).toInt() != LiveSessionItem) {
        return;
    }

    const int sessionId = item->data(SessionIdRole).toInt();
    if (sessionId == 0) {
        return;
    }

    sessionManager_.closeSession(sessionId);
    refreshSessionDirectoryList();
}

QString MainWindow::directoryItemText(const QString &path, const QString &label) const
{
    const QString directory = QDir::cleanPath(path);
    const QString name = label.isEmpty() || label == directory
        ? (QFileInfo(directory).fileName().isEmpty() ? directory : QFileInfo(directory).fileName())
        : label;
    return QStringLiteral("%1\n%2    点击启动").arg(name, compactDirectoryContext(directory));
}

QString MainWindow::compactDirectoryContext(const QString &path) const
{
    const QFileInfo info(QDir::cleanPath(path));
    const QString parentName = QFileInfo(info.absolutePath()).fileName();
    if (!parentName.isEmpty()) {
        return QStringLiteral("%1/%2").arg(parentName, info.fileName());
    }
    return QDir::cleanPath(path);
}

void MainWindow::closeCurrentSession()
{
    if (Session *session = sessionManager_.currentSession()) {
        persistSessionDirectory(session->workingDirectory());
        sessionManager_.closeSession(session->id());
    }
    refreshSessionDirectoryList();
    terminalView_->setFocus();
}

void MainWindow::clearCurrentSession()
{
    if (Session *session = sessionManager_.currentSession()) {
        session->clearOutput();
    }
    terminalView_->refresh();
    terminalView_->setFocus();
}

void MainWindow::updateSessionListItem(Session *session)
{
    if (!session) {
        return;
    }

    for (int row = 0; row < sessionList_->count(); ++row) {
        QListWidgetItem *item = sessionList_->item(row);
        if (item->data(SessionIdRole).toInt() == session->id()) {
            const QString directory = QDir::cleanPath(session->workingDirectory().isEmpty() ? QDir::currentPath() : session->workingDirectory());
            item->setText(sessionTitle(session));
            sessionList_->removeItemWidget(item);
            buildSessionListItemWidget(
                item,
                session->terminalTitle().isEmpty()
                    ? (QFileInfo(directory).fileName().isEmpty() ? session->name() : QFileInfo(directory).fileName())
                    : session->terminalTitle(),
                QStringLiteral("%1    PID %2").arg(compactDirectoryContext(directory)).arg(session->processId()),
                stateText(session->state()),
                true);
            break;
        }
    }

    if (sessionManager_.currentSessionId() == session->id()) {
        currentLabel_->setText(session->terminalTitle().isEmpty() ? session->name() : session->terminalTitle());
        terminalMetaLabel_->setText(QStringLiteral("PID %1 · %2 · %3x%4")
            .arg(session->processId())
            .arg(stateText(session->state()))
            .arg(session->terminalBuffer()->columns())
            .arg(session->terminalBuffer()->rows()));
        syncWindowTitle();
    }
}

void MainWindow::syncWindowTitle()
{
    Session *session = sessionManager_.currentSession();
    if (!session) {
        const QString title = QStringLiteral("终端舱");
        setWindowTitle(title);
        if (windowTitleLabel_) {
            windowTitleLabel_->setText(title);
        }
        return;
    }

    const QString title = session->terminalTitle().isEmpty() ? session->name() : session->terminalTitle();
    setWindowTitle(QStringLiteral("%1 - 终端舱").arg(title));
    if (windowTitleLabel_) {
        windowTitleLabel_->setText(QStringLiteral("终端舱"));
    }
}

void MainWindow::updateMaximizeButton()
{
    if (maximizeButton_) {
        maximizeButton_->setText(isMaximized() ? QStringLiteral("❐") : QStringLiteral("□"));
    }
}

bool MainWindow::isInTitleDragArea(const QPoint &position) const
{
    if (!titleBar_) {
        return false;
    }

    const QWidget *child = titleBar_->childAt(position);
    return child != minimizeButton_ && child != maximizeButton_ && child != windowCloseButton_;
}

void MainWindow::selectSessionInList(int sessionId)
{
    for (int row = 0; row < sessionList_->count(); ++row) {
        QListWidgetItem *item = sessionList_->item(row);
        if (item->data(SessionIdRole).toInt() == sessionId) {
            if (sessionList_->currentItem() != item) {
                sessionList_->setCurrentItem(item);
            }
            return;
        }
    }
}

void MainWindow::refreshStatus()
{
    statusBar()->showMessage(QStringLiteral("%1 个活动会话 · %2 个保存目录 · 点击保存目录再启动")
        .arg(sessionManager_.sessions().size())
        .arg(savedSessionDirectories_.size()));
    updateStatusBadge();
}

QString MainWindow::sessionTitle(Session *session) const
{
    const QString directory = QDir::cleanPath(session->workingDirectory().isEmpty() ? QDir::currentPath() : session->workingDirectory());
    const QString name = session->terminalTitle().isEmpty()
        ? (QFileInfo(directory).fileName().isEmpty() ? session->name() : QFileInfo(directory).fileName())
        : session->terminalTitle();
    return QStringLiteral("%1\n%2    PID %3    %4")
        .arg(name)
        .arg(compactDirectoryContext(directory))
        .arg(session->processId())
        .arg(stateText(session->state()));
}

QString MainWindow::stateText(Session::State state) const
{
    switch (state) {
    case Session::State::Starting:
        return QStringLiteral("启动中");
    case Session::State::Running:
        return QStringLiteral("运行中");
    case Session::State::Exited:
        return QStringLiteral("已退出");
    case Session::State::Error:
        return QStringLiteral("错误");
    }
    return QStringLiteral("未知");
}
