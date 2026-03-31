#include "HalStorage.h"

#include <SDCardManager.h>

#define SDCard SDCardManager::getInstance()

HalStorage HalStorage::instance;

HalStorage::HalStorage() { ioMutex = xSemaphoreCreateRecursiveMutex(); }

void HalStorage::lock() const {
  if (ioMutex) {
    xSemaphoreTakeRecursive(ioMutex, portMAX_DELAY);
  }
}

void HalStorage::unlock() const {
  if (ioMutex) {
    xSemaphoreGiveRecursive(ioMutex);
  }
}

bool HalStorage::begin() {
  LockGuard guard(*this);
  return SDCard.begin();
}

bool HalStorage::ready() const {
  LockGuard guard(*this);
  return SDCard.ready();
}

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  LockGuard guard(*this);
  return SDCard.listFiles(path, maxFiles);
}

String HalStorage::readFile(const char* path) {
  LockGuard guard(*this);
  return SDCard.readFile(path);
}

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  LockGuard guard(*this);
  return SDCard.readFileToStream(path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  LockGuard guard(*this);
  return SDCard.readFileToBuffer(path, buffer, bufferSize, maxBytes);
}

bool HalStorage::writeFile(const char* path, const String& content) {
  LockGuard guard(*this);
  return SDCard.writeFile(path, content);
}

bool HalStorage::ensureDirectoryExists(const char* path) {
  LockGuard guard(*this);
  return SDCard.ensureDirectoryExists(path);
}

FsFile HalStorage::open(const char* path, const oflag_t oflag) {
  LockGuard guard(*this);
  return SDCard.open(path, oflag);
}

bool HalStorage::mkdir(const char* path, const bool pFlag) {
  LockGuard guard(*this);
  return SDCard.mkdir(path, pFlag);
}

bool HalStorage::exists(const char* path) {
  LockGuard guard(*this);
  return SDCard.exists(path);
}

bool HalStorage::remove(const char* path) {
  LockGuard guard(*this);
  return SDCard.remove(path);
}

bool HalStorage::rmdir(const char* path) {
  LockGuard guard(*this);
  return SDCard.rmdir(path);
}

bool HalStorage::rename(const char* path, const char* newPath) {
  LockGuard guard(*this);
  return SDCard.rename(path, newPath);
}

bool HalStorage::openFileForRead(const char* moduleName, const char* path, FsFile& file) {
  LockGuard guard(*this);
  return SDCard.openFileForRead(moduleName, path, file);
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, FsFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, FsFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, FsFile& file) {
  LockGuard guard(*this);
  return SDCard.openFileForWrite(moduleName, path, file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, FsFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, FsFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) {
  LockGuard guard(*this);
  return SDCard.removeDir(path);
}