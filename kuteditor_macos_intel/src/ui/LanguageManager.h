#ifndef LANGUAGEMANAGER_H
#define LANGUAGEMANAGER_H

#include <QObject>
#include <QString>
#include <QTranslator>
#include <QQmlApplicationEngine>

class LanguageManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentLanguage READ currentLanguage WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString effectiveLanguage READ effectiveLanguage NOTIFY languageChanged)

public:
    static LanguageManager *instance();

    void init(QQmlApplicationEngine *engine);

    QString currentLanguage() const { return m_currentLanguage; }
    QString effectiveLanguage() const;

    Q_INVOKABLE void setLanguage(const QString &lang);

signals:
    void languageChanged();

private:
    explicit LanguageManager(QObject *parent = nullptr);
    ~LanguageManager() override = default;

    void applyLanguage();

    QString m_currentLanguage = QStringLiteral("auto");
    QTranslator m_translator;
    bool m_translatorInstalled = false;
    QQmlApplicationEngine *m_engine = nullptr;
};

#endif // LANGUAGEMANAGER_H
