#pragma once

#include <QObject>
#include <QTimer>
#include <QList>
#include <QString>
#include <QMetaObject>

class RudpHost;

// =============================================================================
//  TestRunner
//  Прогон автоматических robustness-тестов протокола RUDP поверх RudpHost.
//
//  Каждый тест — это скриптованная последовательность шагов с фиксированными
//  таймингами (50–300 мс). Тест сам:
//    - запускает нужные транзакции;
//    - шлёт RETX / COMPLETE в нужные моменты;
//    - проверяет ответ FPGA;
//    - эмитит testFinished(passed, detail) через QTimer-таймаут или раньше,
//      если ожидаемое событие пришло.
//
//  Все соединения с сигналами RudpHost разрываются в endTest(), и хост
//  принудительно сбрасывается в idle, чтобы следующий тест стартовал с
//  чистого состояния.
// =============================================================================
class TestRunner : public QObject {
    Q_OBJECT
public:
    explicit TestRunner(RudpHost* host, QObject* parent = nullptr);
    ~TestRunner() override;

    bool isRunning() const { return testActive_; }

public slots:
    // Скриптованные сценарии. Каждый можно дёргать одной кнопкой в GUI.
    void runHappyPath();          // CMD -> ACK -> 2xRESULT -> auto COMPLETE
    void runWrongTidComplete();   // wrong-TID COMPLETE игнорируется, correct закрывает
    void runRetxOutOfRange();     // RETX blk=5 при blocks=2 игнорируется, blk=1 работает
    void runTimeoutFpga();        // FPGA уходит по своему таймауту, следующий CMD проходит
    void runDuplicateComplete();  // второй COMPLETE в idle ничего не ломает

    // Прервать текущий тест (если он висит).
    void cancel();

signals:
    void testStarted (const QString& name);
    void testFinished(const QString& name, bool passed, const QString& detail);

private:
    void beginTest(const QString& name, int timeoutMs);
    void endTest  (bool pass, const QString& detail);

    RudpHost* host_;
    QTimer*   timeoutTimer_;
    QString   currentName_;
    bool      testActive_ = false;

    // Живые соединения текущего теста. Разрываются в endTest().
    QList<QMetaObject::Connection> liveConns_;
};
