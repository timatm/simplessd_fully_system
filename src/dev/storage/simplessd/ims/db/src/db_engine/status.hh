#ifndef LSMDB_STATUS_HH_
#define LSMDB_STATUS_HH_

#include <string>

class Status {
public:
    Status() : state_(nullptr) {}
    ~Status();

    Status(const Status& rhs);
    Status& operator=(const Status& rhs);

    static Status OK();
    static Status NotFound(const std::string& msg);
    static Status Corruption(const std::string& msg);
    static Status IOError(const std::string& msg);

    bool ok() const { return (state_ == nullptr); }
    bool IsNotFound() const;
    bool IsCorruption() const;
    bool IsIOError() const;

    std::string ToString() const;

private:
    enum Code {
        kOk = 0,
        kNotFound = 1,
        kCorruption = 2,
        kIOError = 3,
    };

    Status(Code code, const std::string& msg);
    const char* CopyState(const char* state);

    const char* state_;
};

#endif  // LSMDB_STATUS_HH_
