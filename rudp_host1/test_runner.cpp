#include "test_runner.h"
#include "rudp_host.h"
#include "logger.h"

#include <QTimer>
#include <QByteArray>
#include <memory>

// =============================================================================
//  Конструктор / деструктор
// =============================================================================
TestRunner::TestRunner(RudpHost* host, QObject* parent)
    : QObject(parent), host_(host) {

    timeoutTimer_ = new QTimer(this);
    timeoutTimer_->setSingleShot(true);
    connect(timeoutTimer_, &QTimer::timeout, this, [this]{
        if (testActive_) endTest(false, "global timeout");
    });
}

TestRunner::~TestRunner() { cancel(); }

// =============================================================================
//  Общая обвязка теста
// =============================================================================
void TestRunner::cancel() {
    if (testActive_) endTest(false, "cancelled");
}

void TestRunner::beginTest(const QString& name, int timeoutMs) {
    if (testActive_) endTest(false, "superseded by new test");

    currentName_ = name;
    testActive_  = true;

    LOG_INFO("=== TEST BEGIN: %s  (timeout %d ms) ===",
             qUtf8Printable(name), timeoutMs);
    emit testStarted(name);

    timeoutTimer_->start(timeoutMs);
}

//void TestRunner::endTest(bool pass, const QString& detail) {
//    if (!testActive_) return;
//    testActive_ = false;
//    timeoutTimer_->stop();

//    // Разрываем все соединения, накопленные во время теста.
//    for (auto& c : liveConns_) QObject::disconnect(c);
//    liveConns_.clear();

//    // Принудительный сброс хоста (без отправки пакетов в сеть).
//    host_->forceReset();

//    if (pass) {
//        LOG_INFO("=== TEST PASS: %s ===", qUtf8Printable(currentName_));
//    } else {
//        LOG_ERR ("=== TEST FAIL: %s  reason=%s ===",
//                 qUtf8Printable(currentName_), qUtf8Printable(detail));
//    }
//    emit testFinished(currentName_, pass, detail);
//}

// =============================================================================
//  Завершение теста
// =============================================================================
//
//  Важно: к моменту завершения теста FPGA может остаться с открытой
//  транзакцией (например, тест стартовал CMD для проверки и не закрыл его).
//  Простой host_->forceReset() сбрасывает только ЛОКАЛЬНОЕ состояние хоста;
//  FPGA при этом продолжает висеть в ST_WAIT_COMPLETE до своего 2-секундного
//  таймаута. Если следующий тест стартует раньше — FPGA проигнорирует CMD.
//
//  Поэтому перед сбросом шлём COMPLETE для обоих TID, которые используются
//  в тестах (0x0100 и 0x0200). Открытый TID закроется, закрытый проигнорируется.
//  Ждём 200 мс, даём FPGA обработать пакеты, потом сбрасываем локальное
//  состояние и эмитим testFinished.
void TestRunner::endTest(bool pass, const QString& detail) {
    if (!testActive_) return;
    testActive_ = false;
    timeoutTimer_->stop();

    // Разрываем все соединения, накопленные во время теста.
    for (auto& c : liveConns_) QObject::disconnect(c);
    liveConns_.clear();

    // ---- Cleanup-фаза: закрываем возможную открытую транзакцию на FPGA ----
    LOG_INFO("--- TEST %s: %s — cleanup ---",
             pass ? "PASS" : "FAIL",
             qUtf8Printable(currentName_));

    host_->sendCompleteCustom(0x0100);
    host_->sendCompleteCustom(0x0200);

    const QString name = currentName_;

    // Через 200 мс сбрасываем хост и эмитим результат.
    QTimer::singleShot(200, this, [this, name, pass, detail]{
        host_->forceReset();

        if (pass) {
            LOG_INFO("=== TEST PASS: %s ===", qUtf8Printable(name));
        } else {
            LOG_ERR ("=== TEST FAIL: %s  reason=%s ===",
                     qUtf8Printable(name), qUtf8Printable(detail));
        }
        emit testFinished(name, pass, detail);
    });
}

// =============================================================================
//  Happy path
// =============================================================================
void TestRunner::runHappyPath() {
    beginTest("Happy path", 3000);
    host_->setAutoComplete(true);

    liveConns_.append(connect(host_, &RudpHost::transactionFinished,
        this, [this](quint16 tid){
        if (!testActive_) return;
        if (tid == 0x0100) endTest(true, "");
        else endTest(false, QString("unexpected tid 0x%1")
                                 .arg(tid, 4, 16, QChar('0')));
    }));

    liveConns_.append(connect(host_, &RudpHost::transactionAborted,
        this, [this](quint16, const QString& reason){
        if (!testActive_) return;
        endTest(false, "aborted: " + reason);
    }));

    host_->startTransaction(0x0100, 2);
}

// =============================================================================
//  Wrong-TID COMPLETE
// =============================================================================
void TestRunner::runWrongTidComplete() {
    beginTest("Wrong-TID COMPLETE", 5000);
    host_->setAutoComplete(false);

    liveConns_.append(connect(host_, &RudpHost::allBlocksReceived,
        this, [this](quint16 tid){
        if (!testActive_) return;
        if (tid != 0x0100) { endTest(false, "unexpected tid"); return; }

        // Шаг 1: COMPLETE с чужим TID (FPGA должна игнорировать).
        QTimer::singleShot(50, this, [this]{
            if (!testActive_) return;
            host_->sendCompleteCustom(0x0999);

            // Шаг 2: COMPLETE с правильным TID (FPGA должна закрыть транзакцию).
            QTimer::singleShot(50, this, [this]{
                if (!testActive_) return;
                host_->finishTransaction(true);

                // Шаг 3: новая транзакция. Она пройдёт только если FPGA
                //        действительно ушла в IDLE после шага 2.
                QTimer::singleShot(100, this, [this]{
                    if (!testActive_) return;

                    liveConns_.append(connect(host_, &RudpHost::cmdAckReceived,
                        this, [this](quint16 newTid){
                        if (!testActive_) return;
                        if (newTid == 0x0200) endTest(true, "");
                        else endTest(false,
                            QString("expected ACK 0x0200, got 0x%1")
                                .arg(newTid, 4, 16, QChar('0')));
                    }));

                    host_->startTransaction(0x0200, 2);
                });
            });
        });
    }));

    host_->startTransaction(0x0100, 2);
}

// =============================================================================
//  RETX out-of-range
// =============================================================================
void TestRunner::runRetxOutOfRange() {
    beginTest("RETX out-of-range", 5000);
    host_->setAutoComplete(false);

    // Счётчик принятых RESULT, разделяемый между лямбдами.
    auto resultsSeen = std::make_shared<int>(0);

    liveConns_.append(connect(host_, &RudpHost::resultReceived,
        this, [this, resultsSeen](quint16, quint16, const QByteArray&){
        if (!testActive_) return;
        (*resultsSeen)++;
    }));

    liveConns_.append(connect(host_, &RudpHost::allBlocksReceived,
        this, [this, resultsSeen](quint16 tid){
        if (!testActive_) return;
        if (tid != 0x0100) { endTest(false, "unexpected tid"); return; }
        if (*resultsSeen != 2) {
            endTest(false, QString("expected 2 results, got %1").arg(*resultsSeen));
            return;
        }

        const int before = *resultsSeen;

        // Шаг 1: out-of-range RETX (blk=5 при blocks=2) — FPGA должна игнорировать.
        host_->sendRetxCustom(0x0100, 5);

        QTimer::singleShot(150, this, [this, resultsSeen, before]{
            if (!testActive_) return;
            if (*resultsSeen != before) {
                endTest(false, "out-of-range RETX produced a RESULT");
                return;
            }

            // Шаг 2: valid RETX (blk=1) — FPGA должна вернуть ещё один RESULT.
            host_->sendRetxCustom(0x0100, 1);

            QTimer::singleShot(300, this, [this, resultsSeen, before]{
                if (!testActive_) return;
                const int delta = *resultsSeen - before;
                if (delta == 1) endTest(true, "");
                else endTest(false,
                    QString("expected +1 RESULT, got +%1").arg(delta));
            });
        });
    }));

    host_->startTransaction(0x0100, 2);
}

// =============================================================================
//  Timeout FPGA
// =============================================================================
//  Предполагается, что в FPGA установлен TIMEOUT_CYCLES ≈ 2 секунды.
//  Тест ждёт 2.5 с, потом запускает новую транзакцию. Если FPGA корректно
//  вышла из WAIT_COMPLETE, новый CMD пройдёт и придёт CMD_ACK.
void TestRunner::runTimeoutFpga() {
    beginTest("Timeout FPGA", 4500);
    host_->setAutoComplete(false);

    liveConns_.append(connect(host_, &RudpHost::allBlocksReceived,
        this, [this](quint16 tid){
        if (!testActive_) return;
        if (tid != 0x0100) { endTest(false, "unexpected tid"); return; }

        // Ничего не отправляем. Ждём, пока FPGA сама уйдёт в IDLE.
        QTimer::singleShot(2500, this, [this]{
            if (!testActive_) return;

            // Сбрасываем локальное состояние хоста без пакетов в сеть.
            host_->forceReset();

            liveConns_.append(connect(host_, &RudpHost::cmdAckReceived,
                this, [this](quint16 newTid){
                if (!testActive_) return;
                if (newTid == 0x0200) endTest(true, "");
                else endTest(false,
                    QString("expected ACK 0x0200, got 0x%1")
                        .arg(newTid, 4, 16, QChar('0')));
            }));

            host_->startTransaction(0x0200, 2);
        });
    }));

    host_->startTransaction(0x0100, 2);
}

// =============================================================================
//  Duplicate COMPLETE
// =============================================================================
void TestRunner::runDuplicateComplete() {
    beginTest("Duplicate COMPLETE", 5000);
    host_->setAutoComplete(false);

    liveConns_.append(connect(host_, &RudpHost::allBlocksReceived,
        this, [this](quint16 tid){
        if (!testActive_) return;
        if (tid != 0x0100) { endTest(false, "unexpected tid"); return; }

        // Шаг 1: валидный COMPLETE — FPGA закроет транзакцию.
        host_->finishTransaction(true);

        // Шаг 2: дубликат COMPLETE — FPGA уже в IDLE, должна игнорировать.
        QTimer::singleShot(100, this, [this]{
            if (!testActive_) return;
            host_->sendCompleteCustom(0x0100);

            // Шаг 3: новая транзакция. Если FPGA не сломалась — пройдёт.
            QTimer::singleShot(100, this, [this]{
                if (!testActive_) return;

                liveConns_.append(connect(host_, &RudpHost::cmdAckReceived,
                    this, [this](quint16 newTid){
                    if (!testActive_) return;
                    if (newTid == 0x0200) endTest(true, "");
                    else endTest(false,
                        QString("expected ACK 0x0200, got 0x%1")
                            .arg(newTid, 4, 16, QChar('0')));
                }));

                host_->startTransaction(0x0200, 2);
            });
        });
    }));

    host_->startTransaction(0x0100, 2);
}
