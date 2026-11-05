#pragma once

#include <QObject>
#include <QHostAddress>
#include <QUdpSocket>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QByteArray>

// =============================================================================
//  RudpHost
//  Тестовый хост протокола RUDP. Соответствует RTL rudp_core.sv.
//
//  Форматы пакетов на проводе (little-endian для многобайтовых полей):
//
//    CMD       PC -> FPGA, 1024 B
//              [tid_lo][tid_hi][blk_lo][blk_hi][1020 байт filler]
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
    void startTransaction(quint16 tid, quint16 blockCount);

    void finishTransaction(bool alsoReset = true);

    void sendCompleteCustom(quint16 tid);
    void sendRetxCustom(quint16 tid, quint16 block);
    void requestRetx(quint16 blockIndex);

    void abortTransaction();
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

    bool hasBlock(quint16 blk) const { return blocks_.contains(blk); }

    // Возвращает список block-номеров, по которым есть pending-RETX.
    // TID в возвращаемое значение не входит — это удобно для типичного
    // случая одной активной транзакции.
    QList<quint16> retxPendingBlocks() const {
        QList<quint16> out;
        out.reserve(retx_requested_.size());
        for (quint32 k : retx_requested_)
            out.append(quint16(k & 0xFFFFu));
        return out;
    }

    // -------------------------------------------------------------------------
    //  Диагностика: статистика сокета
    // -------------------------------------------------------------------------
    void resetStats();
    void dumpStats() const;

    quint64 statDatagrams() const { return stat_datagrams_; }
    quint64 statResults()   const { return stat_results_; }
    quint64 statRetxResp()  const { return stat_retx_resp_; }

signals:
    void cmdAckReceived(quint16 tid);
    void resultReceived(quint16 tid, quint16 block, const QByteArray& payload);
    void allBlocksReceived(quint16 tid);
    void transactionFinished(quint16 tid);
    void transactionAborted(quint16 tid, const QString& reason);
    void failed(const QString& reason);

private slots:
    void onReadyRead();
    void onWaitAckTimeout();

private:
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

    QTimer*      tWaitAck_   = nullptr;

    quint16      curTid_    = 0;
    quint16      nBlocks_   = 0;
    quint16      rxBlocks_  = 0;
    bool         active_    = false;
    bool         completed_ = false;

    bool         autoComplete_ = true;

    QHash<quint16, QByteArray> blocks_;

    // Pending RETX. Ключ = (tid << 16) | block.
    // Привязка к транзакции защищает от ложной классификации запоздавшего
    // RESULT со старым blk, но другим tid, как "ответа на наш RETX".
    QSet<quint32> retx_requested_;

    // -------------------------------------------------------------------------
    //  Диагностика (см. resetStats/dumpStats)
    // -------------------------------------------------------------------------
    quint64 stat_datagrams_ = 0;   // всего UDP-датаграмм от FPGA
    quint64 stat_ack_       = 0;   // CMD_ACK
    quint64 stat_results_   = 0;   // RESULT (все, включая RETX-ответы)
    quint64 stat_retx_resp_ = 0;   // RESULT в ответ на наш RETX
    quint64 stat_unknown_   = 0;   // мусор
};
