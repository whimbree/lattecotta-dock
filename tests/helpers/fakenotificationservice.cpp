/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Minimal freedesktop notification service for deterministic removal-Undo
//! acceptance tests. It records the latest action list and exposes one test
//! method that emits the same ActionInvoked signal a notification daemon emits
//! when the visible Undo button is activated.

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>
#include <QStringList>
#include <QVariantMap>

class FakeNotificationService final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public Q_SLOTS:
    QStringList GetCapabilities() const
    {
        return {QStringLiteral("actions")};
    }

    uint Notify(const QString &applicationName,
                const uint replacesId,
                const QString &applicationIcon,
                const QString &summary,
                const QString &body,
                const QStringList &actions,
                const QVariantMap &hints,
                const int timeout)
    {
        Q_UNUSED(applicationName)
        Q_UNUSED(applicationIcon)
        Q_UNUSED(summary)
        Q_UNUSED(body)
        Q_UNUSED(hints)
        Q_UNUSED(timeout)

        m_notificationId = replacesId > 0 ? replacesId : m_nextNotificationId++;
        m_actions = actions;
        ++m_deliveryCount;
        return m_notificationId;
    }

    void CloseNotification(const uint notificationId)
    {
        Q_EMIT NotificationClosed(notificationId, 3U);
    }

    QStringList LastActions() const
    {
        return m_actions;
    }

    uint DeliveryCount() const
    {
        return m_deliveryCount;
    }

    bool InvokeUndo()
    {
        if (m_notificationId == 0 || m_actions.size() < 2) {
            qCritical() << "fake notification service: no notification action is available";
            return false;
        }

        QString actionKey;
        for (qsizetype index = 0; index + 1 < m_actions.size(); index += 2) {
            if (m_actions[index].contains(QStringLiteral("undo"), Qt::CaseInsensitive)
                    || m_actions[index + 1].contains(QStringLiteral("undo"), Qt::CaseInsensitive)) {
                actionKey = m_actions[index];
                break;
            }
        }

        if (actionKey.isEmpty()) {
            qCritical() << "fake notification service: latest notification has no Undo action"
                        << m_actions;
            return false;
        }

        Q_EMIT ActionInvoked(m_notificationId, actionKey);
        return true;
    }

Q_SIGNALS:
    void ActionInvoked(uint notificationId, const QString &actionKey);
    void NotificationClosed(uint notificationId, uint reason);

private:
    uint m_nextNotificationId{1};
    uint m_notificationId{0};
    uint m_deliveryCount{0};
    QStringList m_actions;
};

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("org.freedesktop.Notifications"))) {
        qCritical() << "fake notification service: could not own org.freedesktop.Notifications:"
                    << bus.lastError().message();
        return 2;
    }

    FakeNotificationService service;
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"),
                            &service,
                            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
        qCritical() << "fake notification service: could not export notification object:"
                    << bus.lastError().message();
        return 2;
    }

    return application.exec();
}

#include "fakenotificationservice.moc"
