#include "TerminalWidget.h"
#include "PtySession.h"

#include <QApplication>
#include <QColor>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QResizeEvent>

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
    return m_pty->start(QStringLiteral("docker"), {QStringLiteral("attach"), containerName});
}

bool TerminalWidget::isRunning() const
{
    return m_pty && m_pty->isRunning();
}

void TerminalWidget::onPtyData(const QByteArray &data)
{
    if (m_vterm)
        vterm_input_write(m_vterm, data.constData(), static_cast<size_t>(data.size()));
}

void TerminalWidget::onPtyFinished(int exitCode)
{
    emit sessionFinished(exitCode);
}

void TerminalWidget::sendToPty(const QByteArray &data)
{
    if (m_pty)
        m_pty->write(data);
}

void TerminalWidget::handleDamage(int startRow, int endRow, int startCol, int endCol)
{
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
    updateGridSize();
}

void TerminalWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
}

void TerminalWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setFont(m_font);
    painter.fillRect(rect(), Qt::black);

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
