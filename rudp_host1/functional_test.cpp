#include "functional_test.h"
#include "rudp_host.h"
#include "logger.h"

#include <QTimer>
#include <QByteArray>

// =============================================================================
//  Backoff-расписание между повторными RETX на один блок
// =============================================================================
qint64 FunctionalTest::backoffMsForTries(int tries) {
    if (tries <= 0) return 0;
    if (tries < 10) return 20;
    if (tries < 20) return 100;
    if (tries < 30) return 500;
    return 2000;
}

// =============================================================================
//  Конструктор / деструктор
// =============================================================================
FunctionalTest::FunctionalTest(RudpHost* host, QObject* parent)
    : QObject(parent), host_(host) {

    suite_ = { 1, 2, 4, 8, 16, 64, 256 };

    timeoutTimer_ = new QTimer(this);
    timeoutTimer_->setSingleShot(true);
    connect(timeoutTimer_, &QTimer::timeout, this, [this]{
        if (!active_) return;
        const int stillMissing = int(curBlocks_) - validCount_;
        const qint64 elapsed   = clock_.elapsed();
        finishOne(false, QString("timeout after %1 ms "
                                 "(still missing %2 of %3, "
                                 "initial=%4, peak=%5, retx_sent=%6)")
                  .arg(elapsed).arg(stillMissing).arg(curBlocks_)
                  .arg(initialMissing_).arg(peakMissing_)
                  .arg(totalRetxSent_));
    });

    retxTimer_ = new QTimer(this);
    retxTimer_->setSingleShot(false);
    retxTimer_->setInterval(RETX_INTERVAL_MS);
    connect(retxTimer_, &QTimer::timeout, this, &FunctionalTest::retxTick);
}

FunctionalTest::~FunctionalTest() { cancel(); }

void FunctionalTest::setSuite(const QList<quint16>& counts) { suite_ = counts; }

QByteArray FunctionalTest::expectedPayload(quint16 blk) {
    QByteArray p(1024, 0);
    const quint8 b = quint8(blk & 0xFF);
    for (int a = 0; a < 1024; ++a)
        p[a] = char((b + quint8(a & 0xFF)) & 0xFF);
    return p;
}

void FunctionalTest::cancel() {
    if (active_) finishOne(false, "cancelled");
    if (suiteMode_) { suiteMode_ = false; finishAll(); }
}

// =============================================================================
//  Один суб-тест
// =============================================================================
void FunctionalTest::startOne(quint16 blocks) {
    if (!host_->isConfigured()) {
        emit progress("host not configured");
        return;
    }

    disconnectAll();
    host_->resetStats();

    curTid_    = quint16(0x1000 + suiteIdx_);
    curBlocks_ = blocks;

    // --- GAP Tracker: свежие битмапы ---
    receivedBlocks_ = QBitArray(int(curBlocks_), false);
    validBlocks_    = QBitArray(int(curBlocks_), false);
    pendingRetx_    = QBitArray(int(curBlocks_), false);

    validCount_      = 0;
    highestSeen_     = -1;
    lastNewBlockMs_  = 0;

    gapDetectedMs_.clear();
    badBlocksReasons_.clear();
    retxAttempts_.clear();
    lastAttemptMs_.clear();

    rxCount_         = 0;
    dupBlocks_       = 0;
    initialMissing_  = -1;
    peakMissing_     = 0;
    totalRetxSent_   = 0;
    lastStatusLogMs_ = 0;

    // --- GAP-статистика ---
    gapCountInterior_ = 0;
    gapCountTrailing_ = 0;
    gapsClosedLate_   = 0;
    gapsClosedByRetx_ = 0;
    longestGapMs_     = 0;

    clock_.start();
    active_ = true;

    conns_.append(connect(host_, &RudpHost::cmdAckReceived,
                          this, &FunctionalTest::onCmdAck));
    conns_.append(connect(host_, &RudpHost::resultReceived,
                          this, &FunctionalTest::onResult));
    conns_.append(connect(host_, &RudpHost::allBlocksReceived,
                          this, &FunctionalTest::onAllBlocks));
    conns_.append(connect(host_, &RudpHost::transactionAborted,
                          this, &FunctionalTest::onAborted));

    savedAutoComplete_ = host_->autoComplete();
    host_->setAutoComplete(false);

    host_->startTransaction(curTid_, blocks);

    retxTimer_->start();

    const int timeoutMs = 3000 + int(blocks) * 20;
    timeoutTimer_->start(timeoutMs);
}

// =============================================================================
//  Вспомогательное: зафиксировать новый gap
// =============================================================================
void FunctionalTest::noteGap(quint16 blk, qint64 now, bool trailing) {
    if (gapDetectedMs_.contains(blk)) return;
    gapDetectedMs_.insert(blk, now);

    if (trailing) ++gapCountTrailing_;
    else          ++gapCountInterior_;

    LOG_INFO("GAP detected%s: blk=%u at t=%lld ms",
             trailing ? " (trailing)" : "", blk, (long long)now);
}

// =============================================================================
//  Приём блока
// =============================================================================
void FunctionalTest::onResult(quint16 tid, quint16 blk, const QByteArray& payload) {
    if (!active_ || tid != curTid_) return;
    if (blk >= curBlocks_) return;

    const qint64 now = clock_.elapsed();

    // ---- 1. Получение как факт ----
    const bool wasNew = !receivedBlocks_.testBit(blk);
    if (wasNew) {
        receivedBlocks_.setBit(blk);
        ++rxCount_;
        lastNewBlockMs_ = now;

        if (int(blk) > highestSeen_) {
            for (int b = highestSeen_ + 1; b < int(blk); ++b) {
                if (!validBlocks_.testBit(b)) noteGap(quint16(b), now, false);
            }
            highestSeen_ = int(blk);
        }
    } else {
        ++dupBlocks_;
    }

    // ---- 2. Валидация payload ----
    bool    valid = true;
    QString reason;

    if (payload.size() != 1024) {
        valid  = false;
        reason = QString("blk=%1: wrong size %2 (expected 1024)")
                 .arg(blk).arg(payload.size());
    } else {
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
                                      .arg(got,  2, 16, QChar('0'))
                                      .arg(want, 2, 16, QChar('0')));
                }
            }
        }
        if (mismatchCount > 0) {
            valid  = false;
            reason = QString("blk=%1: %2 mismatched bytes (first %3: %4)")
                     .arg(blk).arg(mismatchCount).arg(mismatches.size())
                     .arg(mismatches.join(", "));
        }
    }

    // ---- 3. Обновление состояний ----
    if (valid) {
        if (!validBlocks_.testBit(blk)) {
            validBlocks_.setBit(blk);
            ++validCount_;
            badBlocksReasons_.remove(blk);

            // GAP закрыт
            if (gapDetectedMs_.contains(blk)) {
                const qint64 detectedAt = gapDetectedMs_.take(blk);
                const qint64 duration   = now - detectedAt;
                if (duration > longestGapMs_) longestGapMs_ = duration;

                if (retxAttempts_.value(blk, 0) == 0) {
                    ++gapsClosedLate_;
                    LOG_INFO("GAP closed: blk=%u at t=%lld ms "
                             "(late packet, duration=%lld ms, no retx)",
                             blk, (long long)now, (long long)duration);
                } else {
                    ++gapsClosedByRetx_;
                    LOG_INFO("GAP closed: blk=%u at t=%lld ms "
                             "(recovered by retx, duration=%lld ms, tries=%d)",
                             blk, (long long)now, (long long)duration, retxAttempts_.value(blk, 0));
                }
            }

            if (pendingRetx_.testBit(blk)) {
                pendingRetx_.clearBit(blk);
            }
        }
    } else {
        if (!validBlocks_.testBit(blk)) {
            badBlocksReasons_[blk] = reason;
        }
    }

    checkForCompletion();
}

// =============================================================================
//  Проверка завершения: успех только если ВСЕ блоки валидны
// =============================================================================
void FunctionalTest::checkForCompletion() {
    if (!active_) return;
    if (validCount_ == int(curBlocks_)) {
        finishOne(true, "");
    }
}

// =============================================================================
//  GAP Tracker + RETX Scheduler: тик раз в RETX_INTERVAL_MS
// =============================================================================
void FunctionalTest::retxTick() {
    if (!active_) { retxTimer_->stop(); return; }
    if (highestSeen_ < 0) return;

    const qint64 now = clock_.elapsed();

    const int validPct = (curBlocks_ > 0)
                         ? (validCount_ * 100) / int(curBlocks_)
                         : 0;
    const qint64 effectiveDelay = (validPct >= FAST_PATH_RX_PCT)
                                  ? FAST_PATH_RETX_DELAY_MS
                                  : FIRST_RETX_DELAY_MS;
    if (now < effectiveDelay) return;

    if (now - lastNewBlockMs_ < QUIET_PERIOD_MS) return;

    // Подтверждение gap'ов
    for (int b = 0; b < int(curBlocks_); ++b) {
        if (validBlocks_.testBit(b))  continue;
        if (pendingRetx_.testBit(b))  continue;

        pendingRetx_.setBit(b);
        if (!gapDetectedMs_.contains(quint16(b))) {
            noteGap(quint16(b), now, true);
        }
    }

    int stillMissing = 0;
    int stuck       = 0;
    int inBackoff   = 0;
    int candidates  = 0;

    for (int b = 0; b < int(curBlocks_); ++b) {
        if (!pendingRetx_.testBit(b)) continue;
        ++stillMissing;

        const int tries = retxAttempts_.value(quint16(b), 0);
        if (tries >= MAX_RETX_PER_BLOCK) { ++stuck; continue; }
        if (tries > 0) {
            const qint64 sinceLast = now - lastAttemptMs_.value(quint16(b), 0);
            if (sinceLast < backoffMsForTries(tries)) { ++inBackoff; continue; }
        }
        ++candidates;
    }

    if (stillMissing == 0) {
        retxTimer_->stop();
        LOG_INFO("RETX: all blocks valid, timer stopped (retx_sent_total=%d)",
                 totalRetxSent_);
        checkForCompletion();
        return;
    }

    if (initialMissing_ < 0) {
        initialMissing_ = stillMissing;
        peakMissing_    = stillMissing;
        LOG_INFO("GAP: initial missing=%d/%d (%.1f%%) "
                 "[valid=%d, delay=%lld ms]",
                 initialMissing_, curBlocks_,
                 100.0 * initialMissing_ / curBlocks_,
                 validCount_, (long long)effectiveDelay);
    }
    if (stillMissing > peakMissing_) peakMissing_ = stillMissing;

    if (candidates == 0 && stuck > 0 && inBackoff == 0) {
        retxTimer_->stop();
        finishOne(false, QString("%1 blocks unrecoverable after %2 attempts "
                                 "(initial=%3, peak=%4)")
                  .arg(stuck).arg(MAX_RETX_PER_BLOCK)
                  .arg(initialMissing_).arg(peakMissing_));
        return;
    }

    if (candidates == 0) {
        if (now - lastStatusLogMs_ >= STATUS_LOG_INTERVAL_MS) {
            LOG_INFO("RETX status: waiting backoff (stuck=%d, backoff=%d), "
                     "total_sent=%d, elapsed=%lld ms",
                     stuck, inBackoff, totalRetxSent_, (long long)now);
            lastStatusLogMs_ = now;
        }
        return;
    }

    int sent = 0;
    for (int b = 0; b < int(curBlocks_); ++b) {
        if (sent >= RETX_BATCH_SIZE) break;
        if (!pendingRetx_.testBit(b)) continue;

        const quint16 blk = quint16(b);
        const int tries = retxAttempts_.value(blk, 0);
        if (tries >= MAX_RETX_PER_BLOCK) continue;
        if (tries > 0) {
            const qint64 sinceLast = now - lastAttemptMs_.value(blk, 0);
            if (sinceLast < backoffMsForTries(tries)) continue;
        }

        host_->requestRetx(blk);
        retxAttempts_[blk] = tries + 1;
        lastAttemptMs_[blk] = now;
        ++sent;
    }
    totalRetxSent_ += sent;

    if (now - lastStatusLogMs_ >= STATUS_LOG_INTERVAL_MS) {
        LOG_INFO("RETX status: missing=%d (stuck=%d, backoff=%d), "
                 "sent=%d, total=%d, elapsed=%lld ms",
                 stillMissing, stuck, inBackoff, sent,
                 totalRetxSent_, (long long)now);
        lastStatusLogMs_ = now;
    }
}

// =============================================================================
//  Завершение одного суб-теста
// =============================================================================
void FunctionalTest::finishOne(bool pass, const QString& detail) {
    if (!active_) return;
    active_ = false;

    if (retxTimer_)    retxTimer_->stop();
    if (timeoutTimer_) timeoutTimer_->stop();
    disconnectAll();

    if (host_->isConfigured()) host_->sendCompleteCustom(curTid_);
    QTimer::singleShot(0, host_, &RudpHost::forceReset);
    host_->setAutoComplete(savedAutoComplete_);

    const qint64 elapsed = clock_.elapsed();
    const double mbps = (rxCount_ > 0)
            ? (double(rxCount_) * 1024.0 * 8.0) / (double(elapsed) / 1000.0) / 1e6
            : 0.0;

    const int initMiss     = (initialMissing_ < 0 ? 0 : initialMissing_);
    const int missingCount = int(curBlocks_) - validCount_;

    // Расчет GAP-статистики для вывода
    const int totalGaps = gapCountInterior_ + gapCountTrailing_;
    const int closedGaps = gapsClosedLate_ + gapsClosedByRetx_;

    if (pass) {
        LOG_INFO("FUNC PASS  blocks=%4u rx=%4d dup=%4d time=%lld ms  "
                 "(%.1f Mbit/s) initial=%d peak=%d retx_sent=%d "
                 "gaps=%d(int=%d,trail=%d) closed=%d(late=%d,retx=%d) longest=%lld ms",
                 curBlocks_, rxCount_, dupBlocks_, (long long)elapsed, mbps,
                 initMiss, peakMissing_, totalRetxSent_,
                 totalGaps, gapCountInterior_, gapCountTrailing_,
                 closedGaps, gapsClosedLate_, gapsClosedByRetx_,
                 (long long)longestGapMs_);

        emit progress(QString("blocks=%1 PASS  rx=%2  dup=%3  time=%4 ms  "
                              "(%5 Mbit/s)  initial=%6  retx=%7  "
                              "| gaps=%8(int=%9,trail=%10) closed=%11(late=%12,retx=%13) longest=%14 ms")
                      .arg(curBlocks_).arg(rxCount_).arg(dupBlocks_)
                      .arg(elapsed).arg(mbps, 0, 'f', 1)
                      .arg(initMiss).arg(totalRetxSent_)
                      .arg(totalGaps).arg(gapCountInterior_).arg(gapCountTrailing_)
                      .arg(closedGaps).arg(gapsClosedLate_).arg(gapsClosedByRetx_)
                      .arg(longestGapMs_));
    } else {
        LOG_ERR ("FUNC FAIL  blocks=%4u rx=%4d missing=%3d dup=%4d time=%lld ms  "
                 "reason=%s gaps=%d(int=%d,trail=%d) closed=%d(late=%d,retx=%d) longest=%lld ms",
                 curBlocks_, rxCount_, missingCount, dupBlocks_,
                 (long long)elapsed, qUtf8Printable(detail),
                 totalGaps, gapCountInterior_, gapCountTrailing_,
                 closedGaps, gapsClosedLate_, gapsClosedByRetx_,
                 (long long)longestGapMs_);

        int shown = 0;
        for (int b = 0; b < int(curBlocks_) && shown < BAD_BLOCKS_LIST_MAX; ++b) {
            if (validBlocks_.testBit(b)) continue;
            const quint16 blk = quint16(b);
            if (badBlocksReasons_.contains(blk)) {
                LOG_ERR("  %s", qUtf8Printable(badBlocksReasons_.value(blk)));
            } else if (gapDetectedMs_.contains(blk)) {
                LOG_ERR("  blk=%u: not received (gap since t=%lld ms, tries=%d)",
                        blk, (long long)gapDetectedMs_.value(blk),
                        retxAttempts_.value(blk, 0));
            } else {
                LOG_ERR("  blk=%u: not received (trailing)", blk);
            }
            ++shown;
        }
        if (missingCount > shown)
            LOG_ERR("  ... %d more", missingCount - shown);

        emit progress(QString("blocks=%1 FAIL  rx=%2  missing=%3  dup=%4  reason=%5  "
                              "| gaps=%6(int=%7,trail=%8) closed=%9(late=%10,retx=%11) longest=%12 ms")
                      .arg(curBlocks_).arg(rxCount_).arg(missingCount)
                      .arg(dupBlocks_).arg(detail)
                      .arg(totalGaps).arg(gapCountInterior_).arg(gapCountTrailing_)
                      .arg(closedGaps).arg(gapsClosedLate_).arg(gapsClosedByRetx_)
                      .arg(longestGapMs_));
    }

    host_->dumpStats();

    if (pass) ++suitePassed_; else ++suiteFailed_;
    if (!pass) suiteFailures_.append(
                QString("blocks=%1: %2").arg(curBlocks_).arg(detail));

    if (suiteMode_) {
        ++suiteIdx_;
        if (suiteIdx_ < suite_.size()) {
            QTimer::singleShot(INTER_TEST_PAUSE_MS, this, [this]{
                if (suiteMode_) startOne(suite_[suiteIdx_]);
            });
        } else {
            suiteMode_ = false;
            finishAll();
        }
    } else {
        finishAll();
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

void FunctionalTest::runSingle(quint16 blocks) {
    suiteMode_   = false;
    suiteIdx_    = 0;
    suitePassed_ = 0;
    suiteFailed_ = 0;
    suiteFailures_.clear();
    LOG_INFO("=== FUNCTIONAL TEST BEGIN (single, blocks=%u) ===", blocks);
    emit progress(QString("single transaction: blocks=%1").arg(blocks));
    startOne(blocks);
}

void FunctionalTest::runSuite() {
    suiteMode_   = true;
    suiteIdx_    = 0;
    suitePassed_ = 0;
    suiteFailed_ = 0;
    suiteFailures_.clear();
    LOG_INFO("=== FUNCTIONAL TEST BEGIN (suite, %d transactions) ===",
             int(suite_.size()));
    emit progress(QString("suite: %1 transactions").arg(suite_.size()));
    startOne(suite_.first());
}

void FunctionalTest::onCmdAck(quint16 tid) {
    if (!active_ || tid != curTid_) return;
}

void FunctionalTest::onAllBlocks(quint16 tid) {
    if (!active_ || tid != curTid_) return;
    LOG_INFO("Host reported all blocks received: valid=%d/%d, pending_retx=%d",
             validCount_, int(curBlocks_), int(curBlocks_) - validCount_);
}

void FunctionalTest::onAborted(quint16 tid, const QString& reason) {
    if (!active_ || tid != curTid_) return;
    LOG_INFO("onAborted: tid=0x%04X reason=%s at t=%lld ms "
             "(valid=%d/%d, lastNew=%lld ms)",
             tid, qUtf8Printable(reason),
             (long long)clock_.elapsed(),
             validCount_, int(curBlocks_),
             (long long)lastNewBlockMs_);
    finishOne(false, "aborted: " + reason);
}

void FunctionalTest::disconnectAll() {
    for (auto& c : conns_) QObject::disconnect(c);
    conns_.clear();
}
