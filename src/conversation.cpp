#include "core/conversation.h"

#include <limits>
#include <stdexcept>
#include <utility>

Conversation::~Conversation() {
    delete[] data_;
}

Conversation::Conversation(const Conversation& other)
    : data_(other.capacity_ ? new Message[other.capacity_] : nullptr),
      size_(0),
      capacity_(other.capacity_) {
    try {
        for (; size_ < other.size_; ++size_) {
            data_[size_] = other.data_[size_];
        }
    } catch (...) {
        delete[] data_;
        throw;
    }
}

Conversation& Conversation::operator=(const Conversation& other) {
    if (this != &other) {
        Conversation copy(other);

        std::swap(data_, copy.data_);
        std::swap(size_, copy.size_);
        std::swap(capacity_, copy.capacity_);
    }
    return *this;
}

Conversation::Conversation(Conversation&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      capacity_(std::exchange(other.capacity_, 0)) {}

Conversation& Conversation::operator=(Conversation&& other) noexcept {
    if (this != &other) {
        delete[] data_;

        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        capacity_ = std::exchange(other.capacity_, 0);
    }
    return *this;
}

void Conversation::append(Message m) {
    if (size_ == capacity_) {
        if (capacity_ > std::numeric_limits<std::size_t>::max() / 2) {
            throw std::length_error("Conversation capacity overflow");
        }

        const std::size_t next = capacity_ ? capacity_ * 2 : 1;
        Message* replacement = new Message[next];

        try {
            for (std::size_t i = 0; i < size_; ++i) {
                replacement[i] = data_[i];
            }
            replacement[size_] = std::move(m);
        } catch (...) {
            delete[] replacement;
            throw;
        }

        delete[] data_;
        data_ = replacement;
        capacity_ = next;
    } else {
        data_[size_] = std::move(m);
    }

    ++size_;
}

const Message& Conversation::at(std::size_t i) const {
    if (i >= size_) {
        throw std::out_of_range("Conversation::at");
    }
    return data_[i];
}