#pragma once

#include <QObject>
#include <QTimer>
#include <QList>
#include <QStringList>
#include <QMetaObject>

class RudpHost;

// =============================================================================
//  FunctionalTest
//  Функциональный тест RUDP на реальном железе.
//
//  Отличия от TestRunner:
//    - TestRunner проверяет поведение протокола (robustness, negative).
//    - FunctionalTest проверяет ПРАВИЛЬНОСТЬ данных: каждый байт payload
//      каждого блока сверяется с эталоном (b + a) & 0xFF.
//
//  Прогон:
//    1. Для каждого block_count из suite_:
//       - запустить транзакцию с уникальным TID;
//       - принять все блоки;
//       - сверить payload;
//       - записать результат (PASS/FAIL, кол-во ошибок, время).
//    2. По окончании — emit testFinished(summary, overall_passed).
//
//  TID для functional-теста: 0x1000 + idx (не пересекается с 0x0100/0x0200,
//  которые использует TestRunner).
// =============================================================================
class FunctionalTest : public QObject {
    Q_OBJECT
public:
    explicit FunctionalTest(RudpHost* host, QObject* parent = nullptr);
    ~FunctionalTest() override;

    bool isRunning() const { return active_; }

    // Список block_count, который пройдёт runSuite().
    // По умолчанию: {1, 2, 4, 16, 64, 256, 1024}.
    void setSuite(const QList<quint16>& counts);
    QList<quint16> suite() const { return suite_; }

    // Сколько первых несовпадений логировать на блок (по умолчанию 4).
    void setMaxMismatchesPerBlock(int n) { maxMismatchPerBlock_ = n; }

public slots:
    // Прогон одного теста для указанного числа блоков.
    void runSingle(quint16 blocks);

    // Прогон всей suite_.
    void runSuite();

    // Отмена текущего прогона.
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
    // Запуск одного суб-теста. tid генерируется автоматически.
    void startOne(quint16 blocks);
    // Завершение одного суб-теста (PASS или FAIL), с cleanup.
    void finishOne(bool pass, const QString& detail);
    // Завершение всей suite_.
    void finishAll();
    void disconnectAll();

    // Эталонный payload для блока blk: byte[a] = (blk + a) & 0xFF.
    static QByteArray expectedPayload(quint16 blk);

    RudpHost* host_ = nullptr;
    QList<QMetaObject::Connection> conns_;

    bool    active_    = false;
    bool    suiteMode_ = false;
    QList<quint16> suite_;
    int     suiteIdx_  = 0;

    // Текущий суб-тест
    quint16 curTid_    = 0;
    quint16 curBlocks_ = 0;
    int     rxCount_   = 0;
    int     badBlocks_ = 0;      // сколько блоков не сошлось по payload
    int     dupBlocks_ = 0;      // сколько раз пришёл уже принятый блок
    QStringList badBlocksList_;  // первые N имён испорченных блоков
    qint64  startMs_   = 0;

    int     maxMismatchPerBlock_ = 4;

    // Общая статистика suite_
    int     suitePassed_ = 0;
    int     suiteFailed_ = 0;
    QStringList suiteFailures_;

    QTimer* timeoutTimer_ = nullptr;
};
