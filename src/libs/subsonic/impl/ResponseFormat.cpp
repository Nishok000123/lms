/*
 * Copyright (C) 2024 Emeric Poupon
 *
 * This file is part of LMS.
 *
 * LMS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LMS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ResponseFormat.hpp"

#include <string_view>

namespace lms::api::subsonic
{
    std::string_view ResponseFormatToMimeType(ResponseFormat format)
    {
        switch (format)
        {
        case ResponseFormat::xml:
            return "text/xml";
        case ResponseFormat::json:
            return "application/json";
        }

        return "";
    }
} // namespace lms::api::subsonic