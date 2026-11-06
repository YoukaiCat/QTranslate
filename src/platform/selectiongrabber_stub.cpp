#include "platform/selectiongrabber.h"

#include <QMimeData>

// Non-Linux stub: there is no global selection or clipboard injection here.
// It serves the remaining non-Linux platforms (macOS and others) and keeps the
// target buildable there.

SelectionGrabber::SelectionGrabber(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(10);
    connect(&m_timer, &QTimer::timeout, this, &SelectionGrabber::poll);
}

SelectionGrabber::~SelectionGrabber() = default;

void SelectionGrabber::grab(Callback callback)
{
    if (!callback)
        return;
    callback(QString());
}

void SelectionGrabber::cancel()
{
}

void SelectionGrabber::setMaxLength(int length)
{
    m_maxLength = length > 0 ? length : 20000;
}

void SelectionGrabber::poll()
{
}

void SelectionGrabber::startCopy()
{
}

void SelectionGrabber::finish(const QString &text)
{
    Q_UNUSED(text);
}

void SelectionGrabber::restoreClipboardData()
{
}
