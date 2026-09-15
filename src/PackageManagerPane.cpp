#include "PackageManagerPane.h"

#include <QLabel>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

PackageManagerPane::PackageManagerPane(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);

    m_label = new QLabel(this);
    // The one handle on this page. A driver on a phone has nothing else to read
    // it by: the pane is a widget, not a QML item, so there is no id to find.
    m_label->setObjectName(QStringLiteral("packageManagerPane.message"));
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setWordWrap(true);
    m_label->setStyleSheet(QStringLiteral("color: #a0a0a0; font-size: 14px;"));
    layout->addWidget(m_label);

    // Single-shot: the clock belongs to one load attempt, and beginLoading()
    // restarts it for the next.
    m_deadline = new QTimer(this);
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, this, [this] {
        showUnavailable(tr("The Package Manager plugin did not come up within %1 seconds.")
                            .arg(m_deadlineMs / 1000));
    });

    setMessage(tr("Package Manager is not open."));
}

QString PackageManagerPane::message() const
{
    return m_label ? m_label->text() : QString();
}

void PackageManagerPane::setMessage(const QString& text)
{
    if (m_label) m_label->setText(text);
}

void PackageManagerPane::beginLoading()
{
    m_state = Loading;
    setMessage(tr("Loading Package Manager…"));
    m_deadline->start(m_deadlineMs);
}

void PackageManagerPane::showUnavailable(const QString& reason)
{
    m_deadline->stop();
    m_state = Unavailable;
    const QString headline = tr("Package Manager is not available in this build.");
    setMessage(reason.trimmed().isEmpty()
                   ? headline
                   : headline + QStringLiteral("\n\n") + reason.trimmed());
}

void PackageManagerPane::setLoadDeadline(int ms)
{
    m_deadlineMs = ms;
    // A clock that is already running is the one this call is about.
    if (m_deadline->isActive()) m_deadline->start(m_deadlineMs);
}
