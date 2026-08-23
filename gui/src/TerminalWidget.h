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

    bool isRunning() const;

signals:
    // The attached process exited (box was stopped/removed, or `docker
    // attach` itself died). exitCode mirrors PtySession::finished.
    void sessionFinished(int exitCode);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;

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

    int m_cursorRow = 0;
    int m_cursorCol = 0;
    bool m_cursorVisible = true;

    void setupVterm();
    void updateGridSize();
    QRect cellRectToPixels(int startRow, int endRow, int startCol, int endCol) const;

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
