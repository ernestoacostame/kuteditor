#include "MacPermissions.h"
#import <AVFoundation/AVFoundation.h>
#include <QDebug>

namespace MacPermissions {

void requestMicrophoneAccess() {
    if (@available(macOS 10.14, *)) {
        AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
        if (status == AVAuthorizationStatusNotDetermined) {
            qDebug() << "[MacPermissions] Solicitando permiso de microfono a macOS...";
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
                if (granted) {
                    qDebug() << "[MacPermissions] Permiso de microfono CONCEDIDO por el usuario.";
                } else {
                    qWarning() << "[MacPermissions] Permiso de microfono DENEGADO por el usuario.";
                }
            }];
        } else if (status == AVAuthorizationStatusAuthorized) {
            qDebug() << "[MacPermissions] Permiso de microfono ya autorizado.";
        } else {
            qWarning() << "[MacPermissions] Permiso de microfono DENEGADO o RESTRINGIDO en Ajustes del Sistema.";
        }
    }
}

bool hasMicrophoneAccess() {
    if (@available(macOS 10.14, *)) {
        AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
        return status == AVAuthorizationStatusAuthorized;
    }
    return true;
}

} // namespace MacPermissions
