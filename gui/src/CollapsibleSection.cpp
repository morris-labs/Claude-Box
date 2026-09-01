#include "CollapsibleSection.h"

#include <QToolButton>
#include <QVBoxLayout>

CollapsibleSection::CollapsibleSection(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(2);

    m_toggleButton = new QToolButton(this);
    m_toggleButton->setText(title);
    m_toggleButton->setCheckable(true);
    m_toggleButton->setChecked(false);
    m_toggleButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toggleButton->setArrowType(Qt::RightArrow);
    // Flat and left-aligned so this reads as a section header, not a
    // button someone might hesitate to click.
    m_toggleButton->setStyleSheet(
        QStringLiteral("QToolButton { border: none; font-weight: 600; padding: 2px 0; }"));
    m_toggleButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    outer->addWidget(m_toggleButton);

    m_content = new QWidget(this);
    m_content->setVisible(false);
    outer->addWidget(m_content);

    connect(m_toggleButton, &QToolButton::toggled, this, [this](bool checked) {
        m_toggleButton->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        m_content->setVisible(checked);
        emit expandedChanged(checked);
    });
}

void CollapsibleSection::setContentLayout(QLayout *layout)
{
    delete m_content->layout(); // no-op the first time; guards a second call from leaking one
    m_content->setLayout(layout);
}

bool CollapsibleSection::isExpanded() const
{
    return m_toggleButton->isChecked();
}

void CollapsibleSection::setExpanded(bool expanded)
{
    m_toggleButton->setChecked(expanded);
}
