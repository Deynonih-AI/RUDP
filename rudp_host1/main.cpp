#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QLabel>
#include <QSpinBox>
#include <QSettings>
#include <QFileInfo>

#include "rudp_host.h"
#include "test_runner.h"
#include "logger.h"
#include "rudp_types.h"
#include "functional_test.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("rudp_host1");
    QCoreApplication::setOrganizationName("RUDP");

    Logger::instance().configure();

    // ---- Чтение сетевых настроек из INI ----
    const QString exe = QCoreApplication::applicationFilePath();
    const QString ini = QFileInfo(exe).absolutePath() + "/" +
                        QFileInfo(exe).completeBaseName() + ".ini";

    QSettings s(ini, QSettings::IniFormat);
    s.beginGroup("Network");
    const QString fpgaIp   = s.value("FPGA_IP",   "192.168.100.10").toString();
    const quint16 fpgaPort = s.value("FPGA_PORT", 1234).toUInt();
    const quint16 pcPort   = s.value("PC_PORT",   1233).toUInt();
    s.endGroup();

    // ---------------- UI ----------------
    QMainWindow w;
    auto* central = new QWidget;
    auto* v       = new QVBoxLayout(central);

    // ---- Network ----
    auto* netBox  = new QGroupBox("Network");
    auto* netForm = new QFormLayout(netBox);
    auto* ipEdit   = new QLineEdit(fpgaIp);
    auto* fpgaEdit = new QLineEdit(QString::number(fpgaPort));
    auto* pcEdit   = new QLineEdit(QString::number(pcPort));
    netForm->addRow("FPGA IP   :", ipEdit);
    netForm->addRow("FPGA port :", fpgaEdit);
    netForm->addRow("PC   port :", pcEdit);
    v->addWidget(netBox);

    // ---- Transaction ----
    auto* txBox  = new QGroupBox("Transaction");
    auto* txForm = new QFormLayout(txBox);
    auto* tidEdit   = new QLineEdit("0100");
    auto* nEdit     = new QLineEdit("2");
    auto* autoChk   = new QCheckBox("Auto COMPLETE after all blocks");
    autoChk->setChecked(true);
    txForm->addRow("TID (hex)     :", tidEdit);
    txForm->addRow("Block count   :", nEdit);
    txForm->addRow(autoChk);
    v->addWidget(txBox);

    // ---- Manual ops ----
    auto* manBox  = new QGroupBox("Manual operations");
    auto* manForm = new QFormLayout(manBox);
    auto* retxBlkSpin = new QSpinBox;
    retxBlkSpin->setRange(0, 4095);
    retxBlkSpin->setValue(1);
    auto* tidOvEdit = new QLineEdit;
    tidOvEdit->setPlaceholderText("auto (use current tid)");
    manForm->addRow("RETX block    :", retxBlkSpin);
    manForm->addRow("TID override  :", tidOvEdit);
    v->addWidget(manBox);

    // ---- Buttons ----
    auto* btnRow      = new QHBoxLayout;
    auto* sendCmdBtn  = new QPushButton("Send CMD");
    auto* sendRetxBtn = new QPushButton("Send RETX");
    auto* sendDoneBtn = new QPushButton("Send COMPLETE");
    auto* abortBtn    = new QPushButton("Abort");
    auto* resetBtn    = new QPushButton("Force Reset");
    btnRow->addWidget(sendCmdBtn);
    btnRow->addWidget(sendRetxBtn);
    btnRow->addWidget(sendDoneBtn);
    btnRow->addWidget(abortBtn);
    btnRow->addWidget(resetBtn);
    v->addLayout(btnRow);

    // ---- Automated Tests ----
    auto* testBox     = new QGroupBox("Automated Tests");
    auto* testLayout  = new QVBoxLayout(testBox);
    auto* testRow1    = new QHBoxLayout;
    auto* btnHappy    = new QPushButton("Happy path");
    auto* btnWrong    = new QPushButton("Wrong-TID COMPLETE");
    auto* btnRetxOOR  = new QPushButton("RETX out-of-range");
    testRow1->addWidget(btnHappy);
    testRow1->addWidget(btnWrong);
    testRow1->addWidget(btnRetxOOR);
    testLayout->addLayout(testRow1);

    auto* testRow2   = new QHBoxLayout;
    auto* btnTimeout = new QPushButton("Timeout FPGA");
    auto* btnDupComp = new QPushButton("Duplicate COMPLETE");
    auto* btnCancel  = new QPushButton("Cancel test");
    testRow2->addWidget(btnTimeout);
    testRow2->addWidget(btnDupComp);
    testRow2->addWidget(btnCancel);
    testLayout->addLayout(testRow2);

    auto* testStatusLabel = new QLabel("no test running");
    testLayout->addWidget(testStatusLabel);
    v->addWidget(testBox);

    // ---- Status / Log ----
    auto* statusLabel = new QLabel("idle");
    v->addWidget(statusLabel);

    auto* logChk = new QCheckBox("Log enabled");
    logChk->setChecked(Logger::instance().enabled());
    v->addWidget(logChk);

    auto* logView = new QPlainTextEdit;
    logView->setReadOnly(true);
    logView->setMaximumBlockCount(5000);
    v->addWidget(logView, 1);

    w.setCentralWidget(central);
    w.resize(950, 850);
    w.show();

    // ---------------- Связка ----------------
    RudpHost    host;
    TestRunner  runner(&host, &w);

    QObject::connect(&Logger::instance(), &Logger::lineWritten,
                     logView, &QPlainTextEdit::appendPlainText);

    QObject::connect(logChk, &QCheckBox::toggled,
                     [](bool on){ Logger::instance().setEnabled(on); });

    QObject::connect(autoChk, &QCheckBox::toggled,
                     [&](bool on){
                         host.setAutoComplete(on);
                         LOG_INFO("auto COMPLETE = %s", on ? "on" : "off");
                     });

    // ---- Статус ----
    auto updateStatus = [&]{
        if (host.isActive()) {
            statusLabel->setText(
                QString("active: tid=0x%1, blocks=%2/%3%4")
                    .arg(host.currentTid(), 4, 16, QChar('0'))
                    .arg(host.blocksReceived())
                    .arg(host.blocksTotal())
                    .arg(host.allBlocksArrived()
                         ? "  [all received, waiting manual COMPLETE]"
                         : ""));
        } else {
            statusLabel->setText("idle");
        }
    };

    QObject::connect(&host, &RudpHost::cmdAckReceived,
                     [updateStatus](quint16){ updateStatus(); });
    QObject::connect(&host, &RudpHost::resultReceived,
                     [updateStatus](quint16, quint16, const QByteArray&){ updateStatus(); });
    QObject::connect(&host, &RudpHost::allBlocksReceived,
                     [updateStatus](quint16){ updateStatus(); });
    QObject::connect(&host, &RudpHost::transactionFinished,
                     [updateStatus](quint16){ updateStatus(); });
    QObject::connect(&host, &RudpHost::transactionAborted,
                     [updateStatus](quint16, const QString&){ updateStatus(); });

    // ---- Однократная инициализация сети ----
    bool configured = false;
    auto ensureConfigured = [&]() -> bool {
        if (configured) return true;
        configured = host.configure(ipEdit->text(),
                                    quint16(fpgaEdit->text().toUInt()),
                                    quint16(pcEdit->text().toUInt()));
        return configured;
    };

    // ---- Send CMD ----
    QObject::connect(sendCmdBtn, &QPushButton::clicked, [&]{
        if (!ensureConfigured()) return;
        const quint16 tid = quint16(tidEdit->text().toUShort(nullptr, 16));
        const quint16 n   = quint16(nEdit->text().toUShort());
        host.startTransaction(tid, n);
        updateStatus();
    });

    // ---- Send RETX ----
    QObject::connect(sendRetxBtn, &QPushButton::clicked, [&]{
        if (!ensureConfigured()) return;
        const quint16 blk = quint16(retxBlkSpin->value());
        const QString ovr = tidOvEdit->text().trimmed();
        if (!ovr.isEmpty()) {
            host.sendRetxCustom(quint16(ovr.toUShort(nullptr, 16)), blk);
        } else {
            if (!host.isActive()) {
                LOG_ERR("no active transaction; fill TID override");
                return;
            }
            host.requestRetx(blk);
        }
    });

    // ---- Send COMPLETE ----
    QObject::connect(sendDoneBtn, &QPushButton::clicked, [&]{
        if (!ensureConfigured()) return;
        const QString ovr = tidOvEdit->text().trimmed();
        if (!ovr.isEmpty()) {
            host.sendCompleteCustom(quint16(ovr.toUShort(nullptr, 16)));
        } else {
            host.finishTransaction(true);
        }
        updateStatus();
    });

    QObject::connect(abortBtn, &QPushButton::clicked, [&]{
        host.abortTransaction(); updateStatus();
    });
    QObject::connect(resetBtn, &QPushButton::clicked, [&]{
        host.forceReset(); updateStatus();
    });

    // ---- Automated Tests ----
    QObject::connect(btnHappy,   &QPushButton::clicked, [&]{
        if (!ensureConfigured()) return;
        runner.runHappyPath();
    });
    QObject::connect(btnWrong,   &QPushButton::clicked, [&]{
        if (!ensureConfigured()) return;
        runner.runWrongTidComplete();
    });
    QObject::connect(btnRetxOOR, &QPushButton::clicked, [&]{
        if (!ensureConfigured()) return;
        runner.runRetxOutOfRange();
    });
    QObject::connect(btnTimeout, &QPushButton::clicked, [&]{
        if (!ensureConfigured()) return;
        runner.runTimeoutFpga();
    });
    QObject::connect(btnDupComp, &QPushButton::clicked, [&]{
        if (!ensureConfigured()) return;
        runner.runDuplicateComplete();
    });
    QObject::connect(btnCancel, &QPushButton::clicked, &runner, &TestRunner::cancel);

    // ---- Обновление статуса тестов ----
    QObject::connect(&runner, &TestRunner::testStarted,
        [testStatusLabel](const QString& name){
            testStatusLabel->setText("RUNNING: " + name);
        });
    QObject::connect(&runner, &TestRunner::testFinished,
        [testStatusLabel](const QString& name, bool pass, const QString& detail){
            testStatusLabel->setText(
                QString("%1: %2%3").arg(name, pass ? "PASS" : "FAIL")
                    .arg(detail.isEmpty() ? "" : " (" + detail + ")"));
        });

    // ---- Блокировка кнопок на время теста ----
    auto setTestButtonsEnabled = [=](bool on){
        btnHappy  ->setEnabled(on);
        btnWrong  ->setEnabled(on);
        btnRetxOOR->setEnabled(on);
        btnTimeout->setEnabled(on);
        btnDupComp->setEnabled(on);

        // Ручные кнопки тоже блокируем, чтобы не мешать скрипту.
        sendCmdBtn ->setEnabled(on);
        sendRetxBtn->setEnabled(on);
        sendDoneBtn->setEnabled(on);
        abortBtn   ->setEnabled(on);
        resetBtn   ->setEnabled(on);
    };
    QObject::connect(&runner, &TestRunner::testStarted,
                     [setTestButtonsEnabled](const QString&){ setTestButtonsEnabled(false); });
    QObject::connect(&runner, &TestRunner::testFinished,
                     [setTestButtonsEnabled](const QString&, bool, const QString&){ setTestButtonsEnabled(true); });

    FunctionalTest funcTest(&host, &w);

    // ---- Functional Test panel ----
    auto* funcBox    = new QGroupBox("Functional Test (data integrity)");
    auto* funcLayout = new QVBoxLayout(funcBox);

    auto* funcRow    = new QHBoxLayout;
    auto* btnFuncSuite = new QPushButton("Run suite (1,2,4,16,64,256,1024)");
//    auto* btnFunc4096  = new QPushButton("Single 4096 blocks");
    auto* btnFunc4096 = new QPushButton("Single 16 blocks");
    auto* btnFunc1     = new QPushButton("Single 1 block");
    funcRow->addWidget(btnFuncSuite);
    funcRow->addWidget(btnFunc1);
    funcRow->addWidget(btnFunc4096);
    funcLayout->addLayout(funcRow);

    auto* funcStatusLabel = new QLabel("no functional test running");
    funcLayout->addWidget(funcStatusLabel);

    auto* funcProgress = new QPlainTextEdit;
    funcProgress->setReadOnly(true);
    funcProgress->setMaximumBlockCount(200);
    funcProgress->setFixedHeight(80);
    funcLayout->addWidget(funcProgress);

    v->addWidget(funcBox);

    // Wiring
    QObject::connect(&funcTest, &FunctionalTest::progress,
                     funcProgress, &QPlainTextEdit::appendPlainText);
    QObject::connect(&funcTest, &FunctionalTest::testFinished,
        [funcStatusLabel](const QString& summary, bool passed){
            funcStatusLabel->setText(
                QString("%1: %2").arg(passed ? "PASS" : "FAIL", summary));
        });

    QObject::connect(btnFuncSuite, &QPushButton::clicked, [&]{
        if (!ensureConfigured()) return;
        funcTest.runSuite();
    });
    QObject::connect(btnFunc1, &QPushButton::clicked, [&]{
        if (!ensureConfigured()) return;
        funcTest.runSingle(1);
    });
//    QObject::connect(btnFunc4096, &QPushButton::clicked, [&]{
//        if (!ensureConfigured()) return;
//        funcTest.runSingle(4096);
//    });
    QObject::connect(btnFunc4096, &QPushButton::clicked, [&]{
        if (!ensureConfigured()) return;
        funcTest.runSingle(16);
    });
    return app.exec();
}
