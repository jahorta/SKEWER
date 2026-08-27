#include "WorkspaceStateStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryFile>

#include <variant>

namespace skewer::qt {
namespace {

[[nodiscard]] QJsonObject keyToJson(const skewer::core::TriangleKey& key) {
    QJsonObject object{};
    if (const auto* grnd = std::get_if<skewer::core::GrndTriangleKey>(&key)) {
        object.insert(QStringLiteral("kind"), QStringLiteral("grnd"));
        object.insert(QStringLiteral("resource_address"), static_cast<qint64>(grnd->resourceAddress));
        object.insert(QStringLiteral("triangle_index"), static_cast<qint64>(grnd->triangleIndex));
    } else {
        const auto& gobj = std::get<skewer::core::GobjTriangleKey>(key);
        object.insert(QStringLiteral("kind"), QStringLiteral("gobj"));
        object.insert(QStringLiteral("resource_address"), static_cast<qint64>(gobj.resourceAddress));
        object.insert(QStringLiteral("node_index"), static_cast<qint64>(gobj.nodeIndex));
        object.insert(QStringLiteral("triangle_index"), static_cast<qint64>(gobj.triangleIndex));
    }
    return object;
}

[[nodiscard]] std::optional<skewer::core::TriangleKey> keyFromJson(const QJsonObject& object) {
    const auto kind = object.value(QStringLiteral("kind")).toString();
    const auto address = static_cast<std::uint32_t>(
        object.value(QStringLiteral("resource_address")).toInteger());
    const auto triangle = static_cast<std::size_t>(
        object.value(QStringLiteral("triangle_index")).toInteger());
    if (kind == QStringLiteral("grnd")) {
        return skewer::core::TriangleKey{ skewer::core::GrndTriangleKey{ address, triangle } };
    }
    if (kind == QStringLiteral("gobj")) {
        const auto node = static_cast<std::size_t>(object.value(QStringLiteral("node_index")).toInteger());
        return skewer::core::TriangleKey{ skewer::core::GobjTriangleKey{ address, node, triangle } };
    }
    return std::nullopt;
}

} // namespace

WorkspaceStateStore::WorkspaceStateStore(QString executableDirectory)
    : executableDirectory_(std::move(executableDirectory)),
      workspaceDirectory_(QDir(executableDirectory_).filePath(QStringLiteral("workspace"))) {
    probe();
}

bool WorkspaceStateStore::isWritable() const noexcept {
    return writable_;
}

QString WorkspaceStateStore::workspaceDirectory() const {
    return workspaceDirectory_;
}

QString WorkspaceStateStore::statePath() const {
    return QDir(workspaceDirectory_).filePath(QStringLiteral("workspace.json"));
}

QString WorkspaceStateStore::errorString() const {
    return error_;
}

void WorkspaceStateStore::probe() {
    QDir directory{};
    if (!directory.mkpath(workspaceDirectory_)) {
        error_ = QStringLiteral("Could not create %1").arg(workspaceDirectory_);
        return;
    }
    QTemporaryFile probe(QDir(workspaceDirectory_).filePath(QStringLiteral(".skewer-write-test-XXXXXX")));
    probe.setAutoRemove(true);
    if (!probe.open()) {
        error_ = probe.errorString();
        return;
    }
    writable_ = true;
}

std::optional<WorkspaceState> WorkspaceStateStore::load() {
    QFile input(statePath());
    if (!input.exists()) return WorkspaceState{};
    if (!input.open(QIODevice::ReadOnly)) {
        error_ = input.errorString();
        return std::nullopt;
    }
    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error_ = QStringLiteral("Invalid workspace JSON: %1").arg(parseError.errorString());
        return std::nullopt;
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("schema_version")).toInt() != 1) {
        error_ = QStringLiteral("Unsupported workspace schema version.");
        return std::nullopt;
    }
    WorkspaceState state{};
    state.gameDataRoot = root.value(QStringLiteral("game_data_root")).toString();
    state.activeField = root.value(QStringLiteral("active_field")).toString();
    state.encounterTable = root.value(QStringLiteral("encounter_table")).toInt();
    state.expertMetadata = root.value(QStringLiteral("expert_metadata")).toBool();
    const auto camera = root.value(QStringLiteral("camera")).toObject();
    state.orbitCenter = QVector3D(
        static_cast<float>(camera.value(QStringLiteral("x")).toDouble()),
        static_cast<float>(camera.value(QStringLiteral("y")).toDouble()),
        static_cast<float>(camera.value(QStringLiteral("z")).toDouble()));
    state.orbitDistance = static_cast<float>(camera.value(QStringLiteral("distance")).toDouble(500.0));
    state.orbitYaw = static_cast<float>(camera.value(QStringLiteral("yaw")).toDouble());
    state.orbitPitch = static_cast<float>(camera.value(QStringLiteral("pitch")).toDouble(-20.0));
    for (const auto value : root.value(QStringLiteral("hidden_batches")).toArray()) {
        state.hiddenBatches.push_back(value.toString());
    }
    for (const auto value : root.value(QStringLiteral("selection")).toArray()) {
        const auto key = keyFromJson(value.toObject());
        if (key.has_value()) state.selection.push_back(*key);
    }
    error_.clear();
    return state;
}

bool WorkspaceStateStore::save(const WorkspaceState& state) {
    if (!writable_) return false;
    QJsonObject camera{};
    camera.insert(QStringLiteral("x"), state.orbitCenter.x());
    camera.insert(QStringLiteral("y"), state.orbitCenter.y());
    camera.insert(QStringLiteral("z"), state.orbitCenter.z());
    camera.insert(QStringLiteral("distance"), state.orbitDistance);
    camera.insert(QStringLiteral("yaw"), state.orbitYaw);
    camera.insert(QStringLiteral("pitch"), state.orbitPitch);
    QJsonArray hidden{};
    for (const auto& label : state.hiddenBatches) hidden.push_back(label);
    QJsonArray selection{};
    for (const auto& key : state.selection) selection.push_back(keyToJson(key));

    QJsonObject root{};
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("game_data_root"), state.gameDataRoot);
    root.insert(QStringLiteral("active_field"), state.activeField);
    root.insert(QStringLiteral("encounter_table"), state.encounterTable);
    root.insert(QStringLiteral("expert_metadata"), state.expertMetadata);
    root.insert(QStringLiteral("camera"), camera);
    root.insert(QStringLiteral("hidden_batches"), hidden);
    root.insert(QStringLiteral("selection"), selection);

    QSaveFile output(statePath());
    if (!output.open(QIODevice::WriteOnly)) {
        error_ = output.errorString();
        return false;
    }
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.commit()) {
        error_ = output.errorString();
        return false;
    }
    error_.clear();
    return true;
}

} // namespace skewer::qt
