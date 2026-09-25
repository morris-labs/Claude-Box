#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QString>
#include <QStyleFactory>

namespace {
// Fusion is the one built-in style that honors a custom QPalette
// consistently across platforms; the native styles on some desktops
// ignore palette roles and would leave light patches in the chrome.
constexpr const char *kStyleName = "Fusion";

const QColor kWindowBg  (0x1b, 0x1c, 0x1e);
const QColor kPanelBg   (0x23, 0x24, 0x27);
const QColor kBaseBg    (0x15, 0x16, 0x18);
const QColor kBorder    (0x33, 0x35, 0x3a);
const QColor kText      (0xe6, 0xe6, 0xe6);
const QColor kDimText   (0x92, 0x97, 0xa0);
const QColor kAccent    (0xd9, 0x77, 0x57);
const QColor kRunning   (0x5f, 0xb3, 0x7a);
const QColor kStopped   (0xd9, 0x6c, 0x6c);
const QColor kKnown     (0x8a, 0x8f, 0x98);
const QColor kTermBg    (0x10, 0x11, 0x13);
const QColor kTermFg    (0xe6, 0xe6, 0xe6);

// Linear mix of two colors; `amount` is how much of `a` survives.
QColor blend(const QColor &a, const QColor &b, qreal amount)
{
    return QColor(int(a.red()   * amount + b.red()   * (1.0 - amount)),
                  int(a.green() * amount + b.green() * (1.0 - amount)),
                  int(a.blue()  * amount + b.blue()  * (1.0 - amount)));
}
}

QColor Theme::windowBg()    { return kWindowBg; }
QColor Theme::panelBg()     { return kPanelBg; }
QColor Theme::baseBg()      { return kBaseBg; }
QColor Theme::border()      { return kBorder; }
QColor Theme::text()        { return kText; }
QColor Theme::dimText()     { return kDimText; }
QColor Theme::accent()      { return kAccent; }
QColor Theme::running()     { return kRunning; }
QColor Theme::stopped()     { return kStopped; }
QColor Theme::known()       { return kKnown; }
QColor Theme::terminalBg()  { return kTermBg; }
QColor Theme::terminalFg()  { return kTermFg; }

QString Theme::progressBarStyle(const QString &chunkColor)
{
    return QStringLiteral(
        "QProgressBar {"
        "  border: none;"
        "  background: %1;"
        "  border-radius: 5px;"
        "  text-align: center;"
        "  color: white;"
        "}"
        "QProgressBar::chunk {"
        "  background: %2;"
        "  border-radius: 5px;"
        "}")
        .arg(kPanelBg.name(), chunkColor);
}

QString Theme::barChunkColor(double fraction)
{
    const QColor green(45, 160, 80);
    const QColor red(200, 80, 50);
    if (fraction <= 0.6)
        return green.name();
    return QColor(int(green.red()   + (red.red()   - green.red())   * (fraction - 0.6) / 0.4),
                  int(green.green() + (red.green() - green.green()) * (fraction - 0.6) / 0.4),
                  int(green.blue()  + (red.blue()  - green.blue())  * (fraction - 0.6) / 0.4)).name();
}

void Theme::apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create(kStyleName));

    QPalette p;
    p.setColor(QPalette::Window,          kWindowBg);
    p.setColor(QPalette::WindowText,      kText);
    p.setColor(QPalette::Base,            kBaseBg);
    p.setColor(QPalette::AlternateBase,   kPanelBg);
    p.setColor(QPalette::Text,            kText);
    p.setColor(QPalette::Button,          kPanelBg);
    p.setColor(QPalette::ButtonText,      kText);
    p.setColor(QPalette::BrightText,      Qt::white);
    p.setColor(QPalette::Highlight,       kAccent);
    p.setColor(QPalette::HighlightedText, kWindowBg);
    p.setColor(QPalette::ToolTipBase,     kPanelBg);
    p.setColor(QPalette::ToolTipText,     kText);
    p.setColor(QPalette::PlaceholderText, kDimText);
    p.setColor(QPalette::Link,            kAccent);

    // Disabled actions must read as clearly unavailable: most of the
    // toolbar is greyed out at any given moment (Open only applies to
    // Known rows, Close only to Running, and so on), so this state is
    // load-bearing rather than an edge case.
    p.setColor(QPalette::Disabled, QPalette::Text,       kDimText.darker(130));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, kDimText.darker(130));
    p.setColor(QPalette::Disabled, QPalette::WindowText, kDimText.darker(130));
    app.setPalette(p);

    const QString qss = QString(R"(
QMainWindow, QDialog { background: %(window)s; }

QToolBar {
    background: %(panel)s;
    border: none;
    border-bottom: 1px solid %(border)s;
    padding: 4px 6px;
    spacing: 2px;
}
QToolButton {
    color: %(text)s;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 5px;
    padding: 5px 9px;
    margin: 0px 1px;
}
QToolButton:hover:!disabled { background: %(hover)s; border-color: %(border)s; }
QToolButton:pressed:!disabled { background: %(base)s; }
QToolButton:disabled { color: %(disabled)s; }
QToolBar::separator { background: %(border)s; width: 1px; margin: 5px 6px; }

QMenuBar { background: %(panel)s; color: %(text)s; border-bottom: 1px solid %(border)s; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 4px; }
QMenuBar::item:selected { background: %(hover)s; }
QMenu { background: %(panel)s; color: %(text)s; border: 1px solid %(border)s; border-radius: 6px; padding: 5px; }
QMenu::item { padding: 6px 26px 6px 22px; border-radius: 4px; }
QMenu::item:selected { background: %(accent)s; color: %(window)s; }
QMenu::item:disabled { color: %(disabled)s; }
QMenu::item:disabled:selected { background: transparent; color: %(disabled)s; }
QMenu::separator { height: 1px; background: %(border)s; margin: 5px 8px; }

QStatusBar { background: %(panel)s; color: %(dim)s; border-top: 1px solid %(border)s; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: %(dim)s; padding: 0px 4px; }

QTableView {
    background: %(base)s;
    alternate-background-color: %(alt)s;
    color: %(text)s;
    border: 1px solid %(border)s;
    border-radius: 6px;
    gridline-color: transparent;
    selection-background-color: %(selbg)s;
    selection-color: %(text)s;
    outline: none;
}
QTableView::item { padding: 5px 6px; border: none; }
QTableView::item:selected { background: %(selbg)s; color: %(text)s; }
QHeaderView::section {
    background: %(panel)s;
    color: %(dim)s;
    padding: 6px 8px;
    border: none;
    border-bottom: 1px solid %(border)s;
    border-right: 1px solid %(border)s;
    font-weight: 600;
}
QHeaderView::section:last { border-right: none; }
QTableCornerButton::section { background: %(panel)s; border: none; }

QTabWidget::pane { border: 1px solid %(border)s; border-radius: 6px; background: %(termbg)s; top: -1px; }
QTabBar::tab {
    background: %(panel)s;
    color: %(dim)s;
    border: 1px solid %(border)s;
    border-bottom: none;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    padding: 6px 12px;
    margin-right: 2px;
}
QTabBar::tab:selected { background: %(termbg)s; color: %(text)s; border-bottom: 1px solid %(termbg)s; }
QTabBar::tab:hover:!selected { color: %(text)s; }
QTabBar::close-button { subcontrol-position: right; }

QLineEdit {
    background: %(base)s;
    color: %(text)s;
    border: 1px solid %(border)s;
    border-radius: 5px;
    padding: 5px 8px;
    selection-background-color: %(accent)s;
    selection-color: %(window)s;
}
QLineEdit:focus { border-color: %(accent)s; }

QPushButton {
    background: %(panel)s;
    color: %(text)s;
    border: 1px solid %(border)s;
    border-radius: 5px;
    padding: 6px 14px;
}
QPushButton:hover:!disabled { background: %(hover)s; }
QPushButton:pressed:!disabled { background: %(base)s; }
QPushButton:disabled { color: %(disabled)s; }
QPushButton:default { border-color: %(accent)s; }

QGroupBox {
    border: 1px solid %(border)s;
    border-radius: 6px;
    margin-top: 10px;
    padding-top: 8px;
    color: %(dim)s;
}
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0px 5px; color: %(dim)s; }

QListWidget { background: %(base)s; color: %(text)s; border: 1px solid %(border)s; border-radius: 5px; outline: none; }
QListWidget::item { padding: 4px 6px; border-radius: 3px; }
QListWidget::item:selected { background: %(accent)s; color: %(window)s; }

QCheckBox { color: %(text)s; spacing: 7px; }
QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid %(border)s; border-radius: 3px; background: %(base)s; }
QCheckBox::indicator:checked { background: %(accent)s; border-color: %(accent)s; }

QSplitter::handle { background: %(border)s; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QSplitter::handle:hover { background: %(accent)s; }

QScrollBar:vertical { background: transparent; width: 11px; margin: 0px; }
QScrollBar::handle:vertical { background: %(border)s; border-radius: 5px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: %(dim)s; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0px; }
QScrollBar::handle:horizontal { background: %(border)s; border-radius: 5px; min-width: 28px; }
QScrollBar::handle:horizontal:hover { background: %(dim)s; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QToolTip { background: %(panel)s; color: %(text)s; border: 1px solid %(border)s; padding: 4px 6px; }
)");

    // Qt's stylesheet parser has no variable support, so the placeholders
    // above are substituted here rather than repeating hex literals.
    QString sheet = qss;
    sheet.replace("%(window)s",   kWindowBg.name());
    sheet.replace("%(panel)s",    kPanelBg.name());
    sheet.replace("%(base)s",     kBaseBg.name());
    sheet.replace("%(alt)s",      kBaseBg.lighter(112).name());
    sheet.replace("%(hover)s",    kPanelBg.lighter(128).name());
    sheet.replace("%(border)s",   kBorder.name());
    sheet.replace("%(text)s",     kText.name());
    sheet.replace("%(dim)s",      kDimText.name());
    sheet.replace("%(disabled)s", kDimText.darker(150).name());
    sheet.replace("%(accent)s",   kAccent.name());
    // A selected row is a *pointer*, not an alarm. Filling it with the full
    // accent turned the row into a solid orange band that fought the status
    // colors sitting on top of it; a heavily-tinted base reads as selected
    // while leaving the row's own colors legible.
    sheet.replace("%(selbg)s",    blend(kAccent, kBaseBg, 0.22).name());
    sheet.replace("%(termbg)s",   kTermBg.name());
    app.setStyleSheet(sheet);
}
