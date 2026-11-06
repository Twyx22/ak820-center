/*
 * hidworker.h — serialized async HID job queue on a dedicated thread.
 *
 * Blocking Device calls (each HID exchange sleeps 35/50 ms) used to run on
 * the GUI thread and freeze the UI. HidWorker owns a single background
 * "HID thread" that all jobs run on, so operations serialize naturally and
 * the GUI stays responsive; completions are delivered back in the caller's
 * thread. Start/stop the thread once from main() around the event loop.
 */
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>
#include <functional>
#include <type_traits>
#include <utility>

#include "device.h"

namespace ak820 {

class HidWorker : public QObject {
    Q_OBJECT
public:
    static HidWorker *instance();

    /* Starts the background HID thread. Call once from main(), before any
       widget is shown (and thus before any job is queued). */
    static void startWorker();
    /* Stops the background HID thread, waiting for queued jobs to finish.
       Call once after QApplication::exec() returns. */
    static void stopWorker();

    /* Queues fn on the HID thread, where it runs as fn(Device *dev) -> bool
       (dev is Device::instance()). If receiver is non-null, onDone(ok, error)
       is invoked in receiver's thread when the job finishes (error is
       Device::lastError() on failure); pass nullptr as receiver for
       fire-and-forget (onDone is then not delivered). Jobs run one at a
       time, in queue order. */
    template<typename F>
    static void run(QObject *receiver, F &&fn,
                    std::function<void(bool ok, QString error)> onDone = {}) {
        HidWorker *w = instance();
        QPointer<QObject> guard(receiver);
        std::decay_t<F> jobFn(std::forward<F>(fn));
        std::function<void(bool, QString)> done(std::move(onDone));
        QMetaObject::invokeMethod(w, [guard, jobFn = std::move(jobFn),
                                      done = std::move(done)]() mutable {
            bool ok = jobFn(Device::instance());
            /* Fire-and-forget when receiver is null; also drop the completion
               if the receiver was destroyed while the job was pending. */
            if (!done || !guard) return;
            QString err = ok ? QString() : Device::instance()->lastError();
            QMetaObject::invokeMethod(guard.data(), [done = std::move(done), ok,
                                                     err = std::move(err)]() mutable {
                done(ok, err);
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    }

private:
    HidWorker() = default;

    QThread m_thread;   /* the HID thread this object lives in */
};

} /* namespace ak820 */
