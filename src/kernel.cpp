#include "kernel.hpp"
#include "utils.hpp"

#include <filesystem>
#include <sstream>
#include <set>
#include <map>

Kernel::Kernel(Package pkg, Package headers) : m_pkg(pkg), m_headers(headers) {}

std::string Kernel::version() const {
    return m_pkg.version;
}

bool Kernel::is_installed() const {
    return m_pkg.is_installed;
}

bool Kernel::has_files() const {
    return m_pkg.has_files;
}

bool Kernel::install() {
    // La lógica real está en KernelBridge::installKernel
    return true;
}

bool Kernel::remove() {
    // La lógica real está en KernelBridge::removeKernel
    return true;
}

std::string Kernel::category() const {
    if (m_pkg.type == "manual" || 
        m_pkg.name.find("linux-neko") != std::string::npos ||
        m_pkg.name.find("linux-cachy-void") != std::string::npos) return "Custom";
    if (m_pkg.name.find("lts") != std::string::npos) return "Longterm";
    if (m_pkg.name.find("rt") != std::string::npos) return "Realtime";
    if (m_pkg.name.find("zen") != std::string::npos) return "Zen";
    if (m_pkg.name.find("hardened") != std::string::npos) return "Hardened";
    if (m_pkg.name.find("mainline") != std::string::npos) return "Mainline";
    return "Stable";
}

std::string Kernel::size() const {
    if (m_pkg.type == "manual") return "Unknown";
    std::string s = utils::exec("xbps-query -p installed_size " + m_pkg.name);
    if (s.empty() || s.find("not found") != std::string::npos) return "Unknown";
    return s;
}

std::string Kernel::installDate() const {
    if (m_pkg.type == "manual") return "N/A";
    std::string d = utils::exec("xbps-query -p install-date " + m_pkg.name);
    if (d.empty() || d.find("not found") != std::string::npos) return "N/A";
    return d;
}

// Verifica si los archivos de un kernel existen en /boot y /lib/modules
static bool kernelFilesExist(const std::string &version) {
    // Verificar vmlinuz en /boot (OBLIGATORIO)
    std::string vmlinuzPath = "/boot/vmlinuz-" + version;
    bool hasVmlinuz = utils::fileExists(vmlinuzPath);
    
    // Si no hay vmlinuz, el kernel no es funcional, devolvemos false
    if (!hasVmlinuz) {
        return false;
    }
    
    // Si existe vmlinuz, consideramos que el kernel tiene archivos
    // (no es necesario comprobar módulos porque el kernel ya es funcional con vmlinuz)
    return true;
}

std::vector<Kernel> Kernel::getKernels() {
    std::vector<Kernel> kernels;
    std::set<std::string> knownNames;
    std::set<std::string> knownVersions;
    std::map<std::string, std::string> installedPkgs;

    auto isKernelPackage = [](const std::string &name) -> bool {
        if (name.rfind("linux", 0) != 0) return false;

        static const std::vector<std::string> excludedNames = {
            "linux-api-headers", "linux-firmware", "linux-libre", "linux-base",
            "linux-user", "linux-atm", "linux-utils", "linux-gpib", "linux-pam",
            "linux-container", "linux-kmod", "linux-driver-management",
            "linux-driver-management-32bit", "linux-vt-setcolors", "linux-wifi-hotspot"
        };
        for (const auto &excluded : excludedNames) {
            if (name == excluded) return false;
        }

        if (name.rfind("linux-firmware-", 0) == 0) return false;
        if (name.find("-headers") != std::string::npos ||
            name.find("-devel") != std::string::npos ||
            name.find("-dbg") != std::string::npos ||
            name.find("-docs") != std::string::npos ||
            name.find("-tools") != std::string::npos ||
            name.find("-common") != std::string::npos ||
            name.find("-progs") != std::string::npos) return false;

        static const std::vector<std::string> allowedNames = {
            "linux", "linux-lts", "linux-mainline", "linux-zen", "linux-rt", "linux-hardened"
        };
        for (const auto &allowed : allowedNames) {
            if (name == allowed) return true;
        }

        if (name.rfind("linux-neko", 0) == 0 || name.rfind("linux-cachy", 0) == 0) return true;
        if (name.rfind("linux-manual-", 0) == 0) return true;

        if (name.size() > 5 && std::isdigit(name[5])) return true;
        if (name.rfind("linux-", 0) == 0 && name.size() > 6 && std::isdigit(name[6])) return true;

        return false;
    };

    auto getPkgInfoFromFile = [](const std::string &filePath, std::string &outPkgName, std::string &outVer) -> bool {
        if (!utils::commandExists("xbps-query")) return false;
        std::string res = utils::exec("xbps-query -o " + filePath);
        if (res.empty() || res.find("not owned") != std::string::npos || res.find("No such file") != std::string::npos) return false;
        size_t colon_pos = res.find(':');
        if (colon_pos == std::string::npos) return false;
        std::string full_pkg = res.substr(0, colon_pos);
        size_t hyphen_pos = full_pkg.find_last_of('-');
        if (hyphen_pos == std::string::npos || hyphen_pos == 0) return false;
        outPkgName = full_pkg.substr(0, hyphen_pos);
        outVer = full_pkg.substr(hyphen_pos + 1);
        return !outPkgName.empty() && !outVer.empty();
    };

    if (utils::commandExists("xbps-query")) {
        std::string installedOutput = utils::exec("xbps-query -l");
        std::vector<std::string> installedLines = utils::split(installedOutput, '\n');
        for (const auto &line : installedLines) {
            if (line.size() < 4) continue;
            std::istringstream iss(line);
            std::string status, full_pkg;
            iss >> status >> full_pkg;
            if (full_pkg.empty()) continue;

            size_t hyphen_pos = full_pkg.find_last_of('-');
            if (hyphen_pos == std::string::npos || hyphen_pos == 0) continue;

            std::string pkg_name = full_pkg.substr(0, hyphen_pos);
            std::string version = full_pkg.substr(hyphen_pos + 1);

            if (isKernelPackage(pkg_name)) {
                installedPkgs[pkg_name] = version;
            }
        }

        for (const auto &pair : installedPkgs) {
            const std::string &pkg_name = pair.first;
            const std::string &version = pair.second;
            bool hasFiles = kernelFilesExist(version);
            bool installed = hasFiles;
            Package pkg{pkg_name, version, "void", "xbps", installed, hasFiles};
            Package headers{pkg_name + "-headers", version, "void", "xbps", true, true};
            kernels.emplace_back(pkg, headers);
            knownNames.insert(pkg_name);
            knownVersions.insert(version);
        }

        std::string repoOutput;
        repoOutput += utils::exec("xbps-query -Rs linux");
        repoOutput += "\n" + utils::exec("xbps-query -Rs linux-neko");
        repoOutput += "\n" + utils::exec("xbps-query -Rs linux-cachy-void");
        std::vector<std::string> repoLines = utils::split(repoOutput, '\n');

        for (const auto &line_raw : repoLines) {
            std::string line = line_raw;
            if (line.empty()) continue;

            std::istringstream iss(line);
            std::string status, full_pkg;
            iss >> status >> full_pkg;
            if (full_pkg.empty()) continue;

            size_t hyphen_pos = full_pkg.find_last_of('-');
            if (hyphen_pos == std::string::npos || hyphen_pos == 0) continue;

            std::string pkg_name = full_pkg.substr(0, hyphen_pos);
            std::string version = full_pkg.substr(hyphen_pos + 1);

            if (!isKernelPackage(pkg_name)) continue;
            if (knownNames.count(pkg_name)) continue;

            bool pkgInstalled = (installedPkgs.count(pkg_name) > 0);
            bool hasFiles = false;
            if (pkgInstalled) {
                version = installedPkgs[pkg_name];
                hasFiles = kernelFilesExist(version);
            }
            bool installed = pkgInstalled && hasFiles;

            Package pkg{pkg_name, version, "void", "xbps", installed, hasFiles};
            Package headers{pkg_name + "-headers", version, "void", "xbps", installed, true};
            kernels.emplace_back(pkg, headers);
            knownNames.insert(pkg_name);
            knownVersions.insert(version);
        }
    }

    std::filesystem::path bootPath("/boot");
    if (std::filesystem::exists(bootPath) && std::filesystem::is_directory(bootPath)) {
        for (const auto &entry : std::filesystem::directory_iterator(bootPath)) {
            if (!entry.is_regular_file() && !entry.is_symlink()) continue;
            std::string filename = entry.path().filename().string();
            if (filename.rfind("vmlinuz-", 0) == 0) {
                std::string version = filename.substr(std::string("vmlinuz-").size());
                if (version.empty() || version == "old" || version.rfind(".old") != std::string::npos || version.rfind(".bak") != std::string::npos) continue;
                if (!std::isdigit(version[0])) continue;
                if (knownVersions.count(version)) continue;

                std::string vmlinuzPath = entry.path().string();
                std::string ownerPkg, ownerVer;
                if (getPkgInfoFromFile(vmlinuzPath, ownerPkg, ownerVer)) {
                    if (!knownNames.count(ownerPkg)) {
                        bool hasFiles = kernelFilesExist(ownerVer);
                        Package pkg{ownerPkg, ownerVer, "void", "xbps", true, hasFiles};
                        Package headers{ownerPkg + "-headers", ownerVer, "void", "xbps", true, true};
                        kernels.emplace_back(pkg, headers);
                        knownNames.insert(ownerPkg);
                        knownVersions.insert(ownerVer);
                    }
                    continue;
                }

                std::string manualName = "linux-manual-" + version;
                if (knownNames.count(manualName)) continue;

                Package pkg{manualName, version, "local", "manual", true, true};
                Package headers{"none", "none", "local", "manual", false, false};
                kernels.emplace_back(pkg, headers);
                knownNames.insert(manualName);
                knownVersions.insert(version);
            }
        }
    }

    std::filesystem::path modulesPath("/usr/lib/modules");
    if (!std::filesystem::exists(modulesPath)) {
        modulesPath = "/lib/modules";
    }
    if (std::filesystem::exists(modulesPath) && std::filesystem::is_directory(modulesPath)) {
        for (const auto &entry : std::filesystem::directory_iterator(modulesPath)) {
            if (!entry.is_directory()) continue;
            std::string version = entry.path().filename().string();
            if (version.empty() || !std::isdigit(version[0])) continue;
            if (knownVersions.count(version)) continue;

            std::string modDir = entry.path().string();
            std::string ownerPkg, ownerVer;
            if (getPkgInfoFromFile(modDir, ownerPkg, ownerVer)) {
                if (!knownNames.count(ownerPkg)) {
                    bool hasFiles = kernelFilesExist(ownerVer);
                    Package pkg{ownerPkg, ownerVer, "void", "xbps", true, hasFiles};
                    Package headers{ownerPkg + "-headers", ownerVer, "void", "xbps", true, true};
                    kernels.emplace_back(pkg, headers);
                    knownNames.insert(ownerPkg);
                    knownVersions.insert(ownerVer);
                }
                continue;
            }

            std::string manualName = "linux-manual-" + version;
            if (knownNames.count(manualName)) continue;

            Package pkg{manualName, version, "local", "manual", true, true};
            Package headers{"none", "none", "local", "manual", false, false};
            kernels.emplace_back(pkg, headers);
            knownNames.insert(manualName);
            knownVersions.insert(version);
        }
    }

    return kernels;
}