#pragma once

#ifdef HAVE_TAGLIB

#include <QString>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

#include <mpegfile.h>
#include <id3v2tag.h>
#include <chapterframe.h>
#include <tableofcontentsframe.h>
#include <attachedpictureframe.h>
#include <textidentificationframe.h>
#include <urllinkframe.h>
#include <tbytevector.h>
#include <tbytevectorlist.h>

#include "ui/ChapterModel.h"

/**
 * ChapterTagger: post-procesamiento de MP3 con TagLib para escribir
 * frames ID3v2 de capítulos completos.
 *
 * FFmpeg solo puede escribir CHAP frames básicos (título + timestamps)
 * via FFMETADATA. No soporta sub-frames embebidos como:
 *   - APIC (imagen de portada por capítulo)
 *   - WXXX (URL asociada al capítulo)
 *
 * Esta clase resuelve esa limitación: después de que FFmpeg genera el MP3,
 * abrimos el archivo con TagLib y escribimos la estructura completa:
 *
 *   CTOC (Table of Contents, top-level, ordered)
 *    └─ childElements: ["ch0", "ch1", ...]
 *
 *   CHAP "ch0" (startMs, endMs)
 *    ├─ TIT2: título del capítulo
 *    ├─ APIC: imagen embebida (PNG/JPEG, leída del disco)
 *    └─ WXXX: URL del capítulo
 *
 * Compatible con Apple Podcasts, Pocket Casts, Overcast, y otros
 * reproductores que soportan ID3v2.3 CHAP frames.
 *
 * NOTA DE COMPATIBILIDAD:
 *   - Element IDs usan "ch0", "ch1", etc. (mismo patrón que FFmpeg)
 *   - TIT2 sub-frames usan Latin1 cuando es posible (máxima compat ID3v2.3)
 *   - CTOC se inserta ANTES de los CHAP para parsers secuenciales
 *   - Se guarda siempre como ID3v2.3 (mejor soporte en podcatchers)
 */
class ChapterTagger
{
public:
    /**
     * Escribe CHAP frames con artwork + URL embebidos en un MP3 existente.
     * Debe llamarse DESPUÉS de que FFmpeg haya generado el archivo.
     *
     * @param mp3Path  Ruta al archivo MP3 ya exportado.
     * @param model    ChapterModel con los capítulos a escribir.
     * @return true si se escribieron correctamente.
     */
    static bool writeChapters(const QString &mp3Path,
                              const ChapterModel *model)
    {
        if (!model || model->count() == 0) return true;  // Nada que hacer

        const QByteArray pathUtf8 = mp3Path.toUtf8();
        TagLib::MPEG::File file(pathUtf8.constData());

        if (!file.isOpen() || file.readOnly()) {
            qWarning() << "[ChapterTagger] No se pudo abrir MP3 para escritura:"
                        << mp3Path;
            return false;
        }

        TagLib::ID3v2::Tag *tag = file.ID3v2Tag(true);
        if (!tag) {
            qWarning() << "[ChapterTagger] No se pudo crear/obtener tag ID3v2";
            return false;
        }

        // ── 1) Limpiar CHAP y CTOC frames existentes ────────────────────
        //    FFmpeg puede haber escrito CHAP frames básicos via FFMETADATA;
        //    los reemplazamos con versiones completas que incluyen artwork.
        removeFramesByType(tag, "CHAP");
        removeFramesByType(tag, "CTOC");

        const auto &chapters = model->chapters();
        const int count = chapters.size();

        // ── 2) Crear CTOC primero (antes de los CHAP) ───────────────────
        //    Algunos parsers secuenciales esperan encontrar CTOC antes
        //    de los CHAP frames. Usamos "ch0", "ch1", etc. como FFmpeg.
        TagLib::ByteVectorList childElements;
        for (int i = 0; i < count; ++i) {
            const QByteArray elemId = QStringLiteral("ch%1").arg(i).toLatin1();
            childElements.append(TagLib::ByteVector(elemId.constData(),
                                                     elemId.size()));
        }

        auto *ctoc = new TagLib::ID3v2::TableOfContentsFrame(
            TagLib::ByteVector("toc", 3),
            childElements
        );
        ctoc->setIsTopLevel(true);
        ctoc->setIsOrdered(true);
        tag->addFrame(ctoc);

        // ── 3) Crear CHAP frames con sub-frames embebidos ───────────────
        for (int i = 0; i < count; ++i) {
            const ChapterModel::Chapter &ch = chapters[i];
            const qint64 endMs = (i + 1 < count)
                ? chapters[i + 1].startMs
                : model->episodeDurationMs();

            // Element ID: "ch0", "ch1", etc. (coincide con FFmpeg)
            const QByteArray elemId = QStringLiteral("ch%1").arg(i).toLatin1();
            const TagLib::ByteVector elementID(elemId.constData(), elemId.size());

            // Offsets = 0xFFFFFFFF indica "usar timestamps, no byte offsets"
            auto *chap = new TagLib::ID3v2::ChapterFrame(
                elementID,
                static_cast<unsigned int>(ch.startMs),
                static_cast<unsigned int>(endMs),
                0xFFFFFFFF,
                0xFFFFFFFF
            );

            // ── Sub-frame TIT2: título del capítulo ─────────────────
            //    Usar Latin1 si el título solo tiene chars ASCII/Latin1,
            //    UTF-16 solo si hay caracteres no-Latin1. Esto maximiza
            //    la compatibilidad con reproductores de podcast que
            //    pueden tener problemas con UTF-8/16 en ID3v2.3.
            {
                const bool needsUnicode = requiresUnicode(ch.title);
                auto *tit2 = new TagLib::ID3v2::TextIdentificationFrame(
                    "TIT2",
                    needsUnicode ? TagLib::String::UTF16 : TagLib::String::Latin1);
                if (needsUnicode) {
                    tit2->setText(TagLib::String(ch.title.toUtf8().constData(),
                                                 TagLib::String::UTF8));
                } else {
                    tit2->setText(TagLib::String(ch.title.toLatin1().constData(),
                                                 TagLib::String::Latin1));
                }
                chap->addEmbeddedFrame(tit2);  // CHAP toma ownership
            }

            // ── Sub-frame APIC: imagen del capítulo ─────────────────
            if (!ch.artworkPath.isEmpty()) {
                embedArtwork(chap, ch.artworkPath);
            }

            // ── Sub-frame WXXX: URL del capítulo ────────────────────
            if (!ch.link.isEmpty()) {
                auto *wxxx = new TagLib::ID3v2::UserUrlLinkFrame(
                    TagLib::String::Latin1);
                wxxx->setDescription(TagLib::String("chapter url",
                                                     TagLib::String::Latin1));
                wxxx->setUrl(TagLib::String(
                    ch.link.toString().toLatin1().constData(),
                    TagLib::String::Latin1));
                chap->addEmbeddedFrame(wxxx);
            }

            tag->addFrame(chap);  // Tag toma ownership
        }

        // ── 4) Guardar como ID3v2.3 ─────────────────────────────────────
        //    v2.3 tiene mejor soporte en reproductores de podcast.
        //    StripNone = no borrar ID3v1 si existe.
        //    DoNotDuplicate = no intentar sincronizar tags ID3v1↔ID3v2.
        const bool ok = file.save(
            TagLib::MPEG::File::ID3v2,
            TagLib::File::StripNone,
            TagLib::ID3v2::v3,
            TagLib::File::DoNotDuplicate
        );

        if (ok) {
            qDebug() << "[ChapterTagger] Escritos" << count
                      << "capítulos con artwork/URL en:" << mp3Path;
        } else {
            qWarning() << "[ChapterTagger] Error al guardar tags ID3v2 en:"
                        << mp3Path;
        }

        return ok;
    }

private:
    /// Elimina todos los frames de un tipo dado (e.g. "CHAP", "CTOC").
    static void removeFramesByType(TagLib::ID3v2::Tag *tag,
                                   const char *frameId)
    {
        const TagLib::ByteVector id(frameId);
        const auto &frames = tag->frameList(id);
        // Copiar la lista porque removeFrame invalida iteradores.
        QVector<TagLib::ID3v2::Frame*> toRemove;
        for (auto *f : frames) toRemove.append(f);
        for (auto *f : toRemove) tag->removeFrame(f, true);
    }

    /// Comprueba si un QString contiene caracteres fuera de Latin1 (>255).
    static bool requiresUnicode(const QString &s)
    {
        for (const QChar &c : s) {
            if (c.unicode() > 255) return true;
        }
        return false;
    }

    /// Lee una imagen del disco y la embebe como sub-frame APIC en un CHAP.
    static void embedArtwork(TagLib::ID3v2::ChapterFrame *chap,
                             const QString &artworkPath)
    {
        // Resolver ruta (puede venir con "file://" o sin él)
        QString path = artworkPath;
        if (path.startsWith("file://"))
            path = path.mid(7);

        QFile imgFile(path);
        if (!imgFile.exists() || !imgFile.open(QIODevice::ReadOnly)) {
            qDebug() << "[ChapterTagger] Imagen no encontrada, omitiendo:"
                      << path;
            return;
        }

        const QByteArray imgData = imgFile.readAll();
        imgFile.close();

        if (imgData.isEmpty()) {
            qDebug() << "[ChapterTagger] Imagen vacía, omitiendo:" << path;
            return;
        }

        // Detectar MIME type por extensión
        const QString ext = QFileInfo(path).suffix().toLower();
        TagLib::String mimeType;
        if (ext == "png")
            mimeType = "image/png";
        else if (ext == "jpg" || ext == "jpeg")
            mimeType = "image/jpeg";
        else if (ext == "gif")
            mimeType = "image/gif";
        else if (ext == "webp")
            mimeType = "image/webp";
        else
            mimeType = "image/jpeg";  // Fallback seguro

        auto *apic = new TagLib::ID3v2::AttachedPictureFrame();
        apic->setMimeType(mimeType);
        apic->setType(TagLib::ID3v2::AttachedPictureFrame::Other);
        apic->setDescription(TagLib::String("Chapter artwork",
                                             TagLib::String::Latin1));
        apic->setTextEncoding(TagLib::String::Latin1);
        apic->setPicture(TagLib::ByteVector(imgData.constData(),
                                            imgData.size()));

        chap->addEmbeddedFrame(apic);

        qDebug() << "[ChapterTagger] Imagen embebida:" << path
                  << "(" << imgData.size() << "bytes," << ext << ")";
    }
};

#endif // HAVE_TAGLIB
