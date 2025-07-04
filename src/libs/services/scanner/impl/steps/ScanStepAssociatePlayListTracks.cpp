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

#include "ScanStepAssociatePlayListTracks.hpp"

#include <deque>
#include <filesystem>

#include "core/ILogger.hpp"
#include "database/Db.hpp"
#include "database/Directory.hpp"
#include "database/PlayListFile.hpp"
#include "database/ReleaseId.hpp"
#include "database/Session.hpp"
#include "database/Track.hpp"
#include "database/TrackList.hpp"
#include "services/scanner/ScanErrors.hpp"

#include "ScanContext.hpp"
#include "ScannerSettings.hpp"

namespace lms::scanner
{
    namespace
    {
        struct TrackInfo
        {
            db::TrackId trackId;
            db::ReleaseId releaseId;
        };

        struct PlayListFileAssociation
        {
            db::PlayListFileId playListFileIdId;

            std::vector<TrackInfo> tracks;
        };
        using PlayListFileAssociationContainer = std::deque<PlayListFileAssociation>;

        struct SearchPlayListFileContext
        {
            db::Session& session;
            db::PlayListFileId lastRetrievedPlayListFileId;
            std::size_t processedPlayListFileCount{};
            const ScannerSettings& settings;
            std::vector<std::shared_ptr<ScanError>> errors;
        };

        db::Track::pointer getMatchingTrack(db::Session& session, const std::filesystem::path& filePath, const db::Directory::pointer& playListDirectory)
        {
            db::Track::pointer matchingTrack;
            if (filePath.is_absolute())
            {
                matchingTrack = db::Track::findByPath(session, filePath);
            }
            else
            {
                const std::filesystem::path absolutePath{ playListDirectory->getAbsolutePath() / filePath };
                matchingTrack = db::Track::findByPath(session, absolutePath.lexically_normal());
            }

            return matchingTrack;
        }

        bool isSingleReleasePlayList(std::span<const TrackInfo> tracks)
        {
            if (tracks.empty())
                return true;

            const db::ReleaseId releaseId{ tracks.front().releaseId };
            return std::all_of(std::cbegin(tracks) + 1, std::cend(tracks), [=](const TrackInfo& trackInfo) { return trackInfo.releaseId == releaseId; });
        }

        bool trackListNeedsUpdate(db::Session& session, std::string_view name, std::span<const TrackInfo> tracks, const db::TrackList::pointer& trackList)
        {
            if (trackList->getName() != name)
                return true;

            db::TrackListEntry::FindParameters params;
            params.setTrackList(trackList->getId());

            bool needUpdate{};
            std::size_t currentIndex{};
            db::TrackListEntry::find(session, params, [&](const db::TrackListEntry::pointer& entry) {
                if (currentIndex > tracks.size() || tracks[currentIndex].trackId != entry->getTrackId())
                    needUpdate = true;

                currentIndex += 1;
            });

            if (currentIndex != tracks.size())
                needUpdate = true;

            return needUpdate;
        }

        bool fetchNextPlayListFilesToUpdate(SearchPlayListFileContext& searchContext, PlayListFileAssociationContainer& playListFileAssociations)
        {
            const db::PlayListFileId playListFileIdId{ searchContext.lastRetrievedPlayListFileId };

            {
                constexpr std::size_t readBatchSize{ 20 };

                auto transaction{ searchContext.session.createReadTransaction() };

                db::PlayListFile::find(searchContext.session, searchContext.lastRetrievedPlayListFileId, readBatchSize, [&](const db::PlayListFile::pointer& playListFile) {
                    PlayListFileAssociation playListAssociation;

                    playListAssociation.playListFileIdId = playListFile->getId();

                    std::vector<std::shared_ptr<ScanError>> pendingErrors;

                    const auto files{ playListFile->getFiles() };
                    for (const std::filesystem::path& file : files)
                    {
                        // TODO optim: no need to fetch the whole track
                        const db::Track::pointer track{ getMatchingTrack(searchContext.session, file, playListFile->getDirectory()) };
                        if (track)
                            playListAssociation.tracks.push_back(TrackInfo{ .trackId = track->getId(), .releaseId = track->getReleaseId() });
                        else
                        {
                            pendingErrors.emplace_back(std::make_shared<PlayListFilePathMissingError>(playListFile->getAbsoluteFilePath(), file));
                        }
                    }

                    if (pendingErrors.size() == files.size())
                    {
                        pendingErrors.clear();
                        pendingErrors.emplace_back(std::make_shared<PlayListFileAllPathesMissingError>(playListFile->getAbsoluteFilePath()));
                    }
                    searchContext.errors.insert(std::end(searchContext.errors), std::begin(pendingErrors), std::end(pendingErrors));

                    if (playListAssociation.tracks.empty()
                        || (searchContext.settings.skipSingleReleasePlayLists && isSingleReleasePlayList(playListAssociation.tracks)))
                    {
                        playListAssociation.tracks.clear();
                    }

                    bool needUpdate{ true };
                    if (const db::TrackList::pointer trackList{ playListFile->getTrackList() })
                    {
                        if (!playListAssociation.tracks.empty())
                            needUpdate = trackListNeedsUpdate(searchContext.session, playListFile->getName(), playListAssociation.tracks, trackList);
                    }

                    if (needUpdate)
                        playListFileAssociations.emplace_back(std::move(playListAssociation));

                    searchContext.processedPlayListFileCount++;
                });
            }

            return playListFileIdId != searchContext.lastRetrievedPlayListFileId;
        }

        void updatePlayListFile(db::Session& session, const PlayListFileAssociation& playListFileAssociation)
        {
            db::PlayListFile::pointer playListFile{ db::PlayListFile::find(session, playListFileAssociation.playListFileIdId) };
            assert(playListFile);

            db::TrackList::pointer trackList{ playListFile->getTrackList() };
            if (playListFileAssociation.tracks.empty())
            {
                if (trackList)
                {
                    LMS_LOG(DBUPDATER, DEBUG, "Removed associated tracklist for " << playListFile->getAbsoluteFilePath() << "");
                    trackList.remove();
                }

                return;
            }

            const bool createTrackList{ !trackList };
            if (createTrackList)
            {
                trackList = session.create<db::TrackList>(playListFile->getName(), db::TrackListType::PlayList);
                playListFile.modify()->setTrackList(trackList);
            }

            trackList.modify()->setVisibility(db::TrackList::Visibility::Public);
            trackList.modify()->setLastModifiedDateTime(playListFile->getLastWriteTime());
            trackList.modify()->setName(playListFile->getName());

            trackList.modify()->clear();
            for (const TrackInfo trackInfo : playListFileAssociation.tracks)
            {
                if (db::Track::pointer track{ db::Track::find(session, trackInfo.trackId) })
                    session.create<db::TrackListEntry>(track, trackList, playListFile->getLastWriteTime());
            }

            LMS_LOG(DBUPDATER, DEBUG, std::string_view{ createTrackList ? "Created" : "Updated" } << " associated tracklist for " << playListFile->getAbsoluteFilePath() << " (" << playListFileAssociation.tracks.size() << " tracks)");
        }

        void updatePlayListFiles(db::Session& session, PlayListFileAssociationContainer& playListFileAssociations)
        {
            constexpr std::size_t writeBatchSize{ 5 };

            while (!playListFileAssociations.empty())
            {
                auto transaction{ session.createWriteTransaction() };

                for (std::size_t i{}; !playListFileAssociations.empty() && i < writeBatchSize; ++i)
                {
                    updatePlayListFile(session, playListFileAssociations.front());
                    playListFileAssociations.pop_front();
                }
            }
        }
    } // namespace

    bool ScanStepAssociatePlayListTracks::needProcess(const ScanContext& context) const
    {
        if (context.stats.nbChanges() > 0)
            return true;

        if (getLastScanSettings() && getLastScanSettings()->skipSingleReleasePlayLists != _settings.skipSingleReleasePlayLists)
            return true;

        return false;
    }

    void ScanStepAssociatePlayListTracks::process(ScanContext& context)
    {
        auto& session{ _db.getTLSSession() };

        {
            auto transaction{ session.createReadTransaction() };
            context.currentStepStats.totalElems = db::PlayListFile::getCount(session);
        }

        SearchPlayListFileContext searchContext{
            .session = session,
            .lastRetrievedPlayListFileId = {},
            .settings = _settings,
            .errors = context.stats.errors
        };

        PlayListFileAssociationContainer playListFileAssociations;
        while (fetchNextPlayListFilesToUpdate(searchContext, playListFileAssociations))
        {
            if (_abortScan)
                return;

            updatePlayListFiles(session, playListFileAssociations);

            context.currentStepStats.processedElems = searchContext.processedPlayListFileCount;
            for (const std::shared_ptr<ScanError>& error : searchContext.errors)
                addError(context, error);
            searchContext.errors.clear();

            _progressCallback(context.currentStepStats);
        }
    }
} // namespace lms::scanner
