#ifndef SPARKLEUPDATER_H
#define SPARKLEUPDATER_H

#include <QObject>

class SparkleUpdater : public QObject
{
    Q_OBJECT
public:
    explicit SparkleUpdater(QObject *parent = nullptr);
    ~SparkleUpdater();

    Q_INVOKABLE void checkForUpdates();

private:
    class Private;
    Private *d;
};

#endif // SPARKLEUPDATER_H
