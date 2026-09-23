#pragma once

#include "core/message.h"
#include <cstddef>

class Conversation {
public:
    Conversation() = default;
    ~Conversation();

    Conversation(const Conversation& other);
    Conversation& operator=(const Conversation& other);

    Conversation(Conversation&& other) noexcept;
    Conversation& operator=(Conversation&& other) noexcept;

    void append(Message m);

    std::size_t size() const noexcept { return size_; }
    const Message& at(std::size_t i) const;

    const Message* begin() const noexcept { return data_; }
    const Message* end() const noexcept {
        return data_ ? data_ + size_ : data_;
    }

private:
    Message* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};