#include "AppUnavailablePane.h"

#include <QLabel>
#include <QSizePolicy>
#include <QVBoxLayout>

AppUnavailablePane::AppUnavailablePane(const QString& displayLabel,
                                       const QString& reason,
                                       QWidget* parent)
    : QWidget(parent)
    , m_label_text(displayLabel)
    , m_reason(reason)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);

    m_label = new QLabel(this);
    // The one handle on this page. A driver on a phone has nothing else to read
    // it by: this is a widget, not a QML item, so there is no id to find.
    m_label->setObjectName(QStringLiteral("appUnavailablePane.message"));
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setWordWrap(true);
    m_label->setStyleSheet(QStringLiteral("color: #a0a0a0; font-size: 14px;"));
    layout->addWidget(m_label);

    render();
}

void AppUnavailablePane::setReason(const QString& reason)
{
    m_reason = reason;
    render();
}

QString AppUnavailablePane::message() const
{
    return m_label->text();
}

void AppUnavailablePane::render()
{
    // The headline names the app even when there is no reason to give: a
    // refusal with no words is still an outcome, and a blank page is the silent
    // tile press this class exists to end.
    const QString headline = tr("%1 cannot open.").arg(m_label_text);
    const QString detail = m_reason.trimmed();
    m_label->setText(detail.isEmpty() ? headline
                                      : headline + QStringLiteral("\n\n") + detail);
}
