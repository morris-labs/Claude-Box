#include "TerminalWidget.h"
#include "PtySession.h"
#include "Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>

#include <algorithm>
#include <utility>

// --- libvterm C callback trampolines -----------------------------------
// VTermScreenCallbacks/output callback are plain C function pointers, so
// they can't be member functions or capturing lambdas. Each one just
// recovers `this` from the opaque `user` pointer we pass at registration
// time and forwards to a real member function. Deliberately minimal: only
// damage/movecursor/bell/output are wired up (see TerminalWidget.h for
// why leaving moverect/settermprop/resize unset is safe -- libvterm falls
// back to `damage` for the parts we don't implement).

int vtermDamageCallback(VTermRect rect, void *user)
{
    auto *self = static_cast<TerminalWidget *>(user);
    self->handleDamage(rect.start_row, rect.end_row, rect.start_col, rect.end_col);
    return 1;
}

int vtermMoveCursorCallback(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    Q_UNUSED(oldpos);
    auto *self = static_cast<TerminalWidget *>(user);
    self->handleMoveCursor(pos.row, pos.col, visible != 0);
    return 1;
}

int vtermBellCallback(void *user)
{
    auto *self = static_cast<TerminalWidget *>(user);
    self->handleBell();
    return 1;
}

void vtermOutputCallback(const char *s, size_t len, void *user)
{
    auto *self = static_cast<TerminalWidget *>(user);
    self->sendToPty(QByteArray(s, static_cast<int>(len)));
}

// -------------------------------------------------------------------------

TerminalWidget::TerminalWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);

    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_font.setPointSize(10);
    const QFontMetrics fm(m_font);
    m_cellWidth = qMax(1, fm.horizontalAdvance('M'));
    m_cellHeight = qMax(1, fm.height());

    setupVterm();

    m_pty = new PtySession(this);
    connect(m_pty, &PtySession::dataReady, this, &TerminalWidget::onPtyData);
    connect(m_pty, &PtySession::finished, this, &TerminalWidget::onPtyFinished);
}

TerminalWidget::~TerminalWidget()
{
    if (m_vterm)
        vterm_free(m_vterm); // also frees m_screen, owned by the VTerm
}

// vterm_screen_set_callbacks() stores this pointer rather than copying the
// struct, so whatever we hand it has to outlive every TerminalWidget -- a
// stack local is a dangling pointer that segfaults on the first byte of
// output. The contents are identical for every instance (per-instance
// state rides in the `user` pointer), so one shared constant is both
// correct and sufficient. Built field-by-field rather than with a braced
// initializer so it stays right regardless of the field order libvterm
// happens to declare.
static const VTermScreenCallbacks &screenCallbacks()
{
    static const VTermScreenCallbacks cbs = [] {
        VTermScreenCallbacks c{};
        c.damage = vtermDamageCallback;
        c.movecursor = vtermMoveCursorCallback;
        c.bell = vtermBellCallback;
        return c;
    }();
    return cbs;
}

void TerminalWidget::setupVterm()
{
    m_vterm = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vterm, 1);

    m_screen = vterm_obtain_screen(m_vterm);
    vterm_screen_set_callbacks(m_screen, &screenCallbacks(), this);

    // Claude Code's TUI runs on the alternate screen, and libvterm only
    // allocates that second buffer when asked. Without this the altscreen
    // escape is silently ignored and alt-screen output is drawn straight
    // over the primary buffer instead.
    vterm_screen_enable_altscreen(m_screen, 1);

    // reset() emits the initial damage, so it comes last -- after the
    // callbacks that are supposed to receive it are registered.
    vterm_screen_reset(m_screen, 1);

    vterm_output_set_callback(m_vterm, vtermOutputCallback, this);
}

bool TerminalWidget::attachToContainer(const QString &containerName)
{
    // Containers run a keeper-loop bash as PID 1 (not the tmux client), so
    // `docker attach` would connect to the bash loop -- not to claude. Use
    // `docker exec` to create a new session-attached tmux client instead.
    //
    // The wait loop handles the race where this fires before the container's
    // tmux session exists (git-config + claude startup takes a moment).
    // 60 iterations * 0.5 s = 30-second timeout; exits 1 if tmux never
    // appears, which shows "Session ended (exit 1)" in the tab.
    return m_pty->start(QStringLiteral("docker"),
                        {QStringLiteral("exec"), QStringLiteral("-it"),
                         containerName,
                         QStringLiteral("bash"), QStringLiteral("-c"),
                         QStringLiteral("count=0; "
                             "until tmux has-session -t main 2>/dev/null; do "
                               "sleep 0.5; count=$((count+1)); "
                               "[ \"$count\" -ge 60 ] && exit 1; "
                             "done; "
                             "exec tmux attach -t main")});
}

bool TerminalWidget::attachToCommand(const QString &program, const QStringList &args,
                                     const QString &workingDir)
{
    return m_pty->start(program, args, workingDir);
}

bool TerminalWidget::isRunning() const
{
    return m_pty && m_pty->isRunning();
}

void TerminalWidget::onPtyData(const QByteArray &data)
{
    if (!m_vterm)
        return;

    // Track the row range that gets damaged by this write so we can decide
    // whether it overlaps the selection. handleDamage() fills in
    // m_pendingDamageMinRow/MaxRow while m_trackingDamage is true.
    m_trackingDamage = true;
    m_pendingDamageMinRow = INT_MAX;
    m_pendingDamageMaxRow = -1;

    vterm_input_write(m_vterm, data.constData(), static_cast<size_t>(data.size()));

    m_trackingDamage = false;

    // Clear the selection only when the incoming data actually modified
    // the selected rows. Claude Code's TUI repaints continuously, so
    // clearing unconditionally makes text selection essentially impossible.
    if (m_selAnchorRow >= 0 && m_pendingDamageMinRow <= m_pendingDamageMaxRow) {
        const int selMin = qMin(m_selAnchorRow, m_selEndRow);
        const int selMax = qMax(m_selAnchorRow, m_selEndRow);
        if (m_pendingDamageMinRow <= selMax && m_pendingDamageMaxRow >= selMin)
            clearSelection();
    }
}

void TerminalWidget::onPtyFinished(int exitCode)
{
    m_disconnected = true;
    m_exitNote = exitCode == 0
        ? QStringLiteral("Session ended.\nThis box is no longer attached.")
        : QStringLiteral("Session ended (exit %1).\nThis box is no longer attached.").arg(exitCode);
    update();
    emit sessionFinished(exitCode);
}

namespace {
// Chords this application keeps for itself. Everything else belongs to
// whatever is running inside the terminal.
//
// The split follows terminal-emulator convention: Ctrl+Shift+X is the
// app's, plain Ctrl+X is the inner program's. That matters here more than
// in a normal terminal, because the inner program is Claude Code -- it
// binds a lot of Ctrl chords, and silently swallowing them into a menu
// would be maddening.
bool isApplicationChord(const QKeyEvent *e)
{
    const Qt::KeyboardModifiers m = e->modifiers();
    if ((m & Qt::ControlModifier) && (m & Qt::ShiftModifier))
        return true;
    if ((m & Qt::AltModifier) && e->key() >= Qt::Key_1 && e->key() <= Qt::Key_9)
        return true;
    if (e->key() == Qt::Key_F5)
        return true;
    return false;
}
}

bool TerminalWidget::event(QEvent *event)
{
    // Qt resolves QAction/QShortcut chords in QShortcutMap *before* the
    // focused widget sees the key. Left alone, every menu accelerator
    // would be stolen out from under the terminal. Accepting the
    // ShortcutOverride tells Qt to deliver the key here as an ordinary
    // press instead -- except for the chords reserved above, which are
    // allowed to reach the menus.
    if (event->type() == QEvent::ShortcutOverride) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (!m_disconnected && !isApplicationChord(ke)) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void TerminalWidget::sendToPty(const QByteArray &data)
{
    if (m_pty)
        m_pty->write(data);
}

void TerminalWidget::handleDamage(int startRow, int endRow, int startCol, int endCol)
{
    if (m_trackingDamage) {
        m_pendingDamageMinRow = qMin(m_pendingDamageMinRow, startRow);
        // end_row is exclusive (libvterm convention), so the last damaged row
        // is end_row - 1. Using end_row directly would make damage [5,7) report
        // max=7 and falsely overlap a selection starting at row 7.
        m_pendingDamageMaxRow = qMax(m_pendingDamageMaxRow, endRow - 1);
    }
    update(cellRectToPixels(startRow, endRow, startCol, endCol));
}

void TerminalWidget::handleMoveCursor(int row, int col, bool visible)
{
    const QRect oldRect = cellRectToPixels(m_cursorRow, m_cursorRow + 1, m_cursorCol, m_cursorCol + 1);
    m_cursorRow = row;
    m_cursorCol = col;
    m_cursorVisible = visible;
    const QRect newRect = cellRectToPixels(m_cursorRow, m_cursorRow + 1, m_cursorCol, m_cursorCol + 1);
    update(oldRect.united(newRect));
}

void TerminalWidget::handleBell()
{
    QApplication::beep();
}

QRect TerminalWidget::cellRectToPixels(int startRow, int endRow, int startCol, int endCol) const
{
    return QRect(startCol * m_cellWidth, startRow * m_cellHeight,
                 (endCol - startCol) * m_cellWidth, (endRow - startRow) * m_cellHeight);
}

void TerminalWidget::updateGridSize()
{
    if (!m_vterm)
        return;

    const int newCols = qMax(1, width() / m_cellWidth);
    const int newRows = qMax(1, height() / m_cellHeight);
    if (newCols == m_cols && newRows == m_rows)
        return;

    m_cols = newCols;
    m_rows = newRows;
    vterm_set_size(m_vterm, m_rows, m_cols);
    if (m_pty)
        m_pty->resize(m_rows, m_cols);
}

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // A selection is a set of (row, col) cells in the old grid; once
    // updateGridSize() reflows rows/cols there is no cell mapping that keeps
    // it meaningful, so drop it rather than paint an overlay outside the new
    // grid's bounds.
    clearSelection();
    updateGridSize();
}

void TerminalWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    // Repaint the cursor cell: focused terminal shows a solid block, unfocused shows an outline.
    update(cellRectToPixels(m_cursorRow, m_cursorRow + 1, m_cursorCol, m_cursorCol + 1));
}

void TerminalWidget::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    update(cellRectToPixels(m_cursorRow, m_cursorRow + 1, m_cursorCol, m_cursorCol + 1));
}

bool TerminalWidget::focusNextPrevChild(bool)
{
    // QWidget::event() intercepts Tab/Shift+Tab to move Qt focus before
    // keyPressEvent() is called. Returning false here tells Qt "focus did
    // not move", so QWidget::event() falls through to keyPressEvent() and
    // Tab reaches the terminal process (Claude Code uses it for autocomplete).
    return false;
}

// -------------------------------------------------------------------------
// Selection helpers

QPoint TerminalWidget::pixelToCell(const QPoint &px) const
{
    return QPoint(qBound(0, px.x() / m_cellWidth, m_cols - 1),
                  qBound(0, px.y() / m_cellHeight, m_rows - 1));
}

bool TerminalWidget::selectionBounds(int &startRow, int &startCol,
                                     int &endRow,   int &endCol) const
{
    if (m_selAnchorRow < 0 || m_selEndRow < 0)
        return false;
    const bool anchorFirst = m_selAnchorRow < m_selEndRow
                          || (m_selAnchorRow == m_selEndRow && m_selAnchorCol <= m_selEndCol);
    if (anchorFirst) {
        startRow = m_selAnchorRow; startCol = m_selAnchorCol;
        endRow   = m_selEndRow;   endCol   = m_selEndCol;
    } else {
        startRow = m_selEndRow;   startCol = m_selEndCol;
        endRow   = m_selAnchorRow; endCol   = m_selAnchorCol;
    }
    return startRow != endRow || startCol != endCol;
}

QString TerminalWidget::selectedText() const
{
    int r0, c0, r1, c1;
    if (!m_screen || !selectionBounds(r0, c0, r1, c1))
        return QString();

    QString result;
    for (int row = r0; row <= r1; ++row) {
        const int colStart = (row == r0) ? c0 : 0;
        const int colEnd   = (row == r1) ? c1 : (m_cols - 1);
        QString rowText;
        for (int col = colStart; col <= colEnd; ++col) {
            VTermScreenCell cell;
            VTermPos pos{row, col};
            if (!vterm_screen_get_cell(m_screen, pos, &cell))
                continue;
            if (cell.chars[0] != 0) {
                const char32_t ch = cell.chars[0];
                rowText += QString::fromUcs4(&ch, 1);
            } else {
                rowText += ' ';
            }
        }
        // Strip trailing spaces from all but the last row of the selection.
        if (row < r1) {
            const QString trimmed = rowText.trimmed();
            result += (trimmed.isEmpty() ? QString() : trimmed) + '\n';
        } else {
            result += rowText;
        }
    }
    return result;
}

void TerminalWidget::clearSelection()
{
    // Also cancels an in-progress drag: a resize mid-drag would otherwise
    // leave m_selecting true with anchors reset to -1, and the next
    // mouseMoveEvent would build a selection rect out of that sentinel.
    m_selecting = false;
    if (m_selAnchorRow < 0)
        return;
    const int r0 = qMin(m_selAnchorRow, m_selEndRow);
    const int r1 = qMax(m_selAnchorRow, m_selEndRow);
    m_selAnchorRow = m_selAnchorCol = m_selEndRow = m_selEndCol = -1;
    update(cellRectToPixels(r0, r1 + 1, 0, m_cols));
}

void TerminalWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        clearSelection();
        const QPoint cell = pixelToCell(event->pos());
        m_selAnchorRow = m_selEndRow = cell.y();
        m_selAnchorCol = m_selEndCol = cell.x();
        m_selecting = true;
        setFocus();
    }
    QWidget::mousePressEvent(event);
}

void TerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selecting && (event->buttons() & Qt::LeftButton)) {
        const QPoint cell = pixelToCell(event->pos());
        if (cell.y() != m_selEndRow || cell.x() != m_selEndCol) {
            const int r0 = std::min({m_selAnchorRow, m_selEndRow, cell.y()});
            const int r1 = std::max({m_selAnchorRow, m_selEndRow, cell.y()});
            m_selEndRow = cell.y();
            m_selEndCol = cell.x();
            update(cellRectToPixels(r0, r1 + 1, 0, m_cols));
        }
    }
    QWidget::mouseMoveEvent(event);
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_selecting = false;
    QWidget::mouseReleaseEvent(event);
}

void TerminalWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    const QString sel = selectedText();

    QAction *copy = menu.addAction(QStringLiteral("Copy"), [this, sel] {
        QGuiApplication::clipboard()->setText(sel);
    });
    copy->setEnabled(!sel.isEmpty());

    const QString clip = QGuiApplication::clipboard()->text();
    QAction *paste = menu.addAction(QStringLiteral("Paste"), [this, clip] {
        if (!m_disconnected && !clip.isEmpty())
            sendToPty(clip.toUtf8());
    });
    paste->setEnabled(!m_disconnected && !clip.isEmpty());

    menu.addSeparator();

    menu.addAction(QStringLiteral("Select All"), [this] {
        m_selAnchorRow = 0; m_selAnchorCol = 0;
        m_selEndRow = m_rows - 1; m_selEndCol = m_cols - 1;
        update();
    });
    QAction *clearSel = menu.addAction(QStringLiteral("Clear Selection"),
                                       [this] { clearSelection(); });
    clearSel->setEnabled(m_selAnchorRow >= 0);

    menu.exec(event->globalPos());
}

void TerminalWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setFont(m_font);
    painter.fillRect(rect(), Theme::terminalBg());

    if (!m_screen)
        return;

    const QRect dirty = event->rect();
    const int firstRow = qMax(0, dirty.top() / m_cellHeight);
    const int lastRow = qMin(m_rows - 1, dirty.bottom() / m_cellHeight);
    const int firstCol = qMax(0, dirty.left() / m_cellWidth);
    const int lastCol = qMin(m_cols - 1, dirty.right() / m_cellWidth);

    for (int row = firstRow; row <= lastRow; ++row) {
        for (int col = firstCol; col <= lastCol; ++col) {
            VTermScreenCell cell;
            VTermPos pos;
            pos.row = row;
            pos.col = col;
            if (!vterm_screen_get_cell(m_screen, pos, &cell))
                continue;

            VTermColor fg = cell.fg;
            VTermColor bg = cell.bg;
            vterm_screen_convert_color_to_rgb(m_screen, &fg);
            vterm_screen_convert_color_to_rgb(m_screen, &bg);

            QColor fgColor(fg.rgb.red, fg.rgb.green, fg.rgb.blue);
            QColor bgColor(bg.rgb.red, bg.rgb.green, bg.rgb.blue);
            if (cell.attrs.reverse)
                std::swap(fgColor, bgColor);

            const bool isCursor = m_cursorVisible && row == m_cursorRow && col == m_cursorCol;
            if (isCursor)
                std::swap(fgColor, bgColor);

            const QRect cellRect(col * m_cellWidth, row * m_cellHeight, m_cellWidth, m_cellHeight);
            painter.fillRect(cellRect, bgColor);

            if (cell.width != 0 && cell.chars[0] != 0) {
                QFont f = m_font;
                f.setBold(cell.attrs.bold);
                f.setItalic(cell.attrs.italic);
                f.setUnderline(cell.attrs.underline != 0);
                painter.setFont(f);
                painter.setPen(fgColor);

                const char32_t ch = cell.chars[0];
                const QString text = QString::fromUcs4(&ch, 1);
                painter.drawText(cellRect, Qt::AlignLeft | Qt::AlignVCenter, text);
            }
        }
    }

    // The cursor and selection overlays must draw regardless of which cell
    // region triggered this repaint. Qt clips the painter to the dirty rect
    // by default, so a damage event far from the cursor would silently
    // suppress it. Disable clipping for these overlays only.
    painter.setClipping(false);

    // Selection overlay
    int selR0, selC0, selR1, selC1;
    if (selectionBounds(selR0, selC0, selR1, selC1)) {
        const QColor selColor(80, 140, 255, 90);
        for (int row = selR0; row <= selR1; ++row) {
            const int c0 = (row == selR0) ? selC0 : 0;
            const int c1 = (row == selR1) ? selC1 + 1 : m_cols;
            painter.fillRect(cellRectToPixels(row, row + 1, c0, c1), selColor);
        }
    }

    // Cursor -- drawn last so it's always on top. The cell loop's color
    // inversion can be invisible on default-color cells; this explicit block
    // is the reliable guarantee.
    if (!m_disconnected && m_cursorVisible
        && m_cursorRow < m_rows && m_cursorCol < m_cols) {
        const QRect cr(m_cursorCol * m_cellWidth, m_cursorRow * m_cellHeight,
                       m_cellWidth, m_cellHeight);
        if (hasFocus()) {
            painter.fillRect(cr, QColor(255, 255, 255, 160));
        } else {
            painter.setPen(QPen(QColor(255, 255, 255, 120), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(cr.adjusted(0, 0, -1, -1));
        }
    }

    if (m_disconnected) {
        painter.fillRect(rect(), QColor(0, 0, 0, 140));
        QFont banner = m_font;
        banner.setPointSizeF(banner.pointSizeF() + 2);
        banner.setBold(true);
        painter.setFont(banner);
        painter.setPen(Theme::stopped());
        painter.drawText(rect(), Qt::AlignCenter, m_exitNote);
    }
}

namespace {
VTermModifier qtModsToVterm(Qt::KeyboardModifiers mods)
{
    int m = VTERM_MOD_NONE;
    if (mods & Qt::ShiftModifier)
        m |= VTERM_MOD_SHIFT;
    if (mods & Qt::AltModifier)
        m |= VTERM_MOD_ALT;
    if (mods & Qt::ControlModifier)
        m |= VTERM_MOD_CTRL;
    return static_cast<VTermModifier>(m);
}
}

void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    if (m_disconnected) {
        // Nothing to type into any more; let the key bubble up so window
        // shortcuts still work while a dead tab happens to hold focus.
        event->ignore();
        return;
    }

    // Ctrl+Shift+C/V are application chords (not sent to the PTY), handled
    // here for copy/paste. Other Ctrl+Shift chords fall through to menu
    // shortcuts (New Box, Close, etc.) via QWidget::keyPressEvent.
    if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)) {
        if (event->key() == Qt::Key_C) {
            const QString text = selectedText();
            if (!text.isEmpty())
                QGuiApplication::clipboard()->setText(text);
            return;
        }
        if (event->key() == Qt::Key_V) {
            const QString text = QGuiApplication::clipboard()->text();
            if (!text.isEmpty())
                sendToPty(text.toUtf8());
            return;
        }
        QWidget::keyPressEvent(event);
        return;
    }

    if (!m_vterm) {
        QWidget::keyPressEvent(event);
        return;
    }

    const VTermModifier mod = qtModsToVterm(event->modifiers());

    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        vterm_keyboard_key(m_vterm, VTERM_KEY_ENTER, mod);
        return;
    case Qt::Key_Backspace:
        vterm_keyboard_key(m_vterm, VTERM_KEY_BACKSPACE, mod);
        return;
    case Qt::Key_Tab:
        vterm_keyboard_key(m_vterm, VTERM_KEY_TAB, mod);
        return;
    case Qt::Key_Backtab:
        // Qt delivers Shift+Tab as Key_Backtab, and depending on platform
        // may drop ShiftModifier while doing so. Claude Code uses Shift+Tab
        // to cycle permission modes, so re-assert the modifier explicitly
        // rather than letting it fall through as a plain tab.
        vterm_keyboard_key(m_vterm, VTERM_KEY_TAB,
                           static_cast<VTermModifier>(mod | VTERM_MOD_SHIFT));
        return;
    case Qt::Key_Escape:
        vterm_keyboard_key(m_vterm, VTERM_KEY_ESCAPE, mod);
        return;
    case Qt::Key_Up:
        vterm_keyboard_key(m_vterm, VTERM_KEY_UP, mod);
        return;
    case Qt::Key_Down:
        vterm_keyboard_key(m_vterm, VTERM_KEY_DOWN, mod);
        return;
    case Qt::Key_Left:
        vterm_keyboard_key(m_vterm, VTERM_KEY_LEFT, mod);
        return;
    case Qt::Key_Right:
        vterm_keyboard_key(m_vterm, VTERM_KEY_RIGHT, mod);
        return;
    case Qt::Key_Insert:
        vterm_keyboard_key(m_vterm, VTERM_KEY_INS, mod);
        return;
    case Qt::Key_Delete:
        vterm_keyboard_key(m_vterm, VTERM_KEY_DEL, mod);
        return;
    case Qt::Key_Home:
        vterm_keyboard_key(m_vterm, VTERM_KEY_HOME, mod);
        return;
    case Qt::Key_End:
        vterm_keyboard_key(m_vterm, VTERM_KEY_END, mod);
        return;
    case Qt::Key_PageUp:
        vterm_keyboard_key(m_vterm, VTERM_KEY_PAGEUP, mod);
        return;
    case Qt::Key_PageDown:
        vterm_keyboard_key(m_vterm, VTERM_KEY_PAGEDOWN, mod);
        return;
    default:
        break;
    }

    if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F12) {
        // VTERM_KEY_FUNCTION_0 + 1 == F1 is the standard libvterm
        // convention; if F-keys come out shifted by one after a real
        // build, this is the line to fix.
        const int n = event->key() - Qt::Key_F1 + 1;
        vterm_keyboard_key(m_vterm, static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + n), mod);
        return;
    }

    // Ctrl+letter: feed the base key + modifier and let libvterm do the
    // ctrl-masking, rather than Qt's already-processed text() (which can
    // vary by platform and risks double-encoding the control byte).
    if ((event->modifiers() & Qt::ControlModifier) && event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z) {
        const QChar base = QChar(event->key()).toLower();
        vterm_keyboard_unichar(m_vterm, base.unicode(), mod);
        return;
    }

    const QString text = event->text();
    if (!text.isEmpty()) {
        for (const QChar &c : text)
            vterm_keyboard_unichar(m_vterm, c.unicode(), mod);
        return;
    }

    QWidget::keyPressEvent(event);
}
