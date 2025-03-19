/*
 * Copyright (C) 2016 Emeric Poupon
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

#include "AudioFileParser.hpp"

#include <span>

#include "core/ILogger.hpp"
#include "core/PartialDateTime.hpp"
#include "core/String.hpp"
#include "metadata/Exception.hpp"

#include "AvFormatImageReader.hpp"
#include "AvFormatTagReader.hpp"
#include "TagLibImageReader.hpp"
#include "TagLibTagReader.hpp"
#include "Utils.hpp"

namespace lms::metadata
{
    namespace
    {
        void visitTagValues(const ITagReader& tagReader, std::string_view tagType, std::span<const std::string> tagDelimiters, ITagReader::TagValueVisitor visitor)
        {
            tagReader.visitTagValues(tagType, [&](std::string_view value) {
                auto visitTagIfNonEmpty{ [&](std::string_view tag) {
                    tag = core::stringUtils::stringTrim(tag);
                    if (!tag.empty())
                        visitor(tag);
                } };

                for (std::string_view tagDelimiter : tagDelimiters)
                {
                    if (value.find(tagDelimiter) != std::string_view::npos)
                    {
                        for (std::string_view splitTag : core::stringUtils::splitString(value, tagDelimiters))
                            visitTagIfNonEmpty(splitTag);

                        return;
                    }
                }

                // no delimiter found, or no delimiter to be used
                visitTagIfNonEmpty(value);
            });
        }

        template<typename T>
        std::vector<T> getTagValuesFirstMatchAs(const ITagReader& tagReader, std::initializer_list<TagType> tagTypes, std::span<const std::string> tagDelimiters)
        {
            std::vector<T> res;

            for (const TagType tagType : tagTypes)
            {
                auto addTagIfNonEmpty{ [&res](std::string_view tag) {
                    tag = core::stringUtils::stringTrim(tag);
                    if (!tag.empty())
                    {
                        std::optional<T> val{ core::stringUtils::readAs<T>(tag) };
                        if (val)
                            res.emplace_back(std::move(*val));
                    }
                } };

                tagReader.visitTagValues(tagType, [&](std::string_view value) {
                    for (std::string_view tagDelimiter : tagDelimiters)
                    {
                        if (value.find(tagDelimiter) != std::string_view::npos)
                        {
                            for (std::string_view splitTag : core::stringUtils::splitString(value, tagDelimiters))
                                addTagIfNonEmpty(splitTag);

                            return;
                        }
                    }

                    // no delimiter found, or no delimiter to be used
                    addTagIfNonEmpty(value);
                });

                if (!res.empty())
                    break;
            }

            return res;
        }

        template<typename T>
        std::optional<T> getTagValueFirstMatchAs(const ITagReader& tagReader, std::initializer_list<TagType> tagTypes)
        {
            std::optional<T> res;
            std::vector<T> values{ getTagValuesFirstMatchAs<T>(tagReader, tagTypes, {} /* don't expect multiple values here */) };
            if (!values.empty())
                res = std::move(values.front());

            return res;
        }

        template<typename T>
        std::vector<T> getTagValuesAs(const ITagReader& tagReader, TagType tagType, std::span<const std::string> tagDelimiters)
        {
            return getTagValuesFirstMatchAs<T>(tagReader, { tagType }, tagDelimiters);
        }

        template<typename T>
        std::optional<T> getTagValueAs(const ITagReader& tagReader, TagType tagType)
        {
            return getTagValueFirstMatchAs<T>(tagReader, { tagType });
        }

        std::vector<Lyrics> getLyrics(const ITagReader& tagReader)
        {
            std::vector<Lyrics> res;

            tagReader.visitLyricsTags([&](std::string_view language, std::string_view lyricsText) {
                std::istringstream iss{ std::string{ lyricsText } }; // TODO avoid copies (ispanstream?)
                try
                {
                    Lyrics lyrics{ parseLyrics(iss) };
                    if (lyrics.language.empty())
                        lyrics.language = language;

                    res.emplace_back(std::move(lyrics));
                }
                catch (const LyricsException& e)
                {
                    LMS_LOG(METADATA, ERROR, "Failed to parse lyrics: " + std::string{ e.what() });
                }
            });

            return res;
        }

        std::vector<Artist> getArtists(const ITagReader& tagReader,
            std::initializer_list<TagType> artistTagNames,
            std::initializer_list<TagType> artistSortTagNames,
            std::initializer_list<TagType> artistMBIDTagNames,
            const AudioFileParserParameters& params)
        {
            std::vector<std::string> artistNames{ getTagValuesFirstMatchAs<std::string>(tagReader, artistTagNames, params.artistTagDelimiters) };
            if (artistNames.empty())
                return {};

            std::vector<std::string> artistSortNames{ getTagValuesFirstMatchAs<std::string>(tagReader, artistSortTagNames, params.artistTagDelimiters) };
            std::vector<core::UUID> artistMBIDs{ getTagValuesFirstMatchAs<core::UUID>(tagReader, artistMBIDTagNames, params.defaultTagDelimiters) };

            std::vector<Artist> artists;
            artists.reserve(artistNames.size());

            for (std::size_t i{}; i < artistNames.size(); ++i)
            {
                Artist& artist{ artists.emplace_back(std::move(artistNames[i])) };

                if (artistNames.size() == artistSortNames.size())
                    artist.sortName = std::move(artistSortNames[i]);
                if (artistNames.size() == artistMBIDs.size())
                    artist.mbid = std::move(artistMBIDs[i]);
            }

            return artists;
        }

        PerformerContainer getPerformerArtists(const ITagReader& tagReader)
        {
            PerformerContainer performers;

            tagReader.visitPerformerTags([&](std::string_view role, std::string_view name) {
                // picard stores like this: (see https://picard-docs.musicbrainz.org/en/appendices/tag_mapping.html#performer)
                // We consider we may hit both styles for the same track
                if (role.empty())
                {
                    // "PERFORMER" "artist (role)"
                    utils::PerformerArtist performer{ utils::extractPerformerAndRole(name) };
                    core::stringUtils::capitalize(performer.role);
                    performers[performer.role].push_back(std::move(performer.artist));
                }
                else
                {
                    // "PERFORMER:role", "artist" (MP3)
                    std::string roleCapitalized{ core::stringUtils::stringToLower(role) };
                    core::stringUtils::capitalize(roleCapitalized);
                    performers[roleCapitalized].push_back(Artist{ name });
                }
            });

            return performers;
        }

        bool strIsMatchingArtistNames(std::string_view str, std::span<const std::string_view> artistNames)
        {
            std::string_view::size_type currentOffset{};

            for (const std::string_view artistName : artistNames)
            {
                std::string_view::size_type newPos{ str.find(artistName, currentOffset) };
                if (newPos == std::string_view::npos)
                    return false;

                currentOffset = newPos + artistName.size();
            }

            return true;
        }

        bool strIsContainingAny(std::string_view str, std::span<const std::string> subStrs)
        {
            return std::any_of(std::cbegin(subStrs), std::cend(subStrs), [&str](const std::string& subStr) { return str.find(subStr) != std::string_view::npos; });
        }

        std::string computeArtistDisplayName(std::span<const Artist> artists, const std::optional<std::string>& artistTag, std::span<const std::string> artistTagDelimiters)
        {
            std::string artistDisplayName;

            if (artists.size() == 1)
                artistDisplayName = artists.front().name;
            else if (artists.size() > 1)
            {
                std::vector<std::string_view> artistNames;
                std::transform(std::cbegin(artists), std::cend(artists), std::back_inserter(artistNames), [](const Artist& artist) -> std::string_view { return artist.name; });

                // Picard use case: if we manage to match all artists in the "artist" tag (considered single-valued), and if no custom delimiter is hit, we use it as the display name
                // Otherwise, we reconstruct the string using a standard, hardcoded, join
                if (artistTag && strIsMatchingArtistNames(*artistTag, artistNames))
                {
                    if (!strIsContainingAny(*artistTag, artistTagDelimiters))
                        artistDisplayName = *artistTag;
                }

                if (artistDisplayName.empty())
                    artistDisplayName = core::stringUtils::joinStrings(artistNames, ", ");
            }

            return artistDisplayName;
        }

        std::optional<Track::Advisory> getAdvisory(const ITagReader& tagReader)
        {
            if (const auto value{ getTagValueAs<int>(tagReader, TagType::Advisory) })
            {
                switch (*value)
                {
                case 1:
                case 4:
                    return Track::Advisory::Explicit;
                case 2:
                    return Track::Advisory::Clean;
                case 0:
                    return Track::Advisory::Unknown;
                }
            }

            return std::nullopt;
        }

        void fillInArtistsWithMbid(std::span<const Artist> artists, std::unordered_map<std::string_view, core::UUID>& artistsWithMbid)
        {
            for (const Artist& artist : artists)
            {
                if (artist.mbid.has_value())
                {
                    // there may collisions, we don't want to replace
                    artistsWithMbid.emplace(artist.name, *artist.mbid);
                }
            }
        }

        void fillInMbids(std::span<Artist> artists, const std::unordered_map<std::string_view, core::UUID>& artistsWithMbid)
        {
            for (Artist& artist : artists)
            {
                if (!artist.mbid)
                {
                    const auto it{ artistsWithMbid.find(artist.name) };
                    if (it != std::cend(artistsWithMbid))
                        artist.mbid = it->second;
                }
            }
        }

        void fillMissingMbids(Track& track)
        {
            // first pass: collect all artists that have mbids
            std::unordered_map<std::string_view, core::UUID> artistsWithMbid;

            // For now, mbids can only set in artist and album artist tags
            // filling order is important: we estimate track-level artists are more likely
            // to be set in other fields than album artists
            fillInArtistsWithMbid(track.artists, artistsWithMbid);
            if (track.medium && track.medium->release)
                fillInArtistsWithMbid(track.medium->release->artists, artistsWithMbid);

            // second pass: fill in all artists that have no mbid set with the same name
            fillInMbids(track.conductorArtists, artistsWithMbid);
            fillInMbids(track.composerArtists, artistsWithMbid);
            fillInMbids(track.lyricistArtists, artistsWithMbid);
            fillInMbids(track.mixerArtists, artistsWithMbid);
            fillInMbids(track.producerArtists, artistsWithMbid);
            fillInMbids(track.remixerArtists, artistsWithMbid);
            for (auto& [role, artists] : track.performerArtists)
                fillInMbids(artists, artistsWithMbid);
        }
    } // namespace

    std::unique_ptr<IAudioFileParser> createAudioFileParser(const AudioFileParserParameters& params)
    {
        return std::make_unique<AudioFileParser>(params);
    }

    AudioFileParser::AudioFileParser(const AudioFileParserParameters& params)
        : _params{ params }
    {
        switch (_params.backend)
        {
        case ParserBackend::TagLib:
            LMS_LOG(METADATA, INFO, "Using TagLib parser with read style = " << utils::readStyleToString(_params.readStyle));
            break;

        case ParserBackend::AvFormat:
            LMS_LOG(METADATA, INFO, "Using AvFormat parser");
            break;
        }
    }

    std::span<const std::filesystem::path> AudioFileParser::getSupportedExtensions() const
    {
        // TODO: use backend capability to retrieve supported formats
        static const std::array<std::filesystem::path, 18> fileExtensions{
            ".aac",
            ".alac",
            ".aif",
            ".aiff",
            ".ape",
            ".dsf",
            ".flac",
            ".m4a",
            ".m4b",
            ".mp3",
            ".mpc",
            ".oga",
            ".ogg",
            ".opus",
            ".shn",
            ".wav",
            ".wma",
            ".wv",
        };
        return fileExtensions;
    }

    std::unique_ptr<Track> AudioFileParser::parseMetaData(const std::filesystem::path& p)
    {
        try
        {
            std::unique_ptr<ITagReader> tagReader;
            switch (_params.backend)
            {
            case ParserBackend::TagLib:
                tagReader = std::make_unique<TagLibTagReader>(p, _params.readStyle, _params.debug);
                break;

            case ParserBackend::AvFormat:
                tagReader = std::make_unique<AvFormatTagReader>(p, _params.debug);
                break;
            }
            if (!tagReader)
                throw ParseException{ "Unhandled parser backend" };

            return parseMetaData(*tagReader);
        }
        catch (const Exception& e)
        {
            LMS_LOG(METADATA, ERROR, "File " << p << ": metadata parsing failed");
            throw ParseException{};
        }
    }

    void AudioFileParser::parseImages(const std::filesystem::path& p, ImageVisitor visitor)
    {
        try
        {
            std::unique_ptr<IImageReader> imageReader;
            switch (_params.backend)
            {
            case ParserBackend::TagLib:
                imageReader = std::make_unique<TagLibImageReader>(p);
                break;

            case ParserBackend::AvFormat:
                imageReader = std::make_unique<AvFormatImageReader>(p);
                break;
            }
            if (!imageReader)
                throw ParseException{ "Unhandled parser backend" };

            parseImages(*imageReader, std::move(visitor));
        }
        catch (const Exception& e)
        {
            LMS_LOG(METADATA, ERROR, "File " << p << ": image parsing failed");
            throw ParseException{};
        }
    }

    std::unique_ptr<Track> AudioFileParser::parseMetaData(const ITagReader& tagReader)
    {
        auto track{ std::make_unique<Track>() };

        track->audioProperties = tagReader.getAudioProperties();
        processTags(tagReader, *track);

        return track;
    }

    void AudioFileParser::processTags(const ITagReader& tagReader, Track& track)
    {
        track.title = getTagValueAs<std::string>(tagReader, TagType::TrackTitle).value_or("");
        track.mbid = getTagValueAs<core::UUID>(tagReader, TagType::MusicBrainzTrackID);
        track.recordingMBID = getTagValueAs<core::UUID>(tagReader, TagType::MusicBrainzRecordingID);
        track.acoustID = getTagValueAs<core::UUID>(tagReader, TagType::AcoustID);
        track.position = getTagValueAs<std::size_t>(tagReader, TagType::TrackNumber); // May parse 'Number/Total', that's fine
        if (const auto dateStr{ getTagValueAs<std::string>(tagReader, TagType::Date) })
        {
            if (const core::PartialDateTime date{ core::PartialDateTime::fromString(*dateStr) }; date.isValid())
                track.date = date;
        }
        if (const auto dateStr = getTagValueAs<std::string>(tagReader, TagType::OriginalReleaseDate))
        {
            if (const core::PartialDateTime date{ core::PartialDateTime::fromString(*dateStr) }; date.isValid())
                track.originalDate = date;
        }
        if (const auto dateStr{ getTagValueAs<std::string>(tagReader, TagType::OriginalReleaseYear) })
            track.originalYear = utils::parseYear(*dateStr);

        if (const auto encodingTimeStr{ getTagValueAs<std::string>(tagReader, TagType::EncodingTime) })
        {
            if (const core::PartialDateTime date{ core::PartialDateTime::fromString(*encodingTimeStr) }; date.isValid())
                track.encodingTime = date;
        }

        track.advisory = getAdvisory(tagReader);

        track.lyrics = getLyrics(tagReader); // no custom delimiter on lyrics
        track.comments = getTagValuesAs<std::string>(tagReader, TagType::Comment, {} /* no custom delimiter on comments */);
        track.copyright = getTagValueAs<std::string>(tagReader, TagType::Copyright).value_or("");
        track.copyrightURL = getTagValueAs<std::string>(tagReader, TagType::CopyrightURL).value_or("");
        track.replayGain = getTagValueAs<float>(tagReader, TagType::ReplayGainTrackGain);

        for (const std::string& userExtraTag : _params.userExtraTags)
        {
            visitTagValues(tagReader, userExtraTag, _params.defaultTagDelimiters, [&](std::string_view value) {
                value = core::stringUtils::stringTrim(value);
                if (!value.empty())
                    track.userExtraTags[userExtraTag].push_back(std::string{ value });
            });
        }

        track.genres = getTagValuesAs<std::string>(tagReader, TagType::Genre, _params.defaultTagDelimiters);
        track.moods = getTagValuesAs<std::string>(tagReader, TagType::Mood, _params.defaultTagDelimiters);
        track.groupings = getTagValuesAs<std::string>(tagReader, TagType::Grouping, _params.defaultTagDelimiters);
        track.languages = getTagValuesAs<std::string>(tagReader, TagType::Language, _params.defaultTagDelimiters);

        std::vector<std::string_view> artistDelimiters{};

        track.medium = getMedium(tagReader);
        track.artists = getArtists(tagReader, { TagType::Artists, TagType::Artist }, { TagType::ArtistSortOrder }, { TagType::MusicBrainzArtistID }, _params);
        track.artistDisplayName = computeArtistDisplayName(track.artists, getTagValueAs<std::string>(tagReader, TagType::Artist), _params.artistTagDelimiters);

        track.conductorArtists = getArtists(tagReader, { TagType::Conductors, TagType::Conductor }, { TagType::ConductorsSortOrder, TagType::ConductorSortOrder }, {}, _params);
        track.composerArtists = getArtists(tagReader, { TagType::Composers, TagType::Composer }, { TagType::ComposersSortOrder, TagType::ComposerSortOrder }, {}, _params);
        track.lyricistArtists = getArtists(tagReader, { TagType::Lyricists, TagType::Lyricist }, { TagType::LyricistsSortOrder, TagType::LyricistSortOrder }, {}, _params);
        track.mixerArtists = getArtists(tagReader, { TagType::Mixers, TagType::Mixer }, { TagType::MixersSortOrder, TagType::MixerSortOrder }, {}, _params);
        track.producerArtists = getArtists(tagReader, { TagType::Producers, TagType::Producer }, { TagType::ProducersSortOrder, TagType::ProducerSortOrder }, {}, _params);
        track.remixerArtists = getArtists(tagReader, { TagType::Remixers, TagType::Remixer }, { TagType::RemixersSortOrder, TagType::RemixerSortOrder }, {}, _params);
        track.performerArtists = getPerformerArtists(tagReader); // artistDelimiters not supported

        fillMissingMbids(track);

        // If a file has originalDate but no originalYear, set it
        if (!track.originalYear)
            track.originalYear = track.originalDate.getYear();
    }

    std::optional<Medium> AudioFileParser::getMedium(const ITagReader& tagReader)
    {
        std::optional<Medium> medium;
        medium.emplace();

        medium->media = getTagValueAs<std::string>(tagReader, TagType::Media).value_or("");
        medium->name = getTagValueAs<std::string>(tagReader, TagType::DiscSubtitle).value_or("");
        medium->trackCount = getTagValueAs<std::size_t>(tagReader, TagType::TotalTracks);
        if (!medium->trackCount)
        {
            // totalTracks may be encoded as "position/count"
            if (const auto value{ getTagValueAs<std::string>(tagReader, TagType::TrackNumber) })
            {
                // Expecting 'Number/Total'
                const std::vector<std::string_view> strings{ core::stringUtils::splitString(*value, '/') };
                if (strings.size() == 2)
                    medium->trackCount = core::stringUtils::readAs<std::size_t>(strings[1]);
            }
        }
        // Expecting 'Number[/Total]'
        medium->position = getTagValueAs<std::size_t>(tagReader, TagType::DiscNumber);
        medium->release = getRelease(tagReader);
        medium->replayGain = getTagValueAs<float>(tagReader, TagType::ReplayGainAlbumGain);

        if (medium->isDefault())
            medium.reset();

        return medium;
    }

    std::optional<Release> AudioFileParser::getRelease(const ITagReader& tagReader)
    {
        std::optional<Release> release;

        auto releaseName{ getTagValueAs<std::string>(tagReader, TagType::Album) };
        if (!releaseName)
            return release;

        release.emplace();
        release->name = std::move(*releaseName);
        release->sortName = getTagValueAs<std::string>(tagReader, TagType::AlbumSortOrder).value_or(release->name);
        release->artists = getArtists(tagReader, { TagType::AlbumArtists, TagType::AlbumArtist }, { TagType::AlbumArtistsSortOrder, TagType::AlbumArtistSortOrder }, { TagType::MusicBrainzReleaseArtistID }, _params);
        release->artistDisplayName = computeArtistDisplayName(release->artists, getTagValueAs<std::string>(tagReader, TagType::AlbumArtist), _params.artistTagDelimiters);
        release->mbid = getTagValueAs<core::UUID>(tagReader, TagType::MusicBrainzReleaseID);
        release->groupMBID = getTagValueAs<core::UUID>(tagReader, TagType::MusicBrainzReleaseGroupID);
        release->mediumCount = getTagValueAs<std::size_t>(tagReader, TagType::TotalDiscs);
        release->isCompilation = getTagValueAs<bool>(tagReader, TagType::Compilation).value_or(false);
        release->barcode = getTagValueAs<std::string>(tagReader, TagType::Barcode).value_or("");
        release->labels = getTagValuesAs<std::string>(tagReader, TagType::RecordLabel, _params.defaultTagDelimiters);
        release->comment = getTagValueAs<std::string>(tagReader, TagType::AlbumComment).value_or("");
        release->countries = getTagValuesAs<std::string>(tagReader, TagType::ReleaseCountry, _params.defaultTagDelimiters);
        if (!release->mediumCount)
        {
            // mediumCount may be encoded as "position/count"
            if (const auto value{ getTagValueAs<std::string>(tagReader, TagType::DiscNumber) })
            {
                // Expecting 'Number/Total'
                const std::vector<std::string_view> strings{ core::stringUtils::splitString(*value, '/') };
                if (strings.size() == 2)
                    release->mediumCount = core::stringUtils::readAs<std::size_t>(strings[1]);
            }
        }

        release->releaseTypes = getTagValuesAs<std::string>(tagReader, TagType::ReleaseType, _params.defaultTagDelimiters);

        return release;
    }

    void AudioFileParser::parseImages(const IImageReader& reader, ImageVisitor visitor)
    {
        reader.visitImages([&](const Image& image) {
            visitor(image);
        });
    }
} // namespace lms::metadata