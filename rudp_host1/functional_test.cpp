#include "functional_test.h"
#include "rudp_host.h"
#include "logger.h"

#include <QDateTime>
#include <QTimer>
#include <QByteArray>
#include <QSet>
#include <memory>

// =============================================================================
//  Конструктор / деструктор
// =============================================================================
FunctionalTest::FunctionalTest(RudpHost* host, QObject* parent)
    : QObject(parent), host_(host) {

    // Дефолтная suite. Все значения — в пределах RTL-лимита 4096.
    // Каждое значение — отдельная транзакция со своим TID.
    //suite_ = { 1, 2, 4, 16, 64, 256, 1024 };
    // Дефолтная suite. Ограничена 16 блоками, потому что result_ram
    // на текущей FPGA-сборке = 16 KB (16 блоков x 1024 байта).
    // Адрес в result_ram — {wr_block[11:0], wr_addr[9:0]}, но физической
    // памяти только 2^14 = 16384 байт, значит старшие 8 бит адреса
    // отбрасываются и блоки >= 16 перезаписывают блоки 0..15.
    // Когда RAM будет расширена (streaming-архитектура или SDRAM),
    // здесь можно вернуть { 1, 2, 4, 16, 64, 256, 1024, 4096 }.
    suite_ = { 1, 2, 4, 8, 16 };

    timeoutTimer_ = new QTimer(this);
    timeoutTimer_->setSingleShot(true);
    connect(timeoutTimer_, &QTimer::timeout, this, [this]{
        if (active_) {
            if (suiteMode_) {
                suiteFailed_++;
                suiteFailures_.append(
                    QString("blocks=%1: timeout").arg(curBlocks_));
                finishOne(false, "timeout");
            } else {
                finishOne(false, "timeout");
            }
        }
    });
}

FunctionalTest::~FunctionalTest() { cancel(); }

void FunctionalTest::setSuite(const QList<quint16>& counts) {
    suite_ = counts;
}

// =============================================================================
//  Утилиты
// =============================================================================

// Эталонный payload для блока blk. exec_engine пишет wr_data = b[7:0] + a[7:0],
// значит для каждого байта a (0..1023) ожидаем (blk + a) & 0xFF.
QByteArray FunctionalTest::expectedPayload(quint16 blk) {
    QByteArray p(1024, 0);
    const quint8 b = quint8(blk & 0xFF);
    for (int a = 0; a < 1024; ++a) {
        p[a] = char((b + quint8(a & 0xFF)) & 0xFF);
    }
    return p;
}

// =============================================================================
//  Управление
// =============================================================================
void FunctionalTest::cancel() {
    if (active_) finishOne(false, "cancelled");
    if (suiteMode_) {
        suiteMode_ = false;
        finishAll();
    }
}

// =============================================================================
//  Один суб-тест
// =============================================================================
void FunctionalTest::startOne(quint16 blocks) {
    if (!host_->isConfigured()) {
        emit progress("host not configured");
        return;
    }

    // Текущая FPGA-сборка: result_ram = 16 блоков.
       // Если запрошено больше — FPGA перезапишет ранние блоки,
       // тест получит заведомо ложный FAIL. Ловим раньше.
       if (blocks > 16) {
           LOG_ERR("blocks=%u exceeds current FPGA RAM capacity (16)", blocks);
           emit progress(QString("blocks=%1 skipped: exceeds RAM").arg(blocks));
           return;
       }

    disconnectAll();

    curTid_    = quint16(0x1000 + suiteIdx_);
    curBlocks_ = blocks;
    rxCount_   = 0;
    badBlocks_ = 0;
    dupBlocks_ = 0;
    badBlocksList_.clear();
    startMs_   = QDateTime::currentMSecsSinceEpoch();

    active_ = true;

    // Все соединения на время суб-теста.
    conns_.append(connect(host_, &RudpHost::cmdAckReceived,
        this, &FunctionalTest::onCmdAck));
    conns_.append(connect(host_, &RudpHost::resultReceived,
        this, &FunctionalTest::onResult));
    conns_.append(connect(host_, &RudpHost::allBlocksReceived,
        this, &FunctionalTest::onAllBlocks));
    conns_.append(connect(host_, &RudpHost::transactionAborted,
        this, &FunctionalTest::onAborted));

    // Отдельный счётчик дублей через resultReceived невозможен (см. ниже),
    // но общее число принятых блоков проверяем через rxCount_.
    // FunctionalTest ждёт РОВНО curBlocks_ уникальных блоков.

    host_->setAutoComplete(true);
    host_->startTransaction(curTid_, blocks);

    // Таймаут. Формула подобрана эмпирически по реальным прогонам:
    //   blocks=1   ~4 ms
    //   blocks=16  ~15 ms
    //   blocks=64  ~40 ms
    //   blocks=256 ~780 ms   (линейно растёт, ~3 ms/блок на хвосте)
    // Берём 5 мс на блок + 1 с фиксированного запаса — этого хватит
    // даже если реальное время вырастет вдвое из-за пауз в eth_vlg.
    const int estimatedMs = 1000 + int(blocks) * 5;
    timeoutTimer_->start(estimatedMs);
}

void FunctionalTest::finishOne(bool pass, const QString& detail) {
    if (!active_) return;
    active_ = false;
    timeoutTimer_->stop();
    disconnectAll();

    // Cleanup на FPGA: COMPLETE для текущего TID, если он ещё висит в WAIT_COMPLETE.
    // Если транзакция уже закрыта — игнорируется. Если транзакция не начиналась
    // (bad blockCount) — тоже безвредно.
    host_->sendCompleteCustom(curTid_);
    host_->forceReset();

    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startMs_;
    const double mbps = (rxCount_ > 0)
        ? (double(rxCount_) * 1024.0 * 8.0) / (double(elapsed) / 1000.0) / 1e6
        : 0.0;

    if (pass) {
        LOG_INFO("FUNC PASS  blocks=%4u rx=%4d time=%lld ms  (%.1f Mbit/s)",
                 curBlocks_, rxCount_, elapsed, mbps);
        emit progress(QString("blocks=%1 PASS  rx=%2  time=%3 ms  (%4 Mbit/s)")
                          .arg(curBlocks_).arg(rxCount_).arg(elapsed)
                          .arg(mbps, 0, 'f', 1));
    } else {
        LOG_ERR ("FUNC FAIL  blocks=%4u rx=%4d bad=%3d dup=%3d time=%lld ms  reason=%s",
                 curBlocks_, rxCount_, badBlocks_, dupBlocks_, elapsed,
                 qUtf8Printable(detail));
        for (const auto& s : badBlocksList_) LOG_ERR("  %s", qUtf8Printable(s));
        emit progress(QString("blocks=%1 FAIL  rx=%2  bad=%3  reason=%4")
                          .arg(curBlocks_).arg(rxCount_).arg(badBlocks_)
                          .arg(detail));
    }

    if (pass) ++suitePassed_; else ++suiteFailed_;
    if (!pass) suiteFailures_.append(
        QString("blocks=%1: %2").arg(curBlocks_).arg(detail));

    // Следующий суб-тест или финал.
    if (suiteMode_) {
        ++suiteIdx_;
        if (suiteIdx_ < suite_.size()) {
            // Небольшая пауза, чтобы FPGA успела закрыть cleanup-COMPLETE.
            QTimer::singleShot(200, this, [this]{
                if (suiteMode_) startOne(suite_[suiteIdx_]);
            });
        } else {
            suiteMode_ = false;
            finishAll();
        }
    }
}

void FunctionalTest::finishAll() {
    disconnectAll();

    const QString summary = QString("Functional suite: %1 passed, %2 failed")
                                .arg(suitePassed_).arg(suiteFailed_);
    if (suiteFailed_ == 0) {
        LOG_INFO("=== FUNCTIONAL TEST PASS: %s ===", qUtf8Printable(summary));
    } else {
        LOG_ERR ("=== FUNCTIONAL TEST FAIL: %s ===", qUtf8Printable(summary));
        for (const auto& s : suiteFailures_) LOG_ERR("  %s", qUtf8Printable(s));
    }
    emit testFinished(summary, suiteFailed_ == 0);
}

// =============================================================================
//  Точки входа
// =============================================================================
void FunctionalTest::runSingle(quint16 blocks) {
    suiteMode_ = false;
    suiteIdx_  = 0;
    suitePassed_ = 0;
    suiteFailed_ = 0;
    suiteFailures_.clear();

    LOG_INFO("=== FUNCTIONAL TEST BEGIN (single, blocks=%u) ===", blocks);
    emit progress(QString("single transaction: blocks=%1").arg(blocks));
    startOne(blocks);
}

void FunctionalTest::runSuite() {
    suiteMode_ = true;
    suiteIdx_  = 0;
    suitePassed_ = 0;
    suiteFailed_ = 0;
    suiteFailures_.clear();

    LOG_INFO("=== FUNCTIONAL TEST BEGIN (suite, %d transactions) ===",
             suite_.size());
    emit progress(QString("suite: %1 transactions").arg(suite_.size()));
    startOne(suite_.first());
}

// =============================================================================
//  Обработчики
// =============================================================================
void FunctionalTest::onCmdAck(quint16 tid) {
    if (!active_ || tid != curTid_) return;
    // Ничего не делаем. Хост всё равно запустит transaction,
    // и RESULT'ы начнут приходить.
}

void FunctionalTest::onResult(quint16 tid, quint16 blk, const QByteArray& payload) {
    if (!active_ || tid != curTid_) return;

    // Хост уже дедуплицирует блоки в blocks_; сюда прилетает только первый.
    // Проверяем payload и считаем.
    ++rxCount_;

    if (payload.size() != 1024) {
        ++badBlocks_;
        badBlocksList_.append(
            QString("blk=%1: wrong size %2 (expected 1024)")
                .arg(blk).arg(payload.size()));
        return;
    }

    const QByteArray expected = expectedPayload(blk);
    int mismatchCount = 0;
    QStringList mismatches;
    for (int i = 0; i < 1024; ++i) {
        const quint8 got  = quint8(payload[i]);
        const quint8 want = quint8(expected[i]);
        if (got != want) {
            ++mismatchCount;
            if (mismatches.size() < maxMismatchPerBlock_) {
                mismatches.append(QString("a=%1 got=0x%2 want=0x%3")
                                      .arg(i)
                                      .arg(got, 2, 16, QChar('0'))
                                      .arg(want, 2, 16, QChar('0')));
            }
        }
    }
    if (mismatchCount > 0) {
        ++badBlocks_;
        badBlocksList_.append(
            QString("blk=%1: %2 mismatched bytes (first %3: %4)")
                .arg(blk).arg(mismatchCount).arg(mismatches.size())
                .arg(mismatches.join(", ")));
    }
}

void FunctionalTest::onAllBlocks(quint16 tid) {
    if (!active_ || tid != curTid_) return;

    if (rxCount_ != curBlocks_) {
        finishOne(false, QString("rxCount=%1 != curBlocks=%2")
                             .arg(rxCount_).arg(curBlocks_));
        return;
    }
    if (badBlocks_ > 0) {
        finishOne(false, QString("%1 blocks with payload errors").arg(badBlocks_));
        return;
    }
    finishOne(true, "");
}

void FunctionalTest::onAborted(quint16 tid, const QString& reason) {
    if (!active_ || tid != curTid_) return;
    finishOne(false, "aborted: " + reason);
}

void FunctionalTest::disconnectAll() {
    for (auto& c : conns_) QObject::disconnect(c);
    conns_.clear();
}
