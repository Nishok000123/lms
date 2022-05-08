/*
 * Copyright (C) 2018 Emeric Poupon
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

#include "ArtistView.hpp"

#include <Wt/WPushButton.h>

#include "services/database/Artist.hpp"
#include "services/database/Cluster.hpp"
#include "services/database/Release.hpp"
#include "services/database/ScanSettings.hpp"
#include "services/database/Session.hpp"
#include "services/database/Track.hpp"
#include "services/database/User.hpp"
#include "services/scrobbling/IScrobblingService.hpp"
#include "services/recommendation/IRecommendationService.hpp"
#include "utils/Logger.hpp"
#include "utils/String.hpp"

#include "common/InfiniteScrollingContainer.hpp"
#include "resource/DownloadResource.hpp"
#include "ArtistListHelpers.hpp"
#include "Filters.hpp"
#include "LmsApplication.hpp"
#include "LmsApplicationException.hpp"
#include "ReleaseListHelpers.hpp"
#include "TrackListHelpers.hpp"

using namespace Database;

namespace UserInterface {

Artist::Artist(Filters* filters)
: Template {Wt::WString::tr("Lms.Explore.Artist.template")}
, _filters {filters}
{
	addFunction("tr", &Wt::WTemplate::Functions::tr);
	addFunction("id", &Wt::WTemplate::Functions::id);

	LmsApp->internalPathChanged().connect(this, [this]
	{
		refreshView();
	});

	filters->updated().connect([this]
	{
		refreshView();
	});

	refreshView();
}

static
std::optional<ArtistId>
extractArtistIdFromInternalPath()
{
	if (wApp->internalPathMatches("/artist/mbid/"))
	{
		const auto mbid {UUID::fromString(wApp->internalPathNextPart("/artist/mbid/"))};
		if (mbid)
		{
			auto transaction {LmsApp->getDbSession().createSharedTransaction()};
			if (const Database::Artist::pointer artist {Database::Artist::find(LmsApp->getDbSession(), *mbid)})
				return artist->getId();
		}

		return std::nullopt;
	}

	return StringUtils::readAs<ArtistId::ValueType>(wApp->internalPathNextPart("/artist/"));
}

void
Artist::refreshView()
{
	if (!wApp->internalPathMatches("/artist/"))
		return;

	clear();
	_artistId = {};
	_trackContainer = nullptr;

	const auto artistId {extractArtistIdFromInternalPath()};
	if (!artistId)
		throw ArtistNotFoundException {};

	const auto similarArtistIds {Service<Recommendation::IRecommendationService>::get()->getSimilarArtists(*artistId, {TrackArtistLinkType::Artist, TrackArtistLinkType::ReleaseArtist}, 5)};

    auto transaction {LmsApp->getDbSession().createSharedTransaction()};

	const Database::Artist::pointer artist {Database::Artist::find(LmsApp->getDbSession(), *artistId)};
	if (!artist)
		throw ArtistNotFoundException {};

	_artistId = *artistId;

	refreshReleases(artist);
	refreshNonReleaseTracks(artist);
	refreshLinks(artist);
	refreshSimilarArtists(similarArtistIds);

	Wt::WContainerWidget* clusterContainers = bindNew<Wt::WContainerWidget>("clusters");

	{
		auto clusterTypes = ScanSettings::get(LmsApp->getDbSession())->getClusterTypes();
		auto clusterGroups = artist->getClusterGroups(clusterTypes, 3);

		for (auto clusters : clusterGroups)
		{
			for (auto cluster : clusters)
			{
				auto clusterId = cluster->getId();
				auto entry = clusterContainers->addWidget(LmsApp->createCluster(cluster));
				entry->clicked().connect([=]
				{
					_filters->add(clusterId);
				});
			}
		}
	}

	bindString("name", Wt::WString::fromUTF8(artist->getName()), Wt::TextFormat::Plain);

	bindNew<Wt::WPushButton>("more-btn", Wt::WString::tr("Lms.Explore.template.more-btn"), Wt::TextFormat::XHTML);
	bindNew<Wt::WPushButton>("play-btn", Wt::WString::tr("Lms.Explore.template.play-btn"), Wt::TextFormat::XHTML)
		->clicked().connect([=]
		{
			artistsAction.emit(PlayQueueAction::Play, {_artistId});
		});

	bindNew<Wt::WPushButton>("play-shuffled", Wt::WString::tr("Lms.Explore.play-shuffled"), Wt::TextFormat::Plain)
		->clicked().connect([=]
		{
			artistsAction.emit(PlayQueueAction::PlayShuffled, {_artistId});
		});
	bindNew<Wt::WPushButton>("play-last", Wt::WString::tr("Lms.Explore.play-last"), Wt::TextFormat::Plain)
		->clicked().connect([=]
		{
			artistsAction.emit(PlayQueueAction::PlayLast, {_artistId});
		});
	bindNew<Wt::WPushButton>("download", Wt::WString::tr("Lms.Explore.download"))
		->setLink(Wt::WLink {std::make_unique<DownloadArtistResource>(*artistId)});

	{
		auto isStarred {[=] { return Service<Scrobbling::IScrobblingService>::get()->isStarred(LmsApp->getUserId(), *artistId); }};

		Wt::WPushButton* starBtn {bindNew<Wt::WPushButton>("star", Wt::WString::tr(isStarred() ? "Lms.Explore.unstar" : "Lms.Explore.star"))};
		starBtn->clicked().connect([=]
		{
			if (isStarred())
			{
				Service<Scrobbling::IScrobblingService>::get()->unstar(LmsApp->getUserId(), *artistId);
				starBtn->setText(Wt::WString::tr("Lms.Explore.star"));
			}
			else
			{
				Service<Scrobbling::IScrobblingService>::get()->star(LmsApp->getUserId(), *artistId);
				starBtn->setText(Wt::WString::tr("Lms.Explore.unstar"));
			}
		});
	}
}

void
Artist::refreshReleases(const ObjectPtr<Database::Artist>& artist)
{
	const auto releases {artist->getReleases(_filters->getClusterIds())};
	if (releases.empty())
		return;

	setCondition("if-has-release", true);

	Wt::WContainerWidget* releasesContainer = bindNew<Wt::WContainerWidget>("releases");
	for (const auto& release : releases)
	{
		releasesContainer->addWidget(ReleaseListHelpers::createEntryForArtist(release, artist));
	}
}

void
Artist::refreshNonReleaseTracks(const ObjectPtr<Database::Artist>& artist)
{
	if (!artist->hasNonReleaseTracks())
		return;

	setCondition("if-has-non-release-track", true);
	_trackContainer = bindNew<InfiniteScrollingContainer>("tracks");
	_trackContainer->onRequestElements.connect(this, [this]
	{
		addSomeNonReleaseTracks();
	});

	addSomeNonReleaseTracks();
}

void
Artist::refreshSimilarArtists(const std::vector<ArtistId>& similarArtistsId)
{
	if (similarArtistsId.empty())
		return;

	setCondition("if-has-similar-artists", true);
	Wt::WContainerWidget* similarArtistsContainer {bindNew<Wt::WContainerWidget>("similar-artists")};

	for (const ArtistId artistId : similarArtistsId)
	{
		const Database::Artist::pointer similarArtist {Database::Artist::find(LmsApp->getDbSession(), artistId)};
		if (!similarArtist)
			continue;

		similarArtistsContainer->addWidget(ArtistListHelpers::createEntry(similarArtist));
	}
}

void
Artist::refreshLinks(const Database::Artist::pointer& artist)
{
	const auto mbid {artist->getMBID()};
	if (mbid)
	{
		setCondition("if-has-mbid", true);
		bindString("mbid-link", std::string {"https://musicbrainz.org/artist/"} + std::string {mbid->getAsString()});
	}
}

void
Artist::addSomeNonReleaseTracks()
{
	auto transaction {LmsApp->getDbSession().createSharedTransaction()};

	const Database::Artist::pointer artist {Database::Artist::find(LmsApp->getDbSession(), _artistId)};
	if (!artist)
		return;

	const auto tracks {artist->getNonReleaseTracks(std::nullopt, Range {static_cast<std::size_t>(_trackContainer->getCount()), _tracksBatchSize})};
	bool moreResults {tracks.moreResults};

	for (const Track::pointer& track : tracks.results)
	{
		if (_trackContainer->getCount() == _tracksMaxCount)
		{
			moreResults = false;
			break;
		}

		_trackContainer->add(TrackListHelpers::createEntry(track, tracksAction));
	}

	_trackContainer->setHasMore(moreResults);
}

} // namespace UserInterface

