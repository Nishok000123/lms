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
#include <string>
#include <string_view>
#include <vector>

#include <Wt/WContainerWidget.h>
#include <Wt/WSignal.h>
#include <Wt/WStackedWidget.h>
#include <Wt/WString.h>

namespace lms::ui
{
    class PathRouter : public Wt::WContainerWidget
    {
    public:
        PathRouter();

        template<typename T, typename... Args>
        T* add(std::string_view path, std::optional<Wt::WString> title, Args&&... args)
        {
            T* widget{ _stack->addNew<T>(std::forward<Args>(args)...) };
            addRoute(path, std::move(title), widget);
            return widget;
        }

        void addRoute(std::string_view path, std::optional<Wt::WString> title, Wt::WWidget* widget);

        // Emitted when no registered route matches the current internal path.
        Wt::Signal<>& noMatch() { return _noMatchSignal; }

        // Call once after all routes are registered — performs initial routing and starts listening to path changes.
        void activate();

    private:
        void handlePathChange();

        Wt::WStackedWidget* _stack{};
        Wt::Signal<> _noMatchSignal;

        struct Route
        {
            std::string path;
            Wt::WWidget* widget{};
            std::optional<Wt::WString> title;
        };
        std::vector<Route> _routes;
    };
} // namespace lms::ui
