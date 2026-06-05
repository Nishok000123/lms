/*
 * Copyright (C) 2026 Emeric Poupon
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

#include <unordered_map>

#include "database/objects/TrackId.hpp"
#include "math/NormalizedCosineDistance.hpp"
#include "math/Vector.hpp"

#include "Types.hpp"
#include "track-selection-constraints/ITrackCandidateHardConstraint.hpp"

namespace lms::recommendation
{
    // Rejects any candidate whose embedding is closer than 'threshold' to any already-selected
    // track's embedding — catches the same recording appearing in multiple compilations.
    template<std::size_t ReducedDimCount>
    class NearDuplicateEmbeddingConstraint : public ITrackCandidateHardConstraint
    {
    public:
        using ReducedVector = math::Vector<ReducedDimCount, FloatType>;
        using TrackVectorMap = std::unordered_map<db::TrackId, const ReducedVector*>;

        NearDuplicateEmbeddingConstraint(const TrackVectorMap& trackVectors, float threshold)
            : _trackVectors{ trackVectors }
            , _threshold{ threshold }
        {
        }

        bool rejects(const TrackCandidateContext& context) const override
        {
            const auto itCandidate{ _trackVectors.find(context.candidateTrackId) };
            if (itCandidate == _trackVectors.cend())
                return false;

            const math::NormalizedCosineDistance distFunc{ *itCandidate->second };
            for (const db::TrackId selectedId : context.selectedTracks)
            {
                const auto it{ _trackVectors.find(selectedId) };
                if (it != _trackVectors.cend() && distFunc(*it->second) < _threshold)
                    return true;
            }
            return false;
        }

    private:
        const TrackVectorMap& _trackVectors;
        float _threshold;
    };
} // namespace lms::recommendation
