/*
 * Copyright (C) 2023 Emeric Poupon
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

#include "ScanStepDiscoverFiles.hpp"

#include "core/ILogger.hpp"
#include "core/Path.hpp"

#include "MediaLibraryInfo.hpp"
#include "ScannerSettings.hpp"
#include "scanners/IFileScanner.hpp"

namespace lms::scanner
{
    bool ScanStepDiscoverFiles::needProcess([[maybe_unused]] const ScanContext& context) const
    {
        // always discover files
        return true;
    }

    void ScanStepDiscoverFiles::process(ScanContext& context)
    {
        context.stats.totalFileCount = 0;

        std::vector<std::filesystem::path> supportedFileExtensions;
        for (IFileScanner* scanner : _fileScanners)
        {
            for (const std::filesystem::path& extension : scanner->getSupportedExtensions())
                supportedFileExtensions.emplace_back(extension);
        }

        for (const MediaLibraryInfo& mediaLibrary : _settings.mediaLibraries)
        {
            std::size_t currentDirectoryProcessElemsCount{};
            core::pathUtils::exploreFilesRecursive(
                mediaLibrary.rootDirectory, [&](std::error_code ec, const std::filesystem::path& path) {
                    if (_abortScan)
                        return false;

                    if (!ec && core::pathUtils::hasFileAnyExtension(path, supportedFileExtensions))
                    {
                        context.currentStepStats.processedElems++;
                        currentDirectoryProcessElemsCount++;
                        _progressCallback(context.currentStepStats);
                    }

                    return true;
                },
                &excludeDirFileName);

            LMS_LOG(DBUPDATER, DEBUG, "Discovered " << currentDirectoryProcessElemsCount << " files in '" << mediaLibrary.rootDirectory << "'");
        }

        context.stats.totalFileCount = context.currentStepStats.processedElems;

        LMS_LOG(DBUPDATER, DEBUG, "Discovered " << context.stats.totalFileCount << " files in all directories");
    }
} // namespace lms::scanner
