/*
 * Copyright (C) 2023 Emeric Poupon
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

#include "responses/Song.hpp"

#include <filesystem>
#include <string_view>
#include <system_error>

#include "core/ITraceLogger.hpp"
#include "core/MimeTypes.hpp"
#include "core/Service.hpp"
#include "core/String.hpp"
#include "database/Types.hpp"
#include "database/objects/Artist.hpp"
#include "database/objects/Artwork.hpp"
#include "database/objects/Cluster.hpp"
#include "database/objects/Directory.hpp"
#include "database/objects/MediaLibrary.hpp"
#include "database/objects/Release.hpp"
#include "database/objects/Track.hpp"
#include "database/objects/TrackArtistLink.hpp"
#include "database/objects/User.hpp"
#include "services/feedback/IFeedbackService.hpp"
#include "services/scrobbling/IScrobblingService.hpp"

#include "CoverArtId.hpp"
#include "RequestContext.hpp"
#include "SubsonicId.hpp"
#include "responses/Artist.hpp"
#include "responses/Contributor.hpp"
#include "responses/ItemGenre.hpp"
#include "responses/ReplayGain.hpp"

namespace lms::api::subsonic
{
    using namespace db;

    namespace
    {
        std::string_view formatToSuffix(TranscodingOutputFormat format)
        {
            switch (format)
            {
            case TranscodingOutputFormat::MP3:
                return "mp3";
            case TranscodingOutputFormat::OGG_OPUS:
                return "opus";
            case TranscodingOutputFormat::MATROSKA_OPUS:
                return "mka";
            case TranscodingOutputFormat::OGG_VORBIS:
                return "ogg";
            case TranscodingOutputFormat::WEBM_VORBIS:
                return "webm";
            }

            return "";
        }
    } // namespace

    Response::Node createSongNode(RequestContext& context, const Track::pointer& track, bool id3)
    {
        LMS_SCOPED_TRACE_DETAILED("Subsonic", "CreateSong");

        Response::Node trackResponse;

        if (!id3)
        {
            if (const auto directory{ track->getDirectory() })
                trackResponse.setAttribute("parent", idToString(directory->getId()));
            trackResponse.setAttribute("isDir", false);
        }

        trackResponse.setAttribute("id", idToString(track->getId()));
        trackResponse.setAttribute("title", track->getName());
        if (track->getTrackNumber())
            trackResponse.setAttribute("track", *track->getTrackNumber());
        if (track->getDiscNumber())
            trackResponse.setAttribute("discNumber", *track->getDiscNumber());
        if (const auto originalYear{ track->getOriginalYear() })
            trackResponse.setAttribute("year", *originalYear);
        else if (const auto year{ track->getYear() })
            trackResponse.setAttribute("year", *year);
        trackResponse.setAttribute("playCount", core::Service<scrobbling::IScrobblingService>::get()->getCount(context.user->getId(), track->getId()));

        // maybe not available if user just removed the library without rescanning
        if (const db::MediaLibrary::pointer library{ track->getMediaLibrary() })
        {
            std::error_code ec;
            const std::filesystem::path relativeTrackPath{ std::filesystem::relative(track->getAbsoluteFilePath(), library->getPath(), ec) };
            if (!ec && !relativeTrackPath.empty())
                trackResponse.setAttribute("path", relativeTrackPath.c_str());
        }

        trackResponse.setAttribute("size", track->getFileSize());

        if (track->getAbsoluteFilePath().has_extension())
        {
            auto extension{ track->getAbsoluteFilePath().extension() };
            trackResponse.setAttribute("suffix", extension.string().substr(1) /* skip leading .*/);
        }

        if (context.user->getSubsonicEnableTranscodingByDefault())
        {
            const std::string fileSuffix{ formatToSuffix(context.user->getSubsonicDefaultTranscodingOutputFormat()) };
            trackResponse.setAttribute("transcodedSuffix", fileSuffix);
            trackResponse.setAttribute("transcodedContentType", core::getMimeType(std::filesystem::path{ "." + fileSuffix }));
        }

        auto artwork{ track->getPreferredMediaArtwork() };
        if (!artwork)
            artwork = track->getPreferredArtwork();

        if (artwork)
        {
            CoverArtId coverArtId{ artwork->getId(), artwork->getLastWrittenTime().toTime_t() };
            trackResponse.setAttribute("coverArt", idToString(coverArtId));
        }

        const std::vector<Artist::pointer>& artists{ track->getArtists({ TrackArtistLinkType::Artist }) };
        if (!artists.empty())
        {
            if (!track->getArtistDisplayName().empty())
                trackResponse.setAttribute("artist", track->getArtistDisplayName());
            else
                trackResponse.setAttribute("artist", utils::joinArtistNames(artists));

            if (artists.size() == 1)
                trackResponse.setAttribute("artistId", idToString(artists.front()->getId()));
        }

        const Release::pointer release{ track->getRelease() };
        if (release)
        {
            trackResponse.setAttribute("album", release->getName());
            trackResponse.setAttribute("albumId", idToString(release->getId()));
        }

        trackResponse.setAttribute("duration", std::chrono::duration_cast<std::chrono::seconds>(track->getDuration()).count());
        trackResponse.setAttribute("bitRate", (track->getBitrate() / 1000));
        trackResponse.setAttribute("type", "music");
        trackResponse.setAttribute("created", core::stringUtils::toISO8601String(track->getAddedTime()));
        trackResponse.setAttribute("contentType", core::getMimeType(track->getAbsoluteFilePath().extension()));
        if (const auto rating{ core::Service<feedback::IFeedbackService>::get()->getRating(context.user->getId(), track->getId()) })
            trackResponse.setAttribute("userRating", *rating);

        if (const Wt::WDateTime dateTime{ core::Service<feedback::IFeedbackService>::get()->getStarredDateTime(context.user->getId(), track->getId()) }; dateTime.isValid())
            trackResponse.setAttribute("starred", core::stringUtils::toISO8601String(dateTime));

        // Report the first GENRE for this track
        std::vector<Cluster::pointer> genres;
        {
            Cluster::FindParameters params;
            params.setTrack(track->getId());
            params.setClusterTypeName("GENRE");

            genres = Cluster::find(context.dbSession, params).results;
            if (!genres.empty())
                trackResponse.setAttribute("genre", genres.front()->getName());
        }

        // OpenSubsonic specific fields (must always be set)
        if (!context.enableOpenSubsonic)
            return trackResponse;

        trackResponse.setAttribute("comment", track->getComment());
        trackResponse.setAttribute("bitDepth", track->getBitsPerSample());
        trackResponse.setAttribute("samplingRate", track->getSampleRate());
        trackResponse.setAttribute("channelCount", track->getChannelCount());

        trackResponse.setAttribute("mediaType", "song");

        {
            const Wt::WDateTime dateTime{ core::Service<scrobbling::IScrobblingService>::get()->getLastListenDateTime(context.user->getId(), track->getId()) };
            trackResponse.setAttribute("played", dateTime.isValid() ? core::stringUtils::toISO8601String(dateTime) : "");
        }

        {
            std::optional<core::UUID> mbid{ track->getRecordingMBID() };
            trackResponse.setAttribute("musicBrainzId", mbid ? mbid->getAsString() : "");
        }

        {
            trackResponse.createEmptyArrayChild("albumartists");
            trackResponse.createEmptyArrayChild("artists");
            trackResponse.createEmptyArrayChild("contributors");

            TrackArtistLink::find(context.dbSession, track->getId(), [&](const TrackArtistLink::pointer& link, const Artist::pointer& artist) {
                switch (link->getType())
                {
                case TrackArtistLinkType::Artist:
                    trackResponse.addArrayChild("artists", createArtistNode(artist));
                    break;
                case TrackArtistLinkType::ReleaseArtist:
                    trackResponse.addArrayChild("albumartists", createArtistNode(artist));
                    break;
                default:
                    trackResponse.addArrayChild("contributors", createContributorNode(link, artist));
                }
            });
        }

        trackResponse.setAttribute("displayArtist", track->getArtistDisplayName());
        if (release)
            trackResponse.setAttribute("displayAlbumArtist", release->getArtistDisplayName());

        auto addClusters{ [&](Response::Node::Key field, std::string_view clusterTypeName) {
            trackResponse.createEmptyArrayValue(field);

            Cluster::FindParameters params;
            params.setTrack(track->getId());
            params.setClusterTypeName(clusterTypeName);

            for (const auto& cluster : Cluster::find(context.dbSession, params).results)
                trackResponse.addArrayValue(field, cluster->getName());
        } };

        addClusters("moods", "MOOD");

        // Genres
        trackResponse.createEmptyArrayChild("genres");
        for (const auto& genre : genres)
            trackResponse.addArrayChild("genres", createItemGenreNode(genre->getName()));

        auto advisoryToExplicitStatus = [](db::Advisory advisory) -> std::string_view {
            switch (advisory)
            {
            case db::Advisory::Clean:
                return "clean";
            case db::Advisory::Explicit:
                return "expicit";
            case db::Advisory::Unknown:
            case db::Advisory::UnSet:
                break;
            }

            return "";
        };
        trackResponse.setAttribute("explicitStatus", advisoryToExplicitStatus(track->getAdvisory()));

        trackResponse.addChild("replayGain", createReplayGainNode(track));

        return trackResponse;
    }
} // namespace lms::api::subsonic