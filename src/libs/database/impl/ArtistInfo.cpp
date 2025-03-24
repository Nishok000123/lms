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

#include "database/ArtistInfo.hpp"

#include <Wt/Dbo/WtSqlTraits.h>

#include "database/Artist.hpp"
#include "database/ArtistInfoId.hpp"
#include "database/Directory.hpp"
#include "database/Session.hpp"

#include "Utils.hpp"
#include "traits/IdTypeTraits.hpp"
#include "traits/PathTraits.hpp"

namespace lms::db
{
    ArtistInfo::pointer ArtistInfo::create(Session& session)
    {
        return session.getDboSession()->add(std::unique_ptr<ArtistInfo>{ new ArtistInfo{} });
    }

    std::size_t ArtistInfo::getCount(Session& session)
    {
        session.checkReadTransaction();

        return utils::fetchQuerySingleResult(session.getDboSession()->query<int>("SELECT COUNT(*) FROM artist_info"));
    }

    ArtistInfo::pointer ArtistInfo::find(Session& session, const std::filesystem::path& p)
    {
        session.checkReadTransaction();

        return utils::fetchQuerySingleResult(session.getDboSession()->query<Wt::Dbo::ptr<ArtistInfo>>("SELECT a_i from artist_info a_i").where("a_i.absolute_file_path = ?").bind(p));
    }

    ArtistInfo::pointer ArtistInfo::find(Session& session, ArtistInfoId id)
    {
        session.checkReadTransaction();

        return utils::fetchQuerySingleResult(session.getDboSession()->query<Wt::Dbo::ptr<ArtistInfo>>("SELECT a_i from artist_info a_i").where("a_i.id = ?").bind(id));
    }

    void ArtistInfo::find(Session& session, ArtistId id, std::optional<Range> range, const std::function<void(const pointer&)>& func)
    {
        session.checkReadTransaction();

        auto query{ session.getDboSession()->query<Wt::Dbo::ptr<ArtistInfo>>("SELECT a_i from artist_info a_i").where("a_i.artist_id = ?").bind(id) };
        utils::forEachQueryRangeResult(query, range, [&](const ArtistInfo::pointer& entry) {
            func(entry);
        });
    }

    void ArtistInfo::find(Session& session, ArtistId id, const std::function<void(const pointer&)>& func)
    {
        find(session, id, std::nullopt, std::move(func));
    }

    void ArtistInfo::find(Session& session, ArtistInfoId& lastRetrievedId, std::size_t count, const std::function<void(const pointer&)>& func)
    {
        session.checkReadTransaction();

        auto query{ session.getDboSession()->query<Wt::Dbo::ptr<ArtistInfo>>("SELECT a_i from artist_info a_i").orderBy("a_i.id").where("a_i.id > ?").bind(lastRetrievedId).limit(static_cast<int>(count)) };

        utils::forEachQueryResult(query, [&](const ArtistInfo::pointer& entry) {
            func(entry);
            lastRetrievedId = entry->getId();
        });
    }

    Artist::pointer ArtistInfo::getArtist() const
    {
        return _artist;
    }

    Directory::pointer ArtistInfo::getDirectory() const
    {
        return _directory;
    }

    void ArtistInfo::setAbsoluteFilePath(const std::filesystem::path& filePath)
    {
        assert(filePath.is_absolute());
        _absoluteFilePath = filePath;
        _fileStem = filePath.stem();
    }

    void ArtistInfo::setDirectory(ObjectPtr<Directory> directory)
    {
        _directory = getDboPtr(directory);
    }

    void ArtistInfo::setArtist(ObjectPtr<Artist> artist)
    {
        _artist = getDboPtr(artist);
    }
} // namespace lms::db
