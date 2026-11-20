#include "SparkleUpdater.h"
#include <QDebug>

#import <Sparkle/Sparkle.h>

class SparkleUpdater::Private
{
public:
    SPUStandardUpdaterController *updaterController;
};

SparkleUpdater::SparkleUpdater(QObject *parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[SparkleUpdater] Inicializando Sparkle 2...";
    @try {
        d->updaterController = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES updaterDelegate:nil userDriverDelegate:nil];
    } @catch (NSException *exception) {
        qWarning() << "[SparkleUpdater] Error al inicializar Sparkle:" 
                   << QString::fromNSString(exception.reason);
    }
}

SparkleUpdater::~SparkleUpdater()
{
    delete d;
}

void SparkleUpdater::checkForUpdates()
{
    qDebug() << "[SparkleUpdater] Buscando actualizaciones programáticamente...";
    if (d->updaterController) {
        [d->updaterController checkForUpdates:nil];
    } else {
        qWarning() << "[SparkleUpdater] El controlador de Sparkle no está inicializado.";
    }
}
