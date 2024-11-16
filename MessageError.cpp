#include "MessageError.hpp"

/******************************************************************************/
// Private Implementation
/******************************************************************************/

namespace
{
    const struct final : public std::error_category
    {
        const char *
        name() const noexcept override
        {
            return "message";
        }
        
        std::string
        message(int const ev) const override
        {
            using MessageError::Code;
            
            switch (static_cast<Code>(ev))
            {
                case Code::json_parsing_error: return "json parsing error";
                case Code::json_not_an_object: return "json not an object";
            
                default: return "message error";
            }
        }
    }
    Category;
}

/******************************************************************************/
// Implementation
/******************************************************************************/

namespace MessageError
{
    std::error_category const &
    category() noexcept
    {
        return Category;
    }
}

/******************************************************************************/
