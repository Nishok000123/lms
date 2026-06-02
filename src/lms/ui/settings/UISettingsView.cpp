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

#include "UISettingsView.hpp"

#include <set>

#include <Wt/WCheckBox.h>
#include <Wt/WComboBox.h>
#include <Wt/WFormModel.h>
#include <Wt/WSelectionBox.h>
#include <Wt/WString.h>
#include <Wt/WTemplateFormView.h>

#include "core/String.hpp"

#include "database/Session.hpp"
#include "database/objects/User.hpp"

#include "LmsApplication.hpp"
#include "common/MandatoryValidator.hpp"
#include "common/ValueStringModel.hpp"

#include "SettingsViewUtils.hpp"

namespace lms::ui
{
    namespace
    {
        // highly unefficient hack to make WSelectionBox work with Wt::WFormModel
        class SelectionBox : public Wt::WSelectionBox
        {
        public:
            static inline constexpr std::string_view valueSeparator{ ", " };

        private:
            void setValueText(const Wt::WString& values) override
            {
                std::set<int> selectedIndexes;
                const std::string strValues{ values.toUTF8() };
                for (std::string_view value : core::stringUtils::splitString(strValues, valueSeparator))
                {
                    const int index{ findText(std::string{ value }) };
                    if (index >= 0)
                        selectedIndexes.insert(index);
                }

                setSelectedIndexes(selectedIndexes);
            }

            Wt::WString valueText() const override
            {
                Wt::WString res;
                for (int index : selectedIndexes())
                {
                    if (!res.empty())
                        res += std::string{ valueSeparator };
                    res += itemText(index);
                }

                return res;
            }
        };

        class UISettingsModel : public Wt::WFormModel
        {
        public:
            static inline const Field ArtistReleaseSortMethodField{ "artist-release-sort-method" };
            static inline const Field EnableInlineArtistRelationships{ "enable-inline-artist-relationships" };
            static inline const Field InlineArtistRelationships{ "inline-artist-relationships" };

            using ArtistReleaseSortMethodModel = ValueStringModel<db::ReleaseSortMethod>;
            using ArtistRelationshipsModel = ValueStringModel<db::TrackArtistLinkType>;

            UISettingsModel()
            {
                _artistReleaseSortMethodModel = std::make_shared<ArtistReleaseSortMethodModel>();
                _artistReleaseSortMethodModel->add(Wt::WString::tr("Lms.Settings.date-asc"), db::ReleaseSortMethod::DateAsc);
                _artistReleaseSortMethodModel->add(Wt::WString::tr("Lms.Settings.date-desc"), db::ReleaseSortMethod::DateDesc);
                _artistReleaseSortMethodModel->add(Wt::WString::tr("Lms.Settings.original-date-asc"), db::ReleaseSortMethod::OriginalDate);
                _artistReleaseSortMethodModel->add(Wt::WString::tr("Lms.Settings.original-date-desc"), db::ReleaseSortMethod::OriginalDateDesc);
                _artistReleaseSortMethodModel->add(Wt::WString::tr("Lms.Settings.name"), db::ReleaseSortMethod::Name);

                _artistRelationshipsModel = std::make_shared<ArtistRelationshipsModel>();
                _artistRelationshipsModel->add(Wt::WString::trn("Lms.Explore.composer", 2), db::TrackArtistLinkType::Composer);
                _artistRelationshipsModel->add(Wt::WString::trn("Lms.Explore.conductor", 2), db::TrackArtistLinkType::Conductor);
                _artistRelationshipsModel->add(Wt::WString::trn("Lms.Explore.lyricist", 2), db::TrackArtistLinkType::Lyricist);
                _artistRelationshipsModel->add(Wt::WString::trn("Lms.Explore.mixer", 2), db::TrackArtistLinkType::Mixer);
                _artistRelationshipsModel->add(Wt::WString::trn("Lms.Explore.performer", 2), db::TrackArtistLinkType::Performer);
                _artistRelationshipsModel->add(Wt::WString::trn("Lms.Explore.producer", 2), db::TrackArtistLinkType::Producer);
                _artistRelationshipsModel->add(Wt::WString::trn("Lms.Explore.remixer", 2), db::TrackArtistLinkType::Remixer);

                addField(ArtistReleaseSortMethodField);
                addField(EnableInlineArtistRelationships);
                addField(InlineArtistRelationships);

                setValidator(ArtistReleaseSortMethodField, createMandatoryValidator());

                loadData();
            }

            std::shared_ptr<ArtistReleaseSortMethodModel> getArtistReleaseSortMethodModel() { return _artistReleaseSortMethodModel; }
            std::shared_ptr<ArtistRelationshipsModel> getArtistRelationshipsModel() { return _artistRelationshipsModel; }

            void saveData()
            {
                auto transaction{ LmsApp->getDbSession().createWriteTransaction() };
                db::User::pointer user{ LmsApp->getUser() };

                const auto artistReleaseSortMethodRow{ _artistReleaseSortMethodModel->getRowFromString(valueText(ArtistReleaseSortMethodField)) };
                if (artistReleaseSortMethodRow)
                    user.modify()->setUIArtistReleaseSortMethod(_artistReleaseSortMethodModel->getValue(*artistReleaseSortMethodRow));

                const bool enableInlineArtistRelationships{ Wt::asNumber(value(EnableInlineArtistRelationships)) != 0 };
                user.modify()->setUIEnableInlineArtistRelationships(enableInlineArtistRelationships);

                core::EnumSet<db::TrackArtistLinkType> artistLinkTypes;
                const std::string relationships{ valueText(InlineArtistRelationships).toUTF8() };
                for (std::string_view relationship : core::stringUtils::splitString(relationships, SelectionBox::valueSeparator))
                {
                    auto artistRelationshipRow{ _artistRelationshipsModel->getRowFromString(Wt::WString{ std::string{ relationship } }) };
                    if (artistRelationshipRow)
                        artistLinkTypes.insert(_artistRelationshipsModel->getValue(*artistRelationshipRow));
                }
                user.modify()->setUIInlineArtistRelationships(artistLinkTypes);
            }

            void loadData()
            {
                auto transaction{ LmsApp->getDbSession().createReadTransaction() };
                const db::User::pointer user{ LmsApp->getUser() };

                auto artistReleaseSortMethodRow{ _artistReleaseSortMethodModel->getRowFromValue(user->getUIArtistReleaseSortMethod()) };
                if (artistReleaseSortMethodRow)
                    setValue(ArtistReleaseSortMethodField, _artistReleaseSortMethodModel->getString(*artistReleaseSortMethodRow));

                setValue(EnableInlineArtistRelationships, user->getUIEnableInlineArtistRelationships());
                setReadOnly(InlineArtistRelationships, !user->getUIEnableInlineArtistRelationships());

                Wt::WString inlineArtistRelationships;
                for (db::TrackArtistLinkType artistLinkType : user->getUIInlineArtistRelationships())
                {
                    if (auto artistRelationshipsRow{ _artistRelationshipsModel->getRowFromValue(artistLinkType) })
                    {
                        if (!inlineArtistRelationships.empty())
                            inlineArtistRelationships += std::string{ SelectionBox::valueSeparator };
                        inlineArtistRelationships += _artistRelationshipsModel->getString(*artistRelationshipsRow);
                    }
                }
                setValue(InlineArtistRelationships, inlineArtistRelationships);
            }

        private:
            std::shared_ptr<ArtistReleaseSortMethodModel> _artistReleaseSortMethodModel;
            std::shared_ptr<ArtistRelationshipsModel> _artistRelationshipsModel;
        };
    } // namespace

    UISettingsView::UISettingsView()
    {
        wApp->internalPathChanged().connect(this, [this] {
            refreshView();
        });

        refreshView();
    }

    void UISettingsView::refreshView()
    {
        if (!wApp->internalPathMatches("/settings/ui"))
            return;

        clear();

        auto* t{ addNew<Wt::WTemplateFormView>(Wt::WString::tr("Lms.Settings.ui.template")) };
        auto model{ std::make_shared<UISettingsModel>() };

        auto enableInlineArtistRelationships{ std::make_unique<Wt::WCheckBox>() };
        auto inlineArtistRelationships{ std::make_unique<SelectionBox>() };
        inlineArtistRelationships->setSelectionMode(Wt::SelectionMode::Extended);
        inlineArtistRelationships->setVerticalSize(3);
        inlineArtistRelationships->setModel(model->getArtistRelationshipsModel());

        auto updateInlineArtistRelationships{ [=](bool readOnly) {
            model->setReadOnly(UISettingsModel::InlineArtistRelationships, readOnly);
            t->updateModel(model.get());
            t->updateView(model.get());
        } };
        enableInlineArtistRelationships->checked().connect([=] { updateInlineArtistRelationships(false); });
        enableInlineArtistRelationships->unChecked().connect([=] { updateInlineArtistRelationships(true); });

        auto artistReleaseSortMethod{ std::make_unique<Wt::WComboBox>() };
        artistReleaseSortMethod->setModel(model->getArtistReleaseSortMethodModel());
        t->setFormWidget(UISettingsModel::ArtistReleaseSortMethodField, std::move(artistReleaseSortMethod));
        t->setFormWidget(UISettingsModel::EnableInlineArtistRelationships, std::move(enableInlineArtistRelationships));
        t->setFormWidget(UISettingsModel::InlineArtistRelationships, std::move(inlineArtistRelationships));

        utils::bindSaveDiscardButtons(t, model.get(), [model] { model->saveData(); }, [model] { model->loadData(); });
        t->updateView(model.get());
    }
} // namespace lms::ui
