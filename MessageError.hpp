#ifndef MESSAGE_ERROR_HPP__
#define MESSAGE_ERROR_HPP__

#include <system_error>

namespace MessageError
{
    enum class Code
    {
        json_parsing_error = -1001,
        json_not_an_object = -1002
    };

    std::error_category const & category() noexcept;
}

namespace std
{
    template<>
    struct is_error_code_enum<MessageError::Code> : public true_type{};

    template<>
    struct is_error_condition_enum<MessageError::Code> : public true_type{};
}

namespace MessageError
{
    inline std::error_code
    make_error_code(Code const e) noexcept
    {
        return {static_cast<int>(e), category()};
    }

    inline std::error_condition
    make_error_condition(Code const e) noexcept
    {
        return {static_cast<int>(e), category()};
    }
}

#endif
