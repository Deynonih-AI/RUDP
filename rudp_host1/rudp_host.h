#pragma once

#include <QObject>
#include <QHostAddress>
#include <QUdpSocket>
#include <QTimer>
#include <QHash>
#include <QByteArray>

// =============================================================================
//  RudpHost
//  Тестовый хост протокола RUDP. Соответствует RTL rudp_core.sv.
//
//  Форматы пакетов на проводе (little-endian для многобайтовых полей):
//
//    CMD       PC -> FPGA, 1024 B
//              [tid_lo][tid_hi][blk_lo][blk_hi][1020 байт filler]
//              (нет type-байта: FPGA распознаёт CMD по длине == 1024)
//
//    CMD_ACK   FPGA -> PC, 8 B
//              [A1][tid_lo][tid_hi][00][00][00][00][00]
//
//    RESULT    FPGA -> PC, 1029 B
//              [B1][tid_lo][tid_hi][blk_lo][blk_hi][1024 B payload]
//
//    RETX      PC -> FPGA, 32 B
//              [C1][tid_lo][tid_hi][blk_lo][blk_hi_lo][27 B padding]
//
//    COMPLETE  PC -> FPGA, 32 B
//              [D1][tid_lo][tid_hi][29 B padding]
//
//  Короткие пакеты (RETX, COMPLETE) удлинены нулями до 32 байт, потому что
//  eth_vlg на FPGA пропускает только UDP-пакеты длиной >= 32 байта.
//
//  Один активный transaction за раз. Приём CMD_ACK и RESULT идёт
//  асинхронно через QUdpSocket::readyRead.
// =============================================================================

class RudpHost : public QObject {
    Q_OBJECT
public:
    explicit RudpHost(QObject* parent = nullptr);
    ~RudpHost() override;

    // -------------------------------------------------------------------------
    //  Настройка сети
    // -------------------------------------------------------------------------
    bool configure(const QString& fpgaIp,
                   quint16        fpgaPort,
                   quint16        pcPort);

    bool    isConfigured() const { return configured_; }
    quint16 pcPort()       const { return pcPort_; }
    quint16 fpgaPort()     const { return fpgaPort_; }
    QString fpgaIp()       const { return fpgaIp_.toString(); }

    // -------------------------------------------------------------------------
    //  Управление транзакцией
    // -------------------------------------------------------------------------
    // Старт транзакции: отправляет CMD и запускает таймаут ожидания CMD_ACK.
    void startTransaction(quint16 tid, quint16 blockCount);

    // Отправить COMPLETE с текущим tid и сбросить состояние (если alsoReset).
    // Если autoComplete = false, это ручной способ закрыть транзакцию.
    void finishTransaction(bool alsoReset = true);

    // Ручная отправка COMPLETE с произвольным TID (для тестов wrong-TID).
    // Локальное состояние не меняется.
    void sendCompleteCustom(quint16 tid);

    // Ручная отправка RETX с произвольным TID и block.
    // Локальное состояние не меняется.
    void sendRetxCustom(quint16 tid, quint16 block);

    // Запросить RETX по активной транзакции (эквивалент sendRetxCustom(curTid_, blk)).
    void requestRetx(quint16 blockIndex);

    // Аварийно прервать транзакцию без COMPLETE (эмитит transactionAborted).
    void abortTransaction();

    // Полный сброс локального состояния без отправки пакетов.
    // Нужен после ручного COMPLETE/wrong-TID тестов, чтобы начать новую транзакцию.
    void forceReset();

    // -------------------------------------------------------------------------
    //  Auto COMPLETE
    // -------------------------------------------------------------------------
    void setAutoComplete(bool on) { autoComplete_ = on; }
    bool autoComplete()     const { return autoComplete_; }

    // -------------------------------------------------------------------------
    //  Состояние
    // -------------------------------------------------------------------------
    bool    isActive()        const { return active_; }
    bool    allBlocksArrived() const { return completed_; }
    quint16 currentTid()      const { return curTid_; }
    quint16 blocksReceived()  const { return rxBlocks_; }
    quint16 blocksTotal()     const { return nBlocks_; }

signals:
    // Принят CMD_ACK с указанным tid.
    void cmdAckReceived(quint16 tid);

    // Принят RESULT. payload содержит ровно 1024 байта.
    void resultReceived(quint16 tid, quint16 block, const QByteArray& payload);

    // Все блоки транзакции приняты (сигнал эмитится один раз на транзакцию).
    void allBlocksReceived(quint16 tid);

    // Транзакция корректно завершена (COMPLETE отправлен + всё принято).
    void transactionFinished(quint16 tid);

    // Транзакция прервана по таймауту или по запросу abortTransaction().
    void transactionAborted(quint16 tid, const QString& reason);

    // Ошибка конфигурации (bind, writeDatagram).
    void failed(const QString& reason);

private slots:
    void onReadyRead();
    void onWaitAckTimeout();

private:
    // -------------------------------------------------------------------------
    //  Внутренние помощники
    // -------------------------------------------------------------------------
    void sendRaw(const QByteArray& pkt);

    void sendCmd(quint16 tid, quint16 blockCount);
    void sendRetx(quint16 tid, quint16 block);
    void sendComplete(quint16 tid);

    void resetTransaction();

    // -------------------------------------------------------------------------
    //  Данные
    // -------------------------------------------------------------------------
    QUdpSocket*  sock_       = nullptr;
    QHostAddress fpgaIp_;
    quint16      fpgaPort_   = 0;
    quint16      pcPort_     = 0;
    bool         configured_ = false;

    // Таймаут ожидания CMD_ACK.
    QTimer*      tWaitAck_   = nullptr;

    // Текущая транзакция.
    quint16      curTid_    = 0;
    quint16      nBlocks_   = 0;
    quint16      rxBlocks_  = 0;
    bool         active_    = false;
    bool         completed_ = false;   // все блоки получены (для autoComplete=false)

    // Режим завершения.
    bool         autoComplete_ = true;

    // Уже принятые блоки — для отсеивания дубликатов RESULT.
    QHash<quint16, QByteArray> blocks_;
};
