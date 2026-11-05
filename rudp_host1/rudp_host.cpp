#include "rudp_host.h"
#include "logger.h"
#include "rudp_types.h"

#include <QTimer>

// =============================================================================
//  Таймауты PC-хоста.
// =============================================================================
static constexpr int WAIT_ACK_MS = 500;

// =============================================================================
//  Размеры UDP-буферов.
//  RX: 16 МБ, чтобы 4 МБ поток (4096 блоков) не переполнял буфер при
//      малейших задержках PC.
// =============================================================================
static constexpr int RX_BUF_BYTES = 16 * 1024 * 1024;
static constexpr int TX_BUF_BYTES =  1 * 1024 * 1024;

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

    // ВАЖНО: ставим буферы ДО первого пакета.
    sock_->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,
                           RX_BUF_BYTES);
    sock_->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption,
                           TX_BUF_BYTES);

    const int actualRx = sock_->socketOption(
                QAbstractSocket::ReceiveBufferSizeSocketOption).toInt();
    const int actualTx = sock_->socketOption(
                QAbstractSocket::SendBufferSizeSocketOption).toInt();

    LOG_INFO("UDP buffers: RX req=%d actual=%d, TX req=%d actual=%d",
             RX_BUF_BYTES, actualRx, TX_BUF_BYTES, actualTx);

    if (actualRx < 8 * 1024 * 1024) {
        LOG_ERR("WARNING: UDP RX buffer < 8 MB. "
                "4096-block transfers may lose packets!");
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
    if (blockCount > 4096) {
        LOG_ERR("blockCount=%u exceeds RTL limit 4096", blockCount);
        return;
    }

    curTid_    = tid;
    nBlocks_   = blockCount;
    rxBlocks_  = 0;
    completed_ = false;
    blocks_.clear();
    retx_requested_.clear();
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

void RudpHost::sendCompleteCustom(quint16 tid) { sendComplete(tid); }
void RudpHost::sendRetxCustom(quint16 tid, quint16 block) { sendRetx(tid, block); }

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
    retx_requested_.clear();
    tWaitAck_->stop();
}

// =============================================================================
//  Статистика
// =============================================================================
void RudpHost::resetStats() {
    stat_datagrams_ = 0;
    stat_ack_       = 0;
    stat_results_   = 0;
    stat_retx_resp_ = 0;
    stat_unknown_   = 0;
}

void RudpHost::dumpStats() const {
    LOG_INFO("UDP stats: datagrams=%llu ack=%llu results=%llu "
             "retx_resp=%llu unknown=%llu",
             (unsigned long long)stat_datagrams_,
             (unsigned long long)stat_ack_,
             (unsigned long long)stat_results_,
             (unsigned long long)stat_retx_resp_,
             (unsigned long long)stat_unknown_);
}

// =============================================================================
//  Формирование и отправка пакетов
// =============================================================================
void RudpHost::sendCmd(quint16 tid, quint16 blockCount) {
    QByteArray cmd(rudp::CMD_SIZE, 0);
    cmd[0] = char( tid        & 0xFF);
    cmd[1] = char((tid  >> 8) & 0xFF);
    cmd[2] = char( blockCount       & 0xFF);
    cmd[3] = char((blockCount >> 8) & 0xFF);
    for (int i = 4; i < rudp::CMD_SIZE; ++i)
        cmd[i] = char(i & 0xFF);

    sendRaw(cmd);
    LOG_INFO("TX CMD      tid=0x%04X blocks=%u (%d B)",
             tid, blockCount, cmd.size());
}

void RudpHost::sendRetx(quint16 tid, quint16 block) {
    QByteArray pkt(rudp::RETX_SIZE, 0);
    pkt[0] = char(rudp::TYPE_RETX);
    pkt[1] = char( tid       & 0xFF);
    pkt[2] = char((tid >> 8) & 0xFF);
    pkt[3] = char( block       & 0xFF);
    pkt[4] = char((block >> 8) & 0x0F);

    // Ключ = (tid << 16) | block. Привязка к конкретной транзакции.
    retx_requested_.insert((quint32(tid) << 16) | block);
    sendRaw(pkt);

    // Throttle: первые 20 и потом каждый 100-й (не спамим GUI)
    static int tx_logged = 0;
    if (tx_logged < 20 || tx_logged % 100 == 0) {
        LOG_INFO("TX RETX     tid=0x%04X blk=%u (%d B)",
                 tid, block, pkt.size());
    }
    ++tx_logged;
}

void RudpHost::sendComplete(quint16 tid) {
    QByteArray pkt(rudp::COMPLETE_SIZE, 0);
    pkt[0] = char(rudp::TYPE_COMPLETE);
    pkt[1] = char( tid       & 0xFF);
    pkt[2] = char((tid >> 8) & 0xFF);

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
        ++stat_datagrams_;

        // ---------- CMD_ACK ----------
        if (dg.size() >= rudp::ACK_SIZE
            && quint8(dg[0]) == rudp::TYPE_CMD_ACK) {

            ++stat_ack_;
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
            ++stat_results_;

            const quint16 tid = quint8(dg[1])
                              | (quint16(quint8(dg[2])) << 8);
            const quint16 blk = quint8(dg[3])
                              | (quint16(quint8(dg[4]) & 0x0F) << 8);
            const QByteArray payload = dg.mid(5);

            // Классификация "RETX-ответ" — по паре (tid, blk),
            // а не только по blk. Иначе запоздавший RESULT со старым blk,
            // но другим tid, ошибочно считался бы ответом на текущий RETX.
            const quint32 retxKey = (quint32(tid) << 16) | blk;
            const bool is_retx_response = retx_requested_.contains(retxKey);

            if (is_retx_response) {
                ++stat_retx_resp_;
                // Throttle: первые 20 и потом каждый 100-й
                static int retx_logged = 0;
                if (retx_logged < 20 || retx_logged % 100 == 0) {
                    LOG_INFO("RX RESULT[RETX] tid=0x%04X blk=%u payload=%d B",
                             tid, blk, payload.size());
                }
                ++retx_logged;
                retx_requested_.remove(retxKey);
            } else {
                // Обычный RESULT: throttle — каждый 100-й блок (для диагностики потока)
                if ((blk % 100) == 0) {
                    LOG_INFO("RX RESULT  tid=0x%04X blk=%u payload=%d B",
                             tid, blk, payload.size());
                }
            }

            emit resultReceived(tid, blk, payload);

            if (active_ && tid == curTid_) {
                if (!blocks_.contains(blk)) {
                    blocks_.insert(blk, payload);
                    ++rxBlocks_;
                }

                if (rxBlocks_ >= nBlocks_ && !completed_) {
                    completed_ = true;
                    quint16 finishedTid = curTid_;

                    emit allBlocksReceived(finishedTid);

                    if (autoComplete_) {
                        sendComplete(finishedTid);
                        LOG_INFO("transaction 0x%04X complete (auto)", finishedTid);
                        emit transactionFinished(finishedTid);
                        resetTransaction();
                    }
                }
            }
            continue;
        }

        // ---------- мусор ----------
        ++stat_unknown_;
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
