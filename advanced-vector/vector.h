#pragma once

#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>

template<typename T>
class RawMemory {
 public:
  RawMemory() = default;

  explicit RawMemory(size_t capacity)
      : buffer_(Allocate(capacity)), capacity_(capacity) {
  }

  RawMemory(const RawMemory &) = delete;

  RawMemory &operator=(const RawMemory &rhs) = delete;

  RawMemory(RawMemory &&other) noexcept {
    buffer_ = std::exchange(other.buffer_, nullptr);
    capacity_ = std::exchange(other.capacity_, 0);
  }

  RawMemory &operator=(RawMemory &&rhs) noexcept {
    if (this != &rhs) {
      buffer_ = std::exchange(rhs.buffer_, nullptr);
      capacity_ = std::exchange(rhs.capacity_, 0);
    }
    return *this;
  }

  ~RawMemory() {
    Deallocate(buffer_);
  }

  T *operator+(size_t offset) noexcept {
    // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
    assert(offset <= capacity_);
    return buffer_ + offset;
  }

  const T *operator+(size_t offset) const noexcept {
    return const_cast<RawMemory &>(*this) + offset;
  }

  const T &operator[](size_t index) const noexcept {
    return const_cast<RawMemory &>(*this)[index];
  }

  T &operator[](size_t index) noexcept {
    assert(index < capacity_);
    return buffer_[index];
  }

  void Swap(RawMemory &other) noexcept {
    std::swap(buffer_, other.buffer_);
    std::swap(capacity_, other.capacity_);
  }

  const T *GetAddress() const noexcept {
    return buffer_;
  }

  T *GetAddress() noexcept {
    return buffer_;
  }

  [[nodiscard]] size_t Capacity() const {
    return capacity_;
  }

 private:
  // Выделяет сырую память под n элементов и возвращает указатель на неё
  static T *Allocate(size_t n) {
    return n != 0 ? static_cast<T *>(operator new(n * sizeof(T))) : nullptr;
  }

  // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
  static void Deallocate(T *buf) noexcept {
    operator delete(buf);
  }

  T *buffer_ = nullptr;
  size_t capacity_ = 0;
};

template<typename T>
class Vector {
 public:
  using iterator = T *;
  using const_iterator = const T *;

  Vector() = default;

  explicit Vector(size_t size)
      : data_(size), size_(size) {
    std::uninitialized_value_construct_n(begin(), size);
  }

  Vector(const Vector &other)
      : data_(other.size_), size_(other.size_) {
    std::uninitialized_copy_n(other.begin(), other.size_, begin());
  }

  Vector(Vector &&other) noexcept: data_(std::exchange(other.data_, RawMemory<T>{})),
                                   size_(std::exchange(other.size_, 0)) {

  }

  Vector &operator=(const Vector &rhs) {
    if (this != &rhs) {
      if (rhs.size_ > data_.Capacity()) {
        Vector rhs_copy(rhs);
        Swap(rhs_copy);
      } else {
        if (rhs.size_ < size_) {
          std::copy_n(rhs.begin(), rhs.size_, begin());
          std::destroy_n(begin() + rhs.size_, size_ - rhs.size_);
        } else {
          std::copy_n(rhs.begin(), size_, begin());
          std::uninitialized_copy_n(rhs.begin() + size_, rhs.size_ - size_, begin() + size_);
        }
        size_ = rhs.size_;
      }
    }
    return *this;
  }

  Vector &operator=(Vector &&rhs) noexcept {
    if (this != &rhs) {
      data_ = std::exchange(rhs.data_, RawMemory<T>{});
      size_ = std::exchange(rhs.size_, 0);
    }
    return *this;
  }

  void Swap(Vector &other) noexcept {
    std::swap(data_, other.data_);
    std::swap(size_, other.size_);
  }

  ~Vector() {
    std::destroy_n(begin(), size_);
  }

  iterator begin() noexcept {
    return data_.GetAddress();
  }

  iterator end() noexcept {
    return data_.GetAddress() + size_;
  }

  const_iterator begin() const noexcept {
    return data_.GetAddress();
  }

  const_iterator end() const noexcept {
    return data_.GetAddress() + size_;
  }

  const_iterator cbegin() const noexcept {
    return data_.GetAddress();
  }

  const_iterator cend() const noexcept {
    return data_.GetAddress() + size_;
  }

  [[nodiscard]] size_t Size() const noexcept {
    return size_;
  }

  [[nodiscard]] size_t Capacity() const noexcept {
    return data_.Capacity();
  }

  const T &operator[](size_t index) const noexcept {
    return const_cast<Vector &>(*this)[index];
  }

  T &operator[](size_t index) noexcept {
    assert(index < size_);
    return data_[index];
  }

  void Reserve(size_t new_capacity) {
    if (new_capacity <= Capacity()) {
      return;
    }
    RawMemory<T> new_data(new_capacity);
    InitializeRawMemory(begin(), size_, new_data.GetAddress());
    std::destroy_n(begin(), size_);
    data_.Swap(new_data);
  }

  void Resize(size_t new_size) {
    if (new_size < size_) {
      std::destroy_n(begin() + new_size, size_ - new_size);
    } else {
      Reserve(new_size);
      std::uninitialized_value_construct_n(begin() + size_, new_size - size_);
    }
    size_ = new_size;
  }

  void PushBack(const T &value) {
    EmplaceBack(value);
  }

  void PushBack(T &&value) {
    EmplaceBack(std::forward<T>(value));
  }

  void PopBack() noexcept {
    std::destroy_at(begin() + size_ - 1);
    --size_;
  }

  template<typename... Args>
  T &EmplaceBack(Args &&... args) {
    if (size_ == Capacity()) {
      RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
      new(new_data.GetAddress() + size_) T(std::forward<Args>(args)...);
      try {
        InitializeRawMemory(begin(), size_, new_data.GetAddress());
      } catch (...) {
        std::destroy_at(new_data.GetAddress() + size_);
        throw;
      }
      std::destroy_n(begin(), size_);
      data_.Swap(new_data);
    } else {
      new(begin() + size_) T(std::forward<Args>(args)...);
    }
    ++size_;
    return *(end() - 1);
  }

  template<typename... Args>
  iterator Emplace(const_iterator pos, Args &&... args) {
    assert(pos >= begin() && pos <= end());
    size_t index = std::distance(cbegin(), pos);
    if (size_ == Capacity()) {
      RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
      new(new_data.GetAddress() + index) T(std::forward<Args>(args)...);
      try {
        InitializeRawMemory(begin(), index, new_data.GetAddress());
      } catch (...) {
        std::destroy_at(new_data.GetAddress() + index);
        throw;
      }
      try {
        InitializeRawMemory(begin() + index, size_ - index, new_data.GetAddress() + index + 1);
      } catch (...) {
        std::destroy_n(new_data.GetAddress(), index + 1);
        throw;
      }
      std::destroy_n(begin(), size_);
      data_.Swap(new_data);
    } else {
      if (size_ != index) {
        new(end()) T(std::forward<T>(*(end() - 1)));
        std::move_backward(begin() + index, end() - 1, end());
        data_[index] = T(std::forward<Args>(args)...);
      } else {
        new(begin() + index) T(std::forward<Args>(args)...);
      }
    }
    ++size_;
    return begin() + index;
  }

  iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
    assert(pos >= begin() && pos < end());
    size_t index = std::distance(cbegin(), pos);
    std::move(begin() + index + 1, end(), begin() + index);
    PopBack();
    return begin() + index;
  }

  iterator Insert(const_iterator pos, const T &value) {
    return Emplace(pos, value);
  }

  iterator Insert(const_iterator pos, T &&value) {
    return Emplace(pos, std::forward<T>(value));
  }

 private:
  RawMemory<T> data_;
  size_t size_ = 0;

  void InitializeRawMemory(iterator source_start, size_t count, iterator dest_start) {
    if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
      std::uninitialized_move_n(source_start, count, dest_start);
    } else {
      std::uninitialized_copy_n(source_start, count, dest_start);
    }
  }
};
