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
    // Rejects any candidate whose distance to every seed track exceeds the threshold
    template<std::size_t ReducedDimCount>
    class MaxDistanceConstraint : public ITrackCandidateHardConstraint
    {
    public:
        using ReducedVector = math::Vector<ReducedDimCount, FloatType>;
        using TrackVectorMap = std::unordered_map<db::TrackId, const ReducedVector*>;

        MaxDistanceConstraint(const TrackVectorMap& trackVectors, float threshold)
            : _trackVectors{ trackVectors }
            , _threshold{ threshold }
        {
        }

        ~MaxDistanceConstraint() override = default;
        MaxDistanceConstraint(const MaxDistanceConstraint&) = delete;
        MaxDistanceConstraint& operator=(const MaxDistanceConstraint&) = delete;

        bool rejects(const TrackCandidateContext& context) const override
        {
            const auto itCand{ _trackVectors.find(context.candidateTrackId) };
            if (itCand == _trackVectors.cend())
                return false;

            for (const db::TrackId seedId : context.seedTrackIds)
            {
                const auto it{ _trackVectors.find(seedId) };
                if (it != _trackVectors.cend() && math::computeNormalizedCosineDistance(*it->second, *itCand->second) <= _threshold)
                    return false;
            }
            return !context.seedTrackIds.empty();
        }

    private:
        const TrackVectorMap& _trackVectors;
        float _threshold;
    };
} // namespace lms::recommendation
