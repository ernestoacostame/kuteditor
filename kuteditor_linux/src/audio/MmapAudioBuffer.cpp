#include "MmapAudioBuffer.h"

#include <QDir>
#include <QDebug>
#include <cstring>

#ifdef _WIN32
// Windows implementation
#include <windows.h>

MmapAudioBuffer::MmapAudioBuffer() = default;

MmapAudioBuffer::~MmapAudioBuffer() {
    release();
}

MmapAudioBuffer::MmapAudioBuffer(MmapAudioBuffer &&other) noexcept
    : m_data(other.m_data), m_size(other.m_size), m_bytes(other.m_bytes),
      m_filePath(std::move(other.m_filePath)),
      m_fileHandle(other.m_fileHandle),
      m_mappingHandle(other.m_mappingHandle)
{
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_bytes = 0;
    other.m_fileHandle = nullptr;
    other.m_mappingHandle = nullptr;
}

MmapAudioBuffer &MmapAudioBuffer::operator=(MmapAudioBuffer &&other) noexcept {
    if (this != &other) {
        release();
        m_data = other.m_data;
        m_size = other.m_size;
        m_bytes = other.m_bytes;
        m_filePath = std::move(other.m_filePath);
        m_fileHandle = other.m_fileHandle;
        m_mappingHandle = other.m_mappingHandle;
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_bytes = 0;
        other.m_fileHandle = nullptr;
        other.m_mappingHandle = nullptr;
    }
    return *this;
}

bool MmapAudioBuffer::create(int64_t totalFloats) {
    release();
    if (totalFloats <= 0) return false;

    m_bytes = totalFloats * sizeof(float);
    m_size = totalFloats;

    // Crear archivo temporal
    m_filePath = QDir::tempPath() + QString("/kut_mmap_%1.raw")
                     .arg(reinterpret_cast<quintptr>(this), 0, 16);

    // Crear/abrir archivo
    m_fileHandle = CreateFileW(
        reinterpret_cast<LPCWSTR>(m_filePath.utf16()),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr);

    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        qWarning() << "[MmapAudioBuffer] CreateFile failed:" << m_filePath;
        m_fileHandle = nullptr;
        return false;
    }

    // Establecer tamaño
    LARGE_INTEGER liSize;
    liSize.QuadPart = m_bytes;
    if (!SetFilePointerEx(m_fileHandle, liSize, nullptr, FILE_BEGIN) ||
        !SetEndOfFile(m_fileHandle)) {
        qWarning() << "[MmapAudioBuffer] SetFileSize failed";
        CloseHandle(m_fileHandle);
        m_fileHandle = nullptr;
        return false;
    }

    // Crear mapping
    DWORD highSize = static_cast<DWORD>(m_bytes >> 32);
    DWORD lowSize = static_cast<DWORD>(m_bytes & 0xFFFFFFFF);
    m_mappingHandle = CreateFileMappingW(
        m_fileHandle, nullptr, PAGE_READWRITE, highSize, lowSize, nullptr);

    if (!m_mappingHandle) {
        qWarning() << "[MmapAudioBuffer] CreateFileMapping failed";
        CloseHandle(m_fileHandle);
        m_fileHandle = nullptr;
        return false;
    }

    // Mapear vista
    m_data = static_cast<float*>(
        MapViewOfFile(m_mappingHandle, FILE_MAP_ALL_ACCESS, 0, 0, m_bytes));

    if (!m_data) {
        qWarning() << "[MmapAudioBuffer] MapViewOfFile failed";
        CloseHandle(m_mappingHandle);
        CloseHandle(m_fileHandle);
        m_mappingHandle = nullptr;
        m_fileHandle = nullptr;
        return false;
    }

    std::memset(m_data, 0, m_bytes);
    return true;
}

bool MmapAudioBuffer::fromVector(const float *src, int64_t count) {
    if (!create(count)) return false;
    std::memcpy(m_data, src, count * sizeof(float));
    return true;
}

void MmapAudioBuffer::adviseSequential() {
    // No portable equivalent on Windows; the OS handles prefetching.
}

void MmapAudioBuffer::adviseRandom() {
    // No portable equivalent on Windows.
}

void MmapAudioBuffer::release() {
    if (m_data) {
        UnmapViewOfFile(m_data);
        m_data = nullptr;
    }
    if (m_mappingHandle) {
        CloseHandle(m_mappingHandle);
        m_mappingHandle = nullptr;
    }
    if (m_fileHandle) {
        CloseHandle(m_fileHandle);
        m_fileHandle = nullptr;
    }
    // FILE_FLAG_DELETE_ON_CLOSE already handles file removal
    m_size = 0;
    m_bytes = 0;
    m_filePath.clear();
}

#else
// POSIX implementation (Linux, macOS)
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

MmapAudioBuffer::MmapAudioBuffer() = default;

MmapAudioBuffer::~MmapAudioBuffer() {
    release();
}

MmapAudioBuffer::MmapAudioBuffer(MmapAudioBuffer &&other) noexcept
    : m_data(other.m_data), m_size(other.m_size), m_bytes(other.m_bytes),
      m_filePath(std::move(other.m_filePath)), m_fd(other.m_fd)
{
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_bytes = 0;
    other.m_fd = -1;
}

MmapAudioBuffer &MmapAudioBuffer::operator=(MmapAudioBuffer &&other) noexcept {
    if (this != &other) {
        release();
        m_data = other.m_data;
        m_size = other.m_size;
        m_bytes = other.m_bytes;
        m_filePath = std::move(other.m_filePath);
        m_fd = other.m_fd;
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_bytes = 0;
        other.m_fd = -1;
    }
    return *this;
}

bool MmapAudioBuffer::create(int64_t totalFloats) {
    release();
    if (totalFloats <= 0) return false;

    m_bytes = totalFloats * static_cast<int64_t>(sizeof(float));
    m_size = totalFloats;

    // Crear archivo temporal
    m_filePath = QDir::tempPath() + QString("/kut_mmap_%1.raw")
                     .arg(reinterpret_cast<quintptr>(this), 0, 16);

    m_fd = ::open(m_filePath.toUtf8().constData(),
                  O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (m_fd < 0) {
        qWarning() << "[MmapAudioBuffer] open() failed:" << m_filePath;
        return false;
    }

    // Establecer tamaño del archivo
    if (::ftruncate(m_fd, m_bytes) != 0) {
        qWarning() << "[MmapAudioBuffer] ftruncate() failed";
        ::close(m_fd);
        m_fd = -1;
        ::unlink(m_filePath.toUtf8().constData());
        return false;
    }

    // Mapear en memoria
    void *ptr = ::mmap(nullptr, static_cast<size_t>(m_bytes),
                       PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
    if (ptr == MAP_FAILED) {
        qWarning() << "[MmapAudioBuffer] mmap() failed";
        ::close(m_fd);
        m_fd = -1;
        ::unlink(m_filePath.toUtf8().constData());
        return false;
    }

    m_data = static_cast<float*>(ptr);

    // Indicar al SO que el acceso será secuencial (optimiza prefetch).
#ifdef MADV_SEQUENTIAL
    ::madvise(m_data, static_cast<size_t>(m_bytes), MADV_SEQUENTIAL);
#endif

    return true;
}

bool MmapAudioBuffer::fromVector(const float *src, int64_t count) {
    if (!create(count)) return false;
    std::memcpy(m_data, src, static_cast<size_t>(count) * sizeof(float));
    return true;
}

void MmapAudioBuffer::adviseSequential() {
#ifdef MADV_SEQUENTIAL
    if (m_data && m_bytes > 0)
        ::madvise(m_data, static_cast<size_t>(m_bytes), MADV_SEQUENTIAL);
#endif
}

void MmapAudioBuffer::adviseRandom() {
#ifdef MADV_RANDOM
    if (m_data && m_bytes > 0)
        ::madvise(m_data, static_cast<size_t>(m_bytes), MADV_RANDOM);
#endif
}

void MmapAudioBuffer::release() {
    if (m_data) {
        ::munmap(m_data, static_cast<size_t>(m_bytes));
        m_data = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    if (!m_filePath.isEmpty()) {
        ::unlink(m_filePath.toUtf8().constData());
        m_filePath.clear();
    }
    m_size = 0;
    m_bytes = 0;
}

#endif // _WIN32
