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

#include "SubsonicSettingsView.hpp"

#include <Wt/WCheckBox.h>
#include <Wt/WComboBox.h>
#include <Wt/WFormModel.h>
#include <Wt/WLineEdit.h>
#include <Wt/WPushButton.h>
#include <Wt/WString.h>
#include <Wt/WTemplateFormView.h>

#include "core/IConfig.hpp"
#include "core/Service.hpp"
#include "core/UUID.hpp"

#include "database/Session.hpp"
#include "database/Types.hpp"
#include "database/objects/User.hpp"
#include "services/auth/IAuthTokenService.hpp"

#include "LmsApplication.hpp"
#include "Tooltip.hpp"
#include "common/MandatoryValidator.hpp"
#include "common/UUIDValidator.hpp"
#include "common/ValueStringModel.hpp"

#include "SettingsViewUtils.hpp"

namespace lms::ui
{
    namespace
    {
        class SubsonicSettingsModel : public Wt::WFormModel
        {
        public:
            static inline const Field SubsonicTokenField{ "subsonic-token" };
            static inline const Field SubsonicEnableTranscodingByDefault{ "subsonic-enable-transcoding-by-default" };
            static inline const Field SubsonicArtistListModeField{ "subsonic-artist-list-mode" };
            static inline const Field SubsonicTranscodingOutputFormatField{ "subsonic-transcoding-output-format" };
            static inline const Field SubsonicTranscodingOutputBitrateField{ "subsonic-transcoding-output-bitrate" };

            explicit SubsonicSettingsModel(auth::IAuthTokenService& authTokenService)
                : _authTokenService{ authTokenService }
            {
                _transcodingOutputBitrateModel = std::make_shared<ValueStringModel<db::Bitrate>>();
                db::visitAllowedAudioBitrates([&](const db::Bitrate bitrate) {
                    _transcodingOutputBitrateModel->add(Wt::WString::fromUTF8(std::to_string(bitrate / 1000)), bitrate);
                });

                _transcodingOutputFormatModel = std::make_shared<ValueStringModel<db::TranscodingOutputFormat>>();
                _transcodingOutputFormatModel->add(Wt::WString::tr("Lms.Settings.transcoding-output-format.mp3"), db::TranscodingOutputFormat::MP3);
                _transcodingOutputFormatModel->add(Wt::WString::tr("Lms.Settings.transcoding-output-format.ogg_opus"), db::TranscodingOutputFormat::OGG_OPUS);
                _transcodingOutputFormatModel->add(Wt::WString::tr("Lms.Settings.transcoding-output-format.ogg_vorbis"), db::TranscodingOutputFormat::OGG_VORBIS);

                _subsonicArtistListModeModel = std::make_shared<ValueStringModel<db::SubsonicArtistListMode>>();
                _subsonicArtistListModeModel->add(Wt::WString::tr("Lms.Settings.subsonic-artist-list-mode.all-artists"), db::SubsonicArtistListMode::AllArtists);
                _subsonicArtistListModeModel->add(Wt::WString::tr("Lms.Settings.subsonic-artist-list-mode.release-artists"), db::SubsonicArtistListMode::ReleaseArtists);
                _subsonicArtistListModeModel->add(Wt::WString::tr("Lms.Settings.subsonic-artist-list-mode.track-artists"), db::SubsonicArtistListMode::TrackArtists);

                addField(SubsonicTokenField);
                addField(SubsonicEnableTranscodingByDefault);
                addField(SubsonicTranscodingOutputBitrateField);
                addField(SubsonicTranscodingOutputFormatField);
                addField(SubsonicArtistListModeField);

                setValidator(SubsonicTokenField, createUUIDValidator());
                setValidator(SubsonicTranscodingOutputBitrateField, createMandatoryValidator());
                setValidator(SubsonicTranscodingOutputFormatField, createMandatoryValidator());

                loadData();
            }

            std::shared_ptr<Wt::WAbstractItemModel> getTranscodingOutputBitrateModel() { return _transcodingOutputBitrateModel; }
            std::shared_ptr<Wt::WAbstractItemModel> getTranscodingOutputFormatModel() { return _transcodingOutputFormatModel; }
            std::shared_ptr<Wt::WAbstractItemModel> getSubsonicArtistListModeModel() { return _subsonicArtistListModeModel; }

            void saveData()
            {
                auto transaction{ LmsApp->getDbSession().createWriteTransaction() };
                db::User::pointer user{ LmsApp->getUser() };

                const std::string token{ Wt::asString(value(SubsonicTokenField)).toUTF8() };
                if (token.empty())
                {
                    _authTokenService.clearAuthTokens("subsonic", user->getId());
                }
                else
                {
                    bool hasNonMatchingToken{ false };
                    bool hasMatchingToken{ false };
                    _authTokenService.visitAuthTokens("subsonic", user->getId(), [&](const auth::IAuthTokenService::AuthTokenInfo&, std::string_view storedToken) {
                        if (storedToken == token)
                            hasMatchingToken = true;
                        else
                            hasNonMatchingToken = true;
                    });

                    if (!hasMatchingToken || hasNonMatchingToken)
                    {
                        _authTokenService.clearAuthTokens("subsonic", user->getId());
                        _authTokenService.createAuthToken("subsonic", user->getId(), token);
                    }
                }

                user.modify()->setSubsonicEnableTranscodingByDefault(Wt::asNumber(value(SubsonicEnableTranscodingByDefault)) != 0);

                auto subsonicTranscodingOutputBitrateRow{ _transcodingOutputBitrateModel->getRowFromString(valueText(SubsonicTranscodingOutputBitrateField)) };
                if (subsonicTranscodingOutputBitrateRow)
                    user.modify()->setSubsonicDefaultTranscodingOutputBitrate(_transcodingOutputBitrateModel->getValue(*subsonicTranscodingOutputBitrateRow));

                auto subsonicTranscodingOutputFormatRow{ _transcodingOutputFormatModel->getRowFromString(valueText(SubsonicTranscodingOutputFormatField)) };
                if (subsonicTranscodingOutputFormatRow)
                    user.modify()->setSubsonicDefaultTranscodintOutputFormat(_transcodingOutputFormatModel->getValue(*subsonicTranscodingOutputFormatRow));

                auto subsonicArtistListModeRow{ _subsonicArtistListModeModel->getRowFromString(valueText(SubsonicArtistListModeField)) };
                if (subsonicArtistListModeRow)
                    user.modify()->setSubsonicArtistListMode(_subsonicArtistListModeModel->getValue(*subsonicArtistListModeRow));
            }

            void loadData()
            {
                auto transaction{ LmsApp->getDbSession().createReadTransaction() };
                const db::User::pointer user{ LmsApp->getUser() };

                _authTokenService.visitAuthTokens("subsonic", user->getId(), [&](const auth::IAuthTokenService::AuthTokenInfo&, std::string_view storedToken) {
                    if (Wt::asString(value(SubsonicTokenField)).empty())
                        setValue(SubsonicTokenField, Wt::WString::fromUTF8(std::string{ storedToken }));
                });

                setValue(SubsonicEnableTranscodingByDefault, user->getSubsonicEnableTranscodingByDefault());

                auto subsonicTranscodingOutputBitrateRow{ _transcodingOutputBitrateModel->getRowFromValue(user->getSubsonicDefaultTranscodingOutputBitrate()) };
                if (subsonicTranscodingOutputBitrateRow)
                    setValue(SubsonicTranscodingOutputBitrateField, _transcodingOutputBitrateModel->getString(*subsonicTranscodingOutputBitrateRow));

                auto subsonicTranscodingOutputFormatRow{ _transcodingOutputFormatModel->getRowFromValue(user->getSubsonicDefaultTranscodingOutputFormat()) };
                if (subsonicTranscodingOutputFormatRow)
                    setValue(SubsonicTranscodingOutputFormatField, _transcodingOutputFormatModel->getString(*subsonicTranscodingOutputFormatRow));

                auto subsonicArtistListModeRow{ _subsonicArtistListModeModel->getRowFromValue(user->getSubsonicArtistListMode()) };
                if (subsonicArtistListModeRow)
                    setValue(SubsonicArtistListModeField, _subsonicArtistListModeModel->getString(*subsonicArtistListModeRow));
            }

        private:
            auth::IAuthTokenService& _authTokenService;
            std::shared_ptr<ValueStringModel<db::Bitrate>> _transcodingOutputBitrateModel;
            std::shared_ptr<ValueStringModel<db::TranscodingOutputFormat>> _transcodingOutputFormatModel;
            std::shared_ptr<ValueStringModel<db::SubsonicArtistListMode>> _subsonicArtistListModeModel;
        };
    } // namespace

    SubsonicSettingsView::SubsonicSettingsView()
    {
        wApp->internalPathChanged().connect(this, [this] {
            refreshView();
        });

        refreshView();
    }

    void SubsonicSettingsView::refreshView()
    {
        if (!wApp->internalPathMatches("/settings/subsonic"))
            return;

        clear();

        auto* t{ addNew<Wt::WTemplateFormView>(Wt::WString::tr("Lms.Settings.subsonic.template")) };
        t->setCondition("if-has-subsonic-token-usage", core::Service<core::IConfig>::get()->getBool("api-subsonic-support-user-password-auth", true));

        auto model{ std::make_shared<SubsonicSettingsModel>(*core::Service<auth::IAuthTokenService>::get()) };

        auto subsonicToken{ std::make_unique<Wt::WLineEdit>() };
        Wt::WLineEdit* subsonicTokenPtr{ subsonicToken.get() };
        subsonicTokenPtr->setEchoMode(Wt::EchoMode::Password);
        subsonicTokenPtr->setReadOnly(true);
        t->setFormWidget(SubsonicSettingsModel::SubsonicTokenField, std::move(subsonicToken));

        auto subsonicTokenRegenBtn{ std::make_unique<Wt::WPushButton>(Wt::WString::tr("Lms.Settings.regen-token")) };
        subsonicTokenRegenBtn->clicked().connect(this, [subsonicTokenPtr] {
            subsonicTokenPtr->setValueText(Wt::WString::fromUTF8(std::string{ core::UUID::generate().getAsString() }));
        });
        t->bindWidget("subsonic-token-regen-btn", std::move(subsonicTokenRegenBtn));

        auto subsonicTokenVisibilityBtn{ std::make_unique<Wt::WPushButton>(Wt::WString::tr("Lms.template.toggle-visibility-btn"), Wt::TextFormat::XHTML) };
        subsonicTokenVisibilityBtn->clicked().connect(this, [subsonicTokenPtr] {
            subsonicTokenPtr->setEchoMode(subsonicTokenPtr->echoMode() == Wt::EchoMode::Password ? Wt::EchoMode::Normal : Wt::EchoMode::Password);
        });
        t->bindWidget("subsonic-token-visibility-btn", std::move(subsonicTokenVisibilityBtn));

        auto subsonicTokenDelBtn{ std::make_unique<Wt::WPushButton>(Wt::WString::tr("Lms.template.trash-btn"), Wt::TextFormat::XHTML) };
        subsonicTokenDelBtn->clicked().connect(this, [subsonicTokenPtr] {
            subsonicTokenPtr->setValueText("");
        });
        t->bindWidget("subsonic-token-del-btn", std::move(subsonicTokenDelBtn));

        t->setFormWidget(SubsonicSettingsModel::SubsonicEnableTranscodingByDefault, std::make_unique<Wt::WCheckBox>());

        auto transcodingOutputFormat{ std::make_unique<Wt::WComboBox>() };
        transcodingOutputFormat->setModel(model->getTranscodingOutputFormatModel());
        t->setFormWidget(SubsonicSettingsModel::SubsonicTranscodingOutputFormatField, std::move(transcodingOutputFormat));

        auto transcodingOutputBitrate{ std::make_unique<Wt::WComboBox>() };
        transcodingOutputBitrate->setModel(model->getTranscodingOutputBitrateModel());
        t->setFormWidget(SubsonicSettingsModel::SubsonicTranscodingOutputBitrateField, std::move(transcodingOutputBitrate));

        auto artistListMode{ std::make_unique<Wt::WComboBox>() };
        artistListMode->setModel(model->getSubsonicArtistListModeModel());
        t->setFormWidget(SubsonicSettingsModel::SubsonicArtistListModeField, std::move(artistListMode));

        utils::bindSaveDiscardButtons(t, model.get(), [model] { model->saveData(); }, [model] { model->loadData(); });
        t->updateView(model.get());
        initTooltipsForWidgetTree(*t);
    }
} // namespace lms::ui
