#include "logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QDateTime>
#include <QMutexLocker>

// -----------------------------------------------------------------------------
//  Singleton
// -----------------------------------------------------------------------------
Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::Logger(QObject* parent) : QObject(parent) {
    configure();
}

Logger::~Logger() {
    QMutexLocker lock(&mutex_);
    if (file_.isOpen()) {
        stream_.flush();
        file_.close();
    }
}

// -----------------------------------------------------------------------------
//  Конфигурация
// -----------------------------------------------------------------------------
QString Logger::iniPath() const {
    const QString exe = QCoreApplication::applicationFilePath();
    return QFileInfo(exe).absolutePath() + "/" +
           QFileInfo(exe).completeBaseName() + ".ini";
}

QString Logger::logPath() const {
    const QString exe  = QCoreApplication::applicationFilePath();
    const QString dir  = QFileInfo(exe).absolutePath();
    const QString base = QFileInfo(exe).completeBaseName();
    return QString("%1/%2_%3.log")
           .arg(dir, base, QDate::currentDate().toString("yyyy-MM-dd"));
}

void Logger::configure() {
    const QString ini = iniPath();

    // Если INI нет — создаём дефолтный (лог выключен).
    if (!QFile::exists(ini)) {
        QSettings s(ini, QSettings::IniFormat);
        s.beginGroup("Logging");
        s.setValue("Enabled", false);
        s.setValue("Level",   "INFO");
        s.endGroup();
        s.sync();
    }

    QSettings s(ini, QSettings::IniFormat);
    s.beginGroup("Logging");
    enabled_ = s.value("Enabled", false).toBool();

    const QString lvl = s.value("Level", "INFO").toString().toUpper();
    if      (lvl == "OFF")   level_ = Off;
    else if (lvl == "ERROR") level_ = Error;
    else                     level_ = Info;
    s.endGroup();

    QMutexLocker lock(&mutex_);
    if (file_.isOpen()) {
        stream_.flush();
        file_.close();
    }
    curDate_ = QDate();
}

void Logger::setEnabled(bool on) {
    QMutexLocker lock(&mutex_);
    enabled_ = on;
}

void Logger::setLevel(Level lvl) {
    QMutexLocker lock(&mutex_);
    level_ = lvl;
}

bool Logger::enabled() const {
    QMutexLocker lock(&mutex_);
    return enabled_;
}

Logger::Level Logger::level() const {
    QMutexLocker lock(&mutex_);
    return level_;
}

// -----------------------------------------------------------------------------
//  Запись
// -----------------------------------------------------------------------------
void Logger::write(const char* lvlTag, const QString& msg) {
    QMutexLocker lock(&mutex_);
    if (!enabled_) return;

    // Переоткрываем файл при смене суток.
    const QDate today = QDate::currentDate();
    if (!file_.isOpen() || today != curDate_) {
        if (file_.isOpen()) {
            stream_.flush();
            file_.close();
        }
        file_.setFileName(logPath());
        if (!file_.open(QIODevice::Append | QIODevice::Text))
            return;
        stream_.setDevice(&file_);
        curDate_ = today;
    }

    const QString line = QString("[%1] [%2] %3")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"),
             QString::fromLatin1(lvlTag),
             msg);

    stream_ << line << '\n';
    stream_.flush();

    emit lineWritten(line);
}

void Logger::info(const QString& msg) {
    if (level_ < Info) return;
    write("INFO", msg);
}

void Logger::error(const QString& msg) {
    if (level_ < Error) return;
    write("ERROR", msg);
}
