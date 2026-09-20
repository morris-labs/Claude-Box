#pragma once

#include <QWidget>
#include <QFont>
#include <QString>
#include <QStringList>

#include <vterm.h>

class PtySession;

// A live terminal view backed by libvterm for VT100/xterm emulation
// (correctness) and this widget for rendering (QPainter over a monospace
// cell grid) and input (QKeyEvent -> vterm_keyboard_*). Owns a PtySession
// that runs `docker attach <name>`.
//
// v1 scope, deliberately: no scrollback buffer (only the live screen is
// shown -- libvterm's sb_pushline/sb_popline callbacks are left
// unimplemented), no text selection/copy-paste, no bold-as-bright-color
// handling beyond what libvterm's own cell attrs give us directly.
class TerminalWidget : public QWidget {
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    // Spawns `docker attach <name>` and starts feeding its output into
    // the terminal. Returns false if the PTY/process spawn itself failed.
    bool attachToContainer(const QString &containerName);

    // Same idea, for any other interactive/long-running command worth
    // watching live rather than running silently in the background -- an
    // `ssh`/`ssh-copy-id` login, a `docker build`. See CommandTerminalDialog,
    // the one thing outside box terminals that uses this.
    bool attachToCommand(const QString &program, const QStringList &args,
                        const QString &workingDir = QString());

    bool isRunning() const;

    // True once the attached process has exited. The widget keeps showing
    // the last screen (dimmed, with a banner) rather than vanishing, so a
    // box that dies while you're looking elsewhere leaves evidence behind
    // instead of silently closing its own tab.
    bool isDisconnected() const { return m_disconnected; }

signals:
    // The attached process exited (box was stopped/removed, or `docker
    // attach` itself died). exitCode mirrors PtySession::finished.
    void sessionFinished(int exitCode);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void onPtyData(const QByteArray &data);
    void onPtyFinished(int exitCode);

private:
    PtySession *m_pty = nullptr;
    VTerm *m_vterm = nullptr;
    VTermScreen *m_screen = nullptr;

    QFont m_font;
    int m_cellWidth = 8;
    int m_cellHeight = 16;
    int m_rows = 24;
    int m_cols = 80;

    bool m_disconnected = false;
    QString m_exitNote;

    int m_cursorRow = 0;
    int m_cursorCol = 0;
    bool m_cursorVisible = true;

    // Text selection. Anchor is where the drag started; end tracks the
    // current position. Both are -1 when no selection exists.
    int m_selAnchorRow = -1, m_selAnchorCol = -1;
    int m_selEndRow = -1, m_selEndCol = -1;
    bool m_selecting = false;

    void setupVterm();
    void updateGridSize();
    QRect cellRectToPixels(int startRow, int endRow, int startCol, int endCol) const;

    // Converts a widget pixel coordinate to a (col, row) cell position.
    QPoint pixelToCell(const QPoint &px) const;
    // Fills startRow/Col/endRow/Col with the normalized (row-major) selection
    // bounds. Returns false when no selection is active.
    bool selectionBounds(int &startRow, int &startCol, int &endRow, int &endCol) const;
    QString selectedText() const;
    void clearSelection();

    // Called by the libvterm C callback trampolines (see TerminalWidget.cpp).
    void handleDamage(int startRow, int endRow, int startCol, int endCol);
    void handleMoveCursor(int row, int col, bool visible);
    void handleBell();
    void sendToPty(const QByteArray &data);

    friend int vtermDamageCallback(VTermRect, void *);
    friend int vtermMoveCursorCallback(VTermPos, VTermPos, int, void *);
    friend int vtermBellCallback(void *);
    friend void vtermOutputCallback(const char *, size_t, void *);
};
