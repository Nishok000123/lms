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

#include "PathRouter.hpp"

#include <Wt/WApplication.h>

#include "LmsApplication.hpp"

namespace lms::ui
{
    PathRouter::PathRouter()
        : _stack{ addNew<Wt::WStackedWidget>() }
    {
        _stack->setOverflow(Wt::Overflow::Visible);
    }

    void PathRouter::addRoute(std::string_view path, std::optional<Wt::WString> title, Wt::WWidget* widget)
    {
        _routes.emplace_back(Route{ std::string{ path }, widget, std::move(title) });
    }

    void PathRouter::activate()
    {
        LmsApp->internalPathChanged().connect(this, [this] {
            handlePathChange();
        });
        handlePathChange();
    }

    void PathRouter::handlePathChange()
    {
        for (const auto& route : _routes)
        {
            if (LmsApp->internalPathMatches(route.path))
            {
                _stack->setCurrentWidget(route.widget);
                if (route.title)
                    LmsApp->setTitle(*route.title);
                return;
            }
        }
        _noMatchSignal.emit();
    }
} // namespace lms::ui
