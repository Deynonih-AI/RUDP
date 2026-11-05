#pragma once

#include <QObject>
#include <QString>
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <QDate>

// =============================================================================
//  Logger
//  Запись лога в файл с управлением через INI.
//
//  INI-файл: рядом с exe, имя совпадает с exe, расширение .ini
//  Пример: rudp_host1.ini
//
//  [Logging]
//  Enabled=1          ; 1 — включён, 0 — выключен
//  Level=INFO         ; OFF / ERROR / INFO
//
//  Использование:
//      LOG_INFO("RX CMD_ACK tid=%04X", tid);
//      LOG_ERR ("timeout waiting for RESULT");
// =============================================================================
class Logger : public QObject {
    Q_OBJECT
public:
    enum Level { Off = 0, Error = 1, Info = 2 };
    Q_ENUM(Level)

    static Logger& instance();

    void configure();                    // читает INI рядом с exe
    void setEnabled(bool on);
    void setLevel(Level lvl);

    bool  enabled() const;
    Level level()   const;

    void info (const QString& msg);
    void error(const QString& msg);

signals:
    // Каждая записанная строка уходит в GUI (живой лог).
    void lineWritten(const QString& line);

private:
    explicit Logger(QObject* parent = nullptr);
    ~Logger() override;
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    void    write(const char* lvlTag, const QString& msg);
    QString iniPath() const;
    QString logPath() const;

    mutable QMutex mutex_;
    bool    enabled_ = false;
    Level   level_   = Info;
    QFile   file_;
    QTextStream stream_;
    QDate   curDate_;
};

// -----------------------------------------------------------------------------
//  Удобные макросы.
// -----------------------------------------------------------------------------
#define LOG_INFO(...)  Logger::instance().info (QString::asprintf(__VA_ARGS__))
#define LOG_ERR(...)   Logger::instance().error(QString::asprintf(__VA_ARGS__))
