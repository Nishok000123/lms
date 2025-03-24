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

#include <limits>

#include "database/TrackEmbeddedImage.hpp"
#include "database/TrackEmbeddedImageLink.hpp"
#include "database/Types.hpp"

#include "Common.hpp"

namespace lms::db::tests
{
    using ScopedTrackEmbeddedImage = ScopedEntity<db::TrackEmbeddedImage>;
    using ScopedTrackEmbeddedImageLink = ScopedEntity<db::TrackEmbeddedImageLink>;

    TEST_F(DatabaseFixture, TrackEmbeddedImage)
    {
        {
            auto transaction{ session.createReadTransaction() };
            EXPECT_EQ(TrackEmbeddedImage::getCount(session), 0);
        }

        ScopedTrackEmbeddedImage image{ session };

        {
            auto transaction{ session.createReadTransaction() };
            EXPECT_EQ(TrackEmbeddedImage::getCount(session), 1);

            const TrackEmbeddedImage::pointer img{ TrackEmbeddedImage::find(session, image.getId()) };
            ASSERT_NE(img, TrackEmbeddedImage::pointer{});
            EXPECT_EQ(img->getHash(), db::ImageHashType{});
            EXPECT_EQ(img->getSize(), 0);
            EXPECT_EQ(img->getWidth(), 0);
            EXPECT_EQ(img->getHeight(), 0);
            EXPECT_EQ(img->getMimeType(), "");
        }

        {
            auto transaction{ session.createWriteTransaction() };

            TrackEmbeddedImage::pointer img{ TrackEmbeddedImage::find(session, image.getId()) };
            ASSERT_NE(img, TrackEmbeddedImage::pointer{});
            img.modify()->setHash(db::ImageHashType{ std::numeric_limits<std::uint64_t>::max() });
            img.modify()->setSize(1024 * 1024);
            img.modify()->setWidth(640);
            img.modify()->setHeight(480);
            img.modify()->setMimeType("image/jpeg");
        }

        {
            auto transaction{ session.createReadTransaction() };

            const TrackEmbeddedImage::pointer img{ TrackEmbeddedImage::find(session, image.getId()) };
            ASSERT_NE(img, TrackEmbeddedImage::pointer{});
            EXPECT_EQ(img->getHash(), db::ImageHashType{ std::numeric_limits<std::uint64_t>::max() });
            EXPECT_EQ(img->getSize(), 1024 * 1024);
            EXPECT_EQ(img->getWidth(), 640);
            EXPECT_EQ(img->getHeight(), 480);
            EXPECT_EQ(img->getMimeType(), "image/jpeg");
        }
    }

    TEST_F(DatabaseFixture, TrackEmbeddedImage_findByHash)
    {
        ScopedTrackEmbeddedImage image{ session };
        constexpr std::size_t size{ 1024 };
        constexpr db::ImageHashType hash{ 42 };
        {
            auto transaction{ session.createWriteTransaction() };

            TrackEmbeddedImage::pointer img{ TrackEmbeddedImage::find(session, image.getId()) };
            ASSERT_NE(img, TrackEmbeddedImage::pointer{});
            img.modify()->setHash(hash);
            img.modify()->setSize(size);
        }

        {
            auto transaction{ session.createReadTransaction() };

            const TrackEmbeddedImage::pointer img{ TrackEmbeddedImage::find(session, size, hash) };
            ASSERT_NE(img, TrackEmbeddedImage::pointer{});
            EXPECT_EQ(image.getId(), img->getId());
        }

        {
            auto transaction{ session.createReadTransaction() };

            const TrackEmbeddedImage::pointer img{ TrackEmbeddedImage::find(session, size + 1, hash) };
            EXPECT_EQ(img, TrackEmbeddedImage::pointer{});
        }
    }

    TEST_F(DatabaseFixture, TrackEmbeddedImage_findByParams)
    {
        ScopedTrackEmbeddedImage image{ session };
        ScopedTrack track{ session };
        ScopedRelease release{ session, "MyRelease" };
        ScopedTrackEmbeddedImageLink link{ session, track.lockAndGet(), image.lockAndGet() };

        {
            auto transaction{ session.createReadTransaction() };

            TrackEmbeddedImage::FindParameters params;
            params.setIsPreferred(true);

            bool visited{};
            TrackEmbeddedImage::find(session, params, [&](const auto&) { visited = true; });
            EXPECT_FALSE(visited);
        }

        {
            auto transaction{ session.createWriteTransaction() };
            link.get().modify()->setIsPreferred(true);
        }

        {
            auto transaction{ session.createReadTransaction() };

            TrackEmbeddedImage::FindParameters params;
            params.setIsPreferred(true);

            bool visited{};
            TrackEmbeddedImage::find(session, params, [&](const auto&) { visited = true; });
            EXPECT_TRUE(visited);
        }

        {
            auto transaction{ session.createReadTransaction() };

            TrackEmbeddedImage::FindParameters params;
            params.setIsPreferred(true);
            params.setRelease(release.getId());

            bool visited{};
            TrackEmbeddedImage::find(session, params, [&](const auto&) { visited = true; });
            EXPECT_FALSE(visited);
        }

        {
            auto transaction{ session.createWriteTransaction() };
            track.get().modify()->setRelease(release.get());
        }

        {
            auto transaction{ session.createReadTransaction() };

            TrackEmbeddedImage::FindParameters params;
            params.setIsPreferred(true);
            params.setRelease(release.getId());
            params.setSortMethod(TrackEmbeddedImageSortMethod::FrontCoverAndSize);

            bool visited{};
            TrackEmbeddedImage::find(session, params, [&](const auto&) { visited = true; });
            EXPECT_TRUE(visited);
        }

        {
            auto transaction{ session.createReadTransaction() };

            TrackEmbeddedImage::FindParameters params;
            params.setIsPreferred(true);
            params.setTrack(track.getId());
            params.setSortMethod(TrackEmbeddedImageSortMethod::FrontCoverAndSize);

            bool visited{};
            TrackEmbeddedImage::find(session, params, [&](const auto&) { visited = true; });
            EXPECT_TRUE(visited);
        }
    }

    TEST_F(DatabaseFixture, Track_findByEmbeddedImage)
    {
        ScopedTrackEmbeddedImage image{ session };
        ScopedTrack track{ session };
        ScopedTrackEmbeddedImageLink link{ session, track.lockAndGet(), image.lockAndGet() };

        {
            auto transaction{ session.createReadTransaction() };

            Track::FindParameters params;
            params.setEmbeddedImage(image->getId());

            bool visited{};
            Track::find(session, params, [&](const auto&) { visited = true; });
            EXPECT_TRUE(visited);
        }
    }

    TEST_F(DatabaseFixture, TrackEmbeddedImage_findOrphans)
    {
        ScopedTrackEmbeddedImage image{ session };

        {
            auto transaction{ session.createReadTransaction() };

            auto orphans{ TrackEmbeddedImage::findOrphanIds(session, std::nullopt) };
            ASSERT_EQ(orphans.results.size(), 1);
            EXPECT_EQ(orphans.results[0], image.getId());
        }

        {
            ScopedTrack track{ session };
            ScopedTrackEmbeddedImageLink link{ session, track.lockAndGet(), image.lockAndGet() };

            {
                auto transaction{ session.createReadTransaction() };

                auto orphans{ TrackEmbeddedImage::findOrphanIds(session, std::nullopt) };
                ASSERT_EQ(orphans.results.size(), 0);
            }
        }

        {
            auto transaction{ session.createReadTransaction() };

            auto orphans{ TrackEmbeddedImage::findOrphanIds(session, std::nullopt) };
            ASSERT_EQ(orphans.results.size(), 1);
            EXPECT_EQ(orphans.results[0], image.getId());
        }
    }

    TEST_F(DatabaseFixture, TrackEmbeddedImageLink)
    {
        {
            auto transaction{ session.createReadTransaction() };
            EXPECT_EQ(TrackEmbeddedImage::getCount(session), 0);
        }

        ScopedTrack track{ session };
        ScopedTrackEmbeddedImage image{ session };
        ScopedTrackEmbeddedImageLink imageLink{ session, track.lockAndGet(), image.lockAndGet() };

        {
            auto transaction{ session.createReadTransaction() };
            EXPECT_EQ(TrackEmbeddedImageLink::getCount(session), 1);

            const TrackEmbeddedImageLink::pointer link{ TrackEmbeddedImageLink::find(session, imageLink.getId()) };
            ASSERT_NE(link, TrackEmbeddedImageLink::pointer{});
            EXPECT_EQ(link->getIndex(), 0);
            EXPECT_EQ(link->getType(), ImageType::Unknown);
            EXPECT_EQ(link->getDescription(), "");
            EXPECT_EQ(link->getTrack(), track.get());
            EXPECT_EQ(link->getImage(), image.get());
        }

        {
            auto transaction{ session.createWriteTransaction() };

            TrackEmbeddedImageLink::pointer link{ TrackEmbeddedImageLink::find(session, imageLink.getId()) };
            ASSERT_NE(link, TrackEmbeddedImage::pointer{});
            link.modify()->setIndex(2);
            link.modify()->setType(ImageType::FrontCover);
            link.modify()->setDescription("MyDesc");
        }

        {
            auto transaction{ session.createReadTransaction() };

            const TrackEmbeddedImageLink::pointer img{ TrackEmbeddedImageLink::find(session, imageLink.getId()) };
            ASSERT_NE(img, TrackEmbeddedImage::pointer{});
            EXPECT_EQ(img->getIndex(), 2);
            EXPECT_EQ(img->getType(), ImageType::FrontCover);
            EXPECT_EQ(img->getDescription(), "MyDesc");
        }
    }
} // namespace lms::db::tests