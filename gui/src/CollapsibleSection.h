#pragma once

#include <QWidget>

class QToolButton;
class QLayout;

// A titled section that can be collapsed to just its header, hiding --
// not merely disabling -- its content. QGroupBox::setCheckable() looks
// similar but only greys children out; their space stays reserved, which
// defeats the point here. Used wherever a dialog has grown enough
// optional sections that showing all of them at once pushes its size
// past what's reasonable (see the "Fix New/Edit Box dialog getting
// shoved onto another monitor" history) -- collapsed by default keeps
// the common case compact without losing the option to expand any one
// of them.
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleSection(const QString &title, QWidget *parent = nullptr);

    // Takes ownership of `layout` and everything already added to it --
    // build the section's content into a fresh layout, then hand it here
    // rather than adding widgets to this section directly.
    void setContentLayout(QLayout *layout);

    bool isExpanded() const;
    void setExpanded(bool expanded);

signals:
    void expandedChanged(bool expanded);

private:
    QToolButton *m_toggleButton = nullptr;
    QWidget *m_content = nullptr;
};
