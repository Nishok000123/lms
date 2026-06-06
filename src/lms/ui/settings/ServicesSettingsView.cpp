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

#include "ServicesSettingsView.hpp"

#include <Wt/WComboBox.h>
#include <Wt/WFormModel.h>
#include <Wt/WLineEdit.h>
#include <Wt/WPushButton.h>
#include <Wt/WString.h>
#include <Wt/WTemplateFormView.h>

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
        class ServicesSettingsModel : public Wt::WFormModel
        {
        public:
            static inline const Field FeedbackBackendField{ "feedback-backend" };
            static inline const Field ScrobblingBackendField{ "scrobbling-backend" };
            static inline const Field ListenBrainzTokenField{ "listenbrainz-token" };

            using FeedbackBackendModel = ValueStringModel<db::FeedbackBackend>;
            using ScrobblingBackendModel = ValueStringModel<db::ScrobblingBackend>;

            ServicesSettingsModel()
            {
                _feedbackBackendModel = std::make_shared<FeedbackBackendModel>();
                _feedbackBackendModel->add(Wt::WString::tr("Lms.Settings.backend.internal"), db::FeedbackBackend::Internal);
                _feedbackBackendModel->add(Wt::WString::tr("Lms.Settings.backend.listenbrainz"), db::FeedbackBackend::ListenBrainz);

                _scrobblingBackendModel = std::make_shared<ScrobblingBackendModel>();
                _scrobblingBackendModel->add(Wt::WString::tr("Lms.Settings.backend.internal"), db::ScrobblingBackend::Internal);
                _scrobblingBackendModel->add(Wt::WString::tr("Lms.Settings.backend.listenbrainz"), db::ScrobblingBackend::ListenBrainz);

                addField(FeedbackBackendField);
                addField(ScrobblingBackendField);
                addField(ListenBrainzTokenField);

                setValidator(ListenBrainzTokenField, createMandatoryValidator());

                loadData();
            }

            std::shared_ptr<FeedbackBackendModel> getFeedbackBackendModel() { return _feedbackBackendModel; }
            std::shared_ptr<ScrobblingBackendModel> getScrobblingBackendModel() { return _scrobblingBackendModel; }

            void saveData()
            {
                auto transaction{ LmsApp->getDbSession().createWriteTransaction() };
                db::User::pointer user{ LmsApp->getUser() };

                if (auto feedbackBackendRow{ _feedbackBackendModel->getRowFromString(valueText(FeedbackBackendField)) })
                    user.modify()->setFeedbackBackend(_feedbackBackendModel->getValue(*feedbackBackendRow));

                if (auto scrobblingBackendRow{ _scrobblingBackendModel->getRowFromString(valueText(ScrobblingBackendField)) })
                    user.modify()->setScrobblingBackend(_scrobblingBackendModel->getValue(*scrobblingBackendRow));

                user.modify()->setListenBrainzToken(Wt::asString(value(ListenBrainzTokenField)).toUTF8());
            }

            void loadData()
            {
                auto transaction{ LmsApp->getDbSession().createReadTransaction() };
                const db::User::pointer user{ LmsApp->getUser() };

                if (auto feedbackBackendRow{ _feedbackBackendModel->getRowFromValue(user->getFeedbackBackend()) })
                    setValue(FeedbackBackendField, _feedbackBackendModel->getString(*feedbackBackendRow));

                if (auto scrobblingBackendRow{ _scrobblingBackendModel->getRowFromValue(user->getScrobblingBackend()) })
                    setValue(ScrobblingBackendField, _scrobblingBackendModel->getString(*scrobblingBackendRow));

                if (const auto listenBrainzToken{ user->getListenBrainzToken() }; !listenBrainzToken.empty())
                    setValue(ListenBrainzTokenField, Wt::WString::fromUTF8(std::string{ listenBrainzToken }));

                {
                    const bool usesListenBrainz{ user->getScrobblingBackend() == db::ScrobblingBackend::ListenBrainz || user->getFeedbackBackend() == db::FeedbackBackend::ListenBrainz };
                    setReadOnly(ServicesSettingsModel::ListenBrainzTokenField, !usesListenBrainz);
                    validator(ServicesSettingsModel::ListenBrainzTokenField)->setMandatory(usesListenBrainz);
                }
            }

        private:
            std::shared_ptr<FeedbackBackendModel> _feedbackBackendModel;
            std::shared_ptr<ScrobblingBackendModel> _scrobblingBackendModel;
        };
    } // namespace

    ServicesSettingsView::ServicesSettingsView()
    {
        wApp->internalPathChanged().connect(this, [this] {
            refreshView();
        });

        refreshView();
    }

    void ServicesSettingsView::refreshView()
    {
        if (!wApp->internalPathMatches("/settings/services"))
            return;

        clear();

        auto* t{ addNew<Wt::WTemplateFormView>(Wt::WString::tr("Lms.Settings.services.template")) };
        auto model{ std::make_shared<ServicesSettingsModel>() };

        Wt::WComboBox* feedbackBackendRaw{};
        {
            auto feedbackBackend{ std::make_unique<Wt::WComboBox>() };
            feedbackBackend->setModel(model->getFeedbackBackendModel());
            feedbackBackendRaw = feedbackBackend.get();
            t->setFormWidget(ServicesSettingsModel::FeedbackBackendField, std::move(feedbackBackend));
        }

        Wt::WComboBox* scrobblingBackendRaw{};
        {
            auto scrobblingBackend{ std::make_unique<Wt::WComboBox>() };
            scrobblingBackend->setModel(model->getScrobblingBackendModel());
            scrobblingBackendRaw = scrobblingBackend.get();
            t->setFormWidget(ServicesSettingsModel::ScrobblingBackendField, std::move(scrobblingBackend));
        }

        auto listenbrainzToken{ std::make_unique<Wt::WLineEdit>() };
        Wt::WLineEdit* listenbrainzTokenPtr{ listenbrainzToken.get() };
        listenbrainzTokenPtr->setEchoMode(Wt::EchoMode::Password);
        t->setFormWidget(ServicesSettingsModel::ListenBrainzTokenField, std::move(listenbrainzToken));

        auto listenbrainzTokenVisibilityBtn{ std::make_unique<Wt::WPushButton>(Wt::WString::tr("Lms.template.toggle-visibility-btn"), Wt::TextFormat::XHTML) };
        listenbrainzTokenVisibilityBtn->clicked().connect(this, [listenbrainzTokenPtr] {
            listenbrainzTokenPtr->setEchoMode(listenbrainzTokenPtr->echoMode() == Wt::EchoMode::Password ? Wt::EchoMode::Normal : Wt::EchoMode::Password);
        });
        t->bindWidget("listenbrainz-token-visibility-btn", std::move(listenbrainzTokenVisibilityBtn));

        auto updateListenBrainzTokenField{ [=] {
            const bool enable{ model->getFeedbackBackendModel()->getValue(feedbackBackendRaw->currentIndex()) == db::FeedbackBackend::ListenBrainz
                               || model->getScrobblingBackendModel()->getValue(scrobblingBackendRaw->currentIndex()) == db::ScrobblingBackend::ListenBrainz };
            model->setReadOnly(ServicesSettingsModel::ListenBrainzTokenField, !enable);
            model->validator(ServicesSettingsModel::ListenBrainzTokenField)->setMandatory(enable);
            t->updateModel(model.get());
            t->updateView(model.get());
        } };

        feedbackBackendRaw->activated().connect([=] { updateListenBrainzTokenField(); });
        scrobblingBackendRaw->activated().connect([=] { updateListenBrainzTokenField(); });

        utils::bindSaveDiscardButtons(t, model.get(), [model] { model->saveData(); }, [model] { model->loadData(); });
        t->updateView(model.get());
    }
} // namespace lms::ui
