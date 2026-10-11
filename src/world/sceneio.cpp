#include "world/sceneio.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace wf::world {
namespace {
constexpr std::string_view kMagic = "WORLDFORGE_SCENE";
constexpr std::size_t kMaxObjects = 2'000'000;

void write_object(std::ostream& out, const ObjectInstance& object) {
    const auto& p = object.transform.position;
    const auto& q = object.transform.rotation;
    const auto& s = object.transform.scale;
    out << "OBJECT " << object.id << ' '
        << std::quoted(object.prefab) << ' '
        << std::quoted(object.layer) << ' '
        << p.x.raw() << ' ' << p.y.raw() << ' ' << p.z.raw() << ' '
        << q.x << ' ' << q.y << ' ' << q.z << ' ' << q.w << ' '
        << s.x.raw() << ' ' << s.y.raw() << ' ' << s.z.raw() << '\n';
}

bool read_object(std::istream& in, ObjectInstance& object) {
    std::string token;
    std::int32_t px{}, py{}, pz{}, sx{}, sy{}, sz{};
    std::int32_t qx{}, qy{}, qz{}, qw{};

    if (!(in >> token) || token != "OBJECT") return false;
    if (!(in >> object.id >> std::quoted(object.prefab) >> std::quoted(object.layer)
          >> px >> py >> pz >> qx >> qy >> qz >> qw >> sx >> sy >> sz)) {
        return false;
    }

    object.transform.position = {
        math::Fixed16::from_raw(px), math::Fixed16::from_raw(py), math::Fixed16::from_raw(pz)};
    object.transform.rotation = {qx, qy, qz, qw};
    object.transform.scale = {
        math::Fixed16::from_raw(sx), math::Fixed16::from_raw(sy), math::Fixed16::from_raw(sz)};
    return object.id != 0 && !object.prefab.empty() && !object.layer.empty();
}

bool write_file(const std::filesystem::path& path, const SceneDocument& scene,
                std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Could not open temporary scene file for writing: " + path.string();
        return false;
    }

    out << kMagic << ' ' << scene.format_version << '\n';
    out << "NAME " << std::quoted(scene.name) << '\n';
    out << "OBJECTS " << scene.objects.size() << '\n';
    for (const auto& object : scene.objects) write_object(out, object);
    out.flush();
    if (!out) {
        error = "Failed while writing scene file: " + path.string();
        return false;
    }
    return true;
}
} // namespace

bool load_scene(const std::filesystem::path& path, SceneDocument& destination,
                std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Could not open scene file: " + path.string();
        return false;
    }

    SceneDocument parsed;
    std::string token;
    if (!(in >> token >> parsed.format_version) || token != kMagic) {
        error = "Not a Worldforge scene file: " + path.string();
        return false;
    }
    if (parsed.format_version != SceneDocument::current_format_version) {
        error = "Unsupported scene format version " + std::to_string(parsed.format_version);
        return false;
    }

    if (!(in >> token >> std::quoted(parsed.name)) || token != "NAME") {
        error = "Scene file is missing a valid NAME record";
        return false;
    }

    std::size_t object_count = 0;
    if (!(in >> token >> object_count) || token != "OBJECTS") {
        error = "Scene file is missing an OBJECTS record";
        return false;
    }
    if (object_count > kMaxObjects) {
        error = "Scene object count exceeds the safety limit";
        return false;
    }

    parsed.objects.reserve(object_count);
    std::set<ObjectId> ids;
    for (std::size_t i = 0; i < object_count; ++i) {
        ObjectInstance object;
        if (!read_object(in, object)) {
            error = "Malformed OBJECT record at index " + std::to_string(i);
            return false;
        }
        if (!ids.insert(object.id).second) {
            error = "Duplicate object ID " + std::to_string(object.id);
            return false;
        }
        parsed.objects.push_back(std::move(object));
    }

    if (in >> token) {
        error = "Unexpected trailing data in scene file: " + token;
        return false;
    }
    if (!in.eof()) {
        error = "I/O error while reading scene file: " + path.string();
        return false;
    }

    destination = std::move(parsed);
    error.clear();
    return true;
}

bool save_scene(const std::filesystem::path& path, const SceneDocument& scene,
                std::string& error) {
    if (scene.format_version != SceneDocument::current_format_version) {
        error = "Refusing to save an unsupported scene format version";
        return false;
    }
    if (scene.objects.size() > kMaxObjects) {
        error = "Scene object count exceeds the safety limit";
        return false;
    }

    std::set<ObjectId> ids;
    for (const auto& object : scene.objects) {
        if (object.id == 0 || object.prefab.empty() || object.layer.empty()) {
            error = "Every object must have a nonzero ID, prefab key, and layer";
            return false;
        }
        if (!ids.insert(object.id).second) {
            error = "Duplicate object ID " + std::to_string(object.id);
            return false;
        }
    }

    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "Could not create scene directory: " + ec.message();
            return false;
        }
    }

    auto temp = path;
    temp += ".tmp";
    auto backup = path;
    backup += ".bak";

    std::filesystem::remove(temp, ec);
    ec.clear();
    if (!write_file(temp, scene, error)) {
        std::filesystem::remove(temp, ec);
        return false;
    }

    // std::filesystem::rename does not replace an existing destination on all
    // supported platforms. Preserve a backup and restore it if promotion fails.
    const bool had_original = std::filesystem::exists(path, ec) && !ec;
    if (had_original) {
        std::filesystem::remove(backup, ec);
        ec.clear();
        std::filesystem::rename(path, backup, ec);
        if (ec) {
            error = "Could not stage previous scene file: " + ec.message();
            std::filesystem::remove(temp, ec);
            return false;
        }
    }

    std::filesystem::rename(temp, path, ec);
    if (ec) {
        const std::string promotion_error = ec.message();
        if (had_original) {
            std::error_code restore_error;
            std::filesystem::rename(backup, path, restore_error);
        }
        std::filesystem::remove(temp, ec);
        error = "Could not replace scene file: " + promotion_error;
        return false;
    }

    if (had_original) std::filesystem::remove(backup, ec);
    error.clear();
    return true;
}

} // namespace wf::world
