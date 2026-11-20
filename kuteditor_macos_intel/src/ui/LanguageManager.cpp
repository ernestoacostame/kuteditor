#include "LanguageManager.h"
#include <QCoreApplication>
#include <QSettings>
#include <QLocale>
#include <QDebug>

LanguageManager *LanguageManager::instance() {
    static LanguageManager s_instance;
    return &s_instance;
}

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent) {
}

void LanguageManager::init(QQmlApplicationEngine *engine) {
    m_engine = engine;
    QSettings settings(QStringLiteral("KutStudio"), QStringLiteral("KutEditor"));
    m_currentLanguage = settings.value(QStringLiteral("app/language"), QStringLiteral("auto")).toString();
    if (m_currentLanguage != QLatin1String("auto") &&
        m_currentLanguage != QLatin1String("es") &&
        m_currentLanguage != QLatin1String("en")) {
        m_currentLanguage = QStringLiteral("auto");
    }
    applyLanguage();
}

QString LanguageManager::effectiveLanguage() const {
    if (m_currentLanguage == QLatin1String("es")) return QStringLiteral("es");
    if (m_currentLanguage == QLatin1String("en")) return QStringLiteral("en");
    // "auto" -> detectar del idioma del sistema
    const QString sys = QLocale::system().name().toLower();
    if (sys.startsWith(QLatin1String("es"))) {
        return QStringLiteral("es");
    }
    return QStringLiteral("en");
}

void LanguageManager::setLanguage(const QString &lang) {
    if (m_currentLanguage == lang) return;
    m_currentLanguage = lang;
    QSettings settings(QStringLiteral("KutStudio"), QStringLiteral("KutEditor"));
    settings.setValue(QStringLiteral("app/language"), m_currentLanguage);
    applyLanguage();
    emit languageChanged();
}

void LanguageManager::applyLanguage() {
    const QString eff = effectiveLanguage();
    if (eff == QLatin1String("en")) {
        if (!m_translatorInstalled) {
            if (m_translator.load(QStringLiteral(":/i18n/kuteditor_en.qm"))) {
                QCoreApplication::installTranslator(&m_translator);
                m_translatorInstalled = true;
                qDebug() << "[LanguageManager] Traductor de inglés instalado (:/i18n/kuteditor_en.qm)";
            } else {
                qWarning() << "[LanguageManager] Error cargando :/i18n/kuteditor_en.qm";
            }
        }
    } else {
        if (m_translatorInstalled) {
            QCoreApplication::removeTranslator(&m_translator);
            m_translatorInstalled = false;
            qDebug() << "[LanguageManager] Traductor desinstalado (idioma español nativo)";
        }
    }

    if (m_engine) {
        m_engine->retranslate();
    }
}
