#include "rudp_host.h"
#include "logger.h"
#include "rudp_types.h"

#include <QTimer>

// =============================================================================
//  Таймауты PC-хоста.
//  WAIT_ACK_MS — сколько ждём CMD_ACK после отправки CMD.
// =============================================================================
static constexpr int WAIT_ACK_MS = 500;

// =============================================================================
//  Конструктор / деструктор
// =============================================================================
RudpHost::RudpHost(QObject* parent) : QObject(parent) {
    sock_ = new QUdpSocket(this);
    connect(sock_, &QUdpSocket::readyRead, this, &RudpHost::onReadyRead);

    tWaitAck_ = new QTimer(this);
    tWaitAck_->setSingleShot(true);
    connect(tWaitAck_, &QTimer::timeout, this, &RudpHost::onWaitAckTimeout);
}

RudpHost::~RudpHost() = default;

// =============================================================================
//  Настройка
// =============================================================================
bool RudpHost::configure(const QString& fpgaIp,
                         quint16        fpgaPort,
                         quint16        pcPort) {
    if (sock_->state() == QAbstractSocket::BoundState)
        sock_->close();

    if (!sock_->bind(QHostAddress::AnyIPv4, pcPort)) {
        LOG_ERR("bind(%u) failed: %s", pcPort,
                qUtf8Printable(sock_->errorString()));
        emit failed("bind failed");
        configured_ = false;
        return false;
    }

    fpgaIp_     = QHostAddress(fpgaIp);
    fpgaPort_   = fpgaPort;
    pcPort_     = pcPort;
    configured_ = true;

    LOG_INFO("configured: PC port=%u -> FPGA %s:%u",
             pcPort, qUtf8Printable(fpgaIp), fpgaPort);
    return true;
}

// =============================================================================
//  Управление транзакцией
// =============================================================================
void RudpHost::startTransaction(quint16 tid, quint16 blockCount) {
    if (!configured_) {
        LOG_ERR("startTransaction before configure()");
        return;
    }
    if (active_) {
        LOG_ERR("startTransaction while busy (tid=0x%04X)", curTid_);
        return;
    }
    if (blockCount == 0) {
        LOG_ERR("blockCount == 0 not allowed");
        return;
    }
    // RTL в rudp_core.sv принимает только cmd_blocks_rx в диапазоне 1..4096.
    // Вне диапазона — FPGA молча проигнорирует CMD, и хост зависнет
    // на ожидании CMD_ACK. Ловим ошибку на стороне хоста.
    if (blockCount > 4096) {
        LOG_ERR("blockCount=%u exceeds RTL limit 4096", blockCount);
        return;
    }

    curTid_    = tid;
    nBlocks_   = blockCount;
    rxBlocks_  = 0;
    completed_ = false;
    blocks_.clear();
    active_    = true;

    sendCmd(tid, blockCount);
    tWaitAck_->start(WAIT_ACK_MS);
}

void RudpHost::requestRetx(quint16 blockIndex) {
    if (!active_) {
        LOG_ERR("requestRetx while idle");
        return;
    }
    sendRetx(curTid_, blockIndex);
}

void RudpHost::finishTransaction(bool alsoReset) {
    if (!active_) {
        LOG_ERR("finishTransaction while idle");
        return;
    }
    sendComplete(curTid_);
    if (alsoReset) {
        LOG_INFO("transaction 0x%04X finished by host", curTid_);
        emit transactionFinished(curTid_);
        resetTransaction();
    }
}

void RudpHost::sendCompleteCustom(quint16 tid) {
    sendComplete(tid);
}

void RudpHost::sendRetxCustom(quint16 tid, quint16 block) {
    sendRetx(tid, block);
}

void RudpHost::abortTransaction() {
    if (!active_) return;
    LOG_INFO("transaction 0x%04X aborted by host", curTid_);
    emit transactionAborted(curTid_, "host abort");
    resetTransaction();
}

void RudpHost::forceReset() {
    if (active_) {
        LOG_INFO("force reset (tid=0x%04X) — no packet sent", curTid_);
    }
    resetTransaction();
}

void RudpHost::resetTransaction() {
    active_    = false;
    curTid_    = 0;
    nBlocks_   = 0;
    rxBlocks_  = 0;
    completed_ = false;
    blocks_.clear();
    tWaitAck_->stop();
}

// =============================================================================
//  Формирование и отправка пакетов
// =============================================================================

// CMD: 1024 байта. tid в байтах 0..1 (LE), blocks в байтах 2..3 (LE).
void RudpHost::sendCmd(quint16 tid, quint16 blockCount) {
    QByteArray cmd(rudp::CMD_SIZE, 0);
    cmd[0] = char( tid        & 0xFF);
    cmd[1] = char((tid  >> 8) & 0xFF);
    cmd[2] = char( blockCount       & 0xFF);
    cmd[3] = char((blockCount >> 8) & 0xFF);
    // Остальные 1020 байт — заполнитель; FPGA их не разбирает.
    for (int i = 4; i < rudp::CMD_SIZE; ++i)
        cmd[i] = char(i & 0xFF);

    sendRaw(cmd);
    LOG_INFO("TX CMD      tid=0x%04X blocks=%u (%d B)",
             tid, blockCount, cmd.size());
}

// RETX: 32 байта. Логически полезные поля в первых 5 байтах:
//   [C1][tid_lo][tid_hi][blk_lo][blk_hi_lo]
// Остальные 27 байт — нули. Это требование eth_vlg на FPGA:
// он пропускает только UDP-пакеты длиной >= 32 байта.
void RudpHost::sendRetx(quint16 tid, quint16 block) {
    QByteArray pkt(rudp::RETX_SIZE, 0);   // 32 байта, все нули
    pkt[0] = char(rudp::TYPE_RETX);
    pkt[1] = char( tid       & 0xFF);
    pkt[2] = char((tid >> 8) & 0xFF);
    pkt[3] = char( block       & 0xFF);
    pkt[4] = char((block >> 8) & 0x0F);   // только 4 младших бита
    // pkt[5..31] остаются нулями

    sendRaw(pkt);
    LOG_INFO("TX RETX     tid=0x%04X blk=%u (%d B)",
             tid, block, pkt.size());
}

// COMPLETE: 32 байта. Логически полезные поля в первых 3 байтах:
//   [D1][tid_lo][tid_hi]
// Остальные 29 байт — нули.
void RudpHost::sendComplete(quint16 tid) {
    QByteArray pkt(rudp::COMPLETE_SIZE, 0);   // 32 байта, все нули
    pkt[0] = char(rudp::TYPE_COMPLETE);
    pkt[1] = char( tid       & 0xFF);
    pkt[2] = char((tid >> 8) & 0xFF);
    // pkt[3..31] остаются нулями

    sendRaw(pkt);
    LOG_INFO("TX COMPLETE tid=0x%04X (%d B)", tid, pkt.size());
}

void RudpHost::sendRaw(const QByteArray& pkt) {
    const qint64 n = sock_->writeDatagram(pkt, fpgaIp_, fpgaPort_);
    if (n != pkt.size()) {
        LOG_ERR("writeDatagram failed: %lld of %d, err=%s",
                n, pkt.size(), qUtf8Printable(sock_->errorString()));
    }
}

// =============================================================================
//  Приём
// =============================================================================
void RudpHost::onReadyRead() {
    while (sock_->hasPendingDatagrams()) {
        QByteArray dg;
        dg.resize(int(sock_->pendingDatagramSize()));

        QHostAddress from;
        quint16 fromPort = 0;
        sock_->readDatagram(dg.data(), dg.size(), &from, &fromPort);

        // ---------- CMD_ACK ----------
        // Логически rudp_tx отдаёт 3 байта [A1][tid_lo][tid_hi].
        // Физически eth_vlg может паддировать до >= 8 байт.
        if (dg.size() >= rudp::ACK_SIZE
            && quint8(dg[0]) == rudp::TYPE_CMD_ACK) {

            const quint16 tid = quint8(dg[1])
                              | (quint16(quint8(dg[2])) << 8);

            if (dg.size() != rudp::ACK_SIZE) {
                LOG_INFO("RX CMD_ACK  tid=0x%04X  len=%d (pad=%s)",
                         tid, dg.size(),
                         qUtf8Printable(dg.mid(rudp::ACK_SIZE).toHex(' ')));
            } else {
                LOG_INFO("RX CMD_ACK  tid=0x%04X", tid);
            }

            tWaitAck_->stop();
            emit cmdAckReceived(tid);
            continue;
        }

        // ---------- RESULT ----------
        if (dg.size() == rudp::RESULT_SIZE
            && quint8(dg[0]) == rudp::TYPE_RESULT) {
            const quint16 tid = quint8(dg[1])
                              | (quint16(quint8(dg[2])) << 8);
            const quint16 blk = quint8(dg[3])
                              | (quint16(quint8(dg[4]) & 0x0F) << 8);
            const QByteArray payload = dg.mid(5);

            LOG_INFO("RX RESULT   tid=0x%04X blk=%u payload=%d B",
                     tid, blk, payload.size());
            emit resultReceived(tid, blk, payload);

            if (active_ && tid == curTid_) {
                if (!blocks_.contains(blk)) {
                    blocks_.insert(blk, payload);
                    ++rxBlocks_;
                }

                if (rxBlocks_ >= nBlocks_ && !completed_) {
                    completed_ = true;
                    emit allBlocksReceived(curTid_);

                    if (autoComplete_) {
                        sendComplete(curTid_);
                        LOG_INFO("transaction 0x%04X complete (auto)", curTid_);
                        emit transactionFinished(curTid_);
                        resetTransaction();
                    } else {
                        LOG_INFO("all blocks received; waiting manual COMPLETE "
                                 "(tid=0x%04X)", curTid_);
                    }
                }
            }
            continue;
        }

        // ---------- мусор ----------
        const int dumpLen = qMin(dg.size(), 32);
        LOG_ERR("RX unknown  len=%d first=%s  from %s:%u",
                dg.size(),
                qUtf8Printable(dg.left(dumpLen).toHex(' ')),
                qUtf8Printable(from.toString()), fromPort);
    }
}

// =============================================================================
//  Таймауты
// =============================================================================
void RudpHost::onWaitAckTimeout() {
    LOG_ERR("timeout waiting CMD_ACK tid=0x%04X", curTid_);
    emit transactionAborted(curTid_, "no CMD_ACK");
    resetTransaction();
}
