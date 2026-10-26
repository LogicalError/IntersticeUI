#pragma once

#include <string>
#include <utility>
#include <variant>

namespace interstice
{
    /// Why a call failed.
    enum class ErrorCode : uint8_t
    {
        Failed,          ///< anything else; see the message
        InvalidArgument,
        NotFound,        ///< a file, a device, an element
        ParseFailed,     ///< a theme, a layout
        Unsupported,     ///< the device or backend lacks a feature
        OutOfMemory,
        DeviceLost,      ///< the GPU device was lost (the library recreates it)
    };

    /// A failure: its category and a human-readable message.
    struct Error
    {
        ErrorCode   code = ErrorCode::Failed;
        std::string message;    ///< what went wrong, for logs and users
    };

    /// A value, or the Error saying why there is none. True when it holds the value:
    ///     if (auto image = LoadImage(path)) Use(*image); else Log(image.GetError().message);
    template<class T>
    class [[nodiscard]] Result
    {
    public:
        /// Implicit from a value or an Error, so functions can return either.
        ///@{
        Result(T value) : m_value(std::move(value)) {}
        Result(Error error) : m_value(std::move(error)) {}
        ///@}

        /// True when it holds the value; same as the bool conversion.
        bool     HasValue() const          { return m_value.index() == 0; }
        explicit operator bool() const     { return HasValue(); }

        /// The value; only valid when HasValue() (std::get throws otherwise). * and -> do the same.
        ///@{
        T&       Value()                   { return std::get<0>(m_value); }
        const T& Value() const             { return std::get<0>(m_value); }
        T&       operator*()               { return Value(); }
        const T& operator*() const         { return Value(); }
        T*       operator->()              { return &Value(); }
        const T* operator->() const        { return &Value(); }
        ///@}
        /// A copy of the value, or `fallback` when there is none.
        T        ValueOr(T fallback) const { return HasValue() ? Value() : std::move(fallback); }

        /// The error; only valid when !HasValue().
        const Error& GetError() const      { return std::get<1>(m_value); }

    private:
        std::variant<T, Error> m_value;
    };

    /// Success, or the Error saying why not.
    template<>
    class [[nodiscard]] Result<void>
    {
    public:
        /// Default-constructed is success; implicit from an Error for failure.
        ///@{
        Result() = default;
        Result(Error error) : m_error(std::move(error)), m_failed(true) {}
        ///@}

        /// True on success; same as the bool conversion.
        bool     HasValue() const      { return !m_failed; }
        explicit operator bool() const { return HasValue(); }

        /// The error; a default Error on success.
        const Error& GetError() const  { return m_error; }

    private:
        Error m_error {};
        bool  m_failed = false;
    };
}
