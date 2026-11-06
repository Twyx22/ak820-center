/*
 * hidworker.cpp — serialized async HID job queue on a dedicated thread.
 */
#include "hidworker.h"

namespace ak820 {

HidWorker *HidWorker::instance() {
    static HidWorker s_instance;
    return &s_instance;
}

void HidWorker::startWorker() {
    HidWorker *w = instance();
    w->m_thread.setObjectName(QStringLiteral("ak820-hid"));
    w->moveToThread(&w->m_thread);
    w->m_thread.start();
}

void HidWorker::stopWorker() {
    HidWorker *w = instance();
    w->m_thread.quit();
    w->m_thread.wait();
}

} /* namespace ak820 */
