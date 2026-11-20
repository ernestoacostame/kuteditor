#pragma once

#include <cstddef>
#include <cstdint>
#include <QString>

/**
 * MmapAudioBuffer: Buffer de audio respaldado por un archivo temporal
 * mapeado en memoria.
 *
 * Para audio largo (>= MMAP_THRESHOLD_FLOATS), este buffer usa mmap (POSIX)
 * o CreateFileMapping (Windows) para almacenar los samples en un archivo
 * temporal. El SO gestiona qué páginas están en RAM física vs. swapped,
 * reduciendo drásticamente la presión de memoria del proceso.
 *
 * Uso:
 *   MmapAudioBuffer buf;
 *   if (buf.create(totalFloats)) {
 *     float *ptr = buf.data();
 *     // Escribir samples...
 *   }
 *   // O desde un QVector existente:
 *   buf.fromVector(existingSamples); // mueve los datos al mmap
 *
 * La interfaz data()/constData()/size() es compatible con QVector<float>
 * para facilitar la integración con el código existente.
 *
 * RAII: el destructor desmapea la memoria y borra el archivo temporal.
 *
 * Threshold: sources con < MMAP_THRESHOLD_FLOATS deben seguir usando
 * QVector<float> en heap (la decisión la toma el caller, no esta clase).
 */
class MmapAudioBuffer {
public:
    /// Umbral: ~30 segundos estéreo @ 48kHz = 30 * 48000 * 2 = 2.88M floats.
    /// Sources más pequeños usan heap (QVector); más grandes usan mmap.
    static constexpr int64_t MMAP_THRESHOLD_FLOATS = 30LL * 48000 * 2;

    MmapAudioBuffer();
    ~MmapAudioBuffer();

    // No copiable, movible.
    MmapAudioBuffer(const MmapAudioBuffer &) = delete;
    MmapAudioBuffer &operator=(const MmapAudioBuffer &) = delete;
    MmapAudioBuffer(MmapAudioBuffer &&other) noexcept;
    MmapAudioBuffer &operator=(MmapAudioBuffer &&other) noexcept;

    /**
     * Crea el buffer con espacio para totalFloats floats.
     * El archivo temporal se crea en el directorio temporal del sistema.
     * Los contenidos se inicializan a cero.
     * Devuelve true si se pudo crear correctamente.
     */
    bool create(int64_t totalFloats);

    /**
     * Crea el buffer desde un QVector existente, copiando los datos al mmap.
     * Devuelve true si tuvo éxito. El QVector original puede liberarse después.
     */
    bool fromVector(const float *src, int64_t count);

    /// Puntero de lectura/escritura al buffer mapeado.
    float *data() { return m_data; }
    const float *constData() const { return m_data; }

    /// Número de floats en el buffer.
    int64_t size() const { return m_size; }

    /// ¿El buffer está activo (mapeado)?
    bool isValid() const { return m_data != nullptr; }

    /// Informa al SO que el acceso será secuencial (optimiza prefetch).
    void adviseSequential();

    /// Informa al SO que el acceso será aleatorio.
    void adviseRandom();

    /// Libera el mapping y borra el archivo temporal.
    void release();

private:
    float *m_data = nullptr;
    int64_t m_size = 0;    // en floats
    int64_t m_bytes = 0;   // en bytes
    QString m_filePath;

#ifdef _WIN32
    void *m_fileHandle = nullptr;   // HANDLE
    void *m_mappingHandle = nullptr; // HANDLE
#else
    int m_fd = -1;
#endif
};
