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

#include <cassert>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

#include "math/EuclideanDistance.hpp"

namespace lms::math
{
    template<typename VectorType>
    class MedoidCalculator
    {
    public:
        static_assert(!std::is_const_v<VectorType>);

        using value_type = typename VectorType::value_type;
        using size_type = std::size_t;

        // provided value must be accessible until finalize() is called
        void add(const VectorType& value)
        {
            _pointers.push_back(&value);
        }

        // Returns a pointer to the medoid among the added vectors.
        // The pointer remains valid as long as the original vectors are alive.
        const VectorType* finalize() const
        {
            assert(!empty());
            return _pointers[findMedoidIndex()];
        }

        bool empty() const
        {
            return _pointers.empty();
        }

        size_type count() const
        {
            return _pointers.size();
        }

        void clear()
        {
            _pointers.clear();
        }

    private:
        size_type findMedoidIndex() const
        {
            size_type medoidIndex{};
            value_type minTotalDistance{ std::numeric_limits<value_type>::max() };

            for (size_type i{}; i < _pointers.size(); ++i)
            {
                value_type totalDistance{};
                const SquaredEuclideanDistance distFunc{ *_pointers[i] };
                for (size_type j{}; j < _pointers.size(); ++j)
                {
                    if (i != j)
                        totalDistance += distFunc(*_pointers[j]);
                }

                if (totalDistance < minTotalDistance)
                {
                    minTotalDistance = totalDistance;
                    medoidIndex = i;
                }
            }

            return medoidIndex;
        }

        std::vector<const VectorType*> _pointers;
    };

    template<typename VectorType>
    VectorType computeMedoid(std::span<const VectorType> values)
    {
        MedoidCalculator<VectorType> calculator;
        for (const auto& value : values)
            calculator.add(value);
        return *calculator.finalize();
    }
} // namespace lms::math
