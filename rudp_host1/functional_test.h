#pragma once

#include <QObject>
#include <QTimer>
#include <QBitArray>
#include <QList>
#include <QStringList>
#include <QMetaObject>
#include <QHash>
#include <QElapsedTimer>

class RudpHost;

class FunctionalTest : public QObject {
    Q_OBJECT
public:
    explicit FunctionalTest(RudpHost* host, QObject* parent = nullptr);
    ~FunctionalTest() override;

    bool isRunning() const { return active_; }

    void setSuite(const QList<quint16>& counts);
    QList<quint16> suite() const { return suite_; }

    void setMaxMismatchesPerBlock(int n) { maxMismatchPerBlock_ = n; }

public slots:
    void runSingle(quint16 blocks);
    void runSuite();
    void cancel();

signals:
    void progress(const QString& msg);
    void testFinished(const QString& summary, bool passed);

private slots:
    void onCmdAck  (quint16 tid);
    void onResult  (quint16 tid, quint16 blk, const QByteArray& payload);
    void onAllBlocks(quint16 tid);
    void onAborted (quint16 tid, const QString& reason);

private:
    void startOne(quint16 blocks);
    void finishOne(bool pass, const QString& detail);
    void finishAll();
    void disconnectAll();
    void checkForCompletion();
    void retxTick();

    // GAP Tracker: зафиксировать пропуск (trailing = true для хвостовых)
    void noteGap(quint16 blk, qint64 now, bool trailing);

    static QByteArray expectedPayload(quint16 blk);
    static qint64 backoffMsForTries(int tries);

    // ---- Параметры RETX-стратегии ----
    static constexpr int    RETX_INTERVAL_MS        = 20;
    static constexpr int    RETX_BATCH_SIZE         = 5;
    static constexpr qint64 FIRST_RETX_DELAY_MS     = 1500;
    static constexpr int    FAST_PATH_RX_PCT        = 95;
    static constexpr qint64 FAST_PATH_RETX_DELAY_MS = 500;
    static constexpr qint64 QUIET_PERIOD_MS         = 100;
    static constexpr int    MAX_RETX_PER_BLOCK      = 30;
    static constexpr int    BAD_BLOCKS_LIST_MAX     = 50;
    static constexpr qint64 STATUS_LOG_INTERVAL_MS  = 1000;
    static constexpr int    INTER_TEST_PAUSE_MS     = 1000;

    RudpHost* host_ = nullptr;
    QList<QMetaObject::Connection> conns_;

    bool    active_    = false;
    bool    suiteMode_ = false;
    QList<quint16> suite_;
    int     suiteIdx_  = 0;

    QTimer* retxTimer_    = nullptr;
    QTimer* timeoutTimer_ = nullptr;
    QElapsedTimer clock_;

    quint16 curTid_    = 0;
    quint16 curBlocks_ = 0;

    // GAP Tracker state
    QBitArray receivedBlocks_;
    QBitArray validBlocks_;
    QBitArray pendingRetx_;

    int   validCount_      = 0;
    int   highestSeen_     = -1;
    qint64 lastNewBlockMs_ = 0;

    QHash<quint16, qint64>  gapDetectedMs_;
    QHash<quint16, QString> badBlocksReasons_;

    // RETX Scheduler state
    QHash<quint16, int>    retxAttempts_;
    QHash<quint16, qint64> lastAttemptMs_;

    // Счётчики прогона
    int     rxCount_         = 0;
    int     dupBlocks_       = 0;
    int     initialMissing_  = -1;
    int     peakMissing_     = 0;
    int     totalRetxSent_   = 0;
    qint64  lastStatusLogMs_ = 0;

    // GAP-статистика
    int     gapCountInterior_ = 0;
    int     gapCountTrailing_ = 0;
    int     gapsClosedLate_   = 0;
    int     gapsClosedByRetx_ = 0;
    qint64  longestGapMs_     = 0;

    int     maxMismatchPerBlock_ = 4;
    int     suitePassed_         = 0;
    int     suiteFailed_         = 0;
    QStringList suiteFailures_;
    bool    savedAutoComplete_   = true;
};
