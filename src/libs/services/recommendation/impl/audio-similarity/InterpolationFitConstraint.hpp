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

#include <optional>
#include <unordered_map>

#include "database/objects/TrackId.hpp"
#include "math/NormalizedCosineDistance.hpp"
#include "math/Vector.hpp"

#include "Types.hpp"
#include "track-selection-constraints/ITrackCandidateSoftConstraint.hpp"

namespace lms::recommendation
{
    template<std::size_t ReducedDimCount>
    class InterpolationFitConstraint : public ITrackCandidateSoftConstraint
    {
    public:
        using ReducedVector = math::Vector<ReducedDimCount, FloatType>;
        using TrackVectorMap = std::unordered_map<db::TrackId, const ReducedVector*>;

        explicit InterpolationFitConstraint(const TrackVectorMap& trackVectors)
            : _trackVectors{ trackVectors }
        {
        }

        ~InterpolationFitConstraint() override = default;
        InterpolationFitConstraint(const InterpolationFitConstraint&) = delete;
        InterpolationFitConstraint& operator=(const InterpolationFitConstraint&) = delete;

        float computeScore(const TrackCandidateContext& context) const override
        {
            const auto itCand{ _trackVectors.find(context.candidateTrackId) };
            if (itCand == _trackVectors.cend())
                return {};

            std::optional<float> best;
            for (const db::TrackId seedId : context.seedTrackIds)
            {
                const auto it{ _trackVectors.find(seedId) };
                if (it != _trackVectors.cend())
                {
                    const float dist{ math::computeNormalizedCosineDistance(*it->second, *itCand->second) };
                    best = best ? std::min(*best, dist) : dist;
                }
            }
            return best.value_or(0.F);
        }

    private:
        const TrackVectorMap& _trackVectors;
    };
} // namespace lms::recommendation
