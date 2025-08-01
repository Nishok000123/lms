/*
 * Copyright (C) 2025 Emeric Poupon
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

#pragma once

#include "core/TaggedType.hpp"
#include "database/objects/Artist.hpp"

namespace lms::metadata
{
    struct Artist;
}

namespace lms::scanner::helpers
{
    using AllowFallbackOnMBIDEntry = core::TaggedBool<struct AllowFallbackOnMBIDEntryTag>;

    db::Artist::pointer getOrCreateArtistByMBID(db::Session& session, const metadata::Artist& artistInfo, AllowFallbackOnMBIDEntry allowFallbackOnMBIDEntries);
    db::Artist::pointer getOrCreateArtistByName(db::Session& session, const metadata::Artist& artistInfo, AllowFallbackOnMBIDEntry allowFallbackOnMBIDEntries);
    db::Artist::pointer getOrCreateArtist(db::Session& session, const metadata::Artist& artistInfo, AllowFallbackOnMBIDEntry allowFallbackOnMBIDEntries);

} // namespace lms::scanner::helpers