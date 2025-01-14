#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <cctype>
#include <chrono>
#include <filesystem>

#if defined(__unix__) || defined(__APPLE__)
    #include <sys/param.h>
    #include <dirent.h>
#endif

#if defined(__linux__)
    #include <mntent.h>
#endif

#if defined (__APPLE__) || defined(BSD)
    #include <sys/mount.h>
    #include <sys/ucred.h>
#endif

#if defined(_WIN32) || defined(WIN32)
    #include <shlobj.h>
    #include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

#if defined(__MACH__)
    #include <mach/mach_time.h>
#endif

// Falltergeist includes
#include "CrossPlatform.h"
#include "Exception.h"
#include "Logger.h"

// Third party includes
#include <SDL.h>

namespace Falltergeist
{
    // static members initialization
    std::string CrossPlatform::_version;
    std::string CrossPlatform::_falloutDataPath;
    std::string CrossPlatform::_falltergeistDataPath;
    std::vector<std::string> CrossPlatform::_dataFiles;
    const std::vector<std::string> CrossPlatform::_necessaryDatFiles = {"master.dat", "critter.dat"};

    std::string CrossPlatform::getVersion()
    {
        if (_version.length() > 0) {
            return _version;
        }

        _version = "Falltergeist 0.4.0";
    #if defined(_WIN32) || defined(WIN32)
        _version += " (Windows)";
    #elif defined(__linux__)
        _version += " (Linux)";
    #elif defined(__APPLE__)
        _version += " (Apple)";
    #elif defined(BSD)
        _version += " (BSD)";
    #else
        _version += " (unknown)";
    #endif
        return _version;
    }

    std::string CrossPlatform::getHomeDirectory()
    {
    #if defined(_WIN32) || defined(WIN32)
        char cwd[256];
        LPITEMIDLIST pidl;
        SHGetSpecialFolderLocation(NULL, CSIDL_PROFILE  ,&pidl);
        SHGetPathFromIDList(pidl, cwd);
    #else
        char *cwd = getenv("HOME");
    #endif
        return std::string(cwd);
    }

    std::string CrossPlatform::getExecutableDirectory()
    {
        return std::filesystem::current_path().string();
    }

    std::vector<std::string> CrossPlatform::getCdDrivePaths()
    {
        std::vector<std::string> result;
    #if defined(_WIN32) || defined(WIN32)
        // Looking for data files on CD-ROM drives
        char buf[256];
        GetLogicalDriveStringsA(sizeof(buf), buf);

        for(char * s = buf; *s; s += strlen(s) + 1) {
            if (GetDriveTypeA(s) == DRIVE_CDROM) {
                result.push_back(std::string(s));
            }
        }

    #elif defined(__linux__)
        FILE *mtab = setmntent("/etc/mtab", "r");
        struct mntent *m = nullptr;
        struct mntent mnt;
        char strings[4096];
        while ((m = getmntent_r(mtab, &mnt, strings, sizeof(strings)))) {
            std::string directory = mnt.mnt_dir;
            std::string type = mnt.mnt_type;
            if (type == "iso9660") {
                result.push_back(directory);
            }
        }
        endmntent(mtab);
    #elif defined (BSD)
        struct statfs *mntbuf = nullptr;
        int mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
        for ( int i = 0; i < mntsize; i++ ) {
            std::string directory = ((struct statfs *)&mntbuf[i])->f_mntonname;
            std::string type = ((struct statfs *)&mntbuf[i])->f_fstypename;
            if (type == "cd9660") {
                result.push_back(directory);
            }
        }
    #else
        throw Exception("CD-ROM detection not supported");
    #endif
        return result;
    }

    // This method is trying to find out where are the DAT files located
    std::string CrossPlatform::findFalloutDataPath()
    {
        if (_falloutDataPath.length() > 0) {
            return _falloutDataPath;
        }
        Logger::info("") << "Looking for Fallout data files" << std::endl;
        std::vector<std::string> directories;
        directories.push_back(getExecutableDirectory());

        for (auto &directory : getDataPaths()) {
            directories.push_back(directory);
        }

        try {
            std::vector<std::string> cdDrives = getCdDrivePaths();
            directories.insert(directories.end(), cdDrives.begin(), cdDrives.end());
        } catch (const Exception& e) {
            Logger::error("") << e.what() << std::endl;
        }

        for (auto &directory : directories) {
            if (std::all_of(
                _necessaryDatFiles.begin(),
                _necessaryDatFiles.end(),
                [directory](const std::string& file) {
                    if (std::filesystem::exists(directory + "/" + file)) {
                        Logger::info("") << "Searching in directory: " << directory << " " << file << " [FOUND]" << std::endl;
                        return true;
                    } else {
                        Logger::info("") << "Searching in directory: " << directory << " " << file << " [NOT FOUND]" << std::endl;
                        return false;
                    }
                })
                )
            {
                _falloutDataPath = directory;
                return _falloutDataPath;
            }
        }

        throw Exception("Fallout data files are not found!");
    }

    std::string CrossPlatform::findFalltergeistDataPath()
    {
        if (_falltergeistDataPath.length() > 0) {
            return _falltergeistDataPath;
        }
        Logger::info("") << "Looking for Falltergeist data files" << std::endl;
        std::vector<std::string> directories;
        directories.push_back(getExecutableDirectory());

        for (auto &directory : getDataPaths()) {
            directories.push_back(directory);
        }

        for (auto &directory : directories) {
            if (std::filesystem::exists(directory + "/data/movies.lst")) {
                Logger::info("") << "Searching in directory: " << directory << "/data/movies.lst [FOUND]" << std::endl;
                _falltergeistDataPath = directory;
                return _falltergeistDataPath;
            } else {
                Logger::info("") << "Searching in directory: " << directory << "/data/movies.lst [NOT FOUND]" << std::endl;
            }
        }

        throw Exception("Falltergeist data files are not found!");
    }

    // this method looks for available dat files
    std::vector<std::string> CrossPlatform::findFalloutDataFiles()
    {
        if (_dataFiles.size()) {
            return _dataFiles;
        }

    #if defined(_WIN32) || defined(WIN32) // Windows
        HANDLE hFind = INVALID_HANDLE_VALUE;
        WIN32_FIND_DATA ffd;
        std::string path = CrossPlatform::findFalloutDataPath()+"*";
        _dataFiles = _necessaryDatFiles;
        hFind = FindFirstFile(path.c_str(), &ffd);

        if (INVALID_HANDLE_VALUE == hFind) {
            return _dataFiles;
        }

        do {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            } else {
                std::string filename(ffd.cFileName);
                std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
                if (filename.length() > 4) // exclude . and ..
                {
                    std::string ext = filename.substr(filename.size() - 4, 4);
                    if (ext == ".dat")
                    {
                        if (filename.length() == 12 && filename.substr(0, 5) == "patch")
                            _dataFiles.insert(_dataFiles.begin(),filename);
                    }
                }
            }
        } while (FindNextFile(hFind, &ffd) != 0);
    #else
        // looking for all available dat files in directory
        DIR *pxDir = opendir(CrossPlatform::findFalloutDataPath().c_str());
        if (!pxDir)
        {
            throw Exception("Can't open data directory: " + CrossPlatform::findFalloutDataPath());
        }
        _dataFiles = _necessaryDatFiles;
        struct dirent *pxItem = 0;
        while ((pxItem = readdir(pxDir)))
        {
            std::string filename(pxItem->d_name);
            std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
            // exclude . and ..
            if (filename.length() > 4) {
                std::string ext = filename.substr(filename.size() - 4, 4);
                if (ext == ".dat") {
                    if (filename.length() == 12 && filename.substr(0, 5) == "patch") {
                        _dataFiles.insert(_dataFiles.begin(), filename);
                    }
                }
            }
        }
        closedir(pxDir);
    #endif
        return _dataFiles;
    }

    bool CrossPlatform::_createDirectory(const char *dir)
    {
    #if defined(__unix__) || defined(__APPLE__) // Linux, OS X, BSD
        struct stat st;

        if (stat(dir, &st) == 0)
        {
            // Directory already exists
            if (S_ISDIR(st.st_mode)) {
                return false;
            }

            throw std::runtime_error("Path `" + std::string(dir) + "' already exists and is not a directory");
        } else {
            if (mkdir(dir, S_IRWXU) == 0) {
                return true;
            }

            throw std::runtime_error(strerror(errno));
        }
    #elif defined(_WIN32) || defined(WIN32) // Windows
        DWORD attrs = GetFileAttributes(dir);

        // Assume path exists
        if (attrs != INVALID_FILE_ATTRIBUTES)
        {
            // Directory already exists
            if (attrs & FILE_ATTRIBUTE_DIRECTORY) return false;

            throw std::runtime_error("Path `" + std::string(dir) + "' already exists and is not a directory");
        }
        else
        {
            if (CreateDirectory(dir,NULL) != 0) return true;

            DWORD errorId = GetLastError();

            // FIXME: Use FormatMessage to get error string
            throw std::runtime_error("CreateDirectory failed with code: " + std::to_string(errorId));
        }
    #else
        #error Platform not supported: CrossPlatform::_createDirectory not implemented
    #endif
        return false;
    }

    void CrossPlatform::createDirectory(std::string path)
    {
        // Start index of 1 means that we skip root directory.
        for (auto dirDelim = path.find_first_of('/', 1U); dirDelim != std::string::npos;
             dirDelim = path.find_first_of('/', dirDelim + 1)) {
            path[dirDelim] = '\0';
            _createDirectory(path.c_str());
            path[dirDelim] = '/';
        }
        _createDirectory(path.c_str());
    }

    bool CrossPlatform::fileExists(std::string file)
    {
    #if defined(__unix__) || defined(__APPLE__) // Linux, OS X, BSD
        struct stat st;

        if (stat(file.c_str(), &st) == 0)
        {
            return true;
        }
        else
        {
            return false;
        }
    #elif defined(_WIN32) || defined(WIN32) // Windows
        DWORD attrs = GetFileAttributes(file.c_str());

        // Assume path exists
        if (attrs != INVALID_FILE_ATTRIBUTES)
        {
            return true;
        }
        else
        {
            return false;
        }
    #else
        #error Platform not supported: CrossPlatform::_createDirectory not implemented
    #endif
        return false;
    }

    std::string CrossPlatform::getConfigPath()
    {
    #if defined(__unix__)
        char *maybeConfigHome = getenv("XDG_CONFIG_HOME");
        if (maybeConfigHome == nullptr || *maybeConfigHome == '\0') {
            return getHomeDirectory() + "/.config/falltergeist";
        }
        return std::string(maybeConfigHome) + "/falltergeist";
    #elif defined(__APPLE__)
        return getHomeDirectory() + "/Library/Application Support/falltergeist";
    #elif defined(_WIN32) || defined(WIN32)
        char path[256];
        SHGetFolderPath(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, path);
        return std::string(path) + "/falltergeist";
    #else
        #error Platform not supported: CrossPlatform::getConfigPath not implemented
    #endif
    }

    std::vector<std::string> CrossPlatform::getDataPaths()
    {
        std::vector<std::string> _dataPaths;
    #if defined(__unix__)
        char *maybeDataHome = getenv("XDG_DATA_HOME");
        if (maybeDataHome == nullptr || *maybeDataHome == '\0') {
            _dataPaths.push_back(getHomeDirectory() + "/.local/share/falltergeist");
        } else {
            _dataPaths.push_back(std::string(maybeDataHome) + "/falltergeist");
        }

        std::string sharedir = getExecutableDirectory() + "/../share/falltergeist";
        DIR *pShareDir = opendir(sharedir.c_str());
        if (pShareDir) {
            _dataPaths.push_back(sharedir.c_str());
            closedir(pShareDir);
        }

        std::string parentdir = getExecutableDirectory() + "/..";
        DIR *pParentDir = opendir(parentdir.c_str());
        if (pParentDir) {
            _dataPaths.push_back(parentdir.c_str());
            closedir(pParentDir);
        }
    #else
        _dataPaths.push_back(getConfigPath());
    #endif
        return _dataPaths;
    }
}
