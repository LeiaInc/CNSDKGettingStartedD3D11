#pragma once

#include "leia/common/api.h"

#include <string>
#include <stdexcept>

namespace leia {

struct SourceLocation {
    const char* filename{nullptr};
    int line{0};
    const char* funcname{nullptr};
};

/// Direct linking to blink libraries leads to corrupted symbols used by our shared library.
/// This leads to a crash on std::exception::what call when we catch std::runtime_error.
/// Seems like std::runtime_error::what function pointer is corrupted.
/// Using an exception defined within our shared library allows us to workaround it.
class Exception : public std::exception {
public:
    LEIA_COMMON_API
    Exception(std::string message);
    LEIA_COMMON_API
    Exception(std::string const& message, leia::SourceLocation sourceLoc);
    LEIA_COMMON_API
    ~Exception() override;

    LEIA_COMMON_API
    const char* what() const noexcept override;

private:
    std::string _message;
};

#ifndef LEIA_FUNCTION
# define LEIA_FUNCTION static_cast<const char*>(__FUNCTION__)
#endif

#define THROW_EXCEPTION_T(Type, message) \
    throw Type(message, leia::SourceLocation{__FILE__, __LINE__, LEIA_FUNCTION});

#define THROW_EXCEPTION(message) \
    THROW_EXCEPTION_T(leia::Exception, message)

} // namespace leia
