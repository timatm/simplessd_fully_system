#include "status.hh"
#include <cassert>
#include <cstring>

Status::Status(Code code, const std::string& msg) {
    size_t len = msg.size();
    char* result = new char[len + 2 + 1];
    result[0] = static_cast<char>(code);
    result[1] = '\0';
    std::memcpy(result + 2, msg.data(), len);
    result[len + 2] = '\0';
    state_ = result;
}

Status::~Status() {
    delete[] state_;
}

Status::Status(const Status& rhs) {
    state_ = (rhs.state_ == nullptr) ? nullptr : CopyState(rhs.state_);
}

Status& Status::operator=(const Status& rhs) {
    if (state_ != rhs.state_) {
        delete[] state_;
        state_ = (rhs.state_ == nullptr) ? nullptr : CopyState(rhs.state_);
    }
    return *this;
}

const char* Status::CopyState(const char* state) {
    size_t len = std::strlen(state);
    char* result = new char[len + 1];
    std::memcpy(result, state, len);
    result[len] = '\0';
    return result;
}

Status Status::OK() {
    return Status();
}
Status Status::Empty() {
    return Status(kEmpty,"");
}

Status Status::InvalidArgument(const std::string& msg) {
    return Status(kInvalidArgument, msg);
}

Status Status::NotSupported(const std::string& msg) {
    return Status(kNotSupported, msg);
}
Status Status::NotFound(const std::string& msg) {
    return Status(kNotFound, msg);
}

Status Status::Corruption(const std::string& msg) {
    return Status(kCorruption, msg);
}

Status Status::IOError(const std::string& msg) {
    return Status(kIOError, msg);
}

bool Status::IsNotFound() const {
    return state_ != nullptr && state_[0] == kNotFound;
}

bool Status::IsCorruption() const {
    return state_ != nullptr && state_[0] == kCorruption;
}

bool Status::IsIOError() const {
    return state_ != nullptr && state_[0] == kIOError;
}

bool Status::IsEmpty() const {
    return state_ != nullptr && state_[0] == kEmpty;
}


bool Status::IsNotSupported() const {
    return state_ != nullptr && state_[0] == kNotSupported;
}

std::string Status::ToString() const {
    if (state_ == nullptr) {
        return "OK";
    }
    switch (state_[0]) {
        case kNotFound:
            return "NotFound: " + std::string(state_ + 2);
        case kCorruption:
            return "Corruption: " + std::string(state_ + 2);
        case kIOError:
            return "IOError: " + std::string(state_ + 2);
        default:
            return "Unknown code(" + std::to_string(state_[0]) + "): " + std::string(state_ + 2);
    }
}
