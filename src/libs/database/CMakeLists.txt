add_library(lmsdatabase STATIC
	impl/Artist.cpp
	impl/ArtistInfo.cpp
	impl/Artwork.cpp
	impl/AuthToken.cpp
	impl/Cluster.cpp
	impl/Db.cpp
	impl/Directory.cpp
	impl/IdType.cpp
	impl/Image.cpp
	impl/Listen.cpp
	impl/MediaLibrary.cpp
	impl/Migration.cpp
	impl/PlayListFile.cpp
	impl/PlayQueue.cpp
	impl/TrackArtistLink.cpp
	impl/TrackFeatures.cpp
	impl/TrackList.cpp
	impl/RatedArtist.cpp
	impl/RatedRelease.cpp
	impl/RatedTrack.cpp
	impl/Release.cpp
	impl/ScanSettings.cpp
	impl/Session.cpp
	impl/StarredArtist.cpp
	impl/StarredRelease.cpp
	impl/StarredTrack.cpp
	impl/SqlQuery.cpp
	impl/Track.cpp
	impl/TrackBookmark.cpp
	impl/TrackEmbeddedImage.cpp
	impl/TrackEmbeddedImageLink.cpp
	impl/TrackLyrics.cpp
	impl/Types.cpp
	impl/UIState.cpp
	impl/User.cpp
	impl/Utils.cpp
	)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	target_sources(lmsdatabase PRIVATE impl/TransactionChecker.cpp)
endif()

target_include_directories(lmsdatabase INTERFACE
	include
	)

target_include_directories(lmsdatabase PRIVATE
	include
	)

target_link_libraries(lmsdatabase PRIVATE
	Wt::DboSqlite3
	)

target_link_libraries(lmsdatabase PUBLIC
	lmscore
	std::filesystem
	Wt::Dbo
	)

if(BUILD_TESTING)
	add_subdirectory(test)
endif()
